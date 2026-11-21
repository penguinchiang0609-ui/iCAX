#pragma once
#include "recovered_escher_maximum_phase.hpp"
namespace study::recovered {
struct EscherEqualMaximumRows {
    std::vector<std::vector<EscherRepeatRow>> members;
    std::vector<int64_t> coordinates;
    uint64_t total=0;
};
EscherEqualMaximumRows escherSeedEqualMaximumRows(std::span<const uint64_t> rowCounts,
    std::span<const EscherIntegerBox>,int64_t columns,int64_t period,EscherIntegerPoint step,int64_t allowance); //7c7550
// Unlike 7c5f20, 7c7840 starts with previousBest as its current score and
// prefers the larger cell area on equal counts. Empty selections can retain
// that input score; candidate.has_value() determines whether geometry exists.
EscherMaximumPhaseSelection escherEqualMaximumPhase(size_t members,
    std::span<const EscherPeriodCandidate>,EscherIntegerBox,bool specialCatalog,
    EscherIntegerPoint axis,uint64_t previousBest,uint64_t limit,int32_t restrictionDegrees=0); //7c7840
EscherPatternLayout escherEqualMaximumPattern(std::span<const EscherPatternRecord>,
    std::span<const EscherPeriodCandidate>,EscherIntegerBox,bool specialCatalog,
    EscherIntegerPoint axis,uint64_t previousBest,uint64_t limit,
    std::span<const EscherRecordGeometry>,int32_t restrictionDegrees=0);
}
