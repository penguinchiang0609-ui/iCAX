#pragma once
#include "ordinary_multi_continuation.hpp"

namespace study {
// Exact SDK stable-ID join domain recovered from 674f40. Retained mappings
// contain every old placed SDK ID exactly once in its NEW group/ordinal;
// additions may explicitly resubmit old unplaced IDs. Old missing demand is
// otherwise discarded. Group indices and quantities may change accordingly.
std::unique_ptr<OrdinaryMultiPredefinedScene> ordinaryMultiNestMoreMapped(
    const OrdinaryMultiPredefinedScene &original,const Input &joinedInput,
    std::span<const OrdinaryMultiAddedInstance> retainedOriginal,
    std::span<const OrdinaryMultiAddedInstance> additions,
    std::span<const OrdinaryMultiPredefinedSheet> newStocks,
    const ReverseRunnerFactory &,Budget &,OrdinaryMultiContinuationTrace * = nullptr);
}
