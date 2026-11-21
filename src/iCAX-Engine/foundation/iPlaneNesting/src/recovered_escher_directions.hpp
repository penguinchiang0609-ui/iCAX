#pragma once
#include "recovered_escher_candidate.hpp"
namespace study::recovered {
void escherSortPointDirections(std::vector<EscherIntegerPoint> &,EscherIntegerPoint,size_t depth=SIZE_MAX); // 7960b0 family
std::vector<EscherIntegerPoint> escherPolygonDirections(const ConvolutionIntegerPolygon &,EscherIntegerPoint,uint64_t,double); // 7908e0
std::vector<EscherIntegerPoint> escherDirectionRange(EscherIntegerPoint,int32_t,int32_t,int32_t); // 77a930
std::vector<EscherIntegerPoint> escherPrimaryDirections(const EscherNfpBoundary &,int32_t,int32_t,int32_t,int32_t,double); // 77b350
void escherPeriodEndpointDirections(const EscherNfpBoundary &,std::vector<EscherIntegerPoint> &); // 77b100
struct EscherDirectionRowOptions {
    bool restricted=false;int32_t stepDegrees=90,density=1;
    double maximumLength=0,areaFactor=1;
    std::vector<std::array<int32_t,2>> worldRelativeRanges;
};
void escherDirectionRows(std::span<const EscherSupportTable>,const EscherNfpBoundary &,const ConvolutionEdgeIndex &,
    std::span<const EscherIntegerPoint>,std::span<const std::array<int32_t,2>>,const EscherDirectionRowOptions &,
    EscherCandidateLists &,int64_t &,const EscherSupportTable *aggregateSupport=nullptr); // 77b8c0
}
