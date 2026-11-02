#include "pch.h"
#include "CadSnapFeatureService.h"
#include "OpenCascadeBRepBuilder.h"
#include "Services/ServicesHelper.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <BRepBuilderAPI_FindPlane.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <GeomLib_IsPlanarSurface.hxx>
#include <Geom_Plane.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_OBB.hxx>
#include <map>
#include <set>

namespace {
    using namespace iCAX::Geometry;
    using Point=std::array<double,3>;
    Point P(const gp_Pnt& p){return {p.X(),p.Y(),p.Z()};}
    Point D(const gp_Dir& p){return {p.X(),p.Y(),p.Z()};}
    gp_Pnt P(const Point& p){return {p[0],p[1],p[2]};}
    gp_Dir D(const Point& p){return {p[0],p[1],p[2]};}
    bool IsPoint(ESnapFeatureKind k){return k==ESnapFeatureKind::Endpoint||k==ESnapFeatureKind::Midpoint||k==ESnapFeatureKind::CircleCenter||k==ESnapFeatureKind::SphereCenter||k==ESnapFeatureKind::Center;}
    void AddBox(SCadSnapFeatures& result,const std::string& kind,const Point& center,const std::array<Point,3>& axes,const Point& half){
        for(int i=0;i<3;++i)if(!std::isfinite(center[i])||!std::isfinite(half[i])||half[i]<0)throw std::invalid_argument("CAD_SNAP_UNBOUNDED_BREP");
        const auto at=[&](Point offset){Point p=center;for(int i=0;i<3;++i)for(int j=0;j<3;++j)p[j]+=offset[i]*half[i]*axes[i][j];return p;};
        const auto feature=[&](const std::string& id,ESnapFeatureKind type,Point p,const std::string& label){SCadSnapFeature f{};f.Id="bounds:"+kind+":"+id;f.Kind=type;f.Point=p;f.Derived=true;f.BoxKind=kind;f.Label=kind+" 包围盒 · "+label;return f;};
        result.Features.push_back(feature("center",ESnapFeatureKind::Center,center,"中心"));
        for(int mask=0;mask<8;++mask)result.Features.push_back(feature("corner:"+std::to_string(mask),ESnapFeatureKind::Endpoint,at({mask&1?1.:-1.,mask&2?1.:-1.,mask&4?1.:-1.}),"角点 "+std::to_string(mask+1)));
        for(int axis=0;axis<3;++axis){const int u=(axis+1)%3,v=(axis+2)%3;
            for(int side:{-1,1}){
                Point offset{};offset[axis]=side;const auto suffix=std::to_string(axis)+":"+std::to_string(side);
                const auto name=std::string(side>0?"+":"-")+"UVW"[axis];
                result.Features.push_back(feature("face-center:"+suffix,ESnapFeatureKind::Center,at(offset),name+" 面中心"));
                if(half[u]>1e-9&&half[v]>1e-9){auto f=feature("face:"+suffix,ESnapFeatureKind::Plane,at(offset),name+" 面");
                    for(int j=0;j<3;++j){f.Direction[j]=side*axes[axis][j];f.XDirection[j]=axes[u][j];}f.Radius=std::max(half[u],half[v]);
                    for(const auto& xy:{std::array<int,2>{-1,-1},{1,-1},{1,1},{-1,1}}){auto c=offset;c[u]=xy[0];c[v]=side*xy[1];f.PlaneCorners.push_back(at(c));}result.Features.push_back(std::move(f));}
            }
            if(half[axis]<=1e-9)continue;
            for(int su:{-1,1})for(int sv:{-1,1}){Point offset{};offset[u]=su;offset[v]=sv;
                const auto suffix=std::to_string(axis)+":"+std::to_string(su)+":"+std::to_string(sv);
                auto f=feature("edge:"+suffix,ESnapFeatureKind::Line,at(offset),"边线");f.Direction=axes[axis];
                offset[axis]=-1;f.EdgeSamples.push_back(at(offset));offset[axis]=1;f.EdgeSamples.push_back(at(offset));result.Features.push_back(std::move(f));
                offset[axis]=0;result.Features.push_back(feature("midpoint:"+suffix,ESnapFeatureKind::Midpoint,at(offset),"边中点"));
            }
        }
    }
    void AddBounds(SCadSnapFeatures& result,const TopoDS_Shape& shape){
        Bnd_OBB oriented;BRepBndLib::AddOBB(shape,oriented,false,true,false);
        if(oriented.IsVoid())throw std::runtime_error("CAD_SNAP_OBB_FAILED");
        AddBox(result,"OBB",P(gp_Pnt(oriented.Center())),{{D(gp_Dir(oriented.XDirection())),D(gp_Dir(oriented.YDirection())),D(gp_Dir(oriented.ZDirection()))}},
            {oriented.XHSize(),oriented.YHSize(),oriented.ZHSize()});
    }
    class CCadSnapFeatureService final:public ICadSnapFeatureService {
        AUTO_REGIST_SERVICE(ICadSnapFeatureService,CCadSnapFeatureService)
    public:
        void OnLoad() override {}
        void OnUnload() override {}
        SCadSnapFeature Construct(ESnapConstruction kind,const std::vector<SCadSnapFeature>& sources)const override{
            const size_t count=kind==ESnapConstruction::ThreePointPlane?3:kind==ESnapConstruction::TwoPointDirection?2:1;
            if(sources.size()!=count)throw std::invalid_argument("CAD_SNAP_CONSTRUCTION_COUNT");
            SCadSnapFeature f;f.Id="construction:"+std::to_string(static_cast<int>(kind));f.Derived=true;
            for(const auto& s:sources){f.Id+=":"+s.Id;f.FaceIds.insert(f.FaceIds.end(),s.FaceIds.begin(),s.FaceIds.end());f.EdgeIds.insert(f.EdgeIds.end(),s.EdgeIds.begin(),s.EdgeIds.end());}
            f.Point=sources[0].Point;
            if(count==1)f.BoxKind=sources[0].BoxKind;
            if(count==1){const auto& plane=sources[0];if(plane.Kind!=ESnapFeatureKind::Plane)throw std::invalid_argument("CAD_SNAP_PLANE_REQUIRED");
                if(kind==ESnapConstruction::PlaneCenter){f.Kind=ESnapFeatureKind::Center;f.Label="基准中心";return f;}
                if(kind==ESnapConstruction::PlaneNormal){f.Kind=ESnapFeatureKind::Direction;f.Direction=plane.Direction;f.Label=plane.Label+"法向";return f;}
                f.Kind=ESnapFeatureKind::Direction;f.Direction=kind==ESnapConstruction::PlaneU?plane.XDirection:D(D(plane.Direction).Crossed(D(plane.XDirection)));f.Label=kind==ESnapConstruction::PlaneU?"基准面 U 方向":"基准面 V 方向";return f;}
            for(const auto& s:sources)if(!IsPoint(s.Kind))throw std::invalid_argument("CAD_SNAP_POINT_REQUIRED");
            gp_Vec u(P(sources[0].Point),P(sources[1].Point));if(u.Magnitude()<1e-7)throw std::invalid_argument("CAD_SNAP_COINCIDENT_POINTS");
            if(count==2){f.Kind=ESnapFeatureKind::Direction;f.Direction=D(gp_Dir(u));f.Label="两点方向";return f;}
            gp_Vec v(P(sources[0].Point),P(sources[2].Point)),normal=u.Crossed(v);
            if(v.Magnitude()<1e-7||normal.Magnitude()/(u.Magnitude()*v.Magnitude())<1e-7)throw std::invalid_argument("CAD_SNAP_COLLINEAR_POINTS");
            f.Kind=ESnapFeatureKind::Plane;f.Direction=D(gp_Dir(normal));f.XDirection=D(gp_Dir(u));f.Radius=std::max(u.Magnitude(),v.Magnitude())*.5;f.Label="三点基准面";return f;
        }
        SCadSnapFeatures Analyze(const iCAX::GeometryData::BRepModel& model) const override {
            const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(model);
            if(!built.bOK)throw std::invalid_argument("CAD_SNAP_INVALID_BREP");
            SCadSnapFeatures result;
            std::map<std::uint64_t,std::set<std::uint64_t>> edgeFaces,vertexFaces;
            for(const auto& face:model.Faces)for(const auto wireId:face.WireIds)
                for(const auto& wire:model.Wires)if(wire.Id==wireId)for(const auto& use:wire.Coedges)edgeFaces[use.EdgeId].insert(face.Id);
            for(const auto& edge:model.Edges)for(auto id:{edge.StartVertexId,edge.EndVertexId})vertexFaces[id].insert(edgeFaces[edge.Id].begin(),edgeFaces[edge.Id].end());
            for(const auto& vertex:model.Vertices){
                SCadSnapFeature f;f.Id="vertex:"+std::to_string(vertex.Id);f.Kind=ESnapFeatureKind::Endpoint;f.VertexId=vertex.Id;
                f.Point={vertex.Position.X,vertex.Position.Y,vertex.Position.Z};f.FaceIds.assign(vertexFaces[vertex.Id].begin(),vertexFaces[vertex.Id].end());result.Features.push_back(std::move(f));
            }
            for(const auto& [id,edge]:built.Edges){
                if(BRep_Tool::Degenerated(edge))continue;
                BRepAdaptor_Curve curve(edge);const auto first=curve.FirstParameter(),last=curve.LastParameter();
                if(!std::isfinite(first)||!std::isfinite(last)||first>=last)throw std::invalid_argument("CAD_SNAP_INVALID_EDGE_RANGE");
                const auto length=GCPnts_AbscissaPoint::Length(curve,first,last,1e-8);
                GCPnts_AbscissaPoint middle(1e-8,curve,length*.5,first);
                if(!middle.IsDone())throw std::runtime_error("CAD_SNAP_MIDPOINT_FAILED");
                SCadSnapFeature f;f.Id="edge:"+std::to_string(id);f.Kind=curve.GetType()==GeomAbs_Line?ESnapFeatureKind::Line:ESnapFeatureKind::Edge;
                f.EdgeId=id;f.EdgeIds={id};f.FaceIds.assign(edgeFaces[id].begin(),edgeFaces[id].end());f.Point=P(curve.Value(middle.Parameter()));
                if(curve.GetType()==GeomAbs_Line)f.Direction=D(curve.Line().Direction());
                const int count=curve.GetType()==GeomAbs_Line?1:96;
                for(int n=0;n<=count;++n)f.EdgeSamples.push_back(P(curve.Value(first+(last-first)*n/count)));
                result.Features.push_back(f);
                f.EdgeSamples.clear();f.Id="midpoint:"+std::to_string(id);f.Kind=ESnapFeatureKind::Midpoint;result.Features.push_back(f);
                if(curve.GetType()==GeomAbs_Circle){f.Id="center:"+std::to_string(id);f.Kind=ESnapFeatureKind::CircleCenter;f.Point=P(curve.Circle().Location());f.Direction=D(curve.Circle().Axis().Direction());f.Radius=curve.Circle().Radius();result.Features.push_back(f);}
                if(curve.GetType()==GeomAbs_Circle||curve.GetType()==GeomAbs_Ellipse){
                    const bool circle=curve.GetType()==GeomAbs_Circle;const auto frame=circle?curve.Circle().Position():curve.Ellipse().Position();
                    f.Derived=true;f.Point=P(frame.Location());f.Direction=D(frame.Direction());f.XDirection=D(frame.XDirection());f.Radius=circle?curve.Circle().Radius():curve.Ellipse().MajorRadius();
                    f.Id="curve-plane:"+std::to_string(id);f.Kind=ESnapFeatureKind::Plane;f.Label=circle?"圆所在平面":"椭圆所在平面";result.Features.push_back(f);
                    if(!circle){f.Id="ellipse-center:"+std::to_string(id);f.Kind=ESnapFeatureKind::Center;f.Label="椭圆中心";result.Features.push_back(f);
                        f.Id="ellipse-major:"+std::to_string(id);f.Kind=ESnapFeatureKind::Direction;f.Direction=f.XDirection;f.Label="椭圆长轴";result.Features.push_back(f);}
                }
            }
            // Closed planar boundaries remain useful even on a curved face.
            // FindPlane verifies all edges using BRep tolerances; an arbitrary
            // spatial loop is never silently replaced by a fitted plane.
            for(const auto& wire:model.Wires){if(!wire.Closed)continue;BRepBuilderAPI_MakeWire builder;SCadSnapFeature f;std::set<std::uint64_t> faces;
                for(const auto& use:wire.Coedges){auto found=built.Edges.find(use.EdgeId);if(found==built.Edges.end())continue;auto e=found->second;if(use.Orientation==iCAX::GeometryData::ETopologyOrientation::Reversed)e.Reverse();builder.Add(e);f.EdgeIds.push_back(use.EdgeId);faces.insert(edgeFaces[use.EdgeId].begin(),edgeFaces[use.EdgeId].end());}
                if(!builder.IsDone())continue;const auto shape=builder.Wire();BRepBuilderAPI_FindPlane planar(shape);if(!planar.Found())continue;
                BRepBuilderAPI_MakeFace cap(planar.Plane()->Pln(),shape,true);if(!cap.IsDone()||!BRepCheck_Analyzer(cap.Face()).IsValid())continue;
                GProp_GProps mass;BRepGProp::SurfaceProperties(cap.Face(),mass,1e-10);if(mass.Mass()<1e-12)continue;
                f.Id="wire-plane:"+std::to_string(wire.Id);f.Kind=ESnapFeatureKind::Plane;f.Point=P(mass.CentreOfMass());f.Direction=D(planar.Plane()->Axis().Direction());f.XDirection=D(planar.Plane()->Position().XDirection());f.Radius=std::sqrt(mass.Mass()/std::acos(-1.));f.FaceIds.assign(faces.begin(),faces.end());f.Derived=true;f.Label="轮廓基准面";result.Features.push_back(f);
                f.Id="wire-center:"+std::to_string(wire.Id);f.Kind=ESnapFeatureKind::Center;f.Label="轮廓面积中心";result.Features.push_back(f);
            }
            for(const auto& [id,canonical]:built.Faces){
                auto face=canonical;
                for(TopExp_Explorer it(built.Shape,TopAbs_FACE);it.More();it.Next())if(it.Current().IsSame(face)){face=TopoDS::Face(it.Current());break;}
                BRepAdaptor_Surface surface(face);SCadSnapFeature f;f.FaceIds={id};
                switch(surface.GetType()){
                case GeomAbs_Plane:{f.Kind=ESnapFeatureKind::Plane;f.Id="plane:"+std::to_string(id);f.Point=P(surface.Plane().Location());f.XDirection=D(surface.Plane().Position().XDirection());auto normal=surface.Plane().Axis().Direction();if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();f.Direction=D(normal);break;}
                case GeomAbs_Cylinder:{
                    const auto cylinder=surface.Cylinder();const auto origin=cylinder.Location();const auto direction=cylinder.Axis().Direction();
                    f.Kind=ESnapFeatureKind::Axis;f.Id="axis:"+std::to_string(id);f.Point=P(origin);f.Direction=D(direction);f.Radius=cylinder.Radius();
                    // A trimmed cylindrical surface has no unique mouth circle.
                    // Its area centroid projected onto the analytic axis defines
                    // a reproducible point independent of the surface UV origin.
                    GProp_GProps mass;BRepGProp::SurfaceProperties(face,mass,1e-10);
                    if(mass.Mass()>1e-12){
                        auto center=f;center.Id="cylinder-center:"+std::to_string(id);center.Kind=ESnapFeatureKind::Center;
                        center.Point=P(origin.Translated(gp_Vec(direction)*gp_Vec(origin,mass.CentreOfMass()).Dot(gp_Vec(direction))));
                        center.Derived=true;center.Label="圆柱面轴心";result.Features.push_back(std::move(center));
                    }
                    break;
                }
                case GeomAbs_Sphere:f.Kind=ESnapFeatureKind::SphereCenter;f.Id="sphere-center:"+std::to_string(id);f.Point=P(surface.Sphere().Location());f.Radius=surface.Sphere().Radius();break;
                case GeomAbs_Cone:f.Kind=ESnapFeatureKind::Center;f.Id="cone-apex:"+std::to_string(id);f.Point=P(surface.Cone().Apex());f.Label="圆锥顶点";f.Derived=true;result.Features.push_back(f);f.Kind=ESnapFeatureKind::Axis;f.Id="axis:"+std::to_string(id);f.Direction=D(surface.Cone().Axis().Direction());f.Label="圆锥轴";break;
                case GeomAbs_Torus:f.Kind=ESnapFeatureKind::Center;f.Id="torus-center:"+std::to_string(id);f.Point=P(surface.Torus().Location());f.Label="圆环中心";f.Derived=true;result.Features.push_back(f);f.Kind=ESnapFeatureKind::Axis;f.Id="axis:"+std::to_string(id);f.Direction=D(surface.Torus().Axis().Direction());f.Label="圆环轴";break;
                case GeomAbs_SurfaceOfRevolution:f.Kind=ESnapFeatureKind::Axis;f.Id="axis:"+std::to_string(id);f.Point=P(surface.AxeOfRevolution().Location());f.Direction=D(surface.AxeOfRevolution().Direction());f.Derived=true;f.Label="回转轴";break;
                default:{GeomLib_IsPlanarSurface planar(BRep_Tool::Surface(face));if(!planar.IsPlanar())continue;
                    f.Kind=ESnapFeatureKind::Plane;f.Id="plane:"+std::to_string(id);f.Point=P(planar.Plan().Location());f.Direction=D(planar.Plan().Axis().Direction());f.XDirection=D(planar.Plan().Position().XDirection());if(face.Orientation()==TopAbs_REVERSED)for(auto& n:f.Direction)n=-n;f.Derived=true;f.Label="共面曲面基准";break;}
                }
                if(f.Kind==ESnapFeatureKind::Plane){GProp_GProps mass;BRepGProp::SurfaceProperties(face,mass,1e-10);if(mass.Mass()>1e-12){f.Point=P(mass.CentreOfMass());f.Radius=std::sqrt(mass.Mass()/std::acos(-1.));}
                    if(f.Label.empty())f.Label="平面";result.Features.push_back(f);f.Kind=ESnapFeatureKind::Center;f.Id="face-center:"+std::to_string(id);f.Label="平面面积中心";f.Derived=true;}
                result.Features.push_back(std::move(f));
            }
            AddBounds(result,built.Shape);
            return result;
        }
    };
}
