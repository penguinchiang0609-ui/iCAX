#pragma once
#include "ExtrusionRecognitionExport.h"
#include "OpenCascadeResourceImport/CadBRepHealService.h"

namespace iCAX::ExtrusionRecognition
{
    // Pipeline stage before axis recognition/alignment. Geometry policy and
    // diagnostics are explicit; resource publication belongs to the caller.
    class _EXTRUSION_RECOGNITION_EXP HealWork final
    {
    public:
        CAD::SCadBRepHealResult Run(const GeometryData::BRepModel& geometry,
            const CAD::SCadBRepHealOptions& options = {},
            const std::function<bool()>& isCancelled = {}) const;
    };
}
