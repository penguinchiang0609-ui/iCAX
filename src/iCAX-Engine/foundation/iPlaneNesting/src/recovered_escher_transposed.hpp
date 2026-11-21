#pragma once
#include "recovered_escher_enumeration.hpp"
namespace study::recovered {
std::vector<EscherRepeatRow> escherTransposedRows(std::span<const EscherIntegerBox>,int64_t width,
    EscherIntegerPoint step,int64_t allowance,uint64_t required,int64_t columns); //7d1820
EscherPatternLayout escherTransposedSinglePattern(std::span<const EscherPatternRecord>,std::span<const EscherPeriodCandidate>,
    EscherIntegerBox rectangle,uint64_t required,std::array<int64_t,3> &key,std::span<const EscherRecordGeometry>,
    int32_t restrictionDegrees=0,bool specialCatalog=false); //7d1a60
struct EscherEnumerationDescriptor {
    std::vector<EscherPatternRecord> records;
    std::vector<EscherPeriodCandidate> normalCandidates,specialCandidates;
    bool sameWidth = false;
    int32_t restrictionDegrees = 0;
    std::vector<bool> orientationChoices; //false=direct, true=transposed
};
EscherPatternLayout escherEnumerateSinglePattern(const EscherEnumerationDescriptor &,EscherIntegerBox rectangle,
    bool specialCatalog,uint64_t required,std::array<int64_t,3> &key,std::span<const EscherRecordGeometry>); //79ce40
}
