#pragma once
#include "BRepTubeUnfoldingService.h"

namespace iCAX::TubeDesigner
{
    // End planes are expressed in the frozen blank's recognized S/Y/Z frame.
    _TUBE_DESIGNER_EXP STubeSideSketchResult ApplyTubeSideSketchCad(
        const TopoDS_Shape& Blank_, const iCAX::Data::ObjectMap& Sketch_,
        const STubeSideSketchOptions& Options_ = {});
    _TUBE_DESIGNER_EXP std::vector<TopoDS_Shape> TubeSideSketchSolids(const TopoDS_Shape& Shape_);
}
