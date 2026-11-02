#pragma once
#include <Services/IService.h>
#include <GeometryData/GeometryData.h>
#include "CollisionDistanceService.h"
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace iCAX::GeometryServices
{
    using TravelPoint = std::array<double,3>;
    struct TravelObstacle
    {
        std::string Id;
        std::shared_ptr<const GeometryData::BRepModel> Geometry;
        std::array<double,16> Transform{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        std::shared_ptr<const CollisionMesh> Mesh;
    };
    struct TravelPose { TravelPoint Point{}, Direction{0,0,1}; };
    // Neutral rotational geometry, shared with rigid-motion collision checking.
    struct AxialSolid { double Z=0, Height=1, LowerRadius=1, UpperRadius=1; };
    enum class SurfaceTravelKind { Follow, Arch, Retract, SafeExterior };
    struct SurfaceTravelQuery
    {
        // All coordinates are in the common obstacle frame. Surface is an index
        // into Obstacles. Anchors are beam targets, not tool-tip positions.
        std::vector<TravelObstacle> Obstacles;
        std::size_t Surface=0;
        TravelPose Start, End;
        SurfaceTravelKind Kind=SurfaceTravelKind::Retract;
        // Arch bridges open space and raises above this minimum until clear;
        // Follow alone requires a continuous supporting surface.
        double FollowHeight=1, LiftHeight=10, Clearance=.3, Tolerance=.05;
        // Optional conservative rotational envelope: cylinder begins at the
        // tool tip and extends along +Direction. Zero means NOT configured.
        double EnvelopeRadius=0, EnvelopeLength=0;
        std::vector<AxialSolid> EnvelopeSolids;
        // An imported body is bounded by a conservative rotational cylinder
        // after calibration. This remains valid for any roll about Direction.
        std::shared_ptr<const GeometryData::BRepModel> EnvelopeBody;
        std::array<double,16> EnvelopeCalibration{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        double LinearSpeed=100, LinearAcceleration=1000;
        double AngularSpeed=1, AngularAcceleration=2;
    };
    struct SurfaceTravelResult
    {
        std::vector<TravelPose> Poses;
        std::vector<TravelPoint> SurfacePoints;
        std::vector<double> Heights;
        // Checked motion is linear translation and spherical direction
        // interpolation between consecutive poses. The cylinder is roll invariant.
        // Clear means the supplied envelope and BReps only. No kinematics,
        // machine qualification, real controller trajectory or export claim.
        std::string Status="Unchecked", Reason, Obstacle;
        // Comparison estimate assumes a stop at every returned pose; it is not
        // controller timing and depends on the sampled path resolution.
        double Length=0, EstimatedSeconds=0, CertifiedClearance=0;
        std::size_t FailedSegment=0, DistanceQueries=0;
    };
    class ISurfaceTravelService : public Services::IService
    {
    public:
        virtual std::vector<SurfaceTravelResult> Plan(const std::vector<SurfaceTravelQuery>&,
            const std::function<bool()>& cancelled) const=0;
    };
}
