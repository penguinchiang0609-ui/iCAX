#pragma once
#include <Services/IService.h>
#include <GeometryData/BRepDisplay.h>
#include <functional>

namespace iCAX::GeometryServices
{
    struct BRepDisplayOptions
    {
        double Deflection = 0.05; // source length units
        double FeatureAngleDegrees = 22;
        std::size_t MaxPoints = 2000000;
        std::function<bool()> IsCancelled;
    };
    // Derived display data only: no resource, product or scene state.
    class IBRepDisplayService : public Services::IService
    {
    public:
        // Retains all CAD record identities; only the derived triangulations change.
        virtual GeometryData::BRepModel RefineMesh(const GeometryData::BRepModel& model,
            const BRepDisplayOptions& options = {}) const = 0;
        virtual GeometryData::BRepDisplayEdges BuildEdges(const GeometryData::BRepModel& model,
            const BRepDisplayOptions& options = {}) const = 0;
    };
}
