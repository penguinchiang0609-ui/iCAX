#pragma once
#include "OpenCascadeResourceImportExport.h"
#include "GeometryData/GeometryData.h"

namespace iCAX::OpenCascade {
    // Deliberate extension point. Currently returns the input unchanged: no
    // assembly detection, connectivity inference, splitting or repair.
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP iCAX::GeometryData::BRepModel
        PrepareAssemblyForRecognition(const iCAX::GeometryData::BRepModel& source);
}
