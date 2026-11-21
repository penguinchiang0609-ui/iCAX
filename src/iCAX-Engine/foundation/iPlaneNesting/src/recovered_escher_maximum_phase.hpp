#pragma once
#include "recovered_escher_candidate_sort.hpp"
namespace study::recovered {
struct EscherMaximumPhaseSelection {
    std::optional<size_t> candidate;
    uint32_t normalization=0;
    uint64_t count=0;
    std::vector<std::vector<EscherRepeatRow>> rows;
    std::vector<EscherIntegerPoint> offsets;
};
// 7c5f20: ordinary-width maximum-count phase, before the common 7901e0
// layout constructor. previousBest controls pruning, not the initial result.
EscherMaximumPhaseSelection escherGeneralMaximumPhase(size_t members,
    std::span<const EscherPeriodCandidate>,EscherIntegerBox,bool specialCatalog,
    EscherIntegerPoint axis,uint64_t previousBest,uint64_t limit,int32_t restrictionDegrees=0);
EscherPatternLayout escherGeneralMaximumPattern(std::span<const EscherPatternRecord>,
    std::span<const EscherPeriodCandidate>,EscherIntegerBox,bool specialCatalog,
    EscherIntegerPoint axis,uint64_t previousBest,uint64_t limit,
    std::span<const EscherRecordGeometry>,int32_t restrictionDegrees=0);
}
