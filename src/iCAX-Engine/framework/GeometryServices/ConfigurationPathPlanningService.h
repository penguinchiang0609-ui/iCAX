#pragma once
#include <Services/IService.h>
#include <functional>
#include <string>
#include <vector>

namespace iCAX::GeometryServices {
    using PathConfiguration=std::vector<double>;
    struct PathAxis {double Minimum=0,Maximum=1,Weight=1;bool Periodic=false;};
    struct ConfigurationPathQuery {
        std::vector<PathAxis> Axes;
        PathConfiguration Start,Goal;
        // Callers define kinematics, collision geometry and allowed motions.
        // CheckMotion must validate the entire interpolated segment, including
        // its endpoint. It must not treat finite samples as a clearance proof.
        // Capture frozen input or worker-local geometry runtime, never a live
        // Scene/Repository. OMPL and collision-library types stay private.
        std::function<bool(const PathConfiguration&)> CheckState;
        std::function<bool(const PathConfiguration&,const PathConfiguration&)> CheckMotion;
        // Cooperative time budget, checked between validator calls. A caller
        // must keep individual validation calls bounded/cancellation-aware;
        // a blocking collision query can overrun this requested budget.
        double MaximumSeconds=.25;
    };
    struct ConfigurationPathResult {
        std::string Status,Method;
        std::vector<PathConfiguration> Path;
        double ElapsedMs=0;
        std::size_t StateChecks=0,MotionChecks=0;
    };
    class IConfigurationPathPlanningService:public Services::IService {
    public:
        virtual ConfigurationPathResult Plan(const ConfigurationPathQuery&,
            const std::function<bool()>& cancelled={})const=0;
    };
}
