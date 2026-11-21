#include "recovered_multiheads_dispatch.hpp"

namespace study::recovered {
int32_t multiheadsAutomaticConstructionPhase(bool rectangleModeC8,
    int32_t assignedTrials8c, int32_t heads48, int32_t currentPhase) noexcept {
    return !rectangleModeC8 && assignedTrials8c>10 && heads48<2 ? 1 : currentPhase;
}
}
