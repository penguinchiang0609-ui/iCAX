#pragma once
#include "recovered_escher.hpp"
namespace study::recovered {
using EscherIntegerPoint=std::array<int64_t,2>;
using EscherIntegerBox=std::array<int64_t,4>;
using EscherLatticeBasis=std::array<EscherIntegerPoint,2>;
struct EscherAnchorEvent {
    int64_t x=0,y=0;
    int32_t a=0,b=0;
    bool operator==(const EscherAnchorEvent &)const=default;
};
static_assert(sizeof(EscherAnchorEvent)==0x18);
struct EscherScanEvent {
    int64_t x=0,y=0;
    int32_t xIndex=0,yIndex=0,weight=0,a=0,b=0,padding=0;
    bool operator==(const EscherScanEvent &)const=default;
};
static_assert(sizeof(EscherScanEvent)==0x28);
struct EscherAnchorMaximum {
    EscherIntegerPoint point{};
    int32_t count=0;
    bool operator==(const EscherAnchorMaximum &)const=default;
};
using EscherIntegerCoefficient=std::array<int32_t,2>;
int32_t escherTolerantCeil(double,double tolerance=.001); // 7c8eb0
int32_t escherTolerantFloor(double,double tolerance=.001); // 7c8e60
std::vector<EscherIntegerCoefficient> escherTriangleScanRow(std::array<double,2> from,std::array<double,2> to,
    int32_t row,double tolerance=.001); // 7c8f30
std::vector<EscherIntegerCoefficient> escherCornerIntegerCoefficients(std::array<double,2> from,
    std::array<double,2> to,std::array<double,2> origin); // 7c94c0
std::vector<EscherAnchorEvent> escherCornerEvents(EscherIntegerPoint u,EscherIntegerPoint v,
    EscherIntegerPoint origin,EscherIntegerPoint sheet); // 7c9f40
void escherBoundaryEvents(std::vector<EscherAnchorEvent> &positive,std::vector<EscherAnchorEvent> &negative,
    EscherIntegerBox box,EscherIntegerPoint sheet,EscherIntegerPoint u,EscherIntegerPoint v); // 7cb890 / 7cb6b0
EscherIntegerPoint escherLatticeAnchor(std::span<const EscherIntegerBox>,EscherIntegerPoint u,
    EscherIntegerPoint v,EscherIntegerPoint sheet); // 7cbe40
std::vector<std::vector<EscherIntegerPoint>> escherFitLatticePoints(EscherLatticeBasis,
    EscherIntegerPoint sheet,std::span<const EscherIntegerBox>); // 7ca570
// The source records have already had 7f7190 applied for their own poses.
// 78f7e0 is called with zero rotation, so its secondary records retain those
// prepared intrinsic/collision bounds and their untranslated source poses.
EscherPatternLayout escherLatticeLayout(std::span<const EscherPatternRecord> preparedRecords,
    std::span<const EscherLatticeBasis> candidates,EscherIntegerBox rectangle,int32_t candidateLimit=-1); // 78dd40
EscherLatticeBasis escherReduceLatticeBasis(EscherLatticeBasis); // 7ca390
EscherScanEvent escherClipAnchorScanEvent(EscherScanEvent,EscherIntegerPoint u,EscherIntegerPoint v); // 7cc510
EscherAnchorMaximum escherMaximumAnchor(std::vector<EscherScanEvent>); // 7cc790
std::vector<EscherAnchorEvent> escherFilterCornerEvents(EscherIntegerPoint u,EscherIntegerPoint v,
    EscherIntegerPoint sheet,std::span<const EscherAnchorEvent>); // 7c9230
}
