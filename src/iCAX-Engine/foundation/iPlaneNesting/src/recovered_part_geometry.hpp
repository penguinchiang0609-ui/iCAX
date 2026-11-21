#pragma once
#include "core.hpp"
#include <span>
namespace study::recovered {
//51d9a0: exact point equality; keeps the first point bits, removes adjacent
//repetitions and a final repeated first point. No tolerance or grid snapping.
Polygon partRemoveAdjacentPoints(std::span<const Point>);
//5213f0: changes between NONZERO turn signs around the closed ring. This is
//not the number of concave vertices; every ring with fewer than4 points is0.
int32_t partRingTurnChanges(std::span<const Point>);
struct PartGeometryStatistics {
    uint64_t points=0,weightedTurnChanges=0;
    int32_t turnChanges=0,exteriors=0,holes=0;
};
//5301e0→530eb0 and5316a0 inputs from actual prepared polygon rings.
PartGeometryStatistics partGeometryStatistics(std::span<const Polygon>);
bool partGeometryTooComplex(const PartGeometryStatistics &,double maximumComplexity=50000.);
//51df30 copies when ring count<=1, regardless of its cleanliness query.
//51ed40 then filters ORIGINAL pointCount<=2, and cleans each retained ring.
//This is the exact single-ring normalization domain; multiple rings require
//the upstream juxtaposition detector/Boolean normalization and are rejected.
std::vector<Polygon> partNormalizeSingleRing(std::span<const Polygon>);
}
