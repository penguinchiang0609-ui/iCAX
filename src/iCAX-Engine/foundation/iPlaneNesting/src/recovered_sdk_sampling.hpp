#pragma once
#include "recovered_rich_conversion.hpp"

namespace study::recovered {
// 8a4940, line-pair branch. The point is the topology vertex between the
// evaluated segments; the endpoints can differ from it within curve tolerance.
bool sdkMergeableLines(const std::array<ShakerPoint, 2> &previous,
                       const std::array<ShakerPoint, 2> &current,
                       ShakerPoint point, double tolerance);
// 8a4d00 / 8a4940 / 8a45d0, on the line-only result of arc sampling.
void sdkCleanSampledCurve(RichGraph &, RichNode &, double tolerance);
// 6584e0, default P_CHECK_GEOMETRY=2, strict per-axis .001 threshold.
void sdkRemoveDuplicatePoints(std::vector<ShakerPoint> &);
// 53ac60 default precision, rich line/circle discretization and point cleanup.
// The result is in source coordinates; the API retains its pivot separately.
std::vector<ShakerPoint> sdkSampleContour(const RichNode &, bool exterior,
                                         double precision = .0001);
}
