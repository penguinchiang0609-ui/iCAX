#pragma once
#include "recovered_convolution_filter.hpp"
namespace study::recovered {
ConvolutionHierarchy convolutionMinimalHierarchy(const ConvolutionHierarchy &); // 827610
bool convolutionHierarchyIsMinimal(const ConvolutionHierarchy &); // 84f6d0
ConvolutionIntegerPoint convolutionMinkowskiDimensions(std::span<const ConvolutionIntegerPoint>); // 855520
bool convolutionMinkowskiHoleFit(std::span<const ConvolutionIntegerPoint>,std::span<const ConvolutionIntegerPoint>); // 855600
struct ConvolutionMinkowskiOption {int32_t arcResolution=0;bool offset=false;}; // packed original low int32 + presence byte
std::vector<ConvolutionIntegerSegment> convolutionMinkowskiEdges(const ConvolutionHierarchy &,const ConvolutionHierarchy &,const ConvolutionHierarchyDiagnostic & = {}); // 8501f0/84fec0
ConvolutionHierarchy convolutionMinkowskiHierarchy(const ConvolutionHierarchy &,const ConvolutionHierarchy &,const ConvolutionHierarchyDiagnostic & = {}); // 850f70 ordinary path
ConvolutionHierarchyFlattened convolutionIntegerMinkowski(const ConvolutionIntegerPolygon &,const ConvolutionIntegerPolygon &,const ConvolutionHierarchyDiagnostic & = {}); // 84fb60 ordinary path
std::vector<ConvolutionIntegerSegment> convolutionMinkowskiEdges(const ConvolutionHierarchy &,const ConvolutionHierarchy &,ConvolutionMinkowskiOption,ConvolutionMinkowskiOption,const ConvolutionHierarchyDiagnostic & = {});
ConvolutionHierarchy convolutionMinkowskiHierarchy(const ConvolutionHierarchy &,const ConvolutionHierarchy &,ConvolutionMinkowskiOption,ConvolutionMinkowskiOption,const ConvolutionHierarchyDiagnostic & = {});
ConvolutionHierarchyFlattened convolutionIntegerMinkowski(const ConvolutionIntegerPolygon &,const ConvolutionIntegerPolygon &,ConvolutionMinkowskiOption,ConvolutionMinkowskiOption,const ConvolutionHierarchyDiagnostic & = {});
}
