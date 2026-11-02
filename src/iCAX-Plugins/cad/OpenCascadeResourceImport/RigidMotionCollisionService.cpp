#include "pch.h"
#include "GeometryServices/RigidMotionCollisionService.h"
#include "OpenCascadeBRepBuilder.h"
#include "Services/ServicesHelper.h"
#include <BRepBndLib.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRep_Tool.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <Bnd_Box.hxx>
#include <TopLoc_Location.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Quaternion.hxx>
#include <limits>
#include <map>
#include <chrono>
#include <iostream>
#include <queue>
#include <numeric>
#include <mutex>
#include <Geom_BSplineSurface.hxx>
#include <Geom_RectangularTrimmedSurface.hxx>
#include <Geom_Plane.hxx>
#include <gp_Pln.hxx>
#include <BRepTools.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <Message_ProgressIndicator.hxx>
#include <GeomConvert.hxx>
#include <GeomConvert_BSplineSurfaceToBezierSurface.hxx>
#include <Geom_BezierSurface.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom_ToroidalSurface.hxx>

namespace
{
    using namespace iCAX::GeometryServices;
    namespace M=RigidMotion;
    class KernelProgress final : public Message_ProgressIndicator
    {
    public: explicit KernelProgress(std::function<bool()> stop):Stop(std::move(stop)){}
    protected: bool UserBreak() override{return Stop();} void Show(const Message_ProgressScope&,bool) override{}
    private: std::function<bool()> Stop;
    };
    void Validate(const RigidMotionPose& p){for(auto v:p.Position)if(!std::isfinite(v)||std::abs(v)>1e8)throw std::invalid_argument("MOTION_POSITION_INVALID");
        double n=0;for(auto v:p.Rotation)n+=v*v;if(!std::isfinite(n)||std::abs(n-1)>1e-8)throw std::invalid_argument("MOTION_ROTATION_INVALID");}
    gp_Trsf Transform(const RigidMotionPose& p){Validate(p);gp_Trsf t;t.SetRotation(gp_Quaternion(p.Rotation[0],p.Rotation[1],p.Rotation[2],p.Rotation[3]));t.SetTranslationPart(gp_Vec(p.Position[0],p.Position[1],p.Position[2]));return t;}
    gp_Trsf Transform(const std::array<double,16>& m){for(auto v:m)if(!std::isfinite(v))throw std::invalid_argument("MOTION_TRANSFORM_INVALID");
        if(std::abs(m[12])+std::abs(m[13])+std::abs(m[14])+std::abs(m[15]-1)>1e-8)throw std::invalid_argument("MOTION_TRANSFORM_INVALID");
        for(int i=0;i<3;++i)for(int j=0;j<3;++j){double d=0;for(int k=0;k<3;++k)d+=m[k*4+i]*m[k*4+j];if(std::abs(d-(i==j?1.:0.))>1e-8)throw std::invalid_argument("MOTION_TRANSFORM_INVALID");}
        const double det=m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);if(std::abs(det-1)>1e-8)throw std::invalid_argument("MOTION_TRANSFORM_INVALID");
        gp_Trsf t;t.SetValues(m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],m[8],m[9],m[10],m[11]);return t;}
    TopoDS_Shape Build(const std::shared_ptr<const iCAX::GeometryData::BRepModel>& model){if(!model)throw std::invalid_argument("MOTION_GEOMETRY_REQUIRED");const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*model);
        if(!built.bOK||built.Shape.IsNull())throw std::invalid_argument("MOTION_BREP_INVALID");return built.Shape;}
    RigidMotionPose Interpolate(const RigidMotionPose& a,const RigidMotionPose& b,double t){return {M::Add(a.Position,M::Scale(M::Sub(b.Position,a.Position),t)),M::Interpolate(a.Rotation,b.Rotation,t)};}
    struct ShapeIndex
    {
        struct Face{
            TopoDS_Shape Shape;Bnd_Box Box;M::Vector Center{};
            std::array<gp_Vec,3> Axes;std::array<double,3> Low{},High{};int AxisCount=0;
            struct Patch{std::array<gp_Vec,3> Axes;std::array<double,3> Low{},High{};int Left=-1,Right=-1;};
            std::vector<Patch> Patches;std::vector<int> PatchRoots;
            mutable bool SamplesReady=false;mutable std::vector<gp_Pnt> Samples;
            const std::vector<gp_Pnt>& Witnesses()const{
                if(SamplesReady)return Samples;SamplesReady=true;
                for(TopExp_Explorer e(Shape,TopAbs_VERTEX);e.More();e.Next())Samples.push_back(BRep_Tool::Pnt(TopoDS::Vertex(e.Current())));
                const auto face=TopoDS::Face(Shape);const auto surface=BRep_Tool::Surface(face);double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
                if(!std::isfinite(u0+u1+v0+v1))return Samples;
                // Samples may prove a collision, never clearance. Every sample
                // belongs to the trimmed BRep face, not its untrimmed support.
                for(int u=1;u<=5;++u)for(int v=1;v<=5;++v){const gp_Pnt2d uv(u0+(u1-u0)*u/6.,v0+(v1-v0)*v/6.);BRepClass_FaceClassifier classifier(face,uv,1e-7);
                    if(classifier.State()==TopAbs_IN||classifier.State()==TopAbs_ON)Samples.push_back(surface->Value(uv.X(),uv.Y()));}
                return Samples;
            }
            void BuildSlabs(){
                double tolerance=BRep_Tool::Tolerance(TopoDS::Face(Shape))+1e-6;
                for(TopExp_Explorer e(Shape,TopAbs_EDGE);e.More();e.Next())tolerance=std::max(tolerance,BRep_Tool::Tolerance(TopoDS::Edge(e.Current()))+1e-6);
                for(TopExp_Explorer e(Shape,TopAbs_VERTEX);e.More();e.Next())tolerance=std::max(tolerance,BRep_Tool::Tolerance(TopoDS::Vertex(e.Current()))+1e-6);
                auto surface=BRep_Tool::Surface(TopoDS::Face(Shape));
                while(const auto trim=occ::handle<Geom_RectangularTrimmedSurface>::DownCast(surface))surface=trim->BasisSurface();
                if(const auto plane=occ::handle<Geom_Plane>::DownCast(surface)){Axes[0]=gp_Vec(plane->Pln().Axis().Direction());const auto p=plane->Pln().Location();Low[0]=High[0]=gp_Vec(p.X(),p.Y(),p.Z()).Dot(Axes[0]);Low[0]-=tolerance;High[0]+=tolerance;AxisCount=1;return;}
                const auto spline=occ::handle<Geom_BSplineSurface>::DownCast(surface);if(!spline)return;
                double u0,u1,v0,v1;spline->Bounds(u0,u1,v0,v1);gp_Pnt p;gp_Vec du,dv;spline->D1((u0+u1)*.5,(v0+v1)*.5,p,du,dv);
                auto normal=du.Crossed(dv);if(du.SquareMagnitude()<1e-20||normal.SquareMagnitude()<1e-20)return;
                Axes={du.Normalized(),normal.Normalized().Crossed(du.Normalized()),normal.Normalized()};
                Low.fill(std::numeric_limits<double>::infinity());High.fill(-std::numeric_limits<double>::infinity());
                // Positive-weight NURBS lie inside the convex hull of their
                // poles. These oriented slabs are exact conservative bounds,
                // including trimming, not a sampled approximation of a surface.
                for(int u=1;u<=spline->NbUPoles();++u)for(int v=1;v<=spline->NbVPoles();++v){if(spline->Weight(u,v)<=0)return;const auto pole=spline->Pole(u,v);const gp_Vec value(pole.X(),pole.Y(),pole.Z());
                    for(int axis=0;axis<3;++axis){const auto d=value.Dot(Axes[axis]);Low[axis]=std::min(Low[axis],d);High[axis]=std::max(High[axis],d);}}
                for(int axis=0;axis<3;++axis){Low[axis]-=tolerance;High[axis]+=tolerance;}AxisCount=3;
            }
            double PointLower(const gp_Pnt& point)const{const gp_Vec value(point.X(),point.Y(),point.Z());double squared=0;for(int i=0;i<AxisCount;++i){const auto d=value.Dot(Axes[i]);const auto gap=std::max({0.,Low[i]-d,d-High[i]});squared+=gap*gap;}Bnd_Box p;p.Add(point);return std::max(p.Distance(Box),std::sqrt(squared));}
            int AddPatch(const occ::handle<Geom_BSplineSurface>& spline,double tolerance,int depth,const std::function<void()>& check){
                check();double u0,u1,v0,v1;spline->Bounds(u0,u1,v0,v1);gp_Pnt p;gp_Vec du,dv;spline->D1((u0+u1)*.5,(v0+v1)*.5,p,du,dv);const auto normal=du.Crossed(dv);Patch patch;
                patch.Axes=du.SquareMagnitude()<1e-20||normal.SquareMagnitude()<1e-20?std::array<gp_Vec,3>{gp_Vec(1,0,0),gp_Vec(0,1,0),gp_Vec(0,0,1)}:
                    std::array<gp_Vec,3>{du.Normalized(),normal.Normalized().Crossed(du.Normalized()),normal.Normalized()};
                patch.Low.fill(std::numeric_limits<double>::infinity());patch.High.fill(-std::numeric_limits<double>::infinity());
                for(int u=1;u<=spline->NbUPoles();++u)for(int v=1;v<=spline->NbVPoles();++v){if(spline->Weight(u,v)<=0)throw std::runtime_error("MOTION_PATCH_WEIGHT");const auto p=spline->Pole(u,v);const gp_Vec value(p.X(),p.Y(),p.Z());
                    for(int axis=0;axis<3;++axis){const auto d=value.Dot(patch.Axes[axis]);patch.Low[axis]=std::min(patch.Low[axis],d);patch.High[axis]=std::max(patch.High[axis],d);}}
                for(int axis=0;axis<3;++axis){patch.Low[axis]-=tolerance;patch.High[axis]+=tolerance;}
                const auto index=static_cast<int>(Patches.size());Patches.push_back(patch);
                if(depth<8&&patch.High[2]-patch.Low[2]>.05+2*tolerance){
                    const auto um=(u0+u1)*.5,vm=(v0+v1)*.5;const bool splitU=du.Magnitude()*(u1-u0)>=dv.Magnitude()*(v1-v0);
                    auto left=occ::handle<Geom_BSplineSurface>::DownCast(spline->Copy()),right=occ::handle<Geom_BSplineSurface>::DownCast(spline->Copy());
                    left->Segment(u0,splitU?um:u1,v0,splitU?v1:vm);right->Segment(splitU?um:u0,u1,splitU?v0:vm,v1);
                    const auto a=AddPatch(left,tolerance,depth+1,check),b=AddPatch(right,tolerance,depth+1,check);Patches[index].Left=a;Patches[index].Right=b;
                }return index;
            }
            void BuildPatches(const std::function<void()>& check){
                auto surface=BRep_Tool::Surface(TopoDS::Face(Shape));while(const auto trim=occ::handle<Geom_RectangularTrimmedSurface>::DownCast(surface))surface=trim->BasisSurface();
                if(occ::handle<Geom_Plane>::DownCast(surface))return;
                // The generic conversion approximates offset/swept surfaces.
                // Only these exact rational conversions may supply a proof.
                if(!occ::handle<Geom_BSplineSurface>::DownCast(surface)&&!occ::handle<Geom_BezierSurface>::DownCast(surface)&&
                    !occ::handle<Geom_CylindricalSurface>::DownCast(surface)&&!occ::handle<Geom_ConicalSurface>::DownCast(surface)&&
                    !occ::handle<Geom_SphericalSurface>::DownCast(surface)&&!occ::handle<Geom_ToroidalSurface>::DownCast(surface))return;
                double tolerance=BRep_Tool::Tolerance(TopoDS::Face(Shape))+1e-6;
                for(TopExp_Explorer e(Shape,TopAbs_EDGE);e.More();e.Next())tolerance=std::max(tolerance,BRep_Tool::Tolerance(TopoDS::Edge(e.Current()))+1e-6);
                for(TopExp_Explorer e(Shape,TopAbs_VERTEX);e.More();e.Next())tolerance=std::max(tolerance,BRep_Tool::Tolerance(TopoDS::Vertex(e.Current()))+1e-6);
                try{double u0,u1,v0,v1;BRepTools::UVBounds(TopoDS::Face(Shape),u0,u1,v0,v1);if(!std::isfinite(u0+u1+v0+v1)||u1-u0<1e-12||v1-v0<1e-12)return;
                    // Exact conversion and knot insertion retain the complete
                    // trimmed surface inside a union of positive pole hulls.
                    // Refinement tightens those bounds without sampling it.
                    const auto spline=GeomConvert::SurfaceToBSplineSurface(new Geom_RectangularTrimmedSurface(surface,u0,u1,v0,v1));
                    GeomConvert_BSplineSurfaceToBezierSurface pieces(spline);
                    for(int u=1;u<=pieces.NbUPatches();++u)for(int v=1;v<=pieces.NbVPatches();++v)PatchRoots.push_back(AddPatch(GeomConvert::SurfaceToBSplineSurface(pieces.Patch(u,v)),tolerance,0,check));
                }catch(const Standard_Failure&){Patches.clear();PatchRoots.clear();}
            }
            double PatchLower(int index,const std::function<std::array<double,2>(const gp_Vec&)>& projection,double cutoff)const{
                const auto& patch=Patches[index];double lower=0;for(int axis=0;axis<3;++axis){const auto interval=projection(patch.Axes[axis]);lower=std::max({lower,patch.Low[axis]-interval[1],interval[0]-patch.High[axis]});}
                if(patch.Left<0||lower>cutoff)return lower;
                return std::max(lower,std::min(PatchLower(patch.Left,projection,cutoff),PatchLower(patch.Right,projection,cutoff)));
            }
            double AnalyticLower(const std::function<std::array<double,2>(const gp_Vec&)>& projection,double cutoff)const{
                double lower=0;for(int axis=0;axis<AxisCount;++axis){const auto interval=projection(Axes[axis]);lower=std::max({lower,Low[axis]-interval[1],interval[0]-High[axis]});}
                if(lower>cutoff||PatchRoots.empty())return lower;
                double refined=std::numeric_limits<double>::infinity();for(auto root:PatchRoots){refined=std::min(refined,PatchLower(root,projection,cutoff));if(refined<=lower)return lower;}return std::max(lower,refined);
            }
        };
        struct Solid{Bnd_Box Box;std::unique_ptr<BRepClass3d_SolidClassifier> Classifier;};
        struct Node{Bnd_Box Box;int Left=-1,Right=-1;std::size_t Begin=0,End=0;};
        std::vector<Face> Faces;std::vector<Solid> Solids;std::vector<gp_Pnt> Probes;
        std::vector<Node> Nodes;std::vector<std::size_t> Order;
        explicit ShapeIndex(const TopoDS_Shape& shape,const std::function<void()>& check,bool analytic=false){
            for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next()){check();Face f{e.Current(),{}};BRepBndLib::Add(f.Shape,f.Box,false);double a,b,c,d,e1,f1;f.Box.Get(a,b,c,d,e1,f1);f.Center={(a+d)*.5,(b+e1)*.5,(c+f1)*.5};f.BuildSlabs();if(analytic)f.BuildPatches(check);Faces.push_back(std::move(f));}
            if(Faces.empty())throw std::invalid_argument("MOTION_SURFACE_REQUIRED");
            for(TopExp_Explorer e(shape,TopAbs_SOLID);e.More();e.Next()){check();Solid s;BRepBndLib::Add(e.Current(),s.Box,false);s.Classifier=std::make_unique<BRepClass3d_SolidClassifier>(e.Current());Solids.push_back(std::move(s));}
            // A representative of every connected boundary component suffices
            // for containment when no boundaries intersect. Keep solids AND
            // free shells/faces so disconnected imported assemblies are covered.
            for(const auto [kind,avoid]:{std::pair{TopAbs_SOLID,TopAbs_SHAPE},std::pair{TopAbs_SHELL,TopAbs_SOLID},std::pair{TopAbs_FACE,TopAbs_SHELL}})
                for(TopExp_Explorer e(shape,kind,avoid);e.More();e.Next()){TopExp_Explorer vertex(e.Current(),TopAbs_VERTEX);if(!vertex.More())throw std::invalid_argument("MOTION_VERTEX_REQUIRED");Probes.push_back(BRep_Tool::Pnt(TopoDS::Vertex(vertex.Current())));}
            Order.resize(Faces.size());std::iota(Order.begin(),Order.end(),0);BuildNode(0,Order.size());
        }
        int BuildNode(std::size_t begin,std::size_t end){const int index=static_cast<int>(Nodes.size());Nodes.push_back({});Nodes[index].Begin=begin;Nodes[index].End=end;for(auto i=begin;i<end;++i)Nodes[index].Box.Add(Faces[Order[i]].Box);
            if(end-begin>8){double x0,y0,z0,x1,y1,z1;Nodes[index].Box.Get(x0,y0,z0,x1,y1,z1);const M::Vector span{x1-x0,y1-y0,z1-z0};const int axis=static_cast<int>(std::max_element(span.begin(),span.end())-span.begin());const auto mid=(begin+end)/2;
                std::nth_element(Order.begin()+begin,Order.begin()+mid,Order.begin()+end,[&](auto a,auto b){return Faces[a].Center[axis]<Faces[b].Center[axis];});const auto left=BuildNode(begin,mid),right=BuildNode(mid,end);Nodes[index].Left=left;Nodes[index].Right=right;}return index;}
        void Nearest(const Bnd_Box& box,double& best,const std::function<void(const Face&)>& visit,bool zeroStops=true)const{
            using Item=std::pair<double,int>;std::priority_queue<Item,std::vector<Item>,std::greater<Item>> queue;queue.emplace(box.Distance(Nodes[0].Box),0);
            while(!queue.empty()){const auto [lower,index]=queue.top();queue.pop();if(lower>best)break;const auto& node=Nodes[index];
                if(node.Left<0){std::vector<std::pair<double,std::size_t>> leaves;for(auto i=node.Begin;i<node.End;++i){const auto distance=box.Distance(Faces[Order[i]].Box);if(distance<=best)leaves.emplace_back(distance,Order[i]);}
                    std::sort(leaves.begin(),leaves.end());for(const auto [distance,face]:leaves){if(distance>best)break;visit(Faces[face]);if(best<0||(zeroStops&&best<1e-8))return;}}
                else for(auto child:{node.Left,node.Right}){const auto distance=box.Distance(Nodes[child].Box);if(distance<=best)queue.emplace(distance,child);}}
        }
        bool Contains(const gp_Pnt& point)const{for(const auto& solid:Solids){if(solid.Box.IsOut(point))continue;solid.Classifier->Perform(point,1e-7);const auto state=solid.Classifier->State();if(state==TopAbs_IN||state==TopAbs_ON)return true;if(state==TopAbs_UNKNOWN)throw std::runtime_error("MOTION_CONTAINMENT_UNRESOLVED");}return false;}
    };
    #include "RigidMotionBoxMesh.inc"
    class CPrimitiveMeshSession final:public IRigidMotionCollisionSession
    {
        RigidMotionCollisionQuery Base;
        std::shared_ptr<ICollisionDistanceService> Collision;
        std::unique_ptr<PrimitiveMeshState> State;
    public:
        CPrimitiveMeshSession(const RigidMotionCollisionQuery& query,std::shared_ptr<ICollisionDistanceService> collision,std::unique_ptr<PrimitiveMeshState> state)
            :Base(query),Collision(std::move(collision)),State(std::move(state)){Base.Segments.clear();}
        RigidMotionCollisionResult Check(const std::vector<RigidMotionSegment>& segments,const std::function<bool()>& cancelled,bool stopAtFirstFailure)override{
            auto query=Base;query.Segments=segments;
            return CalculatePrimitiveMesh(query,*Collision,{},cancelled,{},{},{},State.get(),stopAtFirstFailure);
        }
    };
    class CRigidMotionCollisionService final:public IRigidMotionCollisionService
    {
        AUTO_REGIST_SERVICE(IRigidMotionCollisionService,CRigidMotionCollisionService,ICollisionDistanceService)
        std::shared_ptr<ICollisionDistanceService> Collision;
        struct CachedMesh {std::weak_ptr<const CollisionMesh> Source;CollisionDistanceModelRef Model;};
        mutable std::mutex MeshMutex;mutable std::map<const CollisionMesh*,CachedMesh> Meshes;
        CollisionDistanceModelRef MeshCollider(const std::shared_ptr<const CollisionMesh>& source,const std::function<bool()>& cancelled)const{
            {std::lock_guard lock(MeshMutex);const auto found=Meshes.find(source.get());if(found!=Meshes.end()&&found->second.Source.lock()==source)return found->second.Model;}
            const auto prepared=Collision->PrepareMesh(*source,cancelled);
            std::lock_guard lock(MeshMutex);if(Meshes.size()>=256){for(auto it=Meshes.begin();it!=Meshes.end();)if(it->second.Source.expired())it=Meshes.erase(it);else ++it;if(Meshes.size()>=256)Meshes.erase(Meshes.begin());}
            Meshes[source.get()]={source,prepared};return prepared;
        }
    public:
        explicit CRigidMotionCollisionService(std::shared_ptr<ICollisionDistanceService> collision):Collision(std::move(collision)){}
        void OnLoad() override{} void OnUnload() override{std::lock_guard lock(MeshMutex);Meshes.clear();}
        std::unique_ptr<IRigidMotionCollisionSession> CreatePrimitiveMeshSession(const RigidMotionCollisionQuery& q,const std::function<bool()>& cancelled)const override{
            if(!q.Solids.empty()&&std::any_of(q.Solids.begin(),q.Solids.end(),[](const auto& s){return s.LowerRadius!=s.UpperRadius;}))throw std::invalid_argument("MOTION_PRIMITIVE_SESSION_REQUIRES_BOXES_OR_CYLINDERS");
            auto state=PreparePrimitiveMesh(q,*Collision,[&](const auto& mesh,const auto& cancel){return MeshCollider(mesh,cancel);},cancelled);
            return std::make_unique<CPrimitiveMeshSession>(q,Collision,std::move(state));
        }
        RigidMotionCollisionResult Check(const RigidMotionCollisionQuery& q,const std::function<bool()>& cancelled,const std::function<void(std::size_t,std::size_t)>& progress) const override{
            return Calculate(q,cancelled,progress,{},{});
        }
        RigidMotionCollisionResult CheckIncremental(const RigidMotionCollisionQuery& q,
            const std::function<std::optional<std::size_t>()>& nextSegment,
            const std::function<void(std::size_t,const RigidMotionCollisionSegment&)>& result,const std::function<bool()>& cancelled) const override{
            if(!nextSegment||!result)throw std::invalid_argument("MOTION_INCREMENTAL_CALLBACK_REQUIRED");
            return Calculate(q,cancelled,{},nextSegment,result);
        }
    private:
        RigidMotionCollisionResult Calculate(const RigidMotionCollisionQuery& q,const std::function<bool()>& cancelled,
            const std::function<void(std::size_t,std::size_t)>& progress,const std::function<std::optional<std::size_t>()>& nextSegment,
            const std::function<void(std::size_t,const RigidMotionCollisionSegment&)>& receive) const{
            // Explicit supplied meshes select the same neutral primitive/mesh
            // CCD for boxes and exact circular cylinders. Other frusta and
            // geometry-only queries retain their exact BRep contract below.
            const bool axialMesh=!q.Solids.empty()&&std::all_of(q.Solids.begin(),q.Solids.end(),[](const auto& s){return s.LowerRadius==s.UpperRadius;})&&!q.Obstacles.empty()&&std::all_of(q.Obstacles.begin(),q.Obstacles.end(),[](const auto& obstacle){return bool(obstacle.Mesh);});
            if(!q.Boxes.empty()||axialMesh)return CalculatePrimitiveMesh(q,*Collision,[&](const auto& mesh,const auto& cancel){return MeshCollider(mesh,cancel);},cancelled,progress,nextSegment,receive);
            auto deadline=std::chrono::steady_clock::time_point::max();
            const auto cancelCheck=[&]{if(cancelled&&cancelled())throw std::runtime_error("MOTION_CANCELLED");};
            const auto check=[&]{cancelCheck();if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("MOTION_TIME_BUDGET");};
            occ::handle<Message_ProgressIndicator> kernelProgress=new KernelProgress([&]{return (cancelled&&cancelled())||std::chrono::steady_clock::now()>deadline;});
            const auto perform=[&](BRepExtrema_DistShapeShape& d){check();d.Perform(kernelProgress->Start());check();if(!d.IsDone()||d.NbSolution()<1)throw std::runtime_error("MOTION_DISTANCE_FAILED");};
            const bool profile=GetEnvironmentVariableA("ICAX_COLLISION_PROFILE",nullptr,0)!=0;if(profile)std::clog<<"Collision preparing body"<<std::endl;
            if(q.Obstacles.empty()||q.Obstacles.size()>128||q.Segments.empty()||q.Segments.size()>100000)throw std::invalid_argument("MOTION_INPUT_BUDGET");
            if(q.MaxSegmentTimeMs<1||q.MaxSegmentTimeMs>60000)throw std::invalid_argument("MOTION_TIME_BUDGET_INVALID");
            if(bool(q.Body)==!q.Solids.empty())throw std::invalid_argument("MOTION_BODY_REQUIRED");
            TopoDS_Shape body;
            if(q.Body)body=Build(q.Body);
            else{TopoDS_Compound compound;BRep_Builder builder;builder.MakeCompound(compound);
                for(const auto& s:q.Solids){for(double v:{s.Z,s.Height,s.LowerRadius,s.UpperRadius})if(!std::isfinite(v)||std::abs(v)>1e6)throw std::invalid_argument("MOTION_SOLID_INVALID");
                    if(s.Height<=0||s.LowerRadius<0||s.UpperRadius<0||s.LowerRadius+s.UpperRadius<=0)throw std::invalid_argument("MOTION_SOLID_INVALID");
                    const gp_Ax2 axis(gp_Pnt(0,0,s.Z),gp_Dir(0,0,1));builder.Add(compound,s.LowerRadius==s.UpperRadius?BRepPrimAPI_MakeCylinder(axis,s.LowerRadius,s.Height).Shape():BRepPrimAPI_MakeCone(axis,s.LowerRadius,s.UpperRadius,s.Height).Shape());}body=compound;}
            const auto calibration=Transform(q.BodyCalibration);body=body.Moved(TopLoc_Location(calibration));
            Bnd_Box bounds;BRepBndLib::AddOptimal(body,bounds,false,true);double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
            const double radius=M::Length({std::max(std::abs(x0),std::abs(x1)),std::max(std::abs(y0),std::abs(y1)),std::max(std::abs(z0),std::abs(z1))});
            const ShapeIndex bodyIndex(body,check);
            struct Obstacle{std::string Id;TopoDS_Shape Shape;Bnd_Box Box;std::unique_ptr<ShapeIndex> Index;};std::vector<Obstacle> obstacles;
            for(const auto& o:q.Obstacles){check();if(profile)std::clog<<"Collision preparing obstacle "<<o.Id<<std::endl;Obstacle built{o.Id,Build(o.Geometry).Moved(TopLoc_Location(Transform(o.Transform))),{},nullptr};BRepBndLib::Add(built.Shape,built.Box,false);built.Index=std::make_unique<ShapeIndex>(built.Shape,check,!q.Solids.empty());
                if(profile)std::clog<<"Collision indexed "<<built.Index->Faces.size()<<" faces"<<std::endl;obstacles.push_back(std::move(built));}
            // Resolve nearby obstacles first so their exact distance can prune
            // distant, complex shapes. Database enumeration is unrelated to
            // proximity; starting with a remote perforated plate otherwise
            // spends the interval budget before reaching the nearby workpiece.
            // Ordering changes neither the bounds nor the distance predicate.
            const auto orderedObstacles=[&](const Bnd_Box& box){
                std::vector<std::pair<double,const Obstacle*>> ordered;ordered.reserve(obstacles.size());
                for(const auto& obstacle:obstacles)ordered.emplace_back(box.Distance(obstacle.Box),&obstacle);
                std::stable_sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.first<b.first;});return ordered;
            };
            RigidMotionCollisionResult result;
            struct DistanceValue{double Distance;std::string Id;bool Exact;};
            std::map<std::array<double,7>,DistanceValue> distanceCache;
            struct ClearanceCertificate{RigidMotionPose Pose;double LowerBound;};
            std::vector<ClearanceCertificate> certificates;std::size_t nextCertificate=0;
            const auto remember=[&](const RigidMotionPose& pose,const std::array<double,7>& key,double value,const std::string& obstacle,bool exact){
                if(distanceCache.size()<100000)distanceCache.insert_or_assign(key,DistanceValue{value,obstacle,exact});
                if(!std::isfinite(value)||value<=1e-6)return;
                if(certificates.size()<256)certificates.push_back({pose,value});
                else{certificates[nextCertificate]={pose,value};nextCertificate=(nextCertificate+1)%certificates.size();}
            };
            // Conservative spatial cover of the actual body. Empty cells are
            // discarded only by exact body distance; retained cells are bounded
            // by circumscribed spheres. This accelerates CLEAR cases without
            // replacing BRep collision decisions with sampled mesh geometry.
            struct Cell{M::Vector Center,Half;double Radius,BodyDistance=0;bool Tested=false,Empty=false;std::vector<std::unique_ptr<Cell>> Children;};
            Cell root{{(x0+x1)/2,(y0+y1)/2,(z0+z1)/2},{(x1-x0)/2,(y1-y0)/2,(z1-z0)/2},0};root.Radius=M::Length(root.Half);
            const auto pointDistance=[&](const gp_Pnt& point,std::string& id,gp_Pnt& nearest){check();Bnd_Box box;box.Add(point);double best=std::numeric_limits<double>::infinity();BRepExtrema_DistShapeShape d;d.LoadS1(BRepBuilderAPI_MakeVertex(point).Vertex());
                for(const auto& [lower,entry]:orderedObstacles(box)){if(lower>best)break;const auto& obstacle=*entry;if(obstacle.Index->Contains(point)){id=obstacle.Id;nearest=point;return 0.;}
                    obstacle.Index->Nearest(box,best,[&](const auto& face){check();d.LoadS2(face.Shape);perform(d);if(d.Value()<best){best=d.Value();id=obstacle.Id;nearest=d.PointOnShape2(1);}});if(best<1e-8)return best;}
                return best;};
            const auto pointLower=[&](const gp_Pnt& point){check();Bnd_Box box;box.Add(point);double best=std::numeric_limits<double>::infinity();
                for(const auto& [lower,entry]:orderedObstacles(box)){if(lower>best)break;const auto& obstacle=*entry;if(obstacle.Index->Contains(point))return 0.;obstacle.Index->Nearest(box,best,[&](const auto& face){best=std::min(best,face.PointLower(point));});}return best;};
            const auto distance=[&](const RigidMotionPose& p,std::string& id,double clearance){check();if(++result.DistanceQueries>500000)throw std::runtime_error("MOTION_CHECK_BUDGET");
                const double required=clearance+.25;
                const std::array<double,7> key{p.Position[0],p.Position[1],p.Position[2],p.Rotation[0],p.Rotation[1],p.Rotation[2],p.Rotation[3]};
                if(const auto found=distanceCache.find(key);found!=distanceCache.end()&&(found->second.Exact||found->second.Distance>clearance+1e-6)){id=found->second.Id;return found->second.Distance;}
                // Rigid motion moves every body point by at most translation
                // plus the rotation chord at its enclosing radius. Subtracting
                // that bound from a previous clearance remains a certificate
                // against every obstacle, including unsampled thin obstacles.
                double reused=0;
                for(const auto& c:certificates){const auto displacement=M::Length(M::Sub(p.Position,c.Pose.Position))
                    +2*radius*std::sin(M::Angle(p.Rotation,c.Pose.Rotation)*.5);
                    reused=std::max(reused,c.LowerBound-displacement-1e-6);}
                if(reused>required)return reused;
                const auto transform=Transform(p),inverse=transform.Inverted();const auto placed=body.Moved(TopLoc_Location(transform));const auto box=bounds.Transformed(transform);double best=std::numeric_limits<double>::infinity();
                const auto ordered=orderedObstacles(box);const double exterior=ordered.front().first;
                if(exterior>required){
                    remember(p,key,exterior,"",false);
                    return exterior;
                }
                if(!q.Solids.empty()){
                    // Project the entire analytic frustum onto conservative
                    // face slabs. A sphere cover loses the axial separation
                    // of a tapered head following a sheet, even when no part
                    // of that head approaches the sheet's plane.
                    const auto actual=transform.Multiplied(calibration),rotationInverse=actual.Inverted();
                    const gp_Vec translation(actual.TranslationPart());double lower=std::numeric_limits<double>::infinity();
                    for(const auto& solid:q.Solids){
                        const auto middle=gp_Pnt(0,0,solid.Z+solid.Height*.5).Transformed(actual);
                        const auto projection=[&](const gp_Vec& axis){const auto local=axis.Transformed(rotationInverse);
                            const double radial=std::hypot(local.X(),local.Y()),offset=translation.Dot(axis);
                            const double a=offset+local.Z()*solid.Z,b=offset+local.Z()*(solid.Z+solid.Height);
                            return std::array<double,2>{std::min(a-radial*solid.LowerRadius,b-radial*solid.UpperRadius),std::max(a+radial*solid.LowerRadius,b+radial*solid.UpperRadius)};};
                        const std::array<std::array<double,2>,3> extents{projection(gp_Vec(1,0,0)),projection(gp_Vec(0,1,0)),projection(gp_Vec(0,0,1))};
                        for(const auto& entry:ordered){const auto& obstacle=*entry.second;check();double obstacleLower=std::numeric_limits<double>::infinity();obstacle.Index->Nearest(box,obstacleLower,[&](const auto& face){check();double gap=std::max(box.Distance(face.Box),face.AnalyticLower(projection,required+1e-6));
                                obstacleLower=std::min(obstacleLower,std::max(0.,gap-1e-6));
                            });lower=std::min(lower,obstacleLower);
                            if(lower<=required)break;
                            // Boundary separation alone cannot exclude full
                            // containment. An attained frustum extremum beyond
                            // the obstacle box proves an exterior body point;
                            // otherwise classify an interior body point exactly.
                            double x0,y0,z0,x1,y1,z1;obstacle.Box.Get(x0,y0,z0,x1,y1,z1);const std::array<double,3> lo{x0,y0,z0},hi{x1,y1,z1};bool exteriorPoint=false;
                            for(int axis=0;axis<3;++axis)exteriorPoint|=extents[axis][0]<lo[axis]-1e-6||extents[axis][1]>hi[axis]+1e-6;
                            if(!exteriorPoint&&obstacle.Index->Contains(middle)){lower=0;break;}
                        }
                        if(lower<=required)break;
                    }
                    if(std::isfinite(lower)&&lower>required){remember(p,key,lower,"",false);return lower;}
                }
                bool witness=false;double witnessDistance=0;
                std::size_t visits=0;
                std::function<double(Cell&,int)> cover=[&](Cell& cell,int depth)->double{
                    check();if(++visits>96)return -1;
                    if(!cell.Tested){BRepExtrema_DistShapeShape d;d.LoadS1(BRepBuilderAPI_MakeVertex(gp_Pnt(cell.Center[0],cell.Center[1],cell.Center[2])).Vertex());d.LoadS2(body);perform(d);
                        cell.BodyDistance=d.Value();cell.Empty=d.Value()>cell.Radius+1e-6;cell.Tested=true;}
                    if(cell.Empty)return std::numeric_limits<double>::infinity();
                    const auto center=gp_Pnt(cell.Center[0],cell.Center[1],cell.Center[2]).Transformed(transform);
                    std::string obstacle;double measured=pointLower(center);bool exact=false;
                    if(measured-cell.Radius-1e-6>required)return measured-cell.Radius-1e-6;
                    if((depth==0&&!bodyIndex.Solids.empty())||(depth>=5&&measured+cell.BodyDistance<=clearance)){
                        gp_Pnt nearest;measured=pointDistance(center,obstacle,nearest);exact=true;
                        if(bodyIndex.Contains(nearest.Transformed(inverse))){witness=true;witnessDistance=0;id=obstacle;return -1;}
                    }
                    // Triangle inequality gives a real collision witness, not
                    // a positive result inferred from overlapping bounding boxes.
                    if(exact&&measured+cell.BodyDistance<=clearance){witness=true;witnessDistance=measured+cell.BodyDistance;id=obstacle;return -1;}
                    const double lower=measured-cell.Radius-1e-6;if(lower>required)return lower;
                    if(depth>=7)return -1;
                    if(cell.Children.empty()){for(int bits=0;bits<8;++bits){auto child=std::make_unique<Cell>();child->Half=M::Scale(cell.Half,.5);child->Center=cell.Center;for(int axis=0;axis<3;++axis)child->Center[axis]+=child->Half[axis]*((bits&(1<<axis))?1:-1);child->Radius=M::Length(child->Half);cell.Children.push_back(std::move(child));}}
                    double bound=std::numeric_limits<double>::infinity();for(auto& child:cell.Children){const auto value=cover(*child,depth+1);if(value<0)return -1;bound=std::min(bound,value);}return bound;
                };
                // Analytic solids already have exact projection bounds above.
                // When a trimmed boundary defeats those bounds, recursively
                // covering a cone by spheres repeats expensive point-to-face
                // solves. Continue directly to the exact solid distance.
                const double certified=q.Body?cover(root,0):-1.;
                if(witness)return witnessDistance;
                if(certified>required&&std::isfinite(certified)){remember(p,key,certified,"",false);return certified;}
                if(!bodyIndex.Solids.empty())for(const auto& entry:ordered){const auto& obstacle=*entry.second;
                    if(box.IsOut(obstacle.Box))continue;double overlap=0;
                    obstacle.Index->Nearest(box,overlap,[&](const auto& face){check();for(const auto& point:face.Witnesses()){
                        if(!box.IsOut(point)&&bodyIndex.Contains(point.Transformed(inverse))){overlap=-1;id=obstacle.Id;break;}}},false);
                    if(overlap<0)return 0.;
                }
                BRepExtrema_DistShapeShape d;d.LoadS1(placed);
                for(const auto& [lower,entry]:ordered){check();if(lower>best)break;const auto& obstacle=*entry;
                    bool contained=false;
                    if(!box.IsOut(obstacle.Box)){
                        if(!obstacle.Index->Solids.empty())for(const auto& point:bodyIndex.Probes){check();if(obstacle.Index->Contains(point.Transformed(transform))){contained=true;break;}}
                        if(!contained&&!bodyIndex.Solids.empty())for(const auto& point:obstacle.Index->Probes){check();if(!box.IsOut(point)&&bodyIndex.Contains(point.Transformed(inverse))){contained=true;break;}}
                    }
                    if(contained){best=0;id=obstacle.Id;break;}
                    obstacle.Index->Nearest(box,best,[&](const auto& face){check();d.LoadS2(face.Shape);perform(d);if(d.Value()<best){best=d.Value();id=obstacle.Id;}});
                    if(best<1e-8)break;}
                if(!std::isfinite(best))throw std::runtime_error("MOTION_DISTANCE_FAILED");remember(p,key,best,id,true);return best;};
            // Bound pathological exact solves per interval. OCC's progress
            // callback cooperatively interrupts kernel work; expiry is always
            // Unchecked, and never a clearance certificate.
            std::size_t nextIndex=0;
            for(;;){deadline=std::chrono::steady_clock::time_point::max();cancelCheck();
                const auto requested=nextSegment?nextSegment():nextIndex<q.Segments.size()?std::optional<std::size_t>(nextIndex++):std::nullopt;
                if(!requested)break;if(*requested>=q.Segments.size())throw std::invalid_argument("MOTION_SEGMENT_OUTSIDE_QUERY");
                const auto& segment=q.Segments[*requested];deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(q.MaxSegmentTimeMs);check();Validate(segment.Start);Validate(segment.End);if(!std::isfinite(segment.Clearance)||segment.Clearance<0||segment.Clearance>1e6)throw std::invalid_argument("MOTION_CLEARANCE_INVALID");
                const auto started=std::chrono::steady_clock::now();const auto before=result.DistanceQueries;
                RigidMotionCollisionSegment r;r.CertifiedClearance=std::numeric_limits<double>::infinity();
                if(!segment.Connected){r.Reason="MOTION_DISCONNECTED";r.CertifiedClearance=0;result.Segments.push_back(r);if(receive)receive(*requested,r);if(progress)progress(result.Segments.size(),q.Segments.size());continue;}
                try{
                    const auto test=[&](const RigidMotionPose& p,double t){std::string id;const auto d=distance(p,id,segment.Clearance);if(d<=segment.Clearance+1e-7){r.Status="Collision";r.Reason="MOTION_CLEARANCE_VIOLATION";r.Obstacle=id;r.Fraction=t;}return d;};
                    const auto startDistance=test(segment.Start,0);double endDistance=0;if(r.Status!="Collision")endDistance=test(segment.End,1);
                    const auto displacement=M::Length(M::Sub(segment.End.Position,segment.Start.Position))
                        +2*radius*std::sin(M::Angle(segment.Start.Rotation,segment.End.Rotation)*.5);
                    const auto endpointBound=std::max(startDistance,endDistance)-displacement-1e-6;
                    if(r.Status!="Collision"&&endpointBound>=segment.Clearance){r.Status="Clear";r.CertifiedClearance=endpointBound;}
                    std::size_t segmentBudget=0;
                    std::function<bool(double,double,int)> interval=[&](double a,double b,int depth){if(++segmentBudget>8192)throw std::runtime_error("MOTION_INTERVAL_BUDGET");const double t=(a+b)*.5;const auto p=Interpolate(segment.Start,segment.End,t);
                        const auto bound=M::Length(M::Sub(segment.End.Position,segment.Start.Position))*(b-a)*.5+2*radius*std::sin(M::Angle(segment.Start.Rotation,segment.End.Rotation)*(b-a)*.25);
                        const auto d=test(p,t);if(r.Status=="Collision")return false;
                        const double lower=d-bound-1e-6;if(lower>=segment.Clearance){r.CertifiedClearance=std::min(r.CertifiedClearance,lower);return true;}
                        if(depth>=18){r.Reason="MOTION_INTERVAL_UNRESOLVED";return false;}return interval(a,t,depth+1)&&interval(t,b,depth+1);};
                    if(r.Status!="Collision"&&r.Status!="Clear"&&interval(0,1,0))r.Status="Clear";
                }catch(const Standard_Failure&){r.Status="Unchecked";r.Reason="MOTION_GEOMETRY_FAILED";}
                catch(const std::exception& e){cancelCheck();r.Status="Unchecked";r.Reason=e.what();}
                if(r.Status!="Clear"||!std::isfinite(r.CertifiedClearance))r.CertifiedClearance=0;result.Segments.push_back(std::move(r));if(receive)receive(*requested,result.Segments.back());if(progress)progress(result.Segments.size(),q.Segments.size());
                if(profile&&(result.Segments.size()<10||result.Segments.size()%50==0||std::chrono::steady_clock::now()-started>std::chrono::milliseconds(200)))std::clog<<"Collision segment "<<result.Segments.size()<<" "<<result.Segments.back().Status<<" queries="<<result.DistanceQueries-before<<" ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()<<std::endl;
            }
            return result;
        }
    };
}
