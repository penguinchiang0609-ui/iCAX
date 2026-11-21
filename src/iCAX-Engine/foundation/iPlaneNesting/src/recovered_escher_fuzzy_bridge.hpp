#pragma once
#include "recovered_escher_boundary_length.hpp"
#include "recovered_escher_boundary_cache.hpp"
namespace study::recovered {
// Complete weighted construction and directional query, without a supplied
// region tree or a scripted selection provider.
EscherFuzzySelectionResult escherFuzzySelect(const ConvolutionIntegerPolygon &,
    std::span<const double>,EscherIntegerPoint primary={-1,0},
    EscherIntegerPoint secondary={0,-1});
void escherBindBoundaryFuzzy(EscherBoundaryLengthServices &);
// The cache and services must outlive any prepared host using these callbacks.
// The raw length entry expects the sheet complement (CW exterior, CCW cutouts).
void escherBindBoundaryGeometry(EscherBoundaryLengthServices &,EscherPlacementMapCache &);
}
