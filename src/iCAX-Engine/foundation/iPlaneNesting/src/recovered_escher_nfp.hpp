#pragma once
#include "recovered_convolution_index.hpp"
namespace study::recovered {
struct EscherAngularKey {double cosine=0;int64_t hemisphere=0;bool operator==(const EscherAngularKey &)const=default;};
struct EscherAngularEdge {ConvolutionIntegerSegment segment{};EscherAngularKey first,last;bool operator==(const EscherAngularEdge &)const=default;};
static_assert(sizeof(EscherAngularEdge)==0x40);
struct EscherNfpBoundary {
    double tolerance=.001;ConvolutionIntegerPoint reference{};
    std::vector<EscherAngularEdge> edges;std::vector<EscherAngularKey> prefixMaximum;
};
int escherCompareAngularKeys(EscherAngularKey,EscherAngularKey); // 81b4b0
void escherSortAngularEdges(std::vector<EscherAngularEdge> &,size_t depth=SIZE_MAX); // 81c970 family
EscherNfpBoundary escherBuildNfpBoundary(ConvolutionIntegerPoint,const ConvolutionIntegerPolygon &,double tolerance=.001,
    double minimumAngle=-3.141592653589793,double maximumAngle=3.141592653589793); // 81b570 / 81b600
size_t escherNfpRayLowerBound(const EscherNfpBoundary &,EscherAngularKey); // 81bf80
std::vector<ConvolutionHierarchyRayHit> escherNfpRayIntersections(const EscherNfpBoundary &,ConvolutionIntegerPoint); // 81c350
void escherPrepareNfpRayHits(ConvolutionIntegerPoint,ConvolutionIntegerPoint,size_t,int32_t,std::vector<ConvolutionHierarchyRayHit> &,bool); // 81e340
bool escherSearchNfpPeriod(const EscherNfpBoundary &,const ConvolutionEdgeIndex &,ConvolutionIntegerPoint &,ConvolutionIntegerPoint,
    std::vector<ConvolutionHierarchyRayHit> &,int32_t); // 779c70
using EscherPeriodInterval=std::array<double,2>;
std::vector<EscherPeriodInterval> escherNfpPeriodIntervals(std::span<const ConvolutionHierarchyRayHit>); // 779fe0; descending ray hits
double escherNfpPeriodMinimum(std::span<const EscherPeriodInterval>); // 77a550 / 780f40 / 785c70
ConvolutionIntegerPoint escherNfpPeriodEndpoint(std::vector<ConvolutionHierarchyRayHit> &); // 77a3c0
}
