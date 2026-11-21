#pragma once
#include "recovered_escher_lattice.hpp"
namespace study::recovered {
struct EscherSupportRecord {
    EscherIntegerPoint first{},second{},direction{};
    bool operator==(const EscherSupportRecord &)const=default;
};
static_assert(sizeof(EscherSupportRecord)==0x30);
using EscherSupportTable=std::vector<EscherSupportRecord>;
EscherSupportTable escherBuildSupportTable(std::vector<EscherIntegerPoint>); // 7fe850
std::array<EscherIntegerPoint,2> escherSupportExtrema(std::span<const EscherSupportRecord>,EscherIntegerPoint); // 7fecb0
EscherIntegerPoint escherRotateRecordPoint(EscherIntegerPoint,double radians); // 4045a0
EscherIntegerBox escherBoundsFromSupport(std::array<EscherIntegerPoint,2> first,
    std::array<EscherIntegerPoint,2> second,double cosine,double sine); // 7fef20
EscherIntegerBox escherBoundsForAngle(std::span<const EscherSupportRecord>,int32_t angle); // 7ff1c0
void escherUpdateRecordBounds(EscherPatternRecord &,std::span<const EscherSupportRecord> intrinsic,
    std::span<const EscherSupportRecord> collision); // 7f7190, explicit geometry instead of lazy caches
}
