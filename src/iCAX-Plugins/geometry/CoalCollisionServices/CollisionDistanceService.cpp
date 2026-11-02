#include <GeometryServices/CollisionDistanceService.h>
#include <Services/ServicesHelper.h>
#include <coal/collision.h>
#include <coal/distance.h>
#include <coal/broadphase/broadphase_dynamic_AABB_tree.h>
#include <coal/shape/convex.h>
#include <coal/BVH/BVH_model.h>
#include <coal/BV/OBBRSS.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <deque>
#include <map>
#include <set>
#include <numeric>
#include <mutex>
#include <thread>

namespace {
using namespace iCAX::GeometryServices;
constexpr double Pi=3.14159265358979323846;
void Check(const std::function<bool()>& cancelled){if(cancelled&&cancelled())throw std::runtime_error("COLLISION_DISTANCE_CANCELLED");}
coal::Vec3s Vector(const CollisionVector& p){for(double v:p)if(!std::isfinite(v)||std::abs(v)>1e9)throw std::invalid_argument("COLLISION_VECTOR_INVALID");return {p[0],p[1],p[2]};}
coal::Transform3s Transform(const CollisionTransform& t){
    for(double v:t)if(!std::isfinite(v))throw std::invalid_argument("COLLISION_TRANSFORM_INVALID");
    coal::Matrix3s r;for(int i=0;i<3;++i)for(int j=0;j<3;++j)r(i,j)=t[i*4+j];
    if((r.transpose()*r-coal::Matrix3s::Identity()).norm()>1e-8||std::abs(r.determinant()-1)>1e-8||
       std::abs(t[12])+std::abs(t[13])+std::abs(t[14])+std::abs(t[15]-1)>1e-8)throw std::invalid_argument("COLLISION_TRANSFORM_INVALID");
    return {r,Vector({t[3],t[7],t[11]})};
}
#include "CollisionMeshVolume.inc"
struct Model final:CollisionDistanceModel {
    struct Part {std::shared_ptr<coal::CollisionGeometry> Geometry;coal::Transform3s Placement;double Padding;std::shared_ptr<const MeshVolume> Volume;std::vector<coal::Vec3s> Probes;std::size_t Index;};
    std::deque<Part> Parts;
    std::vector<std::unique_ptr<coal::CollisionObject>> Objects;
    coal::DynamicAABBTreeCollisionManager Tree;
    void Add(std::shared_ptr<coal::CollisionGeometry> geometry,const coal::Transform3s& transform,double padding=0,bool prepared=false,
        std::shared_ptr<const MeshVolume> volume={},std::vector<coal::Vec3s> probes={}){
        if(!prepared)geometry->computeLocalAABB();const auto index=Parts.size();Parts.push_back({geometry,transform,padding,std::move(volume),std::move(probes),index});
        Objects.push_back(std::make_unique<coal::CollisionObject>(geometry,transform,false));
        Objects.back()->setUserData(&Parts.back());
        Objects.back()->getAABB().min_.array()-=padding;Objects.back()->getAABB().max_.array()+=padding;
    }
    void Build(){std::vector<coal::CollisionObject*> objects;for(auto& p:Objects)objects.push_back(p.get());Tree.registerObjects(objects);Tree.setup();}
};
struct ContainedPair {std::size_t Fixed,Moving;};
std::optional<ContainedPair> Contained(const Model& fixed,const Model& moving,const coal::Transform3s& transform,const std::function<bool()>& cancelled){
    // These immutable containers are shared by workers. Indexed access avoids
    // the process-wide Debug STL iterator bookkeeping lock on every query.
    for(std::size_t ai=0;ai<fixed.Parts.size();++ai)for(std::size_t bi=0;bi<moving.Parts.size();++bi){const auto& a=fixed.Parts[ai];const auto& b=moving.Parts[bi];Check(cancelled);
        auto placement=a.Placement;auto relative=placement.inverse()*transform*b.Placement;
        if(a.Volume)for(std::size_t i=0;i<b.Probes.size();++i)if(a.Volume->Contains(relative.transform(b.Probes[i]),cancelled))return ContainedPair{a.Index,b.Index};
        if(b.Volume)for(std::size_t i=0;i<a.Probes.size();++i)if(b.Volume->Contains(relative.inverseTransform(a.Probes[i]),cancelled))return ContainedPair{a.Index,b.Index};
    }return {};
}
struct PointHull final:coal::ConvexBase {
    PointHull(const CollisionHull& hull){
        auto vertices=std::make_shared<std::vector<coal::Vec3s>>();vertices->reserve(hull.Points.size());
        for(const auto& point:hull.Points)vertices->push_back(Vector(point));
        initialize(vertices,static_cast<unsigned>(vertices->size()));setSweptSphereRadius(hull.Padding);
        // GJK uses the exact support maximum over all points. No polygon mesh
        // or hull triangulation is needed; neighbors remain unset.
    }
};
// Uses only Coal's reported lower distance, never witness-point distance.
struct CheckedCollision final:coal::ComputeCollision {
    using coal::ComputeCollision::ComputeCollision;
    bool Failed()const{return solver.gjk.status==coal::details::GJK::Failed;}
};
struct CheckedDistance final:coal::ComputeDistance {
    using coal::ComputeDistance::ComputeDistance;
    bool Failed()const{return solver.gjk.status==coal::details::GJK::Failed;}
};
struct PairSolvers {
    // Owning the geometries keeps the pointer keys valid until eviction.
    std::shared_ptr<coal::CollisionGeometry> First,Second;
    std::optional<CheckedCollision> Collision;
    std::optional<CheckedDistance> Distance;
    coal::CollisionResult CollisionResult;
    coal::DistanceResult DistanceResult;
    PairSolvers(const std::shared_ptr<coal::CollisionGeometry>& first,const std::shared_ptr<coal::CollisionGeometry>& second)
        :First(first),Second(second){}
    CheckedCollision& CollisionSolver(){if(!Collision)Collision.emplace(First.get(),Second.get());return *Collision;}
    CheckedDistance& DistanceSolver(){if(!Distance)Distance.emplace(First.get(),Second.get());return *Distance;}
};
struct SolverContext {
    // Indexing fixed slots also avoids Debug STL iterator locks in callbacks.
    std::array<std::unique_ptr<PairSolvers>,256> Pairs;
    std::size_t Next=0;
    PairSolvers& Get(coal::CollisionObject& a,coal::CollisionObject& b){
        const auto first=a.collisionGeometryPtr(),second=b.collisionGeometryPtr();
        const auto x=std::hash<const coal::CollisionGeometry*>{}(first),y=std::hash<const coal::CollisionGeometry*>{}(second);
        const auto start=(x^(y+0x9e3779b9u+(x<<6)+(x>>2)))%Pairs.size();
        for(std::size_t probe=0;probe<Pairs.size();++probe){auto& pair=Pairs[(start+probe)%Pairs.size()];
            if(pair){if(pair->First.get()==first&&pair->Second.get()==second)return *pair;}
            else{pair=std::make_unique<PairSolvers>(a.collisionGeometry(),b.collisionGeometry());return *pair;}
        }
        auto pair=std::make_unique<PairSolvers>(a.collisionGeometry(),b.collisionGeometry());
        auto& replaced=Pairs[Next];Next=(Next+1)%Pairs.size();replaced=std::move(pair);return *replaced;
    }
};
struct SeparationQuery final:coal::DistanceCallBackBase {
    double Minimum=std::numeric_limits<double>::infinity();bool Overlapping=false;std::size_t Count=0;
    const Model::Part* Moving=nullptr;const coal::CollisionObject* MovingObject=nullptr;std::optional<CollisionDistanceWitness> Witness;
    SolverContext& Solvers;
    const std::function<bool()>& Cancelled;
    SeparationQuery(SolverContext& solvers,const std::function<bool()>& cancelled):Solvers(solvers),Cancelled(cancelled){}
    bool distance(coal::CollisionObject* a,coal::CollisionObject* b,coal::CoalScalar& bound)override{
        Check(Cancelled);++Count;
        coal::DistanceRequest request(true,false,0,0);request.gjk_tolerance=1e-8;
        auto& pair=Solvers.Get(*a,*b);auto& result=pair.DistanceResult;auto& solve=pair.DistanceSolver();result.clear();
        solve(a->getTransform(),b->getTransform(),request,result);
        if(solve.Failed()||!std::isfinite(result.min_distance))throw std::runtime_error("COLLISION_DISTANCE_UNRESOLVED");
        const auto pa=static_cast<const Model::Part*>(a->getUserData()),pb=static_cast<const Model::Part*>(b->getUserData());
        const auto padding=(pa?pa->Padding:0)+(pb?pb->Padding:0);
        const auto distance=std::max(0.,result.min_distance-padding);
        if(distance<Minimum){
            auto first=result.nearest_points[0],second=result.nearest_points[1];
            // Unsigned distance can skip penetration witnesses. Ask the same
            // actual pair for contact points; never substitute a part center.
            if(result.min_distance<=0||!first.allFinite()||!second.allFinite()){
                coal::CollisionRequest contact;contact.enable_contact=true;contact.num_max_contacts=1;contact.gjk_tolerance=1e-8;
                auto& contacts=pair.CollisionResult;auto& solveContact=pair.CollisionSolver();contacts.clear();
                solveContact(a->getTransform(),b->getTransform(),contact,contacts);
                if(contacts.numContacts()){const auto& hit=contacts.getContact(0);first=hit.nearest_points[0];second=hit.nearest_points[1];}
                else if(contacts.nearest_points[0].allFinite()&&contacts.nearest_points[1].allFinite()){first=contacts.nearest_points[0];second=contacts.nearest_points[1];}
            }
            Witness.reset();if(Moving&&pa&&pb&&first.allFinite()&&second.allFinite()){
                const bool movingFirst=a==MovingObject;const auto fixedPoint=movingFirst?second:first,movingPoint=movingFirst?first:second;
                Witness=CollisionDistanceWitness{(movingFirst?pb:pa)->Index,Moving->Index,{fixedPoint[0],fixedPoint[1],fixedPoint[2]},{movingPoint[0],movingPoint[1],movingPoint[2]},distance,(first-second).norm(),false};
            }
        }
        Minimum=std::min(Minimum,distance);bound=std::min(bound,Minimum);
        if(result.min_distance<=padding){Overlapping=true;Minimum=bound=0;return true;}
        return false;
    }
};
struct Query final:coal::DistanceCallBackBase {
    double StopBelow,Minimum=std::numeric_limits<double>::infinity();std::size_t Count=0;
    SolverContext& Solvers;
    const std::function<bool()>& Cancelled;
    Query(double stop,SolverContext& solvers,const std::function<bool()>& cancel):StopBelow(stop),Solvers(solvers),Cancelled(cancel){}
    bool distance(coal::CollisionObject* a,coal::CollisionObject* b,coal::CoalScalar& bound)override{
        Check(Cancelled);++Count;coal::CollisionRequest request;
        const auto pa=static_cast<const Model::Part*>(a->getUserData()),pb=static_cast<const Model::Part*>(b->getUserData());
        request.enable_contact=false;request.security_margin=(pa?pa->Padding:0)+(pb?pb->Padding:0);
        // A collision query's default BV refinement distance is .001. A loose
        // BV bound above that may still be BELOW the caller's safety threshold.
        // Refine against that threshold, with guard room for interval bounds;
        // this changes work/precision only, never geometry or required clearance.
        request.break_distance=std::max(1e-3,StopBelow*4+1e-4);
        request.distance_upper_bound=std::numeric_limits<double>::infinity();
        request.gjk_tolerance=1e-6;request.gjk_convergence_criterion_type=coal::GJKConvergenceCriterionType::Absolute;
        auto& pair=Solvers.Get(*a,*b);auto& result=pair.CollisionResult;auto& solve=pair.CollisionSolver();result.clear();
        solve(a->getTransform(),b->getTransform(),request,result);
        // A non-converged GJK witness distance must never certify clearance.
        if(solve.Failed()){Minimum=bound=0;return true;}
        if(!std::isfinite(result.distance_lower_bound))throw std::runtime_error("COLLISION_DISTANCE_UNRESOLVED");
        const double lower=result.isCollision()?0:std::max(0.,result.distance_lower_bound-1e-6);
        Minimum=std::min(Minimum,lower);bound=std::min(bound,Minimum);
        // Early termination has not visited every volume: report zero rather
        // than incorrectly presenting this pair's distance as the global bound.
        if(Minimum<=StopBelow){Minimum=0;return true;}return false;
    }
};
class Service final:public ICollisionDistanceService {
    AUTO_REGIST_SERVICE(ICollisionDistanceService,Service)
    mutable std::mutex ContextMutex;
    mutable std::map<std::thread::id,std::shared_ptr<SolverContext>> Contexts;
    std::shared_ptr<SolverContext> Context()const{
        // The service owns contexts so they are destroyed before its DLL is
        // unloaded. A running query retains its context across map eviction.
        std::lock_guard lock(ContextMutex);const auto thread=std::this_thread::get_id();
        if(Contexts.contains(thread))return Contexts.at(thread);
        auto context=std::make_shared<SolverContext>();if(Contexts.size()>=64)Contexts.erase(Contexts.begin());
        Contexts.emplace(thread,context);return context;
    }
public:
    void OnLoad()override{}void OnUnload()override{std::lock_guard lock(ContextMutex);Contexts.clear();}
    CollisionDistanceModelRef PrepareBoxes(const std::vector<CollisionBox>& boxes,const std::function<bool()>& cancelled)const override{
        return PrepareVolumes(boxes,{},cancelled);
    }
    CollisionDistanceModelRef PrepareVolumes(const std::vector<CollisionBox>& boxes,const std::vector<CollisionHull>& hulls,const std::function<bool()>& cancelled)const override{
        if(boxes.empty()&&hulls.empty())throw std::invalid_argument("COLLISION_VOLUMES_REQUIRED");
        auto model=std::make_shared<Model>();
        for(const auto& box:boxes){Check(cancelled);const auto extent=Vector(box.HalfExtents);coal::Matrix3s axes;
            for(int i=0;i<3;++i)axes.col(i)=Vector(box.Axes[i]);
            if(extent.minCoeff()<0||(axes.transpose()*axes-coal::Matrix3s::Identity()).norm()>1e-8||std::abs(axes.determinant()-1)>1e-8)throw std::invalid_argument("COLLISION_BOX_INVALID");
            model->Add(std::make_shared<coal::Box>(2*extent.cwiseMax(1e-9)),{axes,Vector(box.Center)},0,false,{}, {coal::Vec3s::Zero()});
        }
        for(const auto& hull:hulls){Check(cancelled);
            if(hull.Points.empty()||hull.Points.size()>std::numeric_limits<unsigned>::max()||!std::isfinite(hull.Padding)||hull.Padding<0||hull.Padding>1e9)throw std::invalid_argument("COLLISION_HULL_INVALID");
            coal::Vec3s probe=coal::Vec3s::Zero();for(const auto& point:hull.Points)probe+=Vector(point);probe/=static_cast<double>(hull.Points.size());
            model->Add(std::make_shared<PointHull>(hull),coal::Transform3s(),0,false,{}, {probe});
        }
        model->Build();return model;
    }
    CollisionDistanceModelRef PrepareMesh(const CollisionMesh& mesh,const std::function<bool()>& cancelled)const override{
        if(mesh.Vertices.empty()||mesh.Triangles.empty()||mesh.Vertices.size()>std::numeric_limits<unsigned>::max()||mesh.Triangles.size()>std::numeric_limits<unsigned>::max()||
           !std::isfinite(mesh.Padding)||mesh.Padding<0||mesh.Padding>1e9)throw std::invalid_argument("COLLISION_MESH_INVALID");
        std::vector<coal::Vec3s> vertices;vertices.reserve(mesh.Vertices.size());
        for(const auto& point:mesh.Vertices){Check(cancelled);vertices.push_back(Vector(point));}
        std::vector<coal::Triangle> triangles;triangles.reserve(mesh.Triangles.size());
        for(const auto& triangle:mesh.Triangles){Check(cancelled);for(auto index:triangle)if(index>=vertices.size())throw std::invalid_argument("COLLISION_MESH_INVALID");
            triangles.emplace_back(triangle[0],triangle[1],triangle[2]);}
        // Oriented BVHs query the immutable mesh directly. AABB mesh collision
        // traversal would copy/transform the entire mesh for every query.
        auto geometry=std::make_shared<coal::BVHModel<coal::OBBRSS>>();
        if(geometry->beginModel(static_cast<unsigned>(triangles.size()),static_cast<unsigned>(vertices.size()))!=coal::BVH_OK||
           geometry->addSubModel(vertices,triangles)!=coal::BVH_OK||geometry->endModel()!=coal::BVH_OK)throw std::runtime_error("COLLISION_MESH_BUILD_FAILED");
        const auto volume=MeshVolume::Closed(vertices,triangles,cancelled,mesh.ClosedVolumes);
        Check(cancelled);auto model=std::make_shared<Model>();model->Add(geometry,coal::Transform3s(),mesh.Padding,false,volume,volume?volume->Probes:std::vector<coal::Vec3s>{vertices[mesh.Triangles.front()[0]]});model->Build();return model;
    }
    CollisionDistanceModelRef Combine(const std::vector<CollisionDistanceModelRef>& inputs,const std::function<bool()>& cancelled)const override{
        if(inputs.empty())throw std::invalid_argument("COLLISION_VOLUMES_REQUIRED");auto model=std::make_shared<Model>();
        for(const auto& input:inputs){Check(cancelled);const auto source=dynamic_cast<const Model*>(input.get());if(!source)throw std::invalid_argument("COLLISION_QUERY_INVALID");
            for(const auto& part:source->Parts)model->Add(part.Geometry,part.Placement,part.Padding,true,part.Volume,part.Probes);}
        model->Build();return model;
    }
    CollisionDistanceModelRef PrepareFrusta(const std::vector<CollisionFrustum>& solids)const override{
        if(solids.empty())throw std::invalid_argument("COLLISION_VOLUMES_REQUIRED");auto model=std::make_shared<Model>();
        for(const auto& solid:solids){for(double v:{solid.Z,solid.Height,solid.LowerRadius,solid.UpperRadius})if(!std::isfinite(v)||std::abs(v)>1e9)throw std::invalid_argument("COLLISION_FRUSTUM_INVALID");
            if(solid.Height<=0||solid.LowerRadius<0||solid.UpperRadius<0||solid.LowerRadius+solid.UpperRadius<=0)throw std::invalid_argument("COLLISION_FRUSTUM_INVALID");
            if(solid.LowerRadius==solid.UpperRadius){model->Add(std::make_shared<coal::Cylinder>(solid.LowerRadius,solid.Height),{coal::Matrix3s::Identity(),coal::Vec3s(0,0,solid.Z+solid.Height*.5)},0,false,{}, {coal::Vec3s::Zero()});continue;}
            // Circumscribed regular polygons contain each circular cross section
            // of the frustum. Linear interpolation of radius preserves inclusion.
            constexpr unsigned n=64;const double inflation=1/std::cos(Pi/n);
            auto points=std::make_shared<std::vector<coal::Vec3s>>();auto faces=std::make_shared<std::vector<coal::Triangle>>();
            for(unsigned ring=0;ring<2;++ring){const auto r=std::max(1e-9,(ring?solid.UpperRadius:solid.LowerRadius)*inflation);for(unsigned i=0;i<n;++i){const auto a=2*Pi*i/n;points->emplace_back(r*std::cos(a),r*std::sin(a),solid.Z+ring*solid.Height);}}
            for(unsigned i=0;i<n;++i){const auto j=(i+1)%n;faces->emplace_back(i,j,n+j);faces->emplace_back(i,n+j,n+i);}
            for(unsigned i=1;i+1<n;++i){faces->emplace_back(0,i+1,i);faces->emplace_back(n,n+i,n+i+1);}
            model->Add(std::make_shared<coal::Convex<coal::Triangle>>(points,static_cast<unsigned>(points->size()),faces,static_cast<unsigned>(faces->size())),coal::Transform3s(),0,false,{}, {coal::Vec3s(0,0,solid.Z+solid.Height*.5)});
        }model->Build();return model;
    }
    CollisionDistanceResult LowerBound(const CollisionDistanceModelRef& fixed,const CollisionDistanceModelRef& moving,
        const CollisionTransform& transform,double stopBelow,const std::function<bool()>& cancelled)const override{
        const auto a=dynamic_cast<const Model*>(fixed.get()),b=dynamic_cast<const Model*>(moving.get());
        if(!a||!b||!std::isfinite(stopBelow)||stopBelow<0)throw std::invalid_argument("COLLISION_QUERY_INVALID");
        const auto pose=Transform(transform);const auto context=Context();Query query(stopBelow,*context,cancelled);
        // Prepared geometry is immutable; mutable solvers belong to this thread.
        for(std::size_t i=0;i<b->Parts.size();++i){const auto& part=b->Parts[i];Check(cancelled);coal::CollisionObject object(part.Geometry,pose*part.Placement,false);object.setUserData(const_cast<Model::Part*>(&part));
            object.getAABB().min_.array()-=part.Padding;object.getAABB().max_.array()+=part.Padding;
            a->Tree.distance(&object,&query);if(query.Minimum==0)break;}
        if(!std::isfinite(query.Minimum))throw std::runtime_error("COLLISION_DISTANCE_UNRESOLVED");
        if(query.Minimum>0&&Contained(*a,*b,pose,cancelled))query.Minimum=0;
        return {query.Minimum,query.Count};
    }
    CollisionSeparationResult Separation(const CollisionDistanceModelRef& fixed,const CollisionDistanceModelRef& moving,
        const CollisionTransform& transform,const std::function<bool()>& cancelled)const override{
        const auto a=dynamic_cast<const Model*>(fixed.get()),b=dynamic_cast<const Model*>(moving.get());
        if(!a||!b)throw std::invalid_argument("COLLISION_QUERY_INVALID");
        const auto pose=Transform(transform);const auto context=Context();SeparationQuery query(*context,cancelled);
        for(std::size_t i=0;i<b->Parts.size();++i){const auto& part=b->Parts[i];Check(cancelled);coal::CollisionObject object(part.Geometry,pose*part.Placement,false);object.setUserData(const_cast<Model::Part*>(&part));
            query.Moving=&part;query.MovingObject=&object;
            object.getAABB().min_.array()-=part.Padding;object.getAABB().max_.array()+=part.Padding;
            a->Tree.distance(&object,&query);if(query.Overlapping)break;
        }
        if(!std::isfinite(query.Minimum))throw std::runtime_error("COLLISION_DISTANCE_UNRESOLVED");
        if(!query.Overlapping)if(const auto contained=Contained(*a,*b,pose,cancelled)){
            const auto& fixedPart=a->Parts[contained->Fixed];const auto& movingPart=b->Parts[contained->Moving];
            coal::CollisionObject first(fixedPart.Geometry,fixedPart.Placement,false),second(movingPart.Geometry,pose*movingPart.Placement,false);
            first.setUserData(const_cast<Model::Part*>(&fixedPart));second.setUserData(const_cast<Model::Part*>(&movingPart));
            SeparationQuery pair(*context,cancelled);pair.Moving=&movingPart;pair.MovingObject=&second;coal::CoalScalar bound=std::numeric_limits<double>::infinity();pair.distance(&first,&second,bound);
            query.Count+=pair.Count;query.Witness=pair.Witness;if(query.Witness){query.Witness->Containment=true;query.Witness->Separation=0;}
            query.Overlapping=true;query.Minimum=0;
        }
        constexpr double tolerance=1e-6;
        return {query.Overlapping?0:std::max(0.,query.Minimum-tolerance),query.Overlapping?0:query.Minimum+tolerance,query.Overlapping,query.Count,query.Witness};
    }
};
}
