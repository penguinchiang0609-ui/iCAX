#pragma once
#include "recovered_escher_owner.hpp"
#include "recovered_escher_producer.hpp"
#include "recovered_escher_transposed.hpp"
#include "recovered_placement.hpp"
namespace study::recovered {
struct EscherOrientationGrid {
    int32_t quantizedStep=3590000; //1 degree=10000; continuous sentinel becomes0
    bool mirrored=false,valid=false;
};
EscherOrientationGrid escherOrientationGrid(std::span<const SdkOrientation>,bool initiallyValid=false); //567730
struct EscherNfpMember {
    const ConvolutionIntegerPolygon *contours=nullptr;
    uintptr_t ownerIdentity=0;
    uint8_t reflected=0;
    int32_t angle=0;
    EscherIntegerPoint translation{};
};
struct EscherNfpCanonicalPair {
    EscherNfpMember first,second;
    int32_t relativeAngle=0;
    uint8_t relativeReflection=0;
    bool swapped=false;
};
struct EscherPreparedSource {
    EscherEnumerationDescriptor descriptor;
    std::vector<EscherSupportTable> supports;
};
void escherAppendSourceRecord(EscherPreparedSource &,const EscherPatternRecord &,
    const ConvolutionIntegerPolygon &normalOuterContours); //78afd0
EscherPreparedSource escherSourceDescriptor(std::span<const EscherPatternRecord>); //78afd0 active-record assembly
EscherNfpMember escherNfpMember(const EscherPatternRecord &); //active normal/alternate owner selection in78b350
EscherNfpCanonicalPair escherCanonicalNfpPair(const EscherNfpMember &,const EscherNfpMember &); //862830 geometry key
void escherTransformNfp(ConvolutionIntegerPolygon &,int32_t angle,uint8_t reflected); //863900 /8639f0
ConvolutionIntegerPolygon escherPairNfp(const EscherNfpMember &,const EscherNfpMember &); //861f80 geometry; cache storage omitted
ConvolutionIntegerPolygon escherPatternSelfNfp(std::span<const EscherNfpMember>); //78b350
ConvolutionIntegerPolygon escherPatternSelfNfp(std::span<const EscherPatternRecord>);
EscherEnumerationDescriptor escherPrimitiveDescriptor(EscherEnumerationDescriptor source,
    std::span<const EscherSupportTable>,const ConvolutionIntegerPolygon &selfNfp,
    int32_t quantizedRestriction,int64_t &minimumCellArea,bool adaptivePeriod=false); //77ed60
}
