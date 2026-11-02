#pragma once
#include "OpenCascadeResourceImportExport.h"
#include "GeometryData/GeometryData.h"
#include "Services/IService.h"
#include <array>

namespace iCAX::Geometry
{
    // Rigid geometry operation only; resources and coordinate-system ownership
    // belong to the caller. Topology IDs and parameterization remain unchanged.
    class _OPEN_CASCADE_RESOURCE_IMPORT_EXP ICadBRepTransformService : public Services::IService
    {
    public:
        virtual GeometryData::BRepModel Transform(const GeometryData::BRepModel&,
            const std::array<double,16>& rowMajor) const = 0;
    };
}
