#pragma once
#include "SheetNestingPipeline.h"

namespace iCAX::SheetNesting
{
    // Construction policies share NFP, combinations, safety and evaluation services.
    // Legacy quantity/source names are compatibility inputs, not extra objectives.
    enum class PlaneStrategyKind
    {
        ArrayQuantityDeferred, ArrayGeometryDeferred,
        ArrayQuantityImmediate, ArrayGeometryImmediate,
        HistoricalFirstArray, SingleProductArray, SingleColumn,
        RectangleContact, Scanline,
        NfpAllSpecifications, NfpClosest, NfpDirectional, NfpStrip,
        ArrayFirstSpecificationSearch, ArrayMultiSpecificationSearch,
        ArrayReduceSearch, RandomArrays, RectangleBlockSearch,
        FourBlockArray, NfpArray, NfpMultiArray,
        ArrayCompetition, MixedCompetition
    };
    struct PlaneStrategySettings
    {
        PlaneStrategyKind Kind = PlaneStrategyKind::ArrayQuantityDeferred;
        bool XFirst = false;
        double ScanStep = 1;       // millimetres, not source-library integer units
        double ContactBand = 1;    // millimetres, used only by a local score
        std::size_t MaxCandidates = 64;
        std::size_t BranchWidth = 8;
        std::size_t BranchDecisions = 6;
        std::size_t MaxNodes = 4096;
        std::size_t Restarts = 16;
        // Retained for source compatibility. The initial NFP controller runs
        // one greedy construction; explicit search policies own branching.
        std::size_t NfpTrials = 1;
        bool EnableLayoutRefinement = false; // opt-in complete-layout compaction/refill
        bool UseSourceArrayEvaluation = false; // source container provenance; all candidates rank by original-material utilization
        bool EnableIndependentPairArrays = true;
        bool EnableHoleNesting = true;
        // Optional board finishing. NFP arrays already place ordinary originals
        // by their contours during construction; this is not required for it.
        bool EnableContourSettling = false;
        bool EnableAllowedAngleSettling = false; // opt-in finite allowed-angle trials followed by contour settling
        // Reuse a verified board selected in this run. Full per-original demand
        // and remaining physical stock bound each copy; an optional quality
        // gate can be set without imposing a universal density threshold.
        bool EnableSheetTemplateReuse = true;
        double MinimumSheetReuseUtilization = 0;
        std::size_t MaxArrayAxis = 128;
    };
    StrategyFactory MakePlaneStrategyFactory(PlaneStrategySettings settings = {});
    // Fast independent skyline construction with conservative planning boxes.
    // Real original contours, holes, quantities and directions remain unchanged.
    StrategyFactory MakePlaneEnvelopeInitialFactory();
    // Only canonical construction policies enter the default race. Compatibility
    // aliases and sorting wrappers remain callable separately. The five
    // rectangle-only methods are omitted when active demand includes other
    // shapes. A bounded worker pool fairly shares one deadline/attempt pool;
    // unused attempt leases return to later constructors. Complete improvements
    // can be published on the calling thread. FairScheduling is always enabled
    // by this factory, including when the generic settings use their old default.
    // No implicit material-area early stop: equal board counts can have very
    // different layouts. An explicit caller StopWhen is still respected.
    StrategyFactory MakePlaneInitialFactory(ParallelConstructionSettings settings = {});
    // NFP single-placement constructors and fixed fill support positive gaps
    // via private fixed-obstacle offsets; originals and board margins stay exact.
    // General circles/holed originals use conservative private proposal
    // contours plus exact original-material checks and true hole-domain fits.
    // Up to 512 compatible specifications at a positive gap. Periodic templates use the same contact geometry and
    // validate the complete repeating neighbourhood at the original gap.
    // Explicit registration leaves the established default portfolio unchanged.
    // Each named entry constructs its own state and consumes its own job budget.
    // nfp-all owns a complete construction/compaction/refill workflow and is
    // therefore excluded by pure-construction portfolio capability checks.
    void RegisterPlaneStrategies(StrategyRegistry& registry);
    StrategyFactory MakePlaneOrderPortfolioFactory(StrategyFactory construct, std::size_t maxOrders = 16);

    // Adds missing originals on existing sheets while preserving every fixed
    // placement. Returns the complete result; the delta is recoverable by identity.
    // Unlike MakeNfpRefillStage, this does not move existing parts between sheets.
    PostProcessStage MakePlaneNfpFillStage(bool directional = false);
}
