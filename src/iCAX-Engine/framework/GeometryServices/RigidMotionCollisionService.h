#pragma once
#include "SurfaceTravelService.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <optional>
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <condition_variable>

namespace iCAX::GeometryServices
{
    namespace RigidMotion
    {
        using Vector = std::array<double,3>;
        using Quaternion = std::array<double,4>; // x,y,z,w; local to world
        inline Vector Add(Vector a,Vector b){for(int i=0;i<3;++i)a[i]+=b[i];return a;}
        inline Vector Sub(Vector a,Vector b){for(int i=0;i<3;++i)a[i]-=b[i];return a;}
        inline Vector Scale(Vector a,double s){for(auto& x:a)x*=s;return a;}
        inline double Dot(Vector a,Vector b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
        inline double Length(Vector a){return std::sqrt(Dot(a,a));}
        inline Vector Cross(Vector a,Vector b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
        inline Vector Unit(Vector a){const auto n=Length(a);if(!std::isfinite(n)||n<1e-12)throw std::invalid_argument("MOTION_DIRECTION_INVALID");return Scale(a,1/n);}
        inline Quaternion Unit(Quaternion q){double n=0;for(auto v:q)n+=v*v;if(!std::isfinite(n)||n<1e-24)throw std::invalid_argument("MOTION_ROTATION_INVALID");for(auto& v:q)v/=std::sqrt(n);return q;}
        inline Quaternion Multiply(Quaternion a,Quaternion b){const auto v=Add(Add(Scale({b[0],b[1],b[2]},a[3]),Scale({a[0],a[1],a[2]},b[3])),Cross({a[0],a[1],a[2]},{b[0],b[1],b[2]}));return Unit(Quaternion{v[0],v[1],v[2],a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]});}
        inline Quaternion Between(Vector a,Vector b){a=Unit(a);b=Unit(b);const auto w=Dot(a,b)+1;Vector v;
            if(w<1e-8){v=std::abs(a[0])>std::abs(a[2])?Vector{-a[1],a[0],0}:Vector{0,-a[2],a[1]};return Unit(Quaternion{v[0],v[1],v[2],0});}
            v=Cross(a,b);return Unit(Quaternion{v[0],v[1],v[2],w});}
        inline Vector Rotate(Quaternion q,Vector p){const Vector v{q[0],q[1],q[2]};const auto t=Scale(Cross(v,p),2);return Add(p,Add(Scale(t,q[3]),Cross(v,t)));}
        inline double Angle(Quaternion a,Quaternion b){double d=0;for(int i=0;i<4;++i)d+=a[i]*b[i];return 2*std::acos(std::min(1.,std::abs(d)));}
        inline Quaternion Interpolate(Quaternion a,Quaternion b,double t){double d=0;for(int i=0;i<4;++i)d+=a[i]*b[i];if(d<0){for(auto& v:b)v=-v;d=-d;}
            const double angle=std::acos(std::min(1.,d)),x=angle<1e-8?1-t:std::sin((1-t)*angle)/std::sin(angle),y=angle<1e-8?t:std::sin(t*angle)/std::sin(angle);
            for(int i=0;i<4;++i)a[i]=a[i]*x+b[i]*y;return Unit(a);}
    }
    struct RigidMotionPose { TravelPoint Position{}; RigidMotion::Quaternion Rotation{0,0,0,1}; };
    // Neutral optional analytic geometry. Each solid is a frustum along local +Z.
    struct RigidMotionSegment { RigidMotionPose Start,End; double Clearance=0; bool Connected=true; };
    struct RigidMotionCollisionQuery
    {
        std::shared_ptr<const GeometryData::BRepModel> Body;
        std::vector<AxialSolid> Solids; // mutually exclusive with Body
        std::vector<CollisionBox> Boxes; // compound primitive collider, model-local
        RigidMotionPose BodyCalibration; // model coordinates to trajectory frame
        std::vector<TravelObstacle> Obstacles;
        std::vector<RigidMotionSegment> Segments;
        // Cooperative exact-kernel deadline for each segment, including its
        // continuous-motion refinement. Expiry remains Unchecked. Callers may
        // explicitly retry unresolved segments with a larger bounded budget.
        unsigned MaxSegmentTimeMs=500;
    };
    struct RigidMotionCollisionWitness
    {
        std::size_t BoxIndex=std::numeric_limits<std::size_t>::max();
        // Common trajectory frame, evaluated at the result's exact Fraction.
        TravelPoint BodyPoint{},ObstaclePoint{};
        double Separation=0,SurfaceDistance=0,RequiredClearance=0;
        bool Containment=false;
    };
    struct RigidMotionCollisionSegment
    {
        std::string Status="Unchecked", Reason, Obstacle;
        double Fraction=0, CertifiedClearance=0;
        std::optional<RigidMotionCollisionWitness> Witness;
    };
    struct RigidMotionCollisionResult
    {
        std::vector<RigidMotionCollisionSegment> Segments;
        std::size_t DistanceQueries=0;
    };
    class IRigidMotionCollisionSession
    {
    public:
        virtual ~IRigidMotionCollisionSession()=default;
        // One caller owns this mutable solver/cache. Collider geometry remains
        // immutable; every interval keeps the same continuous-motion contract.
        virtual RigidMotionCollisionResult Check(const std::vector<RigidMotionSegment>&,
            const std::function<bool()>& cancelled,bool stopAtFirstFailure=false)=0;
    };
    class IRigidMotionCollisionService : public Services::IService
    {
    public:
        // Certifies continuous linear translation and shortest quaternion slerp.
        // Unresolved intervals remain Unchecked. No machining or machine rules.
        virtual RigidMotionCollisionResult Check(const RigidMotionCollisionQuery&,
            const std::function<bool()>& cancelled,
            const std::function<void(std::size_t,std::size_t)>& progress={}) const=0;
        // Prepare geometry once, evaluate requested intervals, and deliver
        // neutral results incrementally without requiring a full path scan.
        virtual RigidMotionCollisionResult CheckIncremental(const RigidMotionCollisionQuery&,
            const std::function<std::optional<std::size_t>()>& nextSegment,
            const std::function<void(std::size_t,const RigidMotionCollisionSegment&)>& result,
            const std::function<bool()>& cancelled) const=0;
        // Prepare a supplied box/mesh (or exact cylinder/mesh) environment once
        // for several alternative paths. Segments in this query are ignored.
        virtual std::unique_ptr<IRigidMotionCollisionSession> CreatePrimitiveMeshSession(
            const RigidMotionCollisionQuery&,const std::function<bool()>& cancelled)const=0;

