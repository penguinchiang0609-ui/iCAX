#include "pch.h"
#include <GeometryServices/RapidPathGeometryService.h>
#include <GeometryServices/RigidMotionCollisionService.h>
#include "OpenCascadeBRepBuilder.h"
#include <Services/ServicesHelper.h>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomLib.hxx>
#include <TopoDS.hxx>
#include <TopLoc_Location.hxx>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    using namespace iCAX::GeometryServices;
    using V=TravelPoint;
    V Add(V a,V b){for(int i=0;i<3;++i)a[i]+=b[i];return a;}
    V Sub(V a,V b){for(int i=0;i<3;++i)a[i]-=b[i];return a;}
    V Scale(V a,double s){for(auto& x:a)x*=s;return a;}
    double Dot(V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
    double Length(V a){return std::sqrt(Dot(a,a));}
    V Unit(V a){return Scale(a,1/Length(a));}
    V Lerp(V a,V b,double t){return Add(Scale(a,1-t),Scale(b,t));}
    double Angle(V a,V b){return std::acos(std::clamp(Dot(a,b),-1.,1.));}
    V Direction(V a,V b,double t){
        const auto angle=Angle(a,b);
        if(angle>3.141592653589793-1e-6)throw std::invalid_argument("TRAVEL_OPPOSED_DIRECTIONS");
        return Unit(angle<1e-7?Lerp(a,b,t):Add(Scale(a,std::sin((1-t)*angle)),Scale(b,std::sin(t*angle))));
    }
    gp_Pnt Point(V p){return {p[0],p[1],p[2]};}
    gp_Trsf Transform(const std::array<double,16>& m){
        for(double x:m)if(!std::isfinite(x))throw std::invalid_argument("TRAVEL_TRANSFORM_INVALID");
        if(std::abs(m[12])+std::abs(m[13])+std::abs(m[14])+std::abs(m[15]-1)>1e-8)throw std::invalid_argument("TRAVEL_TRANSFORM_INVALID");
        for(int i=0;i<3;++i)for(int j=0;j<3;++j){double dot=0;for(int k=0;k<3;++k)dot+=m[k*4+i]*m[k*4+j];if(std::abs(dot-(i==j?1.:0.))>1e-8)throw std::invalid_argument("TRAVEL_TRANSFORM_INVALID");}
        const auto det=m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);
        if(std::abs(det-1)>1e-8)throw std::invalid_argument("TRAVEL_TRANSFORM_INVALID");
        gp_Trsf result;result.SetValues(m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],m[8],m[9],m[10],m[11]);return result;
    }
    struct Surface
    {
        struct Face {TopoDS_Face Shape;Bnd_Box Bounds;};
        std::vector<Face> Faces;
        Surface(const RapidPathGeometryQuery& q,const std::function<void()>& check){
            if(!q.Surface)throw std::invalid_argument("TRAVEL_SURFACE_NOT_FOUND");
            const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*q.Surface);
            if(!built.bOK||built.Shape.IsNull())throw std::invalid_argument("TRAVEL_INVALID_BREP");
            const TopLoc_Location location(Transform(q.SurfaceTransform));
            for(const auto& [_,shape]:built.Faces){check();Face face;face.Shape=TopoDS::Face(shape.Moved(location));BRepBndLib::Add(face.Shape,face.Bounds,false);Faces.push_back(std::move(face));}
        }
        TravelPose Project(V position,V guide,double tolerance,const std::function<void()>& check)const{
            const auto vertex=BRepBuilderAPI_MakeVertex(Point(position)).Vertex();Bnd_Box probe;probe.Add(Point(position));
            std::vector<std::pair<double,const Face*>> candidates;for(const auto& face:Faces)candidates.emplace_back(probe.Distance(face.Bounds),&face);
            std::sort(candidates.begin(),candidates.end(),[](auto a,auto b){return a.first<b.first;});
            double best=std::numeric_limits<double>::infinity();TravelPose result;bool found=false;
            for(const auto& [lower,face]:candidates){if(lower>best+1e-7)break;check();
                BRepExtrema_DistShapeShape distance(vertex,face->Shape);if(!distance.IsDone()||distance.NbSolution()<1)throw std::runtime_error("TRAVEL_PROJECTION_FAILED");
                if(distance.Value()>best+1e-7)continue;
                for(int i=1;i<=distance.NbSolution();++i){const auto p=distance.PointOnShape2(i);const auto surface=BRep_Tool::Surface(face->Shape);GeomAPI_ProjectPointOnSurf uv(p,surface);
                    if(!uv.IsDone()||uv.NbPoints()<1)continue;double u,v;uv.LowerDistanceParameters(u,v);gp_Dir normal;
                    if(GeomLib::NormEstim(surface,gp_Pnt2d(u,v),1e-9,normal)>=2)continue;
                    V axis{normal.X(),normal.Y(),normal.Z()};if(Dot(axis,guide)<0)axis=Scale(axis,-1);if(Dot(axis,guide)<.15)continue;
                    const V point{p.X(),p.Y(),p.Z()};
                    if(found&&std::abs(distance.Value()-best)<1e-7&&Length(Sub(result.Point,point))>tolerance)throw std::invalid_argument("TRAVEL_AMBIGUOUS_SURFACE");
                    if(!found||distance.Value()<best-1e-7||Dot(axis,guide)>Dot(result.Direction,guide)){result={point,axis};best=distance.Value();found=true;}
                }
            }
            if(!found)throw std::invalid_argument("TRAVEL_SURFACE_NOT_FOUND");return result;
        }
    };
    struct Construction
    {
        const RapidPathGeometryQuery& Q;const Surface* Support;const std::function<void()>& Check;
        RapidPathGeometryResult Result;std::size_t Samples=0;
        struct EndpointAdjustment { V Offset;RigidMotion::Quaternion Rotation; };
        std::optional<EndpointAdjustment> StartAdjustment,EndAdjustment;
        struct Sample {TravelPose Pose;V Base;double Height;};
        Sample At(double t){
            Check();if(++Samples>16384)throw std::runtime_error("TRAVEL_SURFACE_BUDGET");
            const auto guide=Direction(Q.Start.Direction,Q.End.Direction,t);
            const auto lateral=Scale(Sub(Q.LateralOffset,Scale(guide,Dot(Q.LateralOffset,guide))),16*t*t*(1-t)*(1-t));
            const auto base=t==0?Q.Start:t==1?Q.End:Q.Kind==RapidPathKind::Arch?TravelPose{Add(Lerp(Q.Start.Point,Q.End.Point,t*t*(3-2*t)),lateral),guide}:Support->Project(Add(Lerp(Q.Start.Point,Q.End.Point,t),lateral),guide,Q.Tolerance,Check);
            const auto height=Q.FollowHeight+(Q.Kind==RapidPathKind::Arch?Q.LiftHeight*4*t*(1-t):0.);
            if(t>0&&t<1&&StartAdjustment){
                // Authored endpoint poses may leave the supporting surface.
                // Carry their residuals continuously across the same Follow
                // construction instead of jumping at its first/last sample.
                const auto position=Add(Add(base.Point,Scale(base.Direction,height)),Lerp(StartAdjustment->Offset,EndAdjustment->Offset,t));
                const auto axis=RigidMotion::Rotate(RigidMotion::Interpolate(StartAdjustment->Rotation,EndAdjustment->Rotation,t),base.Direction);
                return {{position,axis},Sub(position,Scale(axis,height)),height};
            }
            return {{Add(base.Point,Scale(base.Direction,height)),base.Direction},base.Point,height};
        }
        void Append(const Sample& s){Result.Poses.push_back(s.Pose);Result.SurfacePoints.push_back(s.Base);Result.Heights.push_back(s.Height);}
        void Trace(double a,const Sample& x,double b,const Sample& y,int depth){
            const auto t=(a+b)*.5;const auto mid=At(t);
            if(a==0&&b==1)Result.ShapeHandle=mid.Pose;
            if(Length(Sub(mid.Pose.Point,Lerp(x.Pose.Point,y.Pose.Point,.5)))>Q.Tolerance||Length(Sub(y.Base,x.Base))>2||Angle(x.Pose.Direction,y.Pose.Direction)>.05){
                if(depth>=16)throw std::invalid_argument("TRAVEL_SURFACE_DISCONTINUOUS");Trace(a,x,t,mid,depth+1);Trace(t,mid,b,y,depth+1);
            }else Append(y);
        }
        double Duration(double d,double speed,double acceleration)const{return d<=speed*speed/acceleration?2*std::sqrt(d/acceleration):d/speed+speed/acceleration;}
        RapidPathGeometryResult Run(){
            if(Q.Kind==RapidPathKind::Follow&&Q.Manual&&Q.ConstructedManual){
                const auto correction=[&](const TravelPose& requested){const auto projected=Support->Project(requested.Point,requested.Direction,Q.Tolerance,Check);
                    return EndpointAdjustment{Sub(Add(requested.Point,Scale(requested.Direction,Q.FollowHeight)),Add(projected.Point,Scale(projected.Direction,Q.FollowHeight))),RigidMotion::Between(projected.Direction,requested.Direction)};};
                StartAdjustment=correction(Q.Start);EndAdjustment=correction(Q.End);
            }
            if(Q.Manual&&!Q.ConstructedManual){
                Result.Poses.push_back({Add(Q.Start.Point,Scale(Q.Start.Direction,Q.FollowHeight)),Q.Start.Direction});
                Result.Poses.insert(Result.Poses.end(),Q.Waypoints.begin(),Q.Waypoints.end());
                Result.Poses.push_back({Add(Q.End.Point,Scale(Q.End.Direction,Q.FollowHeight)),Q.End.Direction});
            }else if(Q.Kind==RapidPathKind::Retract){
                const TravelPose start{Add(Q.Start.Point,Scale(Q.Start.Direction,Q.FollowHeight)),Q.Start.Direction},end{Add(Q.End.Point,Scale(Q.End.Direction,Q.FollowHeight)),Q.End.Direction};
                // Retraction height and end poses stay exact. The neutral
                // constructor follows an optional lateral guide from a planner.
                Result.Poses={start,{Add(start.Point,Scale(start.Direction,Q.LiftHeight)),start.Direction},{Add(end.Point,Scale(end.Direction,Q.LiftHeight)),end.Direction},end};
                if(Q.TurnPoint){
                    Result.Poses.insert(Result.Poses.begin()+2,{*Q.TurnPoint,Q.End.Direction});
                    Result.ShapeHandle=TravelPose{*Q.TurnPoint,Q.End.Direction};
                }else if(Length(Q.LateralOffset)>1e-10){
                    const auto a=Result.Poses[1],b=Result.Poses[2];
                    const auto axis=Direction(a.Direction,b.Direction,.5);
                    const auto lateral=Sub(Q.LateralOffset,Scale(axis,Dot(Q.LateralOffset,axis)));
                    Result.Poses.insert(Result.Poses.begin()+2,{Add(Lerp(a.Point,b.Point,.5),lateral),axis});
                }
                if(!Q.TurnPoint){const auto a=Result.Poses[1],b=Result.Poses[Result.Poses.size()-2];
                    // A four-anchor retract supported opposed axes before this
                    // editor metadata existed. Do not reject it merely to draw
                    // a handle when there is no lateral guide to project.
                    const auto axis=Angle(a.Direction,b.Direction)>3.141592653589793-1e-6&&Length(Q.LateralOffset)<=1e-10?b.Direction:Direction(a.Direction,b.Direction,.5);
                    const auto lateral=Sub(Q.LateralOffset,Scale(axis,Dot(Q.LateralOffset,axis)));
                    Result.ShapeHandle=TravelPose{Add(Lerp(a.Point,b.Point,.5),lateral),axis};}
            }else{const auto a=At(0),b=At(1);Append(a);Trace(0,a,1,b,0);}
            if(!Q.Manual||Q.ConstructedManual){
                Result.Construction=RapidPathConstruction{Q.Kind,Q.FollowHeight,Q.LiftHeight,Q.LateralOffset,Q.TurnPoint};
            }
            Result.SourceStart=Q.Start;Result.SourceEnd=Q.End;
            for(std::size_t i=1;i<Result.Poses.size();++i){const auto& a=Result.Poses[i-1];const auto& b=Result.Poses[i];const auto distance=Length(Sub(a.Point,b.Point));Result.Length+=distance;
                Result.EstimatedSeconds+=std::max(Duration(distance,Q.LinearSpeed,Q.LinearAcceleration),Duration(Angle(a.Direction,b.Direction),Q.AngularSpeed,Q.AngularAcceleration));}
            Result.Generated=true;return std::move(Result);
        }
    };
    void Validate(const RapidPathGeometryQuery& q){
        const auto pose=[](const TravelPose& p){for(auto x:p.Point)if(!std::isfinite(x)||std::abs(x)>1e8)throw std::invalid_argument("TRAVEL_POINT_INVALID");if(!std::isfinite(Length(p.Direction))||std::abs(Length(p.Direction)-1)>1e-8)throw std::invalid_argument("TRAVEL_DIRECTION_INVALID");};
        pose(q.Start);pose(q.End);if(q.Waypoints.size()>2048)throw std::invalid_argument("RAPID_EDIT_POINT_LIMIT");for(const auto& p:q.Waypoints)pose(p);
        if(q.ConstructedManual&&(!q.Manual||!q.Waypoints.empty()))throw std::invalid_argument("RAPID_CONSTRUCTION_INPUT_INVALID");
        if(!std::isfinite(q.FollowHeight)||q.FollowHeight<0||q.FollowHeight>1e6)throw std::invalid_argument("TRAVEL_PARAMETER_INVALID");
        for(double x:{q.LiftHeight,q.Tolerance,q.LinearSpeed,q.LinearAcceleration,q.AngularSpeed,q.AngularAcceleration})if(!std::isfinite(x)||x<=0||x>1e6)throw std::invalid_argument("TRAVEL_PARAMETER_INVALID");
        for(double x:q.LateralOffset)if(!std::isfinite(x)||std::abs(x)>1e6)throw std::invalid_argument("TRAVEL_PARAMETER_INVALID");
        if(q.TurnPoint){
            if(q.Kind!=RapidPathKind::Retract||(q.Manual&&!q.ConstructedManual)||std::any_of(q.LateralOffset.begin(),q.LateralOffset.end(),[](double x){return x!=0;}))throw std::invalid_argument("RAPID_TURN_POINT_CONSTRUCTION_INVALID");
            for(double x:*q.TurnPoint)if(!std::isfinite(x)||std::abs(x)>1e8)throw std::invalid_argument("TRAVEL_POINT_INVALID");
        }
        if(q.Kind!=RapidPathKind::Retract&&q.Kind!=RapidPathKind::Arch&&q.Kind!=RapidPathKind::Follow)throw std::invalid_argument("TRAVEL_KIND_INVALID");
    }
    class CRapidPathGeometryService final:public IRapidPathGeometryService
    {
        AUTO_REGIST_SERVICE(IRapidPathGeometryService,CRapidPathGeometryService)
    public:
        void OnLoad() override{}void OnUnload() override{}
        std::vector<RapidPathGeometryResult> Generate(const std::vector<RapidPathGeometryQuery>& queries,const std::function<bool()>& cancelled)const override{
            const std::function<void()> check=[&]{if(cancelled&&cancelled())throw std::runtime_error("TRAVEL_CANCELLED");};
            for(const auto& q:queries){check();Validate(q);}
            std::shared_ptr<const iCAX::GeometryData::BRepModel> previous;std::array<double,16> transform{};std::unique_ptr<Surface> surface;
            std::vector<RapidPathGeometryResult> results;results.reserve(queries.size());
            for(const auto& q:queries){check();try{
                if((!q.Manual||q.ConstructedManual)&&q.Kind==RapidPathKind::Follow&&(!surface||previous!=q.Surface||transform!=q.SurfaceTransform)){surface=std::make_unique<Surface>(q,check);previous=q.Surface;transform=q.SurfaceTransform;}
                results.push_back(Construction{q,surface.get(),check}.Run());
            }catch(const std::exception& e){check();RapidPathGeometryResult failed;failed.Reason=e.what();results.push_back(std::move(failed));}}
            check();return results;
        }
    };
}
