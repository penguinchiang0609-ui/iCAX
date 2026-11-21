#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace study::recovered {
struct MultiPredefinedSlice {
    uintptr_t sheet=0;
    int32_t count=0,first=0;
    int64_t partByteOffset=0,orientationByteOffset=0,positionByteOffset=0;
    bool operator==(const MultiPredefinedSlice &) const = default;
};
// 5b9630: each input sheet receives its own owner, including a zero-count
// sheet. SDK arrays remain flat; offsets use the original wrapped int32 sum.
std::vector<MultiPredefinedSlice> multiPredefinedSlices(std::span<const uintptr_t>,
    std::span<const int32_t> counts);
// 5b9aa0 -> complete 5ba290: global demand is a set of concrete SDK IDs.
// Per-sheet manual records themselves are not deduplicated by this producer.
std::vector<uintptr_t> multiPredefinedUniqueDemand(std::span<const uintptr_t>);
struct MultiPredefinedControllerInput {
    std::vector<uintptr_t> sheets,parts;
    std::vector<std::vector<uintptr_t>> groups;
};
struct MultiPredefinedControllerHost {
    // One real global problem must own all stock, concrete instances and
    // predefined owners before any group is submitted. A single-board packing
    // callback cannot satisfy this provider contract.
    std::function<void(std::span<const uintptr_t> sheets,std::span<const int32_t> quantities,
        std::span<const uintptr_t> uniqueParts,std::span<const uintptr_t> flattenedOwners)> buildGlobal;
    std::function<void(double seconds,int32_t preset,bool flag)> begin;
    std::function<void(std::span<const uintptr_t> owners,size_t labelIndex)> submit;
    std::function<void()> finish;
};
// Control recovered from 5b9bd0. Original producer/global model construction
// and per-group 5c0270 execution remain explicit host boundaries.
bool runMultiPredefinedController(const MultiPredefinedControllerInput &,
    MultiPredefinedControllerHost &);
}
