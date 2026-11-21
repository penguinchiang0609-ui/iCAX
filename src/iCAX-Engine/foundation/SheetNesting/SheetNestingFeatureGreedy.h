#pragma once
#include "SheetNesting.h"
#include "SheetNestingConstructionTrace.h"

namespace iCAX::SheetNesting
{
class NfpCache;

enum class FeatureGreedyOrder
{
    IncrementalArea,
    IncrementalWaste,
    LowThenLeft,
    RemainingArea
};

struct FeatureGreedySettings
{
    FeatureGreedyOrder Order = FeatureGreedyOrder::IncrementalArea;
    // Owned feature and placed-geometry payload. Shared geometry caches have separate bounds.
    std::size_t MaxRetainedBytes = 64 * 1024 * 1024;
    // Preferred shortlist. If none fits, continue through the remaining specifications.
    std::size_t MaxSpecificationsPerStep = 16;
    // Fallback query angles. Feature-derived structural angles are retained separately.
    std::size_t MaxAnglesPerPart = 4;
    std::size_t MaxCandidatesPerPart = 4;
    std::function<void(const ConstructionTraceEvent &)> Trace;
    std::size_t MaxTraceEvents = 10000;
};

// Independent single-sheet construction. Each accepted step places one
// original instance. Pair features propose contacts and relative angles.
StrategyFactory MakeFeatureGreedyFactory(FeatureGreedySettings settings = {}, std::shared_ptr<NfpCache> cache = {});
} // namespace iCAX::SheetNesting
