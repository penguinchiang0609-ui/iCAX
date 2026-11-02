#pragma once
#include "SheetNesting.h"

namespace iCAX::SheetNesting
{
    // Both variants use the same motif-generation and batch placement rules.
    // AfterBatch fills only within the just-placed pattern batch's physical envelope.
    // The existing circle-lattice seed is shared and is not immediately gap-filled.
    enum class BatchFillTiming { OnStall, AfterBatch };

    // OnStall is registered as rules.batch-on-stall by DefaultStrategies.
    // AfterBatch remains opt-in through this factory.
    // Every job owns its inventory, repeated-unit plan and collision index.
    StrategyFactory MakeBatchRuleFactory(BatchFillTiming timing = BatchFillTiming::OnStall);
}
