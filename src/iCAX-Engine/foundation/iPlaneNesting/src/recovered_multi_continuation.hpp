#pragma once
#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>

namespace study::recovered {
struct MultiContinuationDemand {
    std::vector<uintptr_t> identities;
    size_t repeated=0;
};
// Complete 674f40 copies this call's demand, appends only each previous
// sheet-result's actual placed records, then retains the first occurrence of
// every SDK ID. Prior unplaced input is not a source. The caller may explicitly
// submit such an ID again in additions.
MultiContinuationDemand multiContinuationDemand(std::span<const uintptr_t> additions,
    std::span<const std::vector<uintptr_t>> previousPlaced);
}
