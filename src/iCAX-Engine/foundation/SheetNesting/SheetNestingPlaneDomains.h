#pragma once
#include "SheetNesting.h"

namespace iCAX::SheetNesting
{
    struct PlaneStockDomain
    {
        std::string StockID;
        Shape Boundary; // real material in stock-local millimetres, including holes
        std::vector<Shape> Obstacles;
        std::vector<Rect> InnerRectangles; // optional certified construction proposals
    };
    struct PlaneDomainTask
    {
        Problem Input;
        std::vector<PlaneStockDomain> Domains;
        bool UseInnerRectangle = false;
        // Independent main rectangles and joint smaller-region/main-rectangle
        // plans compete under one task budget. Material holes are never fill areas.
        std::size_t MaxInnerRectangles = 8;
        std::size_t MaxBoardCandidates = 8;
        bool CombineInnerRectangles = true;
        bool EnableInnerPrefill = true;
        bool EnablePeripheralFill = true;
        // One selected main-rectangle candidate only; at most one eighth of
        // this board's remaining budget. Zero explicitly disables compression.
        std::size_t InnerShrinkProbes = 2;
        // Zero derives a finite range from current material/group bounds.
        // A positive value caps that derived range in input geometry units.
        double MaxRigidShift = 0;
        std::size_t RigidShiftSteps = 10;
        bool EnableSheetTemplateReuse = true;
        // Optional quality policy, normalized to the actual allowed material
        // area. Reuse itself has no minimum-density requirement (doc/Plane/44).
        double MinimumSheetReuseUtilization = 0;
    };
    // The typed task keeps real stock/obstacles distinct from production parts.
    // Its returned layout must be checked with this domain validator after any edit.
    Result RunPlaneDomainTask(const PlaneDomainTask&, const Options&);
    Validation ValidatePlaneDomainLayout(const PlaneDomainTask&, const Result&);

    struct PlaneDomainOptimizationSettings
    {
        std::size_t MaxPasses = 3;
        std::size_t MaxCandidatesPerPart = 64;
        bool Reconstruct = true;
        double ReconstructionTimeSeconds = .25;
    };
    // Fixed original/physical stock identities; every accepted move or bounded
    // reconstruction is rechecked against the typed real domain and obstacles.
    Result OptimizePlaneDomainLayout(const PlaneDomainTask&, const Result&,
        const Options&, PlaneDomainOptimizationSettings = {});

    struct PlaneLabelRequest
    {
        std::string HostID, LabelID;
        Shape Host;
        std::vector<Shape> Obstacles;
        Part Label;
        Point Target;
        double Margin = 0, Step = 1;
        std::size_t MaxRings = 100;
    };
    struct PlaneLabelResult
    {
        std::string HostID, LabelID;
        bool Placed = false;
        Pose Transform;
        double Distance = 0;
        std::string Diagnostic;
    };
    PlaneLabelResult LocatePlaneLabel(const PlaneLabelRequest&, const Options&);
}
