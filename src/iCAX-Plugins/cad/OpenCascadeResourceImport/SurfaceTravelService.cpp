#include "pch.h"
#include "GeometryServices/SurfaceTravelService.h"
#include "GeometryServices/CollisionDistanceService.h"
#include "GeometryServices/RapidPathPlanningService.h"
#include "GeometryServices/RigidMotionCollisionService.h"
#include "OpenCascadeBRepBuilder.h"
#include "OpenCascadeTaskExecution.h"
#include "Services/ServicesHelper.h"
#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomLib.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BezierSurface.hxx>
#include <Geom_RectangularTrimmedSurface.hxx>
#include <Geom_Plane.hxx>
#include <GeomConvert.hxx>
#include <GeomConvert_BSplineSurfaceToBezierSurface.hxx>
#include <Poly_Triangulation.hxx>
#include <Message_ProgressIndicator.hxx>
#include <BRepTools.hxx>
#include <Bnd_Box.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Quaternion.hxx>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <chrono>
#include <iostream>
#include <syncstream>
#include <mutex>
#include <atomic>

namespace
{
    using namespace iCAX::GeometryServices;
    using V=TravelPoint;
    constexpr double CollisionMeshDeflection=.05;
    class TravelMeshProgress final:public Message_ProgressIndicator {
        std::function<void()> Check;std::mutex Mutex;std::atomic<bool> Cancelled=false;
    public:
        explicit TravelMeshProgress(std::function<void()> check):Check(std::move(check)){}
        bool IsCancelled()const{return Cancelled.load();}
    protected:
        bool UserBreak()override{std::lock_guard lock(Mutex);if(!Cancelled)try{Check();}catch(...){Cancelled=true;}return Cancelled.load();}
        void Show(const Message_ProgressScope&,bool)override{}
    };
    struct TravelProfile {
        using Clock=std::chrono::steady_clock;
        bool Enabled=GetEnvironmentVariableA("ICAX_TRAVEL_PROFILE",nullptr,0)!=0;
        Clock::time_point Start=Clock::now();
        double ClassifyMs=0,DistanceMs=0,CoalMs=0;std::size_t Classifications=0,Distances=0,CoalQueries=0;
        ~TravelProfile(){if(Enabled)std::osyncstream(std::clog)<<"Travel profile totalMs="<<std::chrono::duration<double,std::milli>(Clock::now()-Start).count()
            <<" classifyMs="<<ClassifyMs<<" classifyCount="<<Classifications<<" distanceMs="<<DistanceMs<<" distanceCount="<<Distances<<" coalMs="<<CoalMs<<" coalQueries="<<CoalQueries<<std::endl;}
        struct Timer {double* Elapsed;Clock::time_point Start;explicit Timer(double* elapsed):Elapsed(elapsed),Start(elapsed?Clock::now():Clock::time_point{}){}
            ~Timer(){if(Elapsed)*Elapsed+=std::chrono::duration<double,std::milli>(Clock::now()-Start).count();}};
    };
    constexpr double Pi=3.14159265358979323846;
    V Add(V a,V b){for(int i=0;i<3;++i)a[i]+=b[i];return a;}
    V Sub(V a,V b){for(int i=0;i<3;++i)a[i]-=b[i];return a;}
    V Mul(V a,double k){for(auto& v:a)v*=k;return a;}
    double Dot(V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
    double Norm(V a){return std::sqrt(Dot(a,a));}
    V Unit(V a){const auto n=Norm(a);if(!std::isfinite(n)||n<1e-10)throw std::invalid_argument("TRAVEL_DIRECTION_INVALID");return Mul(a,1/n);}
    V Lerp(V a,V b,double t){return Add(Mul(a,1-t),Mul(b,t));}
    gp_Pnt P(V p){return {p[0],p[1],p[2]};}
    V Vec(const gp_Pnt& p){return {p.X(),p.Y(),p.Z()};}
    V Vec(const gp_Vec& p){return {p.X(),p.Y(),p.Z()};}
    double Angle(V a,V b){return std::acos(std::clamp(Dot(a,b),-1.,1.));}
    V Slerp(V a,V b,double t){const auto angle=Angle(a,b);if(angle>Pi-1e-6)throw std::invalid_argument("TRAVEL_OPPOSED_DIRECTIONS");
        return angle<1e-7?Unit(Lerp(a,b,t)):Unit(Add(Mul(a,std::sin((1-t)*angle)),Mul(b,std::sin(t*angle))));}
    TravelPose Mid(const TravelPose& a,const TravelPose& b){return {Lerp(a.Point,b.Point,.5),Slerp(a.Direction,b.Direction,.5)};}
    gp_Trsf Transform(const std::array<double,16>& m){
        for(double v:m)if(!std::isfinite(v))throw std::invalid_argument("TRAVEL_TRANSFORM_INVALID");
        if(std::abs(m[12])+std::abs(m[13])+std::abs(m[14])+std::abs(m[15]-1)>1e-8)throw std::invalid_argument("TRAVEL_TRANSFORM_INVALID");
        for(int i=0;i<3;++i)for(int j=0;j<3;++j){double dot=0;for(int k=0;k<3;++k)dot+=m[k*4+i]*m[k*4+j];if(std::abs(dot-(i==j?1.:0.))>1e-8)throw std::invalid_argument("TRAVEL_TRANSFORM_INVALID");}
        const double det=m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);
        if(std::abs(det-1)>1e-8)throw std::invalid_argument("TRAVEL_TRANSFORM_INVALID");
        gp_Trsf t;t.SetValues(m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],m[8],m[9],m[10],m[11]);return t;
    }
    struct Face {
        TopoDS_Face Shape;Bnd_Box Box;
        CollisionDistanceModelRef CollisionBounds;
        double Tolerance=0;
        struct Slab {
        std::array<V,3> Axes{};std::array<double,3> Low{},High{};int AxisCount=0;
        std::vector<V> Poles;double Padding=0;
        int Left=-1,Right=-1;
        void Build(const occ::handle<Geom_BSplineSurface>& spline,double tolerance){
            double u0,u1,v0,v1;spline->Bounds(u0,u1,v0,v1);gp_Pnt p;gp_Vec du,dv;spline->D1((u0+u1)*.5,(v0+v1)*.5,p,du,dv);
            auto normal=du.Crossed(dv);
            Axes=du.SquareMagnitude()<1e-20||normal.SquareMagnitude()<1e-20?std::array<V,3>{V{1,0,0},V{0,1,0},V{0,0,1}}:
                std::array<V,3>{Vec(du.Normalized()),Vec(normal.Normalized().Crossed(du.Normalized())),Vec(normal.Normalized())};
            Low.fill(std::numeric_limits<double>::infinity());High.fill(-std::numeric_limits<double>::infinity());
            // Positive-weight NURBS, including their trimmed subset, lie in
            // the convex hull of their poles. No mesh/sample clearance proof.
            for(int u=1;u<=spline->NbUPoles();++u)for(int v=1;v<=spline->NbVPoles();++v){if(spline->Weight(u,v)<=0)return;
                const auto point=Vec(spline->Pole(u,v));Poles.push_back(point);for(int a=0;a<3;++a){const auto d=Dot(point,Axes[a]);Low[a]=std::min(Low[a],d);High[a]=std::max(High[a],d);}}
            Padding=tolerance;
            for(int a=0;a<3;++a){Low[a]-=tolerance;High[a]+=tolerance;}AxisCount=3;
        }
        double Lower(const TravelPose& pose,const std::vector<AxialSolid>& solids)const{
            double squared=0;
            for(int a=0;a<AxisCount;++a){
                const auto axial=std::clamp(Dot(pose.Direction,Axes[a]),-1.,1.),radial=std::sqrt(std::max(0.,1-axial*axial)),origin=Dot(pose.Point,Axes[a]);
                double low=std::numeric_limits<double>::infinity(),high=-low;
                // Exact support interval of every cylinder/cone in the head.
                for(const auto& s:solids)for(const auto& [z,r]:{std::pair{s.Z,s.LowerRadius},std::pair{s.Z+s.Height,s.UpperRadius}}){
                    low=std::min(low,origin+z*axial-r*radial);high=std::max(high,origin+z*axial+r*radial);}
                const auto gap=std::max({0.,Low[a]-high,low-High[a]});squared+=gap*gap;
            }
            auto lower=std::sqrt(squared);
            if(AxisCount==3){
                // Tool-axis separation is often much tighter than the patch's
                // three box axes for a tilted conical head near a curved skin.
                double surfaceLow=0,surfaceHigh=0;
                for(int a=0;a<3;++a){const auto d=Dot(pose.Direction,Axes[a]);surfaceLow+=d*(d<0?High[a]:Low[a]);surfaceHigh+=d*(d<0?Low[a]:High[a]);}
                double headLow=std::numeric_limits<double>::infinity(),headHigh=-headLow;
                const auto origin=Dot(pose.Point,pose.Direction);
                for(const auto& s:solids){headLow=std::min(headLow,origin+s.Z);headHigh=std::max(headHigh,origin+s.Z+s.Height);}
                lower=std::max({lower,surfaceLow-headHigh,headLow-surfaceHigh});
            }
            return std::max(0.,lower-1e-6);
        }
        };
        std::vector<Slab> Slabs;
        std::vector<int> Roots;
        bool HasSurfaceBounds()const{return !Slabs.empty()&&std::all_of(Slabs.begin(),Slabs.end(),[](const auto& s){return s.AxisCount==3;});}
        int AddSlab(const occ::handle<Geom_BSplineSurface>& spline,double tolerance,int depth,const std::function<void()>& check){
            check();Slab slab;slab.Build(spline,tolerance);const auto index=static_cast<int>(Slabs.size());Slabs.push_back(slab);
            if(slab.AxisCount!=3||depth>=8||slab.High[2]-slab.Low[2]<=.05+2*tolerance)return index;
            double u0,u1,v0,v1;spline->Bounds(u0,u1,v0,v1);const auto um=(u0+u1)*.5,vm=(v0+v1)*.5;
            gp_Pnt p;gp_Vec du,dv;spline->D1(um,vm,p,du,dv);
            const bool splitU=du.Magnitude()*(u1-u0)>=dv.Magnitude()*(v1-v0);
            auto left=occ::handle<Geom_BSplineSurface>::DownCast(spline->Copy()),right=occ::handle<Geom_BSplineSurface>::DownCast(spline->Copy());
            left->Segment(u0,splitU?um:u1,v0,splitU?v1:vm);right->Segment(splitU?um:u0,u1,splitU?v0:vm,v1);
            const auto a=AddSlab(left,tolerance,depth+1,check),b=AddSlab(right,tolerance,depth+1,check);
            Slabs[index].Left=a;Slabs[index].Right=b;Slabs[index].Poles.clear();return index;
        }
        void BuildSlabs(const std::function<void()>& check,bool needed=true){
            double tolerance=BRep_Tool::Tolerance(Shape)+1e-6;
            for(TopExp_Explorer e(Shape,TopAbs_EDGE);e.More();e.Next())tolerance=std::max(tolerance,BRep_Tool::Tolerance(TopoDS::Edge(e.Current()))+1e-6);
            for(TopExp_Explorer e(Shape,TopAbs_VERTEX);e.More();e.Next())tolerance=std::max(tolerance,BRep_Tool::Tolerance(TopoDS::Vertex(e.Current()))+1e-6);
            Tolerance=tolerance;if(!needed)return;
            auto surface=BRep_Tool::Surface(Shape);
            while(const auto trim=occ::handle<Geom_RectangularTrimmedSurface>::DownCast(surface))surface=trim->BasisSurface();
            if(const auto plane=occ::handle<Geom_Plane>::DownCast(surface)){
                Slab slab;slab.Axes[0]=Vec(gp_Vec(plane->Pln().Axis().Direction()));slab.Low[0]=Dot(Vec(plane->Pln().Location()),slab.Axes[0])-tolerance;
                slab.High[0]=slab.Low[0]+2*tolerance;slab.AxisCount=1;Slabs.push_back(slab);Roots.push_back(0);return;
            }
            try{
                double u0,u1,v0,v1;BRepTools::UVBounds(Shape,u0,u1,v0,v1);
                if(!std::isfinite(u0+u1+v0+v1)||u1-u0<1e-12||v1-v0<1e-12)return;
                // Exact knot insertion / analytic conversion gives a union of
                // positive-weight pole hulls enclosing the complete support.
                // Holes remain conservatively filled; no sampled clearance.
                const auto trimmed=new Geom_RectangularTrimmedSurface(surface,u0,u1,v0,v1);
                const auto spline=GeomConvert::SurfaceToBSplineSurface(trimmed);
                // Split exactly at spline knots before refining. A single
                // bound across a C0 corner or many short profile segments is
                // excessively loose even after uniform parameter bisection.
                GeomConvert_BSplineSurfaceToBezierSurface patches(spline);
                for(int u=1;u<=patches.NbUPatches();++u)for(int v=1;v<=patches.NbVPatches();++v){
                    check();Roots.push_back(AddSlab(GeomConvert::SurfaceToBSplineSurface(patches.Patch(u,v)),tolerance,0,check));
                }
            }catch(const Standard_Failure&){Slabs.clear();Roots.clear();} // Unsupported bounds use the exact kernel below.
        }
        double SlabLower(const TravelPose& pose,const std::vector<AxialSolid>& solids,double cutoff,int index)const{
            const auto& slab=Slabs[index];const auto lower=slab.Lower(pose,solids);
            if(slab.Left<0||lower>=cutoff)return lower;
            const auto a=SlabLower(pose,solids,cutoff,slab.Left),b=SlabLower(pose,solids,cutoff,slab.Right);
            return std::max(lower,std::min(a,b));
        }
        double Lower(const TravelPose& pose,const std::vector<AxialSolid>& solids,double cutoff)const{
            if(Roots.empty())return 0;double lower=std::numeric_limits<double>::infinity();
            for(int root:Roots)lower=std::min(lower,SlabLower(pose,solids,cutoff,root));return lower;
        }
    };
    struct Obstacle {
        std::string Id;TopoDS_Shape Shape;Bnd_Box Box;std::vector<Face> Faces;std::vector<std::unique_ptr<BRepClass3d_SolidClassifier>> Solids;std::vector<gp_Pnt> Probes;
        CollisionDistanceModelRef CollisionBounds;
        bool MeshUsable=false;
        void BuildCollisionMesh(const std::function<void()>& check){
            const auto started=TravelProfile::Clock::now();
            check();IMeshTools_Parameters parameters;parameters.Deflection=parameters.DeflectionInterior=CollisionMeshDeflection;
            parameters.Angle=parameters.AngleInterior=.35;parameters.Relative=false;parameters.InParallel=true;
            parameters.ControlSurfaceDeflection=true;parameters.EnableControlSurfaceDeflectionAllSurfaces=true;
            parameters.MinSize=Precision::Confusion();parameters.AllowQualityDecrease=false;
            occ::handle<TravelMeshProgress> progress=new TravelMeshProgress(check);
            try{
                BRepMesh_IncrementalMesh mesher(Shape,parameters,progress->Start());
                if(progress->IsCancelled())throw std::runtime_error("TRAVEL_CANCELLED");
                MeshUsable=mesher.IsDone()&&mesher.GetStatusFlags()==0;
                if(GetEnvironmentVariableA("ICAX_TRAVEL_PROFILE",nullptr,0))std::osyncstream(std::clog)<<"Travel mesh status="<<mesher.GetStatusFlags()<<" usable="<<MeshUsable<<" elapsedMs="<<std::chrono::duration<double,std::milli>(TravelProfile::Clock::now()-started).count()<<std::endl;
            }catch(const Standard_Failure&){MeshUsable=false;}
            check();
        }
        void BuildCollisionBounds(const ICollisionDistanceService& service,const std::function<void()>& check){
            const auto started=TravelProfile::Clock::now();std::size_t triangleCount=0;
            std::vector<CollisionBox> boxes;std::vector<CollisionHull> hulls;std::vector<CollisionDistanceModelRef> models;
            const auto append=[&](const std::array<V,3>& axes,const V& low,const V& high){CollisionBox box;box.Axes=axes;
                for(int a=0;a<3;++a){box.HalfExtents[a]=(high[a]-low[a])*.5;box.Center=Add(box.Center,Mul(axes[a],(low[a]+high[a])*.5));}boxes.push_back(box);};
            for(auto& face:Faces){check();const auto first=boxes.size(),firstHull=hulls.size();
                TopLoc_Location meshLocation;const auto mesh=MeshUsable?BRep_Tool::Triangulation(face.Shape,meshLocation):occ::handle<Poly_Triangulation>{};
                if(!mesh.IsNull()&&mesh->NbTriangles()>0&&std::isfinite(mesh->Deflection())&&mesh->Deflection()>=0){
                    // Dedicated CAD collision mesh. Its controlled deflection,
                    // plus the face/edge tolerances, expands every triangle.
                    // Overlapping envelopes continue to the exact CAD kernel.
                    CollisionMesh collisionMesh;collisionMesh.Padding=std::max(CollisionMeshDeflection,mesh->Deflection())+face.Tolerance;
                    collisionMesh.Vertices.reserve(mesh->NbNodes());collisionMesh.Triangles.reserve(mesh->NbTriangles());
                    for(int i=1;i<=mesh->NbNodes();++i){if((i&255)==0)check();collisionMesh.Vertices.push_back(Vec(mesh->Node(i).Transformed(meshLocation.Transformation())));}
                    for(int i=1;i<=mesh->NbTriangles();++i){if((i&255)==0)check();int a,b,c;mesh->Triangle(i).Get(a,b,c);
                        collisionMesh.Triangles.push_back({static_cast<unsigned>(a-1),static_cast<unsigned>(b-1),static_cast<unsigned>(c-1)});
                    }
                    triangleCount+=collisionMesh.Triangles.size();face.CollisionBounds=service.PrepareMesh(collisionMesh,[&]{check();return false;});models.push_back(face.CollisionBounds);continue;
                }else if(face.HasSurfaceBounds()){
                    for(const auto& slab:face.Slabs)if(slab.Left<0)hulls.push_back({slab.Poles,slab.Padding});
                }else{
                    double x0,y0,z0,x1,y1,z1;face.Box.Get(x0,y0,z0,x1,y1,z1);
                    std::array<V,3> axes{V{1,0,0},V{0,1,0},V{0,0,1}};V low{x0,y0,z0},high{x1,y1,z1};
                    if(!face.Slabs.empty()&&face.Slabs.front().AxisCount==1){
                        const auto normal=face.Slabs.front().Axes[0];const V helper=std::abs(normal[0])<.8?V{1,0,0}:V{0,1,0};
                        const auto x=Unit(Sub(helper,Mul(normal,Dot(helper,normal))));const V y{normal[1]*x[2]-normal[2]*x[1],normal[2]*x[0]-normal[0]*x[2],normal[0]*x[1]-normal[1]*x[0]};axes={x,y,normal};
                        for(int a=0;a<2;++a){low[a]=high[a]=0;for(int k=0;k<3;++k){const V lo{x0,y0,z0},hi{x1,y1,z1};const auto d=axes[a][k];low[a]+=d*(d<0?hi[k]:lo[k]);high[a]+=d*(d<0?lo[k]:hi[k]);}}
                        low[2]=face.Slabs.front().Low[0];high[2]=face.Slabs.front().High[0];
                    }append(axes,low,high);
                }
                // The whole obstacle may have one unresolved patch. Keep each
                // face's bounds to avoid exact queries on its clear neighbors.
                face.CollisionBounds=service.PrepareVolumes({boxes.begin()+first,boxes.end()},{hulls.begin()+firstHull,hulls.end()},[&]{check();return false;});
                models.push_back(face.CollisionBounds);
            }
            CollisionBounds=service.Combine(models,[&]{check();return false;});
            if(GetEnvironmentVariableA("ICAX_TRAVEL_PROFILE",nullptr,0)){
                double thickness=0;for(const auto& face:Faces)for(const auto& slab:face.Slabs)if(slab.Left<0&&slab.AxisCount==3)thickness=std::max(thickness,slab.High[2]-slab.Low[2]);
                std::osyncstream(std::clog)<<"Travel bounds faces="<<Faces.size()<<" boxes="<<boxes.size()<<" hulls="<<hulls.size()<<" triangles="<<triangleCount<<" maxThickness="<<thickness<<" elapsedMs="<<std::chrono::duration<double,std::milli>(TravelProfile::Clock::now()-started).count()<<std::endl;
            }
        }
        struct Support {V Axis;double Low=std::numeric_limits<double>::infinity(),High=-std::numeric_limits<double>::infinity();};
        std::vector<Support> Hull;
        void BuildHull(const std::function<void()>& check){
            if(Faces.empty())return;
            // Each plane encloses all exact face bounds. This is a conservative
            // convex outer bound, including concavities, cavities and all solids.
            // Directions only control tightness; they are not collision samples.
            for(int i=0;i<128;++i){const auto z=1.-2.*(i+.5)/128.,r=std::sqrt(1-z*z),angle=i*Pi*(3.-std::sqrt(5.));Hull.push_back({{r*std::cos(angle),r*std::sin(angle),z}});}
            for(const auto& face:Faces){check();
                if(face.HasSurfaceBounds()){
                    for(const auto& slab:face.Slabs)if(slab.Left<0)for(auto& support:Hull){
                        double low=0,high=0;
                        for(int a=0;a<3;++a){const auto d=Dot(support.Axis,slab.Axes[a]);low+=d*(d<0?slab.High[a]:slab.Low[a]);high+=d*(d<0?slab.Low[a]:slab.High[a]);}
                        support.Low=std::min(support.Low,low);support.High=std::max(support.High,high);
                    }
                }else{
                    double x0,y0,z0,x1,y1,z1;face.Box.Get(x0,y0,z0,x1,y1,z1);
                    for(auto& support:Hull){double low=0,high=0;const V a{x0,y0,z0},b{x1,y1,z1};
                        for(int axis=0;axis<3;++axis){const auto d=support.Axis[axis];low+=d*(d<0?b[axis]:a[axis]);high+=d*(d<0?a[axis]:b[axis]);}
                        support.Low=std::min(support.Low,low);support.High=std::max(support.High,high);
                    }
                }
            }
            for(auto& s:Hull){s.Low-=1e-6;s.High+=1e-6;}
        }
        bool Outside(const gp_Pnt& p)const{for(const auto& s:Hull){const auto d=Dot(Vec(p),s.Axis);if(d<s.Low||d>s.High)return true;}return false;}
        double Lower(const TravelPose& pose,const std::vector<AxialSolid>& solids)const{
            double lower=0;
            for(const auto& s:Hull){const auto axial=std::clamp(Dot(pose.Direction,s.Axis),-1.,1.),radial=std::sqrt(std::max(0.,1-axial*axial)),origin=Dot(pose.Point,s.Axis);
                double low=std::numeric_limits<double>::infinity(),high=-low;
                for(const auto& part:solids)for(const auto& [z,r]:{std::pair{part.Z,part.LowerRadius},std::pair{part.Z+part.Height,part.UpperRadius}}){
                    low=std::min(low,origin+z*axial-r*radial);high=std::max(high,origin+z*axial+r*radial);}
                lower=std::max({lower,s.Low-high,low-s.High});
            }
            return std::max(0.,lower-1e-6);
        }
    };
    struct Prepared
    {
        std::vector<Obstacle> Objects;Bnd_Box Bounds;
        std::shared_ptr<ICollisionDistanceService> CollisionService;
        explicit Prepared(const SurfaceTravelQuery& q,const std::function<void()>& check,std::shared_ptr<ICollisionDistanceService> service):CollisionService(std::move(service)){
            for(const auto& source:q.Obstacles){check();if(!source.Geometry)throw std::invalid_argument("TRAVEL_GEOMETRY_REQUIRED");
                const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*source.Geometry);if(!built.bOK||built.Shape.IsNull())throw std::invalid_argument("TRAVEL_INVALID_BREP");
                const TopLoc_Location location(Transform(source.Transform));Obstacle object;object.Id=source.Id;object.Shape=built.Shape.Moved(location);
                BRepBndLib::Add(object.Shape,object.Box,false);Bounds.Add(object.Box);
                object.BuildCollisionMesh(check);
                for(TopExp_Explorer e(object.Shape,TopAbs_SOLID);e.More();e.Next())object.Solids.push_back(std::make_unique<BRepClass3d_SolidClassifier>(e.Current()));
                for(const auto [kind,avoid]:{std::pair{TopAbs_SOLID,TopAbs_SHAPE},std::pair{TopAbs_SHELL,TopAbs_SOLID},std::pair{TopAbs_FACE,TopAbs_SHELL}})
                    for(TopExp_Explorer e(object.Shape,kind,avoid);e.More();e.Next()){TopExp_Explorer vertex(e.Current(),TopAbs_VERTEX);if(vertex.More())object.Probes.push_back(BRep_Tool::Pnt(TopoDS::Vertex(vertex.Current())));}
                for(const auto& [id,face]:built.Faces){Face f;f.Shape=TopoDS::Face(face.Moved(location));BRepBndLib::Add(f.Shape,f.Box,false);f.BuildSlabs(check,!object.MeshUsable);object.Faces.push_back(std::move(f));}object.BuildHull(check);object.BuildCollisionBounds(*CollisionService,check);Objects.push_back(std::move(object));
            }
        }
    };
    struct Planner
    {
        const SurfaceTravelQuery& Q;const Prepared& Geometry;std::function<void()> Check;bool ParallelDistance=false;
        SurfaceTravelResult Result;TopoDS_Shape Head;double Radius=0,ArchLift=0;std::size_t Budget=0;
        bool BoundsOnly=false;
        std::vector<V> HeadProbes;
        std::vector<AxialSolid> HeadSolids;
        CollisionDistanceModelRef CollisionHead;
        std::vector<std::unique_ptr<BRepClass3d_SolidClassifier>> HeadClassifiers;
        std::map<std::array<double,6>,std::pair<double,std::string>> Distances;
        struct Certificate {TravelPose Pose;double Lower;std::string Object;};
        std::vector<Certificate> Certificates;std::size_t NextCertificate=0;
        TravelProfile Profile;
        Planner(const SurfaceTravelQuery& q,const Prepared& geometry,std::function<void()> check,bool parallelDistance=false):Q(q),Geometry(geometry),Check(std::move(check)),ParallelDistance(parallelDistance){
            ArchLift=q.LiftHeight;
            auto solids=Q.EnvelopeSolids;
            if(Q.EnvelopeRadius>0&&Q.EnvelopeLength>0)solids.push_back({0,Q.EnvelopeLength,Q.EnvelopeRadius,Q.EnvelopeRadius});
            if(Q.EnvelopeBody){
                const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*Q.EnvelopeBody);
                if(!built.bOK||built.Shape.IsNull())throw std::invalid_argument("TRAVEL_ENVELOPE_BREP_INVALID");
                Bnd_Box box;BRepBndLib::AddOptimal(built.Shape.Moved(TopLoc_Location(Transform(Q.EnvelopeCalibration))),box,false,true);
                double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
                const double r=std::hypot(std::max(std::abs(x0),std::abs(x1)),std::max(std::abs(y0),std::abs(y1)));
                solids.push_back({z0,z1-z0,r,r});
            }
            if(!solids.empty()){
                TopoDS_Compound compound;BRep_Builder builder;builder.MakeCompound(compound);
                for(const auto& s:solids){
                    for(double v:{s.Z,s.Height,s.LowerRadius,s.UpperRadius})if(!std::isfinite(v)||std::abs(v)>1e6)throw std::invalid_argument("TRAVEL_ENVELOPE_INVALID");
                    if(s.Height<=0||s.LowerRadius<0||s.UpperRadius<0||s.LowerRadius+s.UpperRadius<=0)throw std::invalid_argument("TRAVEL_ENVELOPE_INVALID");
                    const gp_Ax2 axis(gp_Pnt(0,0,s.Z),gp_Dir(0,0,1));
                    builder.Add(compound,s.LowerRadius==s.UpperRadius?BRepPrimAPI_MakeCylinder(axis,s.LowerRadius,s.Height).Shape():BRepPrimAPI_MakeCone(axis,s.LowerRadius,s.UpperRadius,s.Height).Shape());
                    Radius=std::max(Radius,std::hypot(std::max(s.LowerRadius,s.UpperRadius),std::max(std::abs(s.Z),std::abs(s.Z+s.Height))));
                    HeadProbes.push_back({0,0,s.Z+s.Height*.5});
                }Head=compound;HeadSolids=solids;
                std::vector<CollisionFrustum> frusta;for(const auto& s:solids)frusta.push_back({s.Z,s.Height,s.LowerRadius,s.UpperRadius});
                CollisionHead=Geometry.CollisionService->PrepareFrusta(frusta);
                for(TopExp_Explorer e(Head,TopAbs_SOLID);e.More();e.Next())HeadClassifiers.push_back(std::make_unique<BRepClass3d_SolidClassifier>(e.Current()));
            }
        }
        TravelPose Project(V query,V guide){
            Check();const auto vertex=BRepBuilderAPI_MakeVertex(P(query)).Vertex();Bnd_Box probe;probe.Add(P(query));
            std::vector<std::pair<double,const Face*>> candidates;for(const auto& face:Geometry.Objects.at(Q.Surface).Faces)candidates.emplace_back(probe.Distance(face.Box),&face);
            std::sort(candidates.begin(),candidates.end(),[](auto a,auto b){return a.first<b.first;});
            double best=std::numeric_limits<double>::infinity();TravelPose answer;bool found=false;
            for(const auto& [lower,face]:candidates){if(lower>best+1e-7)break;Check();
                BRepExtrema_DistShapeShape d(vertex,face->Shape);if(!d.IsDone()||d.NbSolution()<1)throw std::runtime_error("TRAVEL_PROJECTION_FAILED");
                if(d.Value()>best+1e-7)continue;
                for(int i=1;i<=d.NbSolution();++i){const auto point=d.PointOnShape2(i);const auto surface=BRep_Tool::Surface(face->Shape);
                    GeomAPI_ProjectPointOnSurf uv(point,surface);if(!uv.IsDone()||uv.NbPoints()<1)continue;
                    double u,v;uv.LowerDistanceParameters(u,v);gp_Dir n;if(GeomLib::NormEstim(surface,gp_Pnt2d(u,v),1e-9,n)>=2)continue;
                    auto normal=Unit(V{n.X(),n.Y(),n.Z()});if(Dot(normal,guide)<0)normal=Mul(normal,-1);
                    if(Dot(normal,guide)<.15)continue;
                    if(found&&std::abs(d.Value()-best)<1e-7&&Norm(Sub(answer.Point,Vec(point)))>Q.Tolerance)throw std::invalid_argument("TRAVEL_AMBIGUOUS_SURFACE");
                    if(!found||d.Value()<best-1e-7||Dot(normal,guide)>Dot(answer.Direction,guide)){answer={Vec(point),normal};best=d.Value();found=true;}
                }
            }
            if(!found)throw std::invalid_argument("TRAVEL_SURFACE_NOT_FOUND");return answer;
        }
        struct Sample {TravelPose Pose;V Surface;double Height=0;};
        Sample At(double t){
            if(++Budget>16384)throw std::runtime_error("TRAVEL_SURFACE_BUDGET");
            const auto guide=Slerp(Q.Start.Direction,Q.End.Direction,t);
            // Follow traces the trimmed surface. Arch is an airborne hop:
            // its baseline joins the anchors and must bridge holes/open space.
            // Smooth horizontal departure/arrival gives Arch an outward initial
            // tangent and an inward final tangent, including non-coplanar cuts.
            const auto surface=t==0?Q.Start:t==1?Q.End:Q.Kind==SurfaceTravelKind::Arch?TravelPose{Lerp(Q.Start.Point,Q.End.Point,t*t*(3-2*t)),guide}:Project(Lerp(Q.Start.Point,Q.End.Point,t),guide);
            const auto height=Q.FollowHeight+(Q.Kind==SurfaceTravelKind::Arch?ArchLift*4*t*(1-t):0.);
            return {{Add(surface.Point,Mul(surface.Direction,height)),surface.Direction},surface.Point,height};
        }
        void Append(const Sample& s){Result.Poses.push_back(s.Pose);Result.SurfacePoints.push_back(s.Surface);Result.Heights.push_back(s.Height);}
        void Trace(double a,const Sample& x,double b,const Sample& y,int depth){
            Check();const auto t=(a+b)*.5;const auto mid=At(t);
            const bool refine=Norm(Sub(mid.Pose.Point,Lerp(x.Pose.Point,y.Pose.Point,.5)))>Q.Tolerance
                ||Norm(Sub(y.Surface,x.Surface))>2.||Angle(x.Pose.Direction,y.Pose.Direction)>.05;
            if(refine){if(depth>=16)throw std::invalid_argument("TRAVEL_SURFACE_DISCONTINUOUS");Trace(a,x,t,mid,depth+1);Trace(t,mid,b,y,depth+1);}else Append(y);
        }
        double Distance(const TravelPose& p,std::string& object){
            Check();const std::array<double,6> key{p.Point[0],p.Point[1],p.Point[2],p.Direction[0],p.Direction[1],p.Direction[2]};
            if(const auto found=Distances.find(key);found!=Distances.end()){object=found->second.second;return found->second.first;}
            double certified=0;
            for(const auto& prior:Certificates){const auto displacement=Norm(Sub(p.Point,prior.Pose.Point))+2*Radius*std::sin(Angle(p.Direction,prior.Pose.Direction)*.5);
                const auto lower=prior.Lower-displacement-1e-6;if(lower>certified){certified=lower;object=prior.Object;}}
            if(certified>Q.Clearance+.25)return certified;
            const auto distance=ComputeDistance(p,object);Distances.emplace(key,std::pair{distance,object});
            if(distance>Q.Clearance&&std::isfinite(distance)){
                const Certificate certificate{p,distance,object};
                if(Certificates.size()<256)Certificates.push_back(certificate);else{Certificates[NextCertificate]=certificate;NextCertificate=(NextCertificate+1)%Certificates.size();}
            }
            return distance;
        }
        double ComputeDistance(const TravelPose& p,std::string& object){
            Check();if(++Result.DistanceQueries>30000)throw std::runtime_error("TRAVEL_CLEARANCE_BUDGET");
            gp_Trsf pose;pose.SetRotation(gp_Quaternion(gp_Vec(0,0,1),gp_Vec(p.Direction[0],p.Direction[1],p.Direction[2])));pose.SetTranslationPart(gp_Vec(p.Point[0],p.Point[1],p.Point[2]));
            const auto head=Head.Moved(TopLoc_Location(pose));Bnd_Box box;BRepBndLib::Add(head,box,false);double best=std::numeric_limits<double>::infinity();
            // Box separation is a conservative lower distance, sufficient to
            // certify an interval once its motion bound is subtracted. Avoid
            // exact distance to every engraved face for clear exterior moves.
            double separation=best;for(const auto& obstacle:Geometry.Objects){const auto lower=std::max(box.Distance(obstacle.Box),obstacle.Lower(p,HeadSolids));if(lower<separation){separation=lower;object=obstacle.Id;}}
            if(separation>=Q.Clearance+1e-6)return separation;
            BRepExtrema_DistShapeShape distance;distance.SetMultiThread(ParallelDistance);distance.LoadS1(head);
            CollisionTransform transform{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
            for(int row=0;row<3;++row)for(int col=0;col<4;++col)transform[row*4+col]=pose.Value(row+1,col+1);
            const auto coalLower=[&](const CollisionDistanceModelRef& bounds){
                TravelProfile::Timer timer(Profile.Enabled?&Profile.CoalMs:nullptr);
                const auto proximity=Geometry.CollisionService->LowerBound(bounds,CollisionHead,transform,Q.Clearance+1e-6,[&]{Check();return false;});
                Profile.CoalQueries+=proximity.NarrowPhaseQueries;return proximity.LowerBound;
            };
            for(const auto& obstacle:Geometry.Objects){Check();if(box.Distance(obstacle.Box)>best)continue;
                {
                TravelProfile::Timer timer(Profile.Enabled?&Profile.ClassifyMs:nullptr);
                // Faces accelerate exact boundary distance. Explicit solid
                // containment retains the semantics of full solid distance.
                for(const auto& probe:HeadProbes){const auto center=P(probe).Transformed(pose);
                    if(!obstacle.Box.IsOut(center)&&!obstacle.Outside(center))for(const auto& inside:obstacle.Solids){++Profile.Classifications;inside->Perform(center,1e-7);if(inside->State()==TopAbs_IN||inside->State()==TopAbs_ON||(BoundsOnly&&inside->State()==TopAbs_UNKNOWN)){object=obstacle.Id;return 0;}}}
                // Also detect a small obstacle wholly enclosed by the head.
                for(const auto& probe:obstacle.Probes){
                    if(box.IsOut(probe))continue;const auto local=probe.Transformed(pose.Inverted());for(const auto& inside:HeadClassifiers){++Profile.Classifications;inside->Perform(local,1e-7);if(inside->State()==TopAbs_IN||inside->State()==TopAbs_ON||(BoundsOnly&&inside->State()==TopAbs_UNKNOWN)){object=obstacle.Id;return 0;}}}
                }
                {
                    const auto lower=coalLower(obstacle.CollisionBounds);
                    if(BoundsOnly){if(lower<best){best=lower;object=obstacle.Id;}continue;}
                    // Collision geometry includes CAD tolerances and controlled
                    // mesh deflection. Overlap remains unresolved and continues
                    // to the exact CAD kernel.
                    if(lower>=Q.Clearance+1e-6){if(lower<best){best=lower;object=obstacle.Id;}continue;}
                }
                std::vector<std::pair<double,const Face*>> faces;for(const auto& face:obstacle.Faces){auto lower=std::max(box.Distance(face.Box),face.Lower(p,HeadSolids,Q.Clearance+2));
                    if(lower<Q.Clearance+1e-6)lower=std::max(lower,coalLower(face.CollisionBounds));
                    if(lower<=best)faces.emplace_back(lower,&face);}
                std::sort(faces.begin(),faces.end(),[](auto a,auto b){return a.first<b.first;});
                for(const auto& [lower,face]:faces){if(lower>best)break;Check();
                    // Remaining faces have at least this lower distance. It is
                    // conservative for the same continuous-motion bound below.
                    if(lower>=Q.Clearance+1e-6){best=lower;object=obstacle.Id;break;}
                    ++Profile.Distances;TravelProfile::Timer timer(Profile.Enabled?&Profile.DistanceMs:nullptr);distance.LoadS2(face->Shape);distance.Perform();
                    if(!distance.IsDone()||distance.NbSolution()<1)throw std::runtime_error("TRAVEL_CLEARANCE_FAILED");
                    if(distance.Value()<best){best=distance.Value();object=obstacle.Id;}if(best<1e-8)return best;}
            }
            if(!std::isfinite(best))throw std::runtime_error("TRAVEL_CLEARANCE_FAILED");
            return best;
        }
        bool Interval(const TravelPose& a,const TravelPose& b,int depth){
            const auto mid=Mid(a,b);std::string object;const auto d=Distance(mid,object);
            if(d<Q.Clearance+1e-7){Result.Status="Collision";Result.Obstacle=object;Result.Reason="TRAVEL_CLEARANCE_VIOLATION";return false;}
            // Rigid motion displacement bound for EVERY point of the envelope
            // between the midpoint and either endpoint, not finite point sampling.
            const auto bound=Norm(Sub(a.Point,b.Point))*.5+2*Radius*std::sin(Angle(a.Direction,b.Direction)*.25);
            const auto lower=d-bound-1e-6;
            if(lower>=Q.Clearance){Result.CertifiedClearance=std::min(Result.CertifiedClearance,lower);return true;}
            if(depth>=18){Result.Status="Unchecked";Result.Reason="TRAVEL_CLEARANCE_UNRESOLVED";Result.Obstacle=object;return false;}
            return Interval(a,mid,depth+1)&&Interval(mid,b,depth+1);
        }
        bool Validate(){
            Result.CertifiedClearance=std::numeric_limits<double>::infinity();Result.Obstacle.clear();Result.Reason.clear();
            if(Head.IsNull()){Result.Status="Unchecked";Result.Reason="TRAVEL_ENVELOPE_REQUIRED";Result.CertifiedClearance=0;return false;}
            for(std::size_t i=0;i<Result.Poses.size();++i){std::string object;const auto d=Distance(Result.Poses[i],object);
                if(d<Q.Clearance+1e-7){Result.Status="Collision";Result.Reason="TRAVEL_CLEARANCE_VIOLATION";Result.Obstacle=object;Result.FailedSegment=i?i-1:0;return false;}}
            for(std::size_t i=1;i<Result.Poses.size();++i)if(!Interval(Result.Poses[i-1],Result.Poses[i],0)){Result.FailedSegment=i-1;return false;}
            Result.Status="Clear";return true;
        }
        // Reach an exterior corridor before changing sides or turning the head.
        // The box includes every obstacle, expanded by the complete head radius.
        // Visibility edges stay outside its open interior. Exact continuous
        // checks still validate withdrawal, traverse, rotation and approach.
        std::vector<TravelPose> ExteriorRoute(const TravelPose& start,const TravelPose& end){
            double x0,y0,z0,x1,y1,z1;Geometry.Bounds.Get(x0,y0,z0,x1,y1,z1);
            const double pad=Radius+Q.Clearance+std::max(1.,Q.Tolerance*2);
            V low{x0-pad,y0-pad,z0-pad},high{x1+pad,y1+pad,z1+pad};
            const auto exit=[&](const TravelPose& p){
                bool outside=false;for(int i=0;i<3;++i)outside|=p.Point[i]<low[i]||p.Point[i]>high[i];
                double d=outside?0:std::numeric_limits<double>::infinity();
                if(!outside)for(int i=0;i<3;++i)if(std::abs(p.Direction[i])>1e-10)d=std::min(d,((p.Direction[i]>0?high[i]:low[i])-p.Point[i])/p.Direction[i]);
                return Add(p.Point,Mul(p.Direction,std::max(Q.LiftHeight,d+1e-4)));
            };
            std::vector<V> nodes{exit(start),exit(end)};
            for(int mask=0;mask<8;++mask)nodes.push_back({mask&1?high[0]:low[0],mask&2?high[1]:low[1],mask&4?high[2]:low[2]});
            const auto visible=[&](V a,V b){double enter=0,leave=1;
                for(int i=0;i<3;++i){const double l=low[i]+1e-7,h=high[i]-1e-7,d=b[i]-a[i];
                    if(std::abs(d)<1e-12){if(a[i]<=l||a[i]>=h)return true;continue;}
                    double u=(l-a[i])/d,v=(h-a[i])/d;if(u>v)std::swap(u,v);enter=std::max(enter,u);leave=std::min(leave,v);if(enter>=leave)return true;}
                return enter>=leave;
            };
            const auto n=nodes.size();std::vector<double> distance(n,std::numeric_limits<double>::infinity());std::vector<std::size_t> parent(n,n);std::vector<bool> done(n);distance[0]=0;
            for(std::size_t k=0;k<n;++k){std::size_t a=n;for(std::size_t i=0;i<n;++i)if(!done[i]&&(a==n||distance[i]<distance[a]))a=i;
                if(a==n||!std::isfinite(distance[a]))break;done[a]=true;if(a==1)break;
                for(std::size_t b=0;b<n;++b)if(!done[b]&&visible(nodes[a],nodes[b])){const auto cost=distance[a]+Norm(Sub(nodes[a],nodes[b]));if(cost<distance[b]){distance[b]=cost;parent[b]=a;}}}
            if(!std::isfinite(distance[1]))throw std::runtime_error("TRAVEL_NO_EXTERIOR_ROUTE");
            std::vector<std::size_t> route;for(auto i=std::size_t(1);i!=n;i=parent[i])route.push_back(i);std::reverse(route.begin(),route.end());
            std::vector<TravelPose> poses{start};for(auto i:route)poses.push_back({nodes[i],start.Direction});
            if(Angle(start.Direction,end.Direction)>Pi-1e-6){
                const auto a=start.Direction;V side=std::abs(a[0])>std::abs(a[2])?V{-a[1],a[0],0}:V{0,-a[2],a[1]};
                poses.push_back({nodes[1],Unit(side)});
            }
            poses.push_back({nodes[1],end.Direction});poses.push_back(end);return poses;
        }
        static double Duration(double distance,double velocity,double acceleration){return distance<=velocity*velocity/acceleration?2*std::sqrt(distance/acceleration):distance/velocity+velocity/acceleration;}
        void Metrics(){Result.Length=0;Result.EstimatedSeconds=0;for(std::size_t i=1;i<Result.Poses.size();++i){const auto& a=Result.Poses[i-1];const auto& b=Result.Poses[i];const auto length=Norm(Sub(a.Point,b.Point));Result.Length+=length;
            Result.EstimatedSeconds+=std::max(Duration(length,Q.LinearSpeed,Q.LinearAcceleration),Duration(Angle(a.Direction,b.Direction),Q.AngularSpeed,Q.AngularAcceleration));}
            if(Result.Status!="Clear"||!std::isfinite(Result.CertifiedClearance))Result.CertifiedClearance=0;
        }
        SurfaceTravelResult Run(){
            try{
                const TravelPose start{Add(Q.Start.Point,Mul(Q.Start.Direction,Q.FollowHeight)),Q.Start.Direction},end{Add(Q.End.Point,Mul(Q.End.Direction,Q.FollowHeight)),Q.End.Direction};
                // Every strategy has these same endpoint poses. No higher
                // withdrawal or exterior detour can repair a colliding endpoint.
                if(!Head.IsNull())for(const auto& p:{start,end}){std::string object;if(Distance(p,object)<Q.Clearance+1e-7){
                    Result.Poses={start,end};Result.Status="Collision";Result.Reason="TRAVEL_ENDPOINT_CLEARANCE_VIOLATION";Result.Obstacle=object;Metrics();return Result;}}
                if(Q.Kind==SurfaceTravelKind::SafeExterior){
                    // Always leave the complete obstacle bounds before traverse
                    // and rotation. Do not select a lower, faster shortcut.
                    Result.Poses=ExteriorRoute(start,end);Validate();
                }
                else if(Q.Kind==SurfaceTravelKind::Follow){const auto a=At(0),b=At(1);Append(a);Trace(0,a,1,b,0);Validate();}
                else if(Q.Kind==SurfaceTravelKind::Arch){
                    double x0,y0,z0,x1,y1,z1;Geometry.Bounds.Get(x0,y0,z0,x1,y1,z1);
                    const auto span=Norm({x1-x0,y1-y0,z1-z0})+2*Radius+2*Q.Clearance;
                    SurfaceTravelResult failed;bool hasFailure=false;double previousLift=-1;
                    for(double lift:{Q.LiftHeight,std::max(Q.LiftHeight,span*.125),std::max(Q.LiftHeight,span*.25),std::max(Q.LiftHeight,span*.5),std::max(Q.LiftHeight,span),std::max(Q.LiftHeight,span*2)}){
                        if(lift==previousLift)continue;previousLift=lift;Check();ArchLift=lift;Budget=0;Result={};
                        const auto a=At(0),b=At(1);Append(a);Trace(0,a,1,b,0);
                        if(Validate())break;
                        if(!hasFailure){failed=Result;hasFailure=true;}
                        if(Head.IsNull())break;
                    }
                    if(Result.Status!="Clear"&&hasFailure)Result=failed;
                }
                else{
                    double x0,y0,z0,x1,y1,z1;Geometry.Bounds.Get(x0,y0,z0,x1,y1,z1);const double span=Norm({x1-x0,y1-y0,z1-z0})+2*Radius+2*Q.Clearance;
                    SurfaceTravelResult best,failed;double cost=std::numeric_limits<double>::infinity(),previousLift=-1;bool hasFailure=false;
                    for(double lift:{Q.LiftHeight,std::max(Q.LiftHeight,span*.125),std::max(Q.LiftHeight,span*.25),std::max(Q.LiftHeight,span*.5),std::max(Q.LiftHeight,span),std::max(Q.LiftHeight,span*2)}){
                        if(lift==previousLift)continue;previousLift=lift;
                        Check();Result={};Result.Poses={start,{Add(start.Point,Mul(start.Direction,lift)),start.Direction},{Add(end.Point,Mul(end.Direction,lift)),end.Direction},end};
                        // Geometry-independent cost is exact for this comparison
                        // model. A candidate no faster than an already clear one
                        // cannot win, so avoid expensive BRep distance queries.
                        Metrics();if(Result.EstimatedSeconds>=cost)continue;
                        // Opposite directions have no unique spherical arc;
                        // their explicit two-stage turn belongs in the corridor.
                        const bool clear=Angle(start.Direction,end.Direction)<Pi-1e-6&&Validate();Metrics();if(clear&&Result.EstimatedSeconds<cost){cost=Result.EstimatedSeconds;best=Result;}
                        if(!hasFailure){failed=Result;hasFailure=true;}if(Head.IsNull())break;
                    }
                    if(!Head.IsNull()&&!std::isfinite(cost)){
                        Result={};Result.Poses=ExteriorRoute(start,end);
                        if(Validate()){Metrics();best=Result;cost=Result.EstimatedSeconds;}
                        else if(!hasFailure||failed.Reason.empty())failed=Result;
                    }
                    Result=std::isfinite(cost)?best:failed;
                }
            }catch(const std::exception& error){Check();Result.Status="Unchecked";Result.Reason=error.what();}
            Metrics();return Result;
        }
    };
    void ValidateQuery(const SurfaceTravelQuery& q){
        if(q.Obstacles.empty()||q.Surface>=q.Obstacles.size()||q.Obstacles.size()>128)throw std::invalid_argument("TRAVEL_OBSTACLES_INVALID");
        for(const auto& pose:{q.Start,q.End}){for(auto x:pose.Point)if(!std::isfinite(x)||std::abs(x)>1e8)throw std::invalid_argument("TRAVEL_POINT_INVALID");if(!std::isfinite(Norm(pose.Direction))||std::abs(Norm(pose.Direction)-1)>1e-8)throw std::invalid_argument("TRAVEL_DIRECTION_INVALID");}
        for(auto x:{q.FollowHeight,q.LiftHeight,q.Tolerance,q.LinearSpeed,q.LinearAcceleration,q.AngularSpeed,q.AngularAcceleration})if(!std::isfinite(x)||x<=0||x>1e6)throw std::invalid_argument("TRAVEL_PARAMETER_INVALID");
        for(auto x:{q.Clearance,q.EnvelopeRadius,q.EnvelopeLength})if(!std::isfinite(x)||x<0||x>1e6)throw std::invalid_argument("TRAVEL_PARAMETER_INVALID");
        if((q.EnvelopeRadius==0)!=(q.EnvelopeLength==0))throw std::invalid_argument("TRAVEL_ENVELOPE_INVALID");
        if(q.Kind!=SurfaceTravelKind::Follow&&q.Kind!=SurfaceTravelKind::Arch&&q.Kind!=SurfaceTravelKind::Retract&&q.Kind!=SurfaceTravelKind::SafeExterior)throw std::invalid_argument("TRAVEL_KIND_INVALID");
    }
    class CSurfaceTravelService final:public ISurfaceTravelService
    {
        AUTO_REGIST_SERVICE(ISurfaceTravelService,CSurfaceTravelService,ICollisionDistanceService)
        std::shared_ptr<ICollisionDistanceService> CollisionService;
        struct CachedGeometry {std::vector<TravelObstacle> Sources;std::unique_ptr<Prepared> Geometry;};
        mutable std::mutex GeometryMutex;
        mutable std::vector<CachedGeometry> GeometryCache;
        static bool SameGeometry(const std::vector<TravelObstacle>& a,const std::vector<TravelObstacle>& b){
            if(a.size()!=b.size())return false;
            for(std::size_t i=0;i<a.size();++i)if(a[i].Geometry!=b[i].Geometry||a[i].Transform!=b[i].Transform||a[i].Id!=b[i].Id)return false;
            return true;
        }
        std::unique_ptr<Prepared> Acquire(const SurfaceTravelQuery& q,const std::function<void()>& check)const{
            {std::lock_guard lock(GeometryMutex);for(auto it=GeometryCache.begin();it!=GeometryCache.end();++it)if(SameGeometry(it->Sources,q.Obstacles)){
                auto geometry=std::move(it->Geometry);GeometryCache.erase(it);return geometry;}}
            return std::make_unique<Prepared>(q,check,CollisionService);
        }
        void Release(const SurfaceTravelQuery* q,std::unique_ptr<Prepared> geometry)const{
            if(!q||!geometry)return;
            std::lock_guard lock(GeometryMutex);if(GeometryCache.size()>=8)GeometryCache.erase(GeometryCache.begin());
            GeometryCache.push_back({q->Obstacles,std::move(geometry)});
        }
    public:
        explicit CSurfaceTravelService(std::shared_ptr<ICollisionDistanceService> service):CollisionService(std::move(service)){}
        void OnLoad() override{} void OnUnload() override{std::lock_guard lock(GeometryMutex);GeometryCache.clear();}
        std::vector<SurfaceTravelResult> Plan(const std::vector<SurfaceTravelQuery>& queries,const std::function<bool()>& cancelled) const override{
            bool stopped=false;ULONGLONG nextCancellationCheck=0;
            const auto check=[&]{if(stopped)throw std::runtime_error("TRAVEL_CANCELLED");if(cancelled){
                // GJK and CAD bounds can call this millions of times. Poll the
                // caller at most every 5 ms, without serializing every pair.
                const auto now=GetTickCount64();
                if(now>=nextCancellationCheck){nextCancellationCheck=now+5;
                    if(cancelled()){stopped=true;throw std::runtime_error("TRAVEL_CANCELLED");}
                }
            }};
            for(const auto& q:queries){check();ValidateQuery(q);}
            std::vector<SurfaceTravelResult> results(queries.size());
            std::unique_ptr<Prepared> geometry;const SurfaceTravelQuery* previous=nullptr;
            try{
                    // A batch leases one prepared CAD model. Parallel exact
                    // distances run inside OCCT; duplicating this large model
                    // across connection workers causes allocator contention.
                    for(std::size_t index=0;index<queries.size();++index){
                        check();const auto& q=queries[index];const bool same=previous&&SameGeometry(q.Obstacles,previous->Obstacles);
                        if(!same){Release(previous,std::move(geometry));geometry=Acquire(q,check);}previous=&q;
                        results[index]=Planner(q,*geometry,check,true).Run();
                    }
            }catch(...){Release(previous,std::move(geometry));throw;}
            // Other concurrent callers get exclusive classifier leases too.
            Release(previous,std::move(geometry));
            if(cancelled&&cancelled())throw std::runtime_error("TRAVEL_CANCELLED");
            return results;
        }
    };

    class CRapidPathPlanningService final:public IRapidPathPlanningService
    {
        AUTO_REGIST_SERVICE(IRapidPathPlanningService,CRapidPathPlanningService,ICollisionDistanceService,IRapidPathGeometryService,IRigidMotionCollisionService)
        std::shared_ptr<ICollisionDistanceService> Collision;
        std::shared_ptr<IRapidPathGeometryService> Construction;
        std::shared_ptr<IRigidMotionCollisionService> MotionCollision;
        #include "RapidBoxMesh.inc"
        mutable std::mutex CacheMutex;
        mutable std::vector<TravelObstacle> CachedSources;
        mutable std::unique_ptr<Prepared> CachedGeometry;
        static bool Same(const std::vector<TravelObstacle>& a,const std::vector<TravelObstacle>& b){
            if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i)if(a[i].Id!=b[i].Id||a[i].Geometry!=b[i].Geometry||a[i].Transform!=b[i].Transform)return false;return true;
        }
    public:
        CRapidPathPlanningService(std::shared_ptr<ICollisionDistanceService> collision,std::shared_ptr<IRapidPathGeometryService> construction,std::shared_ptr<IRigidMotionCollisionService> motion):Collision(std::move(collision)),Construction(std::move(construction)),MotionCollision(std::move(motion)){}
        void OnLoad()override{}void OnUnload()override{std::lock_guard lock(CacheMutex);CachedGeometry.reset();CachedSources.clear();}
        std::vector<RapidPathPlanningResult> Plan(const std::vector<RapidPathPlanningQuery>& queries,const std::function<bool()>& cancelled)const override{
            if(queries.size()>1&&std::all_of(queries.begin(),queries.end(),[](const auto& q){return q.Collision&&!q.Collision->Boxes.empty();})){
                std::vector<RapidPathPlanningResult> results(queries.size());
                // Warm immutable mesh indices without serializing the entire
                // first route search. Every connection then owns one mutable
                // session, including its alternative-path distance cache.
                auto first=MotionCollision->CreatePrimitiveMeshSession(BoxMotionQuery(queries.front()),cancelled);
                std::atomic_bool failed=false;
                const auto stop=[&]{return failed.load()||(cancelled&&cancelled());};
                const auto concurrency=std::min<std::size_t>(4,iCAX::OpenCascade::detail::GeometryConcurrency(0));
                iCAX::OpenCascade::detail::ForGeometryIndices(queries.size(),concurrency,[&](std::size_t index){
                    try{if(stop())throw std::runtime_error("TRAVEL_CANCELLED");results[index]=PlanBoxes(queries[index],stop,index==0?std::move(first):std::unique_ptr<IRigidMotionCollisionSession>{});}
                    catch(...){failed=true;throw;}
                });
                if(stop())throw std::runtime_error("TRAVEL_CANCELLED");return results;
            }
            const auto check=[&]{if(cancelled&&cancelled())throw std::runtime_error("TRAVEL_CANCELLED");};
            std::unique_ptr<Prepared> geometry;std::vector<TravelObstacle> sources;
            const auto release=[&]{if(!geometry)return;std::lock_guard lock(CacheMutex);CachedSources=sources;CachedGeometry=std::move(geometry);};
            std::vector<RapidPathPlanningResult> results;
            try{for(const auto& query:queries){check();
                if(query.Collision&&!query.Collision->Boxes.empty()){results.push_back(PlanBoxes(query,cancelled));continue;}
                if(!query.Collision||query.Collision->Obstacles.empty()||!std::isfinite(query.Clearance)||query.Clearance<0)throw std::invalid_argument("RAPID_COLLISION_INPUT_REQUIRED");
                const auto& env=*query.Collision;SurfaceTravelQuery q;q.Obstacles=env.Obstacles;q.EnvelopeSolids=env.Solids;q.EnvelopeBody=env.Body;q.EnvelopeCalibration=env.Calibration;
                q.Clearance=query.Clearance;q.Start=query.Geometry.Start;q.End=query.Geometry.End;q.FollowHeight=query.Geometry.FollowHeight;q.LiftHeight=query.Geometry.LiftHeight;q.Tolerance=query.Geometry.Tolerance;
                ValidateQuery(q);
                if(!geometry||!Same(sources,env.Obstacles)){
                    release();sources=env.Obstacles;
                    {std::lock_guard lock(CacheMutex);if(CachedGeometry&&Same(CachedSources,sources))geometry=std::move(CachedGeometry);}
                    if(!geometry)geometry=std::make_unique<Prepared>(q,check,Collision);
                }
                Planner validator(q,*geometry,check);validator.BoundsOnly=true;
                RapidPathPlanningResult result;auto candidate=query.Geometry;
                const auto attempt=[&]()->bool{
                    check();++result.Candidates;
                    const auto built=Construction->Generate({candidate},cancelled).front();
                    if(!built.Generated){result.Reason=built.Reason;return false;}
                    validator.Result={};validator.Result.Poses=built.Poses;
                    if(!validator.Validate()){result.Obstacle=validator.Result.Obstacle;return false;}
                    static_cast<RapidPathGeometryResult&>(result)=built;result.Status="Clear";result.Reason.clear();result.Obstacle.clear();
                    result.CertifiedClearance=validator.Result.CertifiedClearance;result.DetourOffset=candidate.LateralOffset;
                    result.Method=candidate.Manual?"Manual":Norm(candidate.LateralOffset)>1e-9?"LateralDetour":"Direct";return true;
                };
                const auto original=Construction->Generate({candidate},cancelled).front();
                if(!original.Generated){result.Reason=original.Reason;results.push_back(std::move(result));continue;}
                static_cast<RapidPathGeometryResult&>(result)=original;result.Generated=false;result.DetourOffset=candidate.LateralOffset;
                bool endpoints=true;for(const auto& pose:{original.Poses.front(),original.Poses.back()}){std::string obstacle;
                    if(validator.Distance(pose,obstacle)<=q.Clearance+1e-6){endpoints=false;result.Obstacle=obstacle;break;}}
                if(!endpoints){result.Reason="RAPID_ENDPOINT_BLOCKED";results.push_back(std::move(result));continue;}
                if(!attempt()){
                    if(candidate.Manual){result.Reason="RAPID_MANUAL_PATH_BLOCKED";results.push_back(std::move(result));continue;}
                    const auto up=Unit(Add(candidate.Start.Direction,candidate.End.Direction));
                    auto forward=Sub(candidate.End.Point,candidate.Start.Point);forward=Sub(forward,Mul(up,Dot(forward,up)));
                    if(Norm(forward)<1e-9){const V helper=std::abs(up[0])<.8?V{1,0,0}:V{0,1,0};forward=Sub(helper,Mul(up,Dot(helper,up)));}
                    forward=Unit(forward);const V side{up[1]*forward[2]-up[2]*forward[1],up[2]*forward[0]-up[0]*forward[2],up[0]*forward[1]-up[1]*forward[0]};
                    double radial=1;for(const auto& solid:validator.HeadSolids)radial=std::max({radial,solid.LowerRadius,solid.UpperRadius});
                    double a,b,c,d,e,f;geometry->Bounds.Get(a,b,c,d,e,f);const auto span=Norm({d-a,e-b,f-c});
                    std::vector<double> radii{std::max(2.,radial*1.5),std::max(2.,span*.125),std::max(2.,span*.25),std::max(2.,span*.5),std::max(2.,span)};
                    std::sort(radii.begin(),radii.end());radii.erase(std::unique(radii.begin(),radii.end()),radii.end());
                    for(double radius:radii){for(int direction:{0,4,1,7,3,5,2,6}){
                        const auto angle=direction*Pi/4;candidate.LateralOffset=Mul(Add(Mul(side,std::cos(angle)),Mul(forward,std::sin(angle))),radius);
                        if(attempt())break;
                    }if(result.Generated)break;}
                    if(!result.Generated){result.Status="Unchecked";result.Reason="RAPID_NO_FEASIBLE_ROUTE_WITHIN_PARAMETERS";}
                }
                results.push_back(std::move(result));
            }}catch(...){release();throw;}
            release();check();return results;
        }
    };
}
