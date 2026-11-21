#pragma once
#include "recovered_multiheads.hpp"

namespace study::recovered {
struct MultiheadsInferPlacement {
    int64_t x48=0,moduleD0=0;
    int32_t y50=0;
    bool hasNonemptyGroup58=false;
};
// Original 3f6990 and its insertion/partition/heap helpers. Equal-height swaps
// are retained because the selected members and their leader depend on order.
std::vector<size_t> multiheadsInferOrder(std::span<const int32_t> heights,
    std::optional<size_t> initialDepth=std::nullopt);
// 3b9de0 -> 3f63a0. Infer sisters after imported Rectangle/multi-sheet layouts;
// this path is independent of whether manager 9 appears in the chosen preset.
// Results contain indexes into the input, in original group/member order.
std::vector<std::vector<size_t>> multiheadsInferGroups(
    std::span<const MultiheadsInferPlacement>,const MultiheadsHardware &);
}
