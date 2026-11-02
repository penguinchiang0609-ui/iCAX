#pragma once
#include "OpenCascadeResourceImportExport.h"
#include "GeometryData/GeometryData.h"
#include "Services/IService.h"
#include <array>
#include <functional>

namespace iCAX::CAD
{
    // Pure geometry operation. The caller owns resources, revisions and undo.
    class _OPEN_CASCADE_RESOURCE_IMPORT_EXP ICadBRepScaleService : public Services::IService
    {
    public:
        virtual GeometryData::BRepModel Scale(const GeometryData::BRepModel& geometry,
            const std::array<double,3>& factors, const std::array<double,3>& center,
            const std::function<bool()>& isCancelled = {}) const = 0;
    };
}
