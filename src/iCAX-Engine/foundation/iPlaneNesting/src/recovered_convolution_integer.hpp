#pragma once
#include "recovered_convolution_polygon.hpp"
namespace study::recovered {
uint64_t convolutionIntegerGcd(ConvolutionIntegerPoint); // 7ffbf0
void convolutionNormalizeIntegerRing(ConvolutionIntegerRing &); // 819400
size_t convolutionRemoveEmptyIntegerRings(ConvolutionIntegerPolygon &); // 81b0f0
int convolutionIntegerOrientation(std::span<const ConvolutionIntegerPoint>); // 819db0, at least two distinct points
ConvolutionIntegerRing convolutionIntegerDirections(std::span<const ConvolutionIntegerPoint>); // 819f80, no zero edges
using ConvolutionIntegerSegment=std::array<ConvolutionIntegerPoint,2>;
std::vector<ConvolutionIntegerSegment> convolutionIntegerConvolveEdges(std::span<const ConvolutionIntegerPoint>,std::span<const ConvolutionIntegerPoint>,std::span<const ConvolutionIntegerPoint>,std::span<const ConvolutionIntegerPoint>); // 856590
std::vector<ConvolutionIntegerSegment> convolutionIntegerRingEdges(std::span<const ConvolutionIntegerPoint>,std::span<const ConvolutionIntegerPoint>); // 855240 ordinary path
void convolutionSortIntegerSegments(std::vector<ConvolutionIntegerSegment> &); // 853fb0, lexicographic sort and exact unique
struct ConvolutionIntegerGraph {std::vector<size_t> offsets{0},targets;};
struct ConvolutionIntegerComponents {std::vector<int32_t> labels,discovery,colors;std::vector<size_t> roots;uint32_t count=0;};
ConvolutionIntegerGraph convolutionIntegerGraph(size_t vertices,std::span<const std::array<int32_t,2>>); // 8585a0/858bf0
ConvolutionIntegerComponents convolutionIntegerComponents(const ConvolutionIntegerGraph &); // 858fb0/859200
void convolutionFilterIntegerCycles(std::vector<ConvolutionIntegerSegment> &); // 857b40
bool convolutionIntegerDirectionLess(ConvolutionIntegerPoint,ConvolutionIntegerPoint); // 7b7f60
bool convolutionIntegerPointBetween(ConvolutionIntegerPoint,ConvolutionIntegerPoint,ConvolutionIntegerPoint); // 7ba110, strict lexicographic interval
int convolutionIntegerPointSide(ConvolutionIntegerPoint,const ConvolutionIntegerSegment &); // 7b9f70
bool convolutionIntegerSegmentsIntersect(const ConvolutionIntegerSegment &,const ConvolutionIntegerSegment &); // 7b99b0, split predicate (excludes identical segments and ordinary shared endpoints)
bool convolutionIntegerPointInSegmentCell(ConvolutionIntegerPoint,const ConvolutionIntegerSegment &); // 7b8060, original unit-cell membership
bool convolutionIntegerApproximateIntersection(ConvolutionIntegerPoint &,const ConvolutionIntegerSegment &,const ConvolutionIntegerSegment &); // 7b95a0, leaves output unchanged on false
bool convolutionIntegerExactIntersection(ConvolutionIntegerPoint &,const ConvolutionIntegerSegment &,const ConvolutionIntegerSegment &); // 7b8cb0, exact rational fallback
struct ConvolutionIntegerSplitSegment {int64_t source=0;ConvolutionIntegerSegment segment;bool operator==(const ConvolutionIntegerSplitSegment &)const=default;};
std::vector<ConvolutionIntegerSplitSegment> convolutionSplitIntegerSegments(std::span<const ConvolutionIntegerSegment>); // 7ae4c0/7af1d0/7b1010/7b4c60/7b1610
std::vector<ConvolutionIntegerSegment> convolutionSplitDirectedIntegerSegments(std::span<const ConvolutionIntegerSegment>); // 8573c0, original wrapped dot direction
uint64_t convolutionIntegerEdgeHash(const ConvolutionIntegerSegment &); // 7b3970
std::vector<ConvolutionIntegerSegment> convolutionIntegerEdgeOrder(std::span<const ConvolutionIntegerSegment>); // 7aeaa0/7affc0/7b4380, original hash iteration order, unique edges
ConvolutionIntegerPolygon convolutionTraceIntegerSegments(std::span<const ConvolutionIntegerSegment>); // 855870, unique edges with count 1; no zero edges
ConvolutionIntegerPolygon convolutionIntegerConvolutionContours(std::span<const ConvolutionIntegerPoint>,std::span<const ConvolutionIntegerPoint>); // ordinary ring chain through normalized contours; before Minkowski hierarchy/filtering
}
