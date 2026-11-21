#pragma once
#include "ordinary_multi_predefined.hpp"

namespace study {
struct OrdinaryMultiAddedInstance {size_t group=0,ordinal=0;uintptr_t sdkIdentity=0;};
struct OrdinaryMultiContinuationTrace {
    size_t globalCatalogBuilds=0,clonedOriginal=0,originalBoards=0,newBoards=0;
    size_t boardAttempts=0,added=0,rejectedGeometry=0;
};
// Continues the original live global scene into another independently owned
// scene. Existing boards/instances are cloned through recovered native commit;
// the supplied real runner fills their residual stock, then the supplied new
// physical stock occurrences. Geometry/group/ordinal definitions of old demand
// must remain stable, and this initial domain requires old demand complete.
// The caller supplies its actual continuation strategy; there is no algorithm
// alias or invented default multi-sheet strategy in this entry.
std::unique_ptr<OrdinaryMultiPredefinedScene> ordinaryMultiNestMore(
    const OrdinaryMultiPredefinedScene &original,const Input &expandedInput,
    std::span<const OrdinaryMultiAddedInstance> added,
    std::span<const OrdinaryMultiPredefinedSheet> newStocks,
    const ReverseRunnerFactory &,Budget &,OrdinaryMultiContinuationTrace * = nullptr);
}
