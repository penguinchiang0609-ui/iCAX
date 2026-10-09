#pragma once

#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>

namespace iCAX::TubeDesigner
{
    // Complete finite-width planar region in world XY (U,S). Open ends use
    // perpendicular butt caps. General offset curves use smooth BSplines
    // with a checked approximation error of at most 1e-7 mm.
    TopoDS_Shape BuildTubeSideSketchKerf(
        const TopoDS_Wire& Wire_, double Width_, double Tolerance_ = 1.0e-7,
        bool* Approximate_ = nullptr);
}
