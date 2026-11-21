#pragma once
#include "SheetNesting.h"
#include <utility>

namespace iCAX::SheetNesting
{
    // Optional cold-start constructor. It consumes only the current Problem;
    // no previously placed layout or task-specific geometry is accepted.
    struct SheetCountSettings
    {
        std::int64_t CoordinateScale = 10;
        int Workers = 3;
        double SecondsPerBoardCount = 20;
        std::int64_t MaxConflicts = 2000000;
        std::size_t MaxInstances = 128;
        // Redundant packing propagation using one exact inner rectangle per
        // original polygon. Original contours remain the legality authority.
        bool EnableInteriorRectangles = true;
        // Build a legal current-input NFP incumbent before the fixed-count
        // search. It supplies soft hints only; every original stays movable.
        bool EnableNfpInitializer = true;
        // Optional bounded micro-translations after integer pose conversion.
        // The default preserves the caller's declared validation tolerance.
        bool EnableRoundoffRefinement = false;
        // Hand a newly solved complete constructor result back to its caller
        // before unhinted smaller-count retries. An already checked source
        // hint never triggers this handoff, and perturbations ignore it.
        bool StopAfterFirstComplete = false;
    };

    // One material-compatible stock type; convex, hole-free polygons with
    // finite quarter-turn angles. Original vertices and parameters must be
    // lossless on CoordinateScale. Unsupported input is diagnosed explicitly.
    // Fixed sheet counts are tried from the material-area lower bound upward.
    // A verified complete incumbent is retained while remaining total budget
    // revisits unresolved smaller counts with independently recorded seeds.
    // Common edges are a permitted contact, never an optimization reward.
    // A timeout or failure of this conservative lattice model is not a proof
    // that a continuous layout with that sheet count cannot exist.
    StrategyFactory MakeSheetCountStrategy(SheetCountSettings settings = {});

    struct SheetCountCurrentTaskHint
    {
        Problem Input;
        Result Layout;
    };

    // Soft guidance from an assessed layout created during this task. Run checks
    // the snapshot against its current Problem, allowing only reduced inventory.
    // Original instances stay free to move, turn and change sheets. Source poses
    // may be rounded only for AddHint, never for publication or validation.
    StrategyFactory MakeSheetCountRepairStrategy(SheetCountCurrentTaskHint currentTaskHint,
        SheetCountSettings settings = {});

    struct SheetCountPartition
    {
        using Identity = std::pair<std::string, std::size_t>;
        std::vector<std::vector<Identity>> Groups;
    };

    // Current-task neighborhood reconstruction at exactly Groups.size() sheets.
    // Groups must cover every demanded original instance exactly once. At least
    // one formerly co-located pair must separate, excluding board renumbering.
    // Every sheet is nonempty. No old positions or original group locks are used.
    // Within this fixed count, minimize the last sheet's actual material area
    // while preceding sheets remain ordered by decreasing actual material.
    // Continue improving that local objective for the supplied run budget.
    // A complete result certifies a legal changed partition, without a sheet-count
    // minimum claim. Singleton-only partitions have no mutable pair to exclude.
    StrategyFactory MakeSheetCountPerturbationStrategy(SheetCountPartition partition,
        SheetCountSettings settings = {});
    StrategyFactory MakeSheetCountPerturbationStrategy(SheetCountPartition partition,
        SheetCountCurrentTaskHint currentTaskHint, SheetCountSettings settings = {});
}
