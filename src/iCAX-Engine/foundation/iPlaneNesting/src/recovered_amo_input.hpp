#pragma once
#include "recovered_amo_shape.hpp"
namespace study::recovered {
// 8bac00. The distance is already the component's half inter-part spacing.
std::vector<Point> amoTilingOffsetInputRing(std::span<const Point>,double halfDistance,bool field58);
// 491640/8b9710/8bb590. Preserves the source ring for an intersection result,
// records its first crossing, and accumulates bounds only on the accepted path.
AmoTilingTransformedShape amoTilingCreateInputShape(std::span<const Point>,double halfDistance,bool field58);
}
