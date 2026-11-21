#pragma once
#include "recovered_escher_intersections.hpp"
#include "recovered_escher_candidate_sort.hpp"
namespace study::recovered {
struct EscherCandidateBuildOptions {
    EscherDirectionRowOptions rows;
    int32_t primaryBeginDegrees=0,primaryEndDegrees=360,primaryDenominator=1;
    uint64_t vertexLimit=0;
    double vertexAngleThreshold=0,areaFilterFraction=1,discardFraction=0;
    bool adaptivePeriod=false;
    std::vector<std::array<int32_t,2>> vertexRelativeRanges;
};
EscherCandidateBuildOptions escherDefaultCandidateBuildOptions(int32_t quantizedRestriction,bool adaptivePeriod=false); //779ab0
void escherSortPeriodicCandidatesByArea(std::vector<EscherPeriodCandidate> &); // 77c6e0
size_t escherPrunePeriodicCandidatesByArea(std::vector<EscherPeriodCandidate> &,double); //77fe70
void escherAppendSwappedCatalogs(EscherCandidateLists &,std::span<const EscherSupportTable>); //77d730
void escherReducePeriodicCandidateDirections(std::vector<EscherPeriodCandidate> &,double tolerance=0.000005); //77ce20
// nfpRings is the actual self-NFP of the prepared member pattern, not the part
// polygon or its box. All sampling options are supplied by the descriptor owner.
EscherCandidateLists escherBuildPeriodicCandidates(std::span<const EscherSupportTable>,const ConvolutionIntegerPolygon &nfpRings,
    const EscherCandidateBuildOptions &,int64_t &minimumCellArea); //77db70
}
