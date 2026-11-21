#pragma once
#include "recovered_escher_maximum_runtime.hpp"
namespace study::recovered {
bool escherBoundaryPointContains(const ConvolutionIntegerPolygon &,EscherIntegerPoint); //81a9d0
EscherIntegerPoint escherBoundaryScaleVector(EscherIntegerPoint,double); //7798a0
ConvolutionIntegerPolygon escherBoundaryMapBasisTransform(const ConvolutionIntegerPolygon &,
    EscherIntegerPoint first,EscherIntegerPoint second); //7a99e0
void escherBoundaryMapBasisRestore(ConvolutionIntegerPolygon &,EscherIntegerPoint first,
    EscherIntegerPoint second); //81a440
std::vector<ConvolutionIntegerSegment> escherBoundaryGridLines(int32_t first,int32_t last,
    int64_t period,int64_t lower,int64_t upper); //7a31a0
std::vector<ConvolutionIntegerSegment> escherBoundaryGridSplit(std::span<const ConvolutionIntegerSegment>,
    std::span<const ConvolutionIntegerSegment> cutters); //7a33c0
std::vector<ConvolutionIntegerSegment> escherBoundaryCancelEdges(std::span<const ConvolutionIntegerSegment>); //7a8410
struct EscherBoundaryCellGroups {
    std::vector<std::array<int32_t,2>> cells;
    std::vector<std::vector<ConvolutionIntegerSegment>> segments;
    bool operator==(const EscherBoundaryCellGroups &)const=default;
};
EscherBoundaryCellGroups escherBoundaryCollectCells(std::span<const ConvolutionIntegerSegment>,int64_t width,int64_t height); //7a3cf0
std::vector<EscherPatternRecord> escherExpandBoundaryCoefficients(std::span<const EscherPatternRecord>,
    EscherIntegerPoint horizontal,EscherIntegerPoint step,std::span<const std::vector<int32_t>> horizontalCoefficients,
    std::span<const std::vector<int32_t>> verticalCoefficients,EscherIntegerPoint origin); //78fcc0
EscherPatternLayout escherBoundaryLayoutFromCoefficients(std::span<const EscherPatternRecord>,
    const EscherPeriodCandidate &,std::span<const std::vector<int32_t>> horizontalCoefficients,
    std::span<const std::vector<int32_t>> verticalCoefficients,EscherIntegerPoint origin,
    std::span<const EscherRecordGeometry>); //78ffa0
struct EscherWeightedBoundaryRegion {ConvolutionIntegerPolygon polygon;double weight=1.;};
struct EscherBoundarySampleSelection {
    EscherIntegerPoint origin{};
    std::vector<int32_t> indices;
    double weight=0.;
    bool operator==(const EscherBoundarySampleSelection &)const=default;
};
EscherBoundarySampleSelection escherSampleBoundaryRegions(EscherIntegerPoint horizontal,EscherIntegerPoint step,
    int32_t attempts,std::span<const EscherWeightedBoundaryRegion>); //7ab330
}
