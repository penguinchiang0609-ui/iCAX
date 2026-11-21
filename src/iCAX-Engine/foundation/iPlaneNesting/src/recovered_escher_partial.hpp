#pragma once
#include "recovered_escher_maximum_phase.hpp"
namespace study::recovered {
struct EscherPartialPhaseSelection {
    EscherMaximumPhaseSelection geometry;
    double density=0;
};
// 7c4b40: outer area is the original selected normal/alternate outer-contour
// integer area. The phase always consumes the special candidate catalog.
EscherPartialPhaseSelection escherPartialPatternPhase(size_t members,
    std::span<const EscherPeriodCandidate>,EscherIntegerBox,int64_t outerArea,
    int32_t restrictionDegrees=0);
EscherPatternLayout escherPartialPattern(std::span<const EscherPatternRecord>,
    std::span<const EscherPeriodCandidate>,EscherIntegerBox,int64_t outerArea,
    std::span<const EscherRecordGeometry>,int32_t restrictionDegrees=0);
}
