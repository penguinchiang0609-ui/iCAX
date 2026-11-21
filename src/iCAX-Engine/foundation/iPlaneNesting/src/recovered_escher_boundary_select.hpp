#pragma once
#include "recovered_escher_boundary_geometry.hpp"
#include "recovered_convolution_offset.hpp"
namespace study::recovered {
struct EscherFuzzySelectionResult {EscherBoundarySampleSelection selection;bool success=false;};
using EscherFuzzySelectionProvider=std::function<EscherFuzzySelectionResult(const ConvolutionIntegerPolygon &,
    std::span<const double>,EscherIntegerPoint,EscherIntegerPoint)>;
using EscherBoundaryOffsetProvider=std::function<ConvolutionIntegerPolygon(const ConvolutionIntegerRing &,int64_t)>;
EscherBoundarySampleSelection escherBoundaryBooleanSelect(const ConvolutionIntegerPolygon &,std::span<const double>,
    int32_t retries,const EscherFuzzySelectionProvider &,const EscherBoundaryOffsetProvider & = {},
    const ConvolutionHierarchyDiagnostic & = {}); //7ab7c0
}
