#pragma once
#include "SheetNestingPipeline.h"

namespace iCAX::SheetNesting
{
    enum class PlaneOptimizationStop { Target, Stable, Time, Attempts, Cancelled, InitialNotFound, InvalidInput };
    struct PlaneCompactionSettings
    {
        double TargetUtilization = 0; // 0 disables the target; otherwise (0,1].
        std::size_t StableIterations = 16; // 0 disables stagnation stopping.
        double SignificantImprovement = 1e-7;
        double StageTimeSeconds = .25;
        bool Rotate = true;
        bool Reassemble = true;
        bool ShrinkRegion = true;
        bool EnablePerturbation = true;
        std::size_t PerturbEvery = 4;
        std::size_t MaxPerturbationAttempts = 32;
        double MaxRelativeEnvelopeGrowth = .1;
        bool RefillAcrossSheets = true;
        bool MixSheetRecipes = true;
    };
    struct PlaneOptimizationResult
    {
        Result Output;
        PlaneOptimizationStop Stop = PlaneOptimizationStop::InvalidInput;
        double InitialUtilization = 0, BestUtilization = 0;
        Rect BestEnvelope;
        double MaterialArea = 0, ElapsedSeconds = 0;
        std::size_t Iterations = 0, Improvements = 0, StableCount = 0;
        std::size_t Perturbations = 0, AcceptedWorse = 0;
    };
    // Single physical sheet, complete fixed original instance set. The Options
    // time/attempt limits include every operator and verification. Cancellation
    // retains the best complete layout. Feasible bounded perturbations affect
    // only the private current state; published best never deteriorates.
    PlaneOptimizationResult OptimizePlaneLayout(const Problem&, const Result& initial,
        const Options&, PlaneCompactionSettings settings = {});
    // Fixed-instance, two-axis bounded shrink/rebuild of the last physical sheet.
    // All earlier sheets remain unchanged. The construction factory
    // receives a private working region. Failed probes do not prove infeasibility.
    PostProcessStage MakePlaneShrinkStage(StrategyFactory construct, std::size_t probesPerAxis = 8);
    StrategyFactory MakePlaneOptimizationFactory(StrategyFactory construct,
        PlaneCompactionSettings settings = {}, double constructionBudgetFraction = .25);
    // Refine an existing complete multi-sheet job. Each single-sheet operator
    // keeps original identities; cross-sheet refill/recipe mixing is optional.
    // Each completed board improvement receives an independent whole-job
    // quality checkpoint, so later budget/cancellation retains earlier wins.
    // Ordinary rectangular Problem only. Real-domain/manufacturing wrappers
    // must preserve their extra constraints and apply their own validators.
    PostProcessStage MakePlaneOptimizationStage(PlaneCompactionSettings settings = {},
        StrategyFactory reconstruct = {});
}
