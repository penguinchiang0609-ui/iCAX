#pragma once
#include "recovered_escher_prepared.hpp"
namespace study::recovered {
EscherIntegerPoint escherPairPolygonCenter(const ConvolutionIntegerPolygon &); //7907d0
EscherIntegerPoint escherPairCentralPoint(const ConvolutionIntegerPolygon &); //792800
std::vector<EscherIntegerPoint> escherPairBoxPoints(ConvolutionIntegerPolygon,EscherIntegerBox); //790ca0
std::vector<EscherIntegerPoint> escherPairEdgePoints(const ConvolutionIntegerPolygon &,EscherIntegerPoint); //791c60
std::vector<std::vector<EscherIntegerPoint>> escherPairSpatialClusters(std::span<const EscherIntegerPoint>,size_t); //829290
void escherPairFilterPoints(const ConvolutionIntegerPolygon &,std::vector<EscherIntegerPoint> &); //794820
std::vector<EscherIntegerPoint> escherPairSpanPoints(const ConvolutionIntegerPolygon &,EscherIntegerPoint); //792000
struct EscherPairScoredPoint {EscherIntegerPoint point{};int64_t score=0;bool operator==(const EscherPairScoredPoint &)const=default;};
void escherSortPairPointScores(std::vector<EscherPairScoredPoint> &,size_t depth=SIZE_MAX); //796230
struct EscherPairPointProximity {size_t first=0,second=0;double distance=0;bool operator==(const EscherPairPointProximity &)const=default;};
void escherSortPairProximities(std::vector<EscherPairPointProximity> &,size_t depth=SIZE_MAX); //797930
void escherLimitPairPoints(std::vector<EscherIntegerPoint> &,size_t,
    const std::function<bool(EscherIntegerPoint,EscherIntegerPoint)> &); //7925f0
std::vector<EscherIntegerPoint> escherPairContactPoints(const ConvolutionIntegerPolygon &firstPosedOuter,
    const ConvolutionIntegerPolygon &secondPosedOuter,const ConvolutionIntegerPolygon &pairNfp); //793e80 after normal outer cache acquisition and poses
EscherIntegerPoint escherPairSpecialPoint(const EscherIntegerShapeRecord &); //792ce0
}
