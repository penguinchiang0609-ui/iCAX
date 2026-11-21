#pragma once
#include "SheetNestingPipeline.h"

namespace iCAX::SheetNesting
{
    struct RotationCompactionSettings
    {
        std::size_t MaxPasses = 4;
        std::size_t MaxAnglesPerPart = 24;
        std::size_t MaxPivotsPerPart = 8;
        double InitialAngleStepDegrees = 5;
        double MinimumAngleStepDegrees = .05;
        bool SettleWithAxis = true;
        bool FiniteAnglesOnly = false; // initial construction can omit continuous-angle refinement
    };

    // Optional original-material rotation refinement. Finite angle domains are
    // retained. Free rotation uses local angle steps, original edge alignment
    // and nearby actual edge directions. Single originals rotate around their
    // own material vertices. Additional registration proposals preserve the
    // local bounding-box center or the previous world bounding-box lower-left.
    // Every proposal uses original material, bounded numerical retreat and
    // optional fixed-angle settling. Circles keep their authoritative angle.
    // Original stock/instance identities, inventory, group and filler provenance
    // remain unchanged. Safe private working states may follow material
    // potential, while the returned incumbent uses the shared full Quality.
    // All rotation/settling work shares one time/attempt/cancellation budget.
    // Finite feature/probe limits provide no global or closure certificate.
    PostProcessStage MakeRotationCompactionStage(RotationCompactionSettings settings = {});
}
