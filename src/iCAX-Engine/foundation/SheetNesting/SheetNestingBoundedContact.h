#pragma once
#include "SheetNesting.h"

namespace iCAX::SheetNesting
{
    class NfpCache;
    // Optional single-board contact search. Finite beam/alternatives are
    // explored against real geometry; an incomplete result is not a proof.
    // Optional externally owned geometry cache can be shared across jobs;
    // placement, demand and all search state stay job-local.
    StrategyFactory MakeBoundedContactFactory(std::shared_ptr<NfpCache> cache = {});
}
