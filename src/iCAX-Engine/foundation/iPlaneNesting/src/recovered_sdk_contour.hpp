#pragma once
#include "recovered_rich_conversion.hpp"

namespace study::recovered {
enum class SdkContourKind { Points, Bulges, Sagittas, Centers };
struct SdkContourInput {
    std::span<const ShakerPoint> points;
    std::span<const double> values;
    std::span<const ShakerPoint> centers;
    std::span<const int32_t> directions;
    SdkContourKind kind = SdkContourKind::Points;
    double geometryMode = 2.;
    bool positive = true;
};
struct RichConstructedSegment {
    std::optional<RichCircle> circle;
    std::array<double, 2> parameters{};
    std::array<ShakerPoint, 2> endpoints{};
};
// b7a0b0, including the closed-circle and original straight fallback branches.
RichConstructedSegment richThreePointSegment(ShakerPoint, ShakerPoint, ShakerPoint);
// b82fd0 / b82530. Positive rich sagitta is left of the directed chord;
// the SDK's public sagitta and bulge adders negate it.
RichNode *richAppendSagittaArc(RichGraph &, RichNode &, ShakerPoint, double sagitta);
// 6593e0 / 654d00 / 659670 / 659720. Returns the graph-owned rich curve.
// Empty inputs and contours below the SDK's absolute-area threshold are invalid.
RichNode &sdkBuildContour(RichGraph &, const SdkContourInput &);
}
