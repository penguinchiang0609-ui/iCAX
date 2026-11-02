#pragma once
#include "SheetNestingPipeline.h"
namespace iCAX::SheetNesting {
// Optional local repair for a valid partial single-sheet polygon layout.
// Failed or truncated searches return the original incumbent unchanged.
PostProcessStage MakeDomainRepairStage();
}
