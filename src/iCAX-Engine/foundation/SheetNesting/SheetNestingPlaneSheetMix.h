#pragma once
#include "SheetNestingPipeline.h"

namespace iCAX::SheetNesting
{
    struct PlaneSheetMixSettings
    {
        std::size_t MaxRecipes = 32;
        std::size_t MaxConstructionProbes = 12; // Zero uses current-run recipes only.
        std::size_t MaxSearchNodes = 20000;
        std::size_t MaxTailTrials = 8; // Zero disables residual reconstruction.
        double SecondsPerProbe = .1;
        double SecondsPerTail = .25;
    };
    // Optional utilization refinement. Complete original-valid input only.
    // Recipes are extracted from this task's incumbent or freshly constructed
    // from its immutable original geometry. Integer copies of DIFFERENT recipes
    // compete together; exact residual demand can be reconstructed afterward.
    // Stock identity/material/quantity and every original instance are retained.
    // Only a complete candidate improving shared Quality replaces the incumbent.
    // Cancellation/limits preserve the best complete result; no persistent cache.
    // This entry uses ordinary rectangular Problem constraints. Specialized
    // domain/manufacturing tasks must retain their own validators and context.
    Result OptimizePlaneSheetMix(const Problem&, const Result& incumbent,
        const Options&, StrategyFactory construct, PlaneSheetMixSettings settings = {});
    PostProcessStage MakePlaneSheetMixStage(StrategyFactory construct,
        PlaneSheetMixSettings settings = {});
}
