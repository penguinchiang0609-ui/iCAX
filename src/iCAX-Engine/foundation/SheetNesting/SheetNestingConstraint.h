#pragma once
#include "SheetNestingPipeline.h"
#include <cstdint>

namespace iCAX::SheetNesting
{
    struct ConstraintCompletionSettings
    {
        // Oriented bounding-box minima lie on this explicitly declared lattice.
        // Original polygon coordinates must be represented without rounding.
        std::int64_t CoordinateScale = 14;
        int Workers = 2;
        std::int64_t MaxConflicts = 1000000;
        std::size_t MaxInstances = 64;
        std::size_t MaxHintCandidates = 4096;
        bool UseInscribedRectangles = true;
        bool CompleteSeparationHints = false;
    };

    // Optional constraint-solver completion, implemented in a separate target
    // that links OR-Tools. No core/default strategy dependency is introduced.
    // First scope: one compatible stock, simple exteriors, zero part gap and
    // explicit quarter-turn angles. Failure on the recorded position lattice
    // is never a claim of continuous geometric infeasibility.
    // The official Windows OR-Tools binary requires the Release CRT.
    PostProcessStage MakeConstraintCompletionStage(ConstraintCompletionSettings settings = {});

    enum class ConstraintContactOrder { LayoutOrder, SmallMaterialContact };
    struct ConstraintNeighborhoodSettings
    {
        ConstraintCompletionSettings Solver;
        std::size_t MaxMovableInstances = 8;
        std::size_t MaxPlans = 8;
        // Source-layout obstacle cap, before selecting released members;
        // the shared NFP event generator currently supports at most 128.
        std::size_t MaxFixedObstacles = 128;
        double SecondsPerPlan = 5;
        ConstraintContactOrder ContactOrder = ConstraintContactOrder::LayoutOrder;
    };
    // Independent, optional one-missing-part neighborhood stage. NFP blockers
    // and their actual contact neighbors choose the movable originals. Every
    // other placement keeps its original double pose, including off-grid poses.
    PostProcessStage MakeConstraintNeighborhoodStage(ConstraintNeighborhoodSettings settings = {});

    struct ConvexTailRepackSettings
    {
        std::size_t MinWindowBoards = 3;
        std::size_t MaxWindowBoards = 8;
        std::size_t MaxReleasedInstances = 18;
        std::size_t TargetBoardReduction = 1;
        double SecondsPerWindow = 5;
        int Workers = 1;
        std::int64_t MaxConflicts = 1000000;
        bool AllowSameSheetCount = true;
        std::size_t MaxCompactionPasses = 4;
    };

    // Independent optional tail reconstruction over one original StockID at a
    // time. Initial finite scope: complete incumbents, strictly convex integer
    // exteriors and integer poses, original finite quarter-turn subsets, zero
    // gap/margin. Other stock instances remain field-for-field unchanged.
    // Original identities may separate from their construction groups. The
    // CP search uses actual polygon separating axes, never bbox collisions.
    // Whole-original Validate and the common quality order gate every update.
    // The first pass tries fewer sheets; an optional same-count pass minimizes
    // terminal original material before geometry is considered. It is not
    // registered in DefaultStrategies. A lattice failure is not global proof.
    PostProcessStage MakeConvexTailRepackStage(ConvexTailRepackSettings settings = {});
}
