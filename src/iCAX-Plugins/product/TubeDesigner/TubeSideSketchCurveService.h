#pragma once

#include "BRepTubeUnfoldingService.h"

namespace iCAX::TubeDesigner
{
    _TUBE_DESIGNER_EXP STubeBRepUnfoldingResult UnfoldTubeSideSketchCurveReference(
        const TopoDS_Shape& Shape_);

    // Complete analytic sketch regions are mapped onto the native extrusion
    // surfaces. No polygon tessellation is used to create removal volumes.
    _TUBE_DESIGNER_EXP STubeSideSketchResult ApplyTubeSideSketchCurves(
        const TopoDS_Shape& Shape_, const iCAX::Data::ObjectMap& Sketch_,
        const STubeSideSketchOptions& Options_ = {});
}
