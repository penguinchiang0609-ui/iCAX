#pragma once
#include "recovered_multi.hpp"

namespace study::recovered {
// 4f9bf0. The random selections refer to the initial layout. Ratio candidates
// exclude the final board; the final unique removal set is traversed backwards.
std::vector<size_t> multiLayoutRemovalIndices(std::span<const MultiLnsSheet>, bool includeLast,
    int32_t lowestRatioCount, int32_t randomCount, const std::function<size_t(size_t)> &randomIndex);
uint32_t multiModifyLayout(MultiLnsHost &, bool includeLast, int32_t lowestRatioCount, int32_t randomCount);
struct MultiColumnCompletionHost {
    bool hasTechnology = false, preserveLast = false;
    std::function<bool()> stopped;
    std::function<double()> unit;
    std::function<int32_t(int32_t, int32_t)> integer;
    std::function<void(bool, int32_t, int32_t)> modify;
    // OneByOne runs against the current demand/stock after each modification.
    std::function<int32_t()> complete;
};
struct MultiColumnCompletionResult {
    bool modifiedFirst = false, modifiedSecond = false;
    int32_t addedFirst = 0, addedSecond = 0;
};
// 4b63a0 after rounding/reuse: .75 initial destruction gate, then a second
// ordinary destruction/rebuild when the first completion adds fewer than 2.
MultiColumnCompletionResult multiCompleteColumns(MultiColumnCompletionHost &);
}
