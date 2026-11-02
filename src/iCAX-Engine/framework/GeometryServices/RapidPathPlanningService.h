#pragma once
#include "RapidPathGeometryService.h"

namespace iCAX::GeometryServices
{
    struct RapidCollisionGeometry
    {
        std::vector<TravelObstacle> Obstacles;
        std::vector<AxialSolid> Solids;
        std::vector<CollisionBox> Boxes;
        std::shared_ptr<const GeometryData::BRepModel> Body;
        std::array<double,16> Calibration{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    };
    struct RapidPathPlanningQuery
    {
        RapidPathGeometryQuery Geometry;
        std::shared_ptr<const RapidCollisionGeometry> Collision;
        double Clearance=.3;
    };
    struct RapidPathPlanningResult : RapidPathGeometryResult
    {
        // A failed plan may retain the original constructed route for editing.
        // Poses alone never qualify it for motion: Generated and Clear are required.
        std::string Status="Unchecked",Obstacle,Method;
        TravelPoint DetourOffset{};
        double CertifiedClearance=0;
        std::size_t Candidates=0;
    };
    class IRapidPathPlanningService : public Services::IService
    {
    public:
        // Search lateral alternatives only. Kind, LiftHeight, FollowHeight and
        // endpoint poses are immutable constraints. No Scene or persistence.
        // Independent mesh/box connections may run concurrently. Cancellation
        // must be safe to read concurrently; result order matches query order.
        virtual std::vector<RapidPathPlanningResult> Plan(const std::vector<RapidPathPlanningQuery>&,
            const std::function<bool()>& cancelled={})const=0;
    };
}
