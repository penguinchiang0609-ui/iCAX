#pragma once
#include "recovered_escher_period.hpp"
namespace study::recovered {
void escherSortSingleRowPointers(std::vector<EscherRepeatRow *> &,size_t depth=SIZE_MAX); //79fad0
void escherDistributeSingleRows(int64_t phase,int64_t width,uint64_t rowCount,uint64_t remainder,
    int32_t columns,std::vector<std::vector<EscherRepeatRow>> &); //79a340
EscherPatternLayout escherSingleWidthPattern(std::span<const EscherPatternRecord>,std::span<const EscherPeriodCandidate>,
    EscherIntegerBox rectangle,uint64_t required,std::array<int64_t,3> &key,std::span<const EscherRecordGeometry>,
    int32_t restrictionDegrees=0); //79a660
bool escherLayoutRotationAccept(std::vector<EscherPatternRecord> &,int32_t restrictionDegrees,
    std::span<const EscherRecordGeometry>); //78a770
EscherIntegerBox escherTransposeRectangle(EscherIntegerBox); //78f6f0
}
