#pragma once
#include "recovered_escher_normalization.hpp"

namespace study::recovered {
ConvolutionIntegerRing escherIntegerCornerChamfer(ConvolutionIntegerPoint previous,
    ConvolutionIntegerPoint point, ConvolutionIntegerPoint next); // 83aab0
void escherRemoveSharedIntegerPoint(ConvolutionIntegerRing &, ConvolutionIntegerPoint); // 83a900
bool escherIntegerRingHasRepeatedPoint(const ConvolutionIntegerRing &); // 83a820
// 83a380 processes the first shared vertex in the original hash iteration order.
// Every ring containing that vertex receives the original 32-unit corner repair.
bool escherResolveSharedIntegerVertex(std::span<ConvolutionIntegerRing *>);
}
