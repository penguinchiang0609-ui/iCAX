#pragma once

#include "SheetNesting.h"
#include "SheetNestingPipeline.h"

namespace iCAX::SheetNesting
{
    struct SparrowSettings
    {
        std::size_t Threads = 3; // 1..32 kernel workers, independent of portfolio workers.
        double ExplorationFraction = 0.8; // (0, 1) of the kernel search budget.
        // Negative disables the corresponding upstream preprocessing step.
        // Nonnegative ratios must be <=1. Original input rings remain the
        // authority for native final validation in either case.
        double PolygonSimplificationRatio = 0.001;
        double NarrowConcavityRatio = 0.01;
        double NumericalGuard = 0; // Internal conservative spacing only; never changes Problem.
    };

    // Optional independent Sparrow kernel strategy. It is not registered in
    // DefaultStrategies and creates all solve state separately for every job.
    //
    // The first adapter supports one rectangular stock type, one physical
    // sheet, compatible material, zero gap/margin, and simple polygon exteriors
    // without holes/circles. FreeRotation and original finite angle domains are
    // transported explicitly. Unsupported scope is reported without invoking
    // the kernel, with the complete original demand in Remaining.
    //
    // Kernel output is a proposal: every original instance, angle, material,
    // stock cap and collision is checked by the ordinary native Validate before
    // a complete result is exposed. No author placement is used as a seed.
    // The pinned kernel searches in f32; extremely tight rotated fits may fail
    // cleanly even when another strategy can solve the original double input.
    StrategyFactory MakeSparrowFactory(SparrowSettings settings = {});

    // Optional compress-only refinement of a complete, original-native-valid
    // single-sheet incumbent. Restores original external poses into the pinned
    // kernel; never invokes its cold constructor. A seed that becomes invalid
    // in the upstream f32 search representation may undergo an explicitly
    // marked bounded internal separation before compression. Only a complete
    // zero-loss feasible snapshot may enter compression; failure retains the
    // incumbent. Internal search never enlarges the original physical cap.
    // Every returned proposal must pass original native validation and the
    // shared Ranking policy. Failure/worse output retains all incumbent layout
    // fields and original instance provenance. Never registered by default.
    PostProcessStage MakeSparrowRefineStage(SparrowSettings settings = {});
}
