#pragma once
#include "recovered_fuzzy_polygon.hpp"
namespace study::recovered {
struct FuzzyWeightedSegment {
    ConvolutionIntegerSegment segment{};
    double weight=0.;
    bool operator==(const FuzzyWeightedSegment &)const=default;
};
struct FuzzySegmentReference {
    int32_t index=0;
    bool aligned=true;
    bool operator==(const FuzzySegmentReference &)const=default;
};
struct FuzzyMergeInput {
    std::vector<ConvolutionIntegerSegment> segments;
    std::vector<double> weights;
    std::vector<FuzzySegmentReference> sources;
};
struct FuzzyMergeOutput {
    std::vector<FuzzyWeightedSegment> segments;
    std::vector<std::vector<FuzzySegmentReference>> sources;
};
struct FuzzyWeightedBuild {
    std::vector<FuzzyWeightedSegment> segments;
    // Original 829950 ordered recursive expansion; duplicate polygon ids remain.
    std::vector<std::vector<int32_t>> sources;
};
void fuzzyNormalizeSegmentWeights(std::vector<ConvolutionIntegerSegment> &,
    std::vector<double> &); //83d590
FuzzyMergeInput fuzzyPrepareMerge(std::vector<ConvolutionIntegerSegment>,
    std::vector<double>); //82b530, separately recovered 7ae4c0 splitter
FuzzyMergeOutput fuzzyMergeSegments(const FuzzyMergeInput &); //82bf70
FuzzyWeightedBuild fuzzyBuildWeightedSegments(const ConvolutionIntegerPolygon &,
    std::span<const double>); //82d3d0 ->82c9a0
struct FuzzyGeometryRange {
    std::shared_ptr<const std::vector<ConvolutionIntegerSegment>> segments;
    int32_t begin=0,end=0;
};
using FuzzyRegionGeometry=std::vector<FuzzyGeometryRange>;
struct FuzzyTopologyRegion {
    FuzzyRegionGeometry geometry;
    double weight=0.;
    EscherIntegerPoint summit{};
    bool relativePresent=false;
    int32_t relativeRegion=-1;
    bool relativeDirect=false;
    ConvolutionIntegerSegment relativeSegment{};
};
struct FuzzyTopologySegmentIndex {
    int32_t path=-1,position=-1;
    bool forward=false;
};
struct FuzzyTopology {
    std::vector<FuzzyTopologyRegion> regions;
    std::vector<std::shared_ptr<const std::vector<ConvolutionIntegerSegment>>> paths;
    //842a90 lookup: forward says the requested edge was found in its original
    //direction; reverse-only lookup reports false and preserves path/offset.
    std::map<ConvolutionIntegerSegment,FuzzyTopologySegmentIndex> segmentIndex;
    std::map<EscherIntegerPoint,std::vector<ConvolutionIntegerSegment>> incidentEdges;
};
std::vector<ConvolutionIntegerSegment> fuzzyRegionBoundary(
    std::span<const FuzzyGeometryRange>,std::span<const FuzzyRegionGeometry>);
//83c600: gather range segment starts, 83bc60 closes each path, directed set
//difference removes immediate-child edges without changing remaining hash order.
}
