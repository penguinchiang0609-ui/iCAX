#pragma once
#include "SheetNestingPipeline.h"
#include "SheetNestingSheetCount.h"

namespace iCAX::SheetNesting
{
    struct SheetCountRefillSettings
    {
        SheetCountSettings Solver;
        std::size_t MinWindowBoards = 2;
        // Fast local neighborhoods. Exchange mode additionally schedules the
        // entire current same-stock cohort using Solver.MaxInstances scope.
        std::size_t MaxWindowBoards = 4;
        std::size_t MaxReleasedInstances = 32;
        std::size_t MaxPlans = 16;
        double SecondsPerWindow = 5;
        bool EnableDonorTransfers = false;
        std::size_t MaxTransferPlans = 32;
        double SecondsPerTransfer = 2;
        bool EnableExchanges = false;
        // With exchanges enabled, plan caps apply to each reduction phase.
        // Total time/validation attempts remain shared across the whole stage.
        std::size_t MaxPerturbations = 16;
        double SecondsPerPerturbation = 2;
        std::size_t MaxStagnation = 4;
    };

    // Refine current-job original instances through bounded same-stock windows.
    // Optional exchanges keep a private working layout and a checked best layout.
    // Each neighborhood releases its members' positions and permitted angles.
    // Unreleased boards stay fixed for that neighborhood. All complete proposals
    // are validated against the whole input, and only the best layout is returned.
    // Exchange mode applies plan quotas per reduction phase while retaining one
    // shared time/validation budget and cumulative work statistics.
    // Initial reduction phases reserve a full-cohort K-1 reconstruction. First
    // and every third perturbation release the entire cohort at the current K.
    // Each child solve receives a validated snapshot of its current selected
    // members, retaining original poses as soft guidance for reconstruction.
    PostProcessStage MakeSheetCountRefillStage(SheetCountRefillSettings settings = {});
}
