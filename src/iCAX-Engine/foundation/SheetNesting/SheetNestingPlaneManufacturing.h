#pragma once
#include "SheetNesting.h"

namespace iCAX::SheetNesting
{
    enum class PlaneManufacturingMode { AdjacentRectangles, CommonEdgeArray, CommonEdgeDistance, Guillotine, SawMill };
    enum class PlanePartProcess { Saw, Mill };
    enum class PlaneSawMillClassification { Explicit, RectanglesSawPolygonsMill };
    struct PlaneProcessAssignment
    {
        std::string PartID;
        PlanePartProcess Process = PlanePartProcess::Mill;
    };
    struct PlaneAdjacentPair
    {
        std::string First, Second;
        double MaximumSeparation = 1;
        bool AllowUnpairedRemainder = false;
    };
    struct PlaneManufacturingTask
    {
        Problem Input;
        PlaneManufacturingMode Mode = PlaneManufacturingMode::AdjacentRectangles;
        std::vector<PlaneAdjacentPair> Pairs; // empty in adjacency mode: explicit A/B suffix import
        std::size_t MaxGroupMembers = 6, MaxCandidates = 64;
        double MinimumSharedLength = 1, GroupGap = 0;
        bool ParallelSharedEdgesOnly = false;
        std::vector<std::string> SawPartIDs; // SawMill only; the other originals are milled
        // A nonempty assignment table is complete: every original is classified.
        // The optional geometric rule must be selected explicitly; IDs are never guessed.
        PlaneSawMillClassification ProcessClassification = PlaneSawMillClassification::Explicit;
        std::vector<PlaneProcessAssignment> ProcessAssignments;
        double SawGap = 0, MillGap = 0;
        bool EnableSheetTemplateReuse = true;
        double MinimumTemplateUtilization = 0;
    };
    struct PlaneCut
    {
        std::string StockID;
        std::size_t StockInstance = 0;
        Rect Region;
        bool Vertical = true;
        double Position = 0, Kerf = 0;
    };
    struct PlaneManufacturingResult
    {
        Result Output;
        std::vector<PlaneCut> Cuts; // ordered cuts of actual remaining rectangular leaves
    };
    PlaneManufacturingResult RunPlaneManufacturingTask(const PlaneManufacturingTask&, const Options&);
    // Includes the original geometry validation plus pair/group/cut constraints.
    // Re-run this validator after any downstream edit or optimization.
    Validation ValidatePlaneManufacturingLayout(const PlaneManufacturingTask&, const PlaneManufacturingResult&);
    struct PlaneManufacturingOptimizationSettings
    {
        bool Translate = true, Rotate = true;
        std::size_t MaxIterations = 4;
    };
    // The unchanged process task and cut evidence validate every proposed edit.
    // A rejected, stopped or failed edit retains the best legal incumbent.
    PlaneManufacturingResult OptimizePlaneManufacturingLayout(const PlaneManufacturingTask&,
        const PlaneManufacturingResult&, const Options&,
        PlaneManufacturingOptimizationSettings settings = {});
}