        // Independent immutable intervals, with one solver/cache per worker.
        // Result slots retain query order; callbacks are serialized but may
        // arrive out of order. Cancellation must be safe to read concurrently.
        // All workers join before return/throw, providing a phase barrier.
        RigidMotionCollisionResult CheckParallel(const RigidMotionCollisionQuery& query,unsigned workers,
            const std::function<bool()>& cancelled,
            const std::function<void(std::size_t)>& started={},
            const std::function<void(std::size_t,const RigidMotionCollisionSegment&)>& completed={}) const
        {
            if(!workers||workers>64||query.Segments.empty())throw std::invalid_argument("MOTION_PARALLEL_BUDGET_INVALID");
            workers=static_cast<unsigned>(std::min<std::size_t>(workers,query.Segments.size()));
            RigidMotionCollisionResult result;result.Segments.resize(query.Segments.size());
            std::mutex mutex;std::condition_variable prepared;bool firstSolverReady=false;std::atomic_bool failed=false;std::exception_ptr error;std::size_t next=0,received=0;
            const auto stop=[&]{return failed.load()||(cancelled&&cancelled());};
            const auto batch=std::max<std::size_t>(1,std::min<std::size_t>(32,query.Segments.size()/(workers*4)));
            const auto run=[&]{try{
                std::size_t cursor=0,limit=0;
                const auto take=[&]()->std::optional<std::size_t>{std::lock_guard lock(mutex);if(!firstSolverReady){firstSolverReady=true;prepared.notify_all();}if(stop())return {};
                    // Adjacent intervals share endpoint distances. Reserve a
                    // contiguous batch for each solver so its local cache can
                    // reuse them, while multiple batches retain load balancing.
                    if(cursor==limit){if(next==query.Segments.size())return {};cursor=next;limit=std::min(next+batch,query.Segments.size());next=limit;}
                    const auto index=cursor++;if(started)started(index);return index;};
                const auto put=[&](std::size_t index,const RigidMotionCollisionSegment& value){std::lock_guard lock(mutex);if(stop())return;result.Segments.at(index)=value;++received;if(completed)completed(index,value);};
                const auto local=CheckIncremental(query,take,put,stop);
                std::lock_guard lock(mutex);result.DistanceQueries+=local.DistanceQueries;firstSolverReady=true;prepared.notify_all();
            }catch(...){const auto failure=std::current_exception();std::lock_guard lock(mutex);if(!error)error=failure;failed.store(true);prepared.notify_all();}};
            std::vector<std::jthread> pool;pool.reserve(workers);
            try{
                if(workers==1)run();
                else{
                    // CheckIncremental prepares immutable geometry before it
                    // requests work. Let the first solver populate shared model
                    // caches before other workers prepare their local solvers.
                    pool.emplace_back(run);
                    {std::unique_lock lock(mutex);prepared.wait(lock,[&]{return firstSolverReady||failed.load();});}
                    if(!failed.load()){for(unsigned i=2;i<workers;++i)pool.emplace_back(run);run();}
                }
            }
            catch(...){failed.store(true);for(auto& worker:pool)worker.join();throw;}
            for(auto& worker:pool)worker.join();
            if(error)std::rethrow_exception(error);
            if(cancelled&&cancelled())throw std::runtime_error("MOTION_CANCELLED");
            if(received!=query.Segments.size())throw std::runtime_error("MOTION_RESULT_INCOMPLETE");
            return result;
        }
    };
}
