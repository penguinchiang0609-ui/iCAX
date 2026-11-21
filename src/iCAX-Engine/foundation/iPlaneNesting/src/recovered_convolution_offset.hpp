#pragma once
#include "recovered_convolution_index.hpp"
namespace study::recovered {
ConvolutionIntegerPoint convolutionOffsetClosestPoint(ConvolutionIntegerPoint,const ConvolutionIntegerSegment &,bool); // 81a620
double convolutionOffsetDistance(ConvolutionIntegerPoint,const ConvolutionIntegerPolygon &); // 81aaf0
ConvolutionIntegerBox convolutionOffsetBounds(const ConvolutionHierarchy &); // 827b40
std::vector<ConvolutionIntegerSegment> convolutionOffsetArc(ConvolutionIntegerPoint,ConvolutionIntegerPoint,ConvolutionIntegerPoint,int32_t,int64_t); // 8540e0
std::vector<ConvolutionIntegerSegment> convolutionOffsetEdges(std::span<const ConvolutionIntegerPoint>,int32_t,int64_t,const ConvolutionHierarchyDiagnostic & = {}); // 8543f0
ConvolutionIntegerPolygon convolutionOffsetFilter(const ConvolutionHierarchy &,const ConvolutionHierarchy &,const ConvolutionIntegerPolygon &); // 850de0
ConvolutionIntegerRing convolutionIntegerOffsetCircle(int64_t radius, int32_t pointCount); // 851dd0
ConvolutionHierarchyFlattened convolutionIntegerOffset(
    const ConvolutionIntegerPolygon &rings, int64_t radius, int32_t resolution,
    const ConvolutionHierarchyDiagnostic &diagnostic = {}); // 851f20
// 802da0, with Boost.Polygon supplying the integer polygon-set operations.
// The first ring chooses the outer winding for each input polygon group.
void convolutionMergeIntegerOffsets(ConvolutionIntegerMultiPolygon &,
                                    const ConvolutionHierarchyDiagnostic &diagnostic = {});
}
