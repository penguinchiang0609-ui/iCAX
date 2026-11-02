#pragma once
#include "SheetNestingPipeline.h"

namespace iCAX::SheetNesting
{
    // Optional bounded partial-layout repair. This stage considers actual missing
    // originals, temporarily evicts up to two blocking placements, then tries to
    // restore them using original geometry and allowed directions. It is intended
    // for a pipeline whose constructor may return a valid Partial result.
    // No stage is registered in DefaultStrategies.
    PostProcessStage MakeEvictionRepairStage();
}
