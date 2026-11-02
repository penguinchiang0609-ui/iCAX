#pragma once
#include "SheetNestingPipeline.h"
namespace iCAX::SheetNesting
{
    // Optional one-missing-piece translation repair using triangulated polygon
    // separation and bounded halfspace projection. No default registration.
    PostProcessStage MakeTriangleSeparationRepairStage();
}
