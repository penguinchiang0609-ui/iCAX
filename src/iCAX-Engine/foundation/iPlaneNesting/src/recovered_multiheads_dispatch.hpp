#pragma once
#include <cstdint>

namespace study::recovered {
// 5a596b..5a5998 / 5a5b82..5a5baf. Refinement and continuation
// switch to partial replay only on a single-head, non-rectangle machine.
// The original writes phase=1 conditionally; otherwise the old phase survives.
int32_t multiheadsAutomaticConstructionPhase(bool rectangleModeC8,
    int32_t assignedTrials8c, int32_t heads48, int32_t currentPhase=0) noexcept;
}
