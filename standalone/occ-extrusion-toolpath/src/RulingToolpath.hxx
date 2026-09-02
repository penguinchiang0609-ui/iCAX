#pragma once

#include "SectionRegion.hxx"

namespace etp::detail
{
    // Internal implementation of the public bool + out-parameter API.  The
    // supplied trajectory is the drive rail: its points define XYZ, while the
    // recognized ruling through each point defines IJK and material depth.
    [[nodiscard]] bool TryBuildRulingToolpath(
        const TopoDS_Face& face,
        const TopoDS_Wire& cuttingTrajectory,
        const SectionRegion& section,
        const AnalyzerOptions& options,
        Toolpath& outToolpath);
}
