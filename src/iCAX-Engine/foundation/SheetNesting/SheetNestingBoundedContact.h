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
    enum class NfpCompositeOrder { MaterialArea, AnchorArea, BalancedSpecifications, PreserveRemnant };
    enum class NfpCompositePlacement { EnvelopeArea, LowThenLeft, SharedBoundary };
    struct NfpCompositeSettings
    {
        NfpCompositeOrder Order=NfpCompositeOrder::MaterialArea;
        std::size_t MaxCombinationAttempts=40000;
        bool PreserveComplementaryInventory=false;
        NfpCompositePlacement Placement=NfpCompositePlacement::EnvelopeArea;
    };
    // Independent full-budget construction from an empty sheet. Motifs retain
    // original members and voids; only immutable pair geometry may be shared.
    // This optional factory is never added to DefaultStrategies automatically.
    StrategyFactory MakeNfpCompositeContactFactory(NfpCompositeSettings settings={},
        std::shared_ptr<NfpCache> cache={});
}
