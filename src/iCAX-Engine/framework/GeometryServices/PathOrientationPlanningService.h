#pragma once
#include <Services/IService.h>
#include <array>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace iCAX::GeometryServices {
    using OrientationVector = std::array<double,3>;
    struct PathOrientation {
        OrientationVector Direction{0,0,1};
        // Optional physical axis solutions, already unwrapped by the owner.
        // Never reduce these modulo 2*pi: travel limits and cable winding matter.
        std::vector<double> Axes;
    };
    struct OrientationWaypoint {
        OrientationVector Position{};
        PathOrientation Original;
        std::vector<PathOrientation> Candidates;
        double MaximumDeviationRadians=0;
        bool Locked=false;
    };
    struct OrientationEditRange {
        // Only points STRICTLY INSIDE a range may change. Its two boundary
        // anchors and all points outside ranges retain the complete Original.
        std::size_t First=0,Last=0;
    };
    struct OrientationAxisLimit {
        double Minimum=0,Maximum=0,MaximumStep=0,Weight=1;
    };
    struct PathOrientationQuery {
        std::vector<OrientationWaypoint> Waypoints;
        std::vector<OrientationEditRange> EditableRanges;
        std::vector<OrientationAxisLimit> Axes;
        // Explicit finite machine candidate graph mode. Default preserves the
        // local-edit rule: range boundaries/outside remain Original. When true
        // every unlocked waypoint, including endpoints, may select candidates.
        bool AllWaypointsEditable=false;
        double MaximumDirectionStepRadians=3.14159265358979323846;
        double DeviationWeight=1,DirectionChangeWeight=1;
        // Frozen-input/worker-local callbacks only. The owner supplies candidate
        // process cones and IK solutions; this neutral service owns no CAM rules.
        std::function<bool(std::size_t,const PathOrientation&)> CheckState;
        // Edge i follows the ORIGINAL EXACT curve between i and i+1, with the
        // owner's specified interpolation of orientation/physical axes. Check
        // the entire motion, not merely the two endpoints or finite samples.
        std::function<bool(std::size_t,const PathOrientation&,const PathOrientation&)> CheckMotion;
        double MaximumSeconds=5;
        std::size_t MaximumGraphEdges=2000000,MaximumSearchIterations=512;
    };
    struct PathOrientationResult {
        std::string Status="Unchecked";
        // No replacement positions/curves: callers can only apply orientations
        // at the SAME original stations. Failed/cancelled results contain none.
        std::vector<PathOrientation> Orientations;
        std::size_t FailedWaypoint=std::numeric_limits<std::size_t>::max();
        std::size_t FailedEdge=std::numeric_limits<std::size_t>::max();
        std::size_t CandidateCount=0,GraphBuilds=0,StateChecks=0,MotionChecks=0;
        double Cost=0,ElapsedMs=0,GraphMs=0,ValidationMs=0;
    };
    class IPathOrientationPlanningService : public Services::IService {
    public:
        // Best path in the supplied finite candidate graph; NoSolution does NOT
        // prove continuous-space infeasibility. Budget is cooperative, checked
        // around callbacks; individual geometry queries must also be bounded.
        virtual PathOrientationResult Plan(const PathOrientationQuery&,
            const std::function<bool()>& cancelled={}) const=0;
    };
}
