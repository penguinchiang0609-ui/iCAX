#pragma once
#include "recovered_escher_record.hpp"
namespace study::recovered {
struct EscherPeriodCandidate {
    EscherLatticeBasis basis{};
    int64_t width=0;
    EscherIntegerPoint step{};
    std::vector<EscherIntegerBox> boxes;
    std::vector<EscherIntegerPoint> offsets;
    double basisSlope=0,stepSlope=0;
};
struct EscherRepeatRow {
    int64_t x=0,y=0,count=0;
    bool operator==(const EscherRepeatRow &)const=default;
};
static_assert(sizeof(EscherRepeatRow)==0x18);
struct EscherRecordGeometry {EscherSupportTable intrinsic,collision;};
bool escherPeriodOrthogonal(EscherIntegerPoint,EscherIntegerPoint); // 7799c0
EscherPeriodCandidate escherNormalizePeriodBasis(EscherLatticeBasis); // 78c770 / 78d280
std::vector<EscherIntegerBox> escherCandidateBoxes(std::span<const EscherSupportTable>,EscherIntegerPoint); // 78ced0
std::vector<std::vector<EscherRepeatRow>> escherSingleRowPoints(std::span<const EscherIntegerBox>,
    std::span<const uint64_t> rowCounts,int64_t width,EscherIntegerPoint step); // 79a210
std::vector<EscherPatternRecord> escherRotateRecords(std::span<const EscherPatternRecord>,int32_t angle,
    std::span<const EscherRecordGeometry>); // 78f7e0
std::vector<EscherPatternRecord> escherRepeatRows(std::span<const EscherPatternRecord>,int64_t width,
    std::span<const std::vector<EscherRepeatRow>>,std::span<const EscherIntegerPoint> offsets); // 78fa40
EscherPatternLayout escherLayoutFromRows(std::span<const EscherPatternRecord>,const EscherPeriodCandidate &,
    std::span<const std::vector<EscherRepeatRow>>,std::span<const EscherIntegerPoint> offsets,
    std::span<const EscherRecordGeometry>); // 7901e0
void escherNormalizePairBoxes(uint32_t,EscherIntegerPoint horizontal,EscherIntegerPoint step,
    std::vector<EscherIntegerBox> &); // 78da90
EscherPatternLayout escherUnrotatedPattern(std::span<const EscherPatternRecord>,std::span<const EscherPeriodCandidate>,
    EscherIntegerBox rectangle,uint64_t required,std::array<int64_t,3> &key,std::span<const EscherRecordGeometry>,
    int32_t restrictionDegrees=0); // 79b930
}
