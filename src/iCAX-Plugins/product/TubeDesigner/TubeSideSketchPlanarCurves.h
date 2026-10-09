#pragma once

#include "Data/Variant.h"

#include <TopoDS_Face.hxx>

#include <cstddef>
#include <string>
#include <vector>

namespace iCAX::TubeDesigner
{
    // Complete removal regions in the unfolded plane: X is circumferential
    // distance U and Y is axial distance S. Curved boundaries stay OCC curves.
    struct SSideSketchPlanarRegions final
    {
        bool bOK = false;
        bool Approximate = false;
        std::string Diagnostic;
        std::vector<TopoDS_Face> Faces;
        std::size_t ClosedLoopCount = 0;
        std::size_t OpenTrajectoryCount = 0;
        std::size_t CurveCount = 0;
    };

    SSideSketchPlanarRegions BuildTubeSideSketchPlanarRegions(
        const iCAX::Data::ObjectMap& Sketch_, double Length_,
        double Perimeter_, double Tolerance_ = 1.0e-7);
}
