#pragma once
#include "SheetNesting.h"
#include "SheetNestingConstructionTrace.h"

namespace iCAX::SheetNesting
{
class NfpCache;

struct FeatureConstructionSettings
{
    // Job-owned payload ceiling. The shared NFP cache has its own budget.
    std::size_t MaxRetainedBytes = 64 * 1024 * 1024;
    std::size_t MaxSearchStates = 128;
    // Total additional feature combinations and repeated-contact blocks.
    std::size_t MaxArrayVariants = 32;
    // Optional observation only. Empty callbacks construct no trace payload.
    // A callback failure disables observation and preserves solver execution.
    std::function<void(const ConstructionTraceEvent &)> Trace;
    std::size_t MaxTraceEvents = 10000;
};

// Independent construction on one selected stock sheet. Stock types are
// alternative roots. Carrier residuals propose complementary placements and
// bounded repeated-contact arrays. Original members are checked at world poses.
StrategyFactory MakeFeatureConstructionFactory(FeatureConstructionSettings settings = {},
                                               std::shared_ptr<NfpCache> cache = {});
} // namespace iCAX::SheetNesting
