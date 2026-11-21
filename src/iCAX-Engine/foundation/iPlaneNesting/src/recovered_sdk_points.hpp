#pragma once
#include "recovered_rich_conversion.hpp"
#include <stdexcept>

namespace study::recovered {
class SdkPointError : public std::invalid_argument {
public:
    int32_t code;
    std::optional<ShakerPoint> intersection;
    SdkPointError(int32_t code, std::optional<ShakerPoint> intersection = {});
};
struct SdkLinearSegment {
    ShakerPoint origin{}, direction{};
    std::array<double, 2> parameters{};
    std::array<ShakerPoint, 2> endpoints{};
};
struct SdkPointIntersection {
    ShakerPoint point{};
    size_t first = 0, second = 0;
    std::array<double, 2> parameters{};
};
struct SdkPointLoop {
    std::vector<SdkLinearSegment> segments;
    std::vector<ShakerPoint> points;
    bool closed = false;
};
bool sdkPointsWithinLimits(std::span<const ShakerPoint>); // 658180
double sdkPointsSignedArea(std::span<const ShakerPoint>); // 520430
void sdkOrientPoints(std::vector<ShakerPoint> &, bool positive); // 5204a0
// b7a610 / b7d9e0 / b78030, for the line-only point normalization pipeline.
std::vector<SdkPointIntersection> sdkLinePairIntersections(const SdkLinearSegment &, const SdkLinearSegment &,
    std::array<bool, 4> excludedEndpoints = {});
// 538090 / b8e1e0. Preserves query-before-insert, R-tree and edge-pair order.
std::vector<SdkPointIntersection> sdkPointSelfIntersections(std::span<const ShakerPoint>);
std::vector<SdkPointLoop> sdkSplitPointLoops(std::span<const ShakerPoint>); // b8eb80
std::optional<size_t> sdkSelectDominantPointLoop(std::span<const double> signedAreas, double areaRatio); // 538480
bool sdkBooleanRepairPointSelfIntersections(std::vector<ShakerPoint> &); // 5381e0
// 5387e0 / 5381e0 / 538480. The actual Boolean union is attempted first;
// recursive intersection splitting and unique dominant-loop selection follow.
bool sdkRepairPointSelfIntersections(std::vector<ShakerPoint> &, double areaRatio = .01);
// 658620. Mutates the actual contour; original rejection codes are -996/-979.
void sdkNormalizePoints(std::vector<ShakerPoint> &, bool exterior, double geometryMode = 2., double repairAreaRatio = .01);
}
