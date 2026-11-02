#pragma once
#include <GeometryServices/SurfaceTravelService.h>
#include <optional>

namespace iCAX::GeometryServices
{
    enum class RapidPathKind { Retract, Arch, Follow };
    struct RapidPathConstruction
    {
        RapidPathKind Kind=RapidPathKind::Retract;
        double FollowHeight=1, LiftHeight=10;
        TravelPoint LateralOffset{};
        std::optional<TravelPoint> TurnPoint;
    };
    struct RapidPathGeometryQuery
    {
        // Beam-target anchors in a common frame. Their prescribed directions
        // are interpolated, never optimized. Returned points are tool tips.
        TravelPose Start, End;
        RapidPathKind Kind=RapidPathKind::Retract;
        double FollowHeight=1, LiftHeight=10, Tolerance=.05;
        // Lateral guide deviation. It is projected perpendicular to the local
        // prescribed axis; it never contributes to the selected lift height.
        TravelPoint LateralOffset{};
        // Optional explicit retraction corner. Its prescribed direction is
        // End.Direction; lifted start/end anchors retain the original formula.
        // Mutually exclusive with a lateral guide, and valid only for Retract.
        std::optional<TravelPoint> TurnPoint;
        // Explicit tool-tip waypoints. Endpoints still come from the anchors.
        // A manual route follows these points exactly, without a lift profile.
        bool Manual=false;
        // A user-authored construction keeps the exact strategy formula but
        // remains Manual to collision planners: validate it without rerouting.
        bool ConstructedManual=false;
        std::vector<TravelPose> Waypoints;
        double LinearSpeed=100, LinearAcceleration=1000;
        double AngularSpeed=1, AngularAcceleration=2;
        // Only Follow needs a supporting surface, in its own rigid frame.
        // There are deliberately no collision bodies, clearances, head models,
        // inverse kinematics or search callbacks in this construction contract.
        std::shared_ptr<const GeometryData::BRepModel> Surface;
        std::array<double,16> SurfaceTransform{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    };
    struct RapidPathGeometryResult
    {
        bool Generated=false;
        std::string Reason;
        std::vector<TravelPose> Poses;
        std::vector<TravelPoint> SurfacePoints;
        std::vector<double> Heights;
        std::optional<RapidPathConstruction> Construction;
        std::optional<TravelPose> ShapeHandle;
        TravelPose SourceStart, SourceEnd;
        double Length=0, EstimatedSeconds=0;
    };
    class IRapidPathGeometryService : public Services::IService
    {
    public:
        // Constructs exactly one route per query from the supplied strategy
        // and parameters. A generated route has NOT been collision checked.
        virtual std::vector<RapidPathGeometryResult> Generate(
            const std::vector<RapidPathGeometryQuery>&,
            const std::function<bool()>& cancelled) const=0;
    };
}
