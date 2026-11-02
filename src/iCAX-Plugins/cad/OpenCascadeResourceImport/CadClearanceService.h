#pragma once
#include "OpenCascadeResourceImportExport.h"
#include "GeometryData/GeometryData.h"
#include "Services/IService.h"
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace iCAX::Geometry
{
    struct SClearanceObject
    {
        std::string ID;
        std::shared_ptr<const iCAX::GeometryData::BRepModel> Geometry;
        std::array<double,16> Transform;
    };
    struct SClearancePair
    {
        std::string First, Second;
        double DistanceMm = 0;
        std::array<double,3> FirstPoint{}, SecondPoint{};
        double MeshAllowanceMm = 0;
    };
    // Pure geometry: no Scene, resource publication or product state.
    class _OPEN_CASCADE_RESOURCE_IMPORT_EXP ICadClearanceService : public iCAX::Services::IService
    {
    public:
        virtual std::vector<SClearancePair> Measure(const std::vector<SClearanceObject>& objects,
            const std::function<bool()>& isCancelled) const = 0;
    };
}
