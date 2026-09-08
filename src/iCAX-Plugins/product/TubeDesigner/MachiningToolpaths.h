#pragma once
#include "TubeDesignerExport.h"
#include "Data/Variant.h"
#include "../../cam/BRepToCamPath/BRepToCamPathService.h"

namespace iCAX::TubeDesigner
{
    // Standalone world-space nominal paths. Source CAD is read only at analysis.
    _TUBE_DESIGNER_EXP iCAX::Data::ObjectMap SerializeMachiningToolpaths(const iCAX::CAM::CamPathAnalysis& Analysis_);
    _TUBE_DESIGNER_EXP iCAX::Data::VariantArray ValidateMachiningToolpaths(const iCAX::Data::VariantArray& Paths_);
}
