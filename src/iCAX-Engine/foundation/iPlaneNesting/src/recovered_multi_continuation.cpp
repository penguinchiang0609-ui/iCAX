#include "recovered_multi_continuation.hpp"
#include <set>

namespace study::recovered {
MultiContinuationDemand multiContinuationDemand(std::span<const uintptr_t> additions,
    std::span<const std::vector<uintptr_t>> previousPlaced){
    MultiContinuationDemand result;
    std::set<uintptr_t> seen;
    const auto append=[&](uintptr_t identity){
        if(seen.insert(identity).second)result.identities.push_back(identity);
        else ++result.repeated;
    };
    for(const auto identity:additions)append(identity);
    for(const auto &board:previousPlaced)for(const auto identity:board)append(identity);
    return result;
}
}
