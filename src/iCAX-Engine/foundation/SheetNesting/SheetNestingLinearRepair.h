#pragma once
#include "SheetNestingPipeline.h"

namespace iCAX::SheetNesting
{
    class NfpCache;
    struct ConvexLinearRepairSettings
    {
        std::size_t MaxSeeds=24;
        std::size_t MaxSearchNodes=16000;
        std::size_t MaxOpenNodes=1024;
        std::size_t MaxAxisChoices=12;
        std::size_t MaxLearnedConflicts=1024;
        std::size_t MaxOrientationPlans=4;
        bool LearnConflicts=true;
    };
    // Optional one-missing-piece repair. Convex original polygons retain their
    // allowed angles; each bounded orientation plan chooses separation directions
    // and solves all translations together. No default portfolio registration.
    PostProcessStage MakeConvexLinearRepairStage(ConvexLinearRepairSettings settings={},
        std::shared_ptr<NfpCache> geometryCache={});
}
