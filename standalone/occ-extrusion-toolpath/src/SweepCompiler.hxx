#pragma once

#include "SectionRegion.hxx"

namespace etp::detail
{
    [[nodiscard]] FeatureAnalysis CompileSweepFeature(
        const TopoDS_Shape& featureGroup,
        const SectionRegion& section,
        const AnalyzerOptions& options,
        std::size_t featureIndex);
}

