#pragma once
#include "SheetNesting.h"

namespace iCAX::SheetNesting
{
    class NfpCache;
    enum class NfpBeamScore { HullProgress, MaterialWaste };
    enum class NfpBeamQuickPolicy { Scored, HullGreedy };
    struct NfpBeamSettings
    {
        std::size_t BeamWidth = 96;
        std::size_t InitialBeamWidth = 16;
        // Optional independent job seed. The maximum value lets the beam
        // choose freely; otherwise this original specification is placed first.
        std::size_t FirstPartIndex = static_cast<std::size_t>(-1);
        std::size_t PerInventory = 4;
        std::size_t PlacementsPerPart = 12;
        std::size_t ProfileSamples = 80;
        std::size_t MaxNarrowingRounds = 8;
        // Late inventories compete as groups of different layouts, rather
        // than exhausting the beam with one geometry per inventory.
        std::size_t EndgameDepth = 0;
        std::size_t EndgameGeometry = 2;
        // Necessary placement checks for the last few originals. A failed
        // finite event probe is a heuristic only; a fallback is retained.
        std::size_t RemainingLookahead = 0;
        NfpBeamScore Score = NfpBeamScore::HullProgress;
        // A separate width-one first-fit construction uses at most this share
        // of the remaining job time and attempts. Zero disables it, and the
        // range [0,.5] reserves at least half for the configured beam search.
        // It does not consume any MaxNarrowingRounds search rounds.
        double QuickPassBudgetFraction = .5;
        // Independent construction policy. Scored preserves the original
        // width-one candidate competition; HullGreedy advances the first
        // feasible large-hull specification using cheap checked adjacencies.
        NfpBeamQuickPolicy QuickPolicy = NfpBeamQuickPolicy::Scored;
    };
    // Optional constructive search over original polygons and inventory using
    // legal direction candidates, including edge-aligned free-rotation seeds.
    // Free rotation retains its unrestricted input domain for later refinement.
    // Search state is private to each job. No stochastic ordering.
    StrategyFactory MakeNfpBeamFactory(NfpBeamSettings settings = {});
    // Optional shared immutable pair geometry. Each constructed strategy keeps
    // its own inventory, placements, search limits and cancellation state.
    StrategyFactory MakeNfpBeamFactory(NfpBeamSettings settings, std::shared_ptr<NfpCache> cache);
}
