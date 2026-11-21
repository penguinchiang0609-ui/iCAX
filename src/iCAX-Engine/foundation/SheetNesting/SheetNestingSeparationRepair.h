#pragma once
#include "SheetNestingPipeline.h"
namespace iCAX::SheetNesting
{
    struct TriangleSeparationRepairSettings
    {
        bool UseNfpSeeds=false;
        std::size_t MaxProbeBlockers=2;
        std::size_t MaxProbeAttempts=25000;
        double ProbeTimeShare=.18;
    };
    // Optional one-missing-piece translation repair using triangulated polygon
    // separation and bounded halfspace projection. No default registration.
    PostProcessStage MakeTriangleSeparationRepairStage();
    PostProcessStage MakeTriangleSeparationRepairStage(TriangleSeparationRepairSettings settings);
}
