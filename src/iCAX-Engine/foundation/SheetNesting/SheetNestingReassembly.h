#pragma once
#include "SheetNestingPipeline.h"

namespace iCAX::SheetNesting
{
    class NfpCache;
    struct NfpReassemblySettings
    {
        std::size_t MaxWindows = 8;
        std::size_t MaxReleased = 8;
        std::size_t BeamWidth = 24;
        std::size_t MaxMissing = 2;
        std::size_t CandidatesPerAngle = 48;
        std::size_t BranchPerPart = 6;
        std::size_t ProfileSamples = 40;
    };
    // Optional bounded neighbourhood reconstruction. A window releases original
    // placements, then jointly chooses their order, allowed directions and true
    // contact positions against all fixed originals. An unsuccessful window
    // preserves the incumbent. Never registered in DefaultStrategies implicitly.
    PostProcessStage MakeNfpReassemblyStage(NfpReassemblySettings settings = {},
        std::shared_ptr<NfpCache> geometryCache = {});
}
