#pragma once
#include "recovered_offset.hpp"
#include "recovered_placement.hpp"
#include <span>

namespace study::recovered {
struct ContextPartMeasure {
    double width = 0, height = 0, area = 0;
    int32_t required = 0;
    bool optional = false;
};
struct OrdinaryContextMeasures {
    double width218 = 1, height220 = 1, area228 = 1;
    int32_t batch238 = 0, multiplicity23c = 0;
};
double moduleAverageWidth(std::span<const ContextPartMeasure>, int32_t population); // 36af90
// Scalar producers in 54c980, separate from the variant averages used by 550550.
OrdinaryContextMeasures ordinaryContextMeasures(std::span<const ContextPartMeasure>,
    int32_t population, double totalArea, double sheetWidth, double sheetHeight);
struct ShapeAxisSymmetry {
    bool horizontal = true, vertical = true;
    std::array<double, 2> maximumDistance{};
};
// 521e80. Original contour vertices and edge midpoints are reflected about
// the supplied geometry bounds. The nearest-boundary query uses all rings.
// The original calls this with the fixed 0.010038 tolerance at 38320c.
ShapeAxisSymmetry shapeAxisSymmetry(std::span<const Polygon>, Box,
                                  double tolerance = 0.010038);
// 382d90 -> 373b20 -> 551960. Constructor symmetry reduction precedes
// insertion into the context's angle-keyed per-part module cache.
std::vector<PlacementOrientation> partOrientationCache(
    std::span<const SdkOrientation>, bool simplifySameSymmetry,
    bool horizontalSymmetry, bool verticalSymmetry);
// 550210. Quantities do not multiply cache population; arithmetic is 32-bit.
int32_t orientationCachePopulation(std::span<const size_t> perPartCacheSizes);
}
