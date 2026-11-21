#pragma once
#include "recovered_convolution_minkowski.hpp"
namespace study::recovered {
// Nonempty finite rings that remain nondegenerate after quantization. Original
// orientation can divide by zero on collapsed rings; no vendor error contract exists.
ConvolutionPolygon convolutionFloatingMinkowski(const ConvolutionRing &,const ConvolutionRing &,const ConvolutionHierarchyDiagnostic & = {}); // 7f38d0
ConvolutionPolygon convolutionFloatingMinkowski(const ConvolutionTopology &,const ConvolutionTopology &,const ConvolutionHierarchyDiagnostic & = {}); // 853ea0
struct ConvolutionFloatingOffsetResult {
    ConvolutionPolygon rings;
    std::vector<int32_t> roots;
};
ConvolutionFloatingOffsetResult convolutionFloatingOffset(
    const ConvolutionPolygon &rings, double radius, int32_t resolution,
    const ConvolutionHierarchyDiagnostic &diagnostic = {}); // 7f1880
// The original bounds reader requires a nonempty first ring in the first
// operand. Invalid input is rejected before entering that dereference.
std::array<ConvolutionPolygon, 2> convolutionFloatingOffsetPair(
    const ConvolutionPolygon &first, const ConvolutionPolygon &second,
    double radius, int32_t resolution,
    const ConvolutionHierarchyDiagnostic &diagnostic = {}); // 7f1bb0
ConvolutionPolygon convolutionFloatingAlternatingOffset(
    const ConvolutionMultiPolygon &,
    const ConvolutionHierarchyDiagnostic &diagnostic = {}); // 7f2100
}
