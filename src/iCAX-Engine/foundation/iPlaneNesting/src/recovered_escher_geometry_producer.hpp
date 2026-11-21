#pragma once
#include "recovered_escher_prepared.hpp"
namespace study::recovered {
struct EscherMemberPose {uint32_t reflected=0;int32_t angle=0;}; //only the low reflected byte is semantic; upper three bytes are original ABI padding
static_assert(sizeof(EscherMemberPose)==8);
EscherEnumerationDescriptor escherRestrictedDescriptor(EscherEnumerationDescriptor,
    std::span<const EscherSupportTable>,const ConvolutionIntegerPolygon &,int32_t candidateLimit,
    int64_t &minimumCellArea); //77f400 ->77f590; original fixed4-degree configuration
EscherPeriodCandidate escherTransformPeriodicCandidate(const EscherPeriodCandidate &,
    std::span<const EscherSupportTable>,int32_t first,int32_t second); //78cbc0
void escherAugmentGeometryCatalog(std::span<EscherEnumerationDescriptor>,
    std::span<const EscherPreparedSource>,int64_t minimumCellArea,
    std::span<const std::array<int32_t,2>> transforms,double areaRatio=1.025); //786c20
std::vector<EscherPreparedSource> escherPairGeometrySources(const EscherIntegerShapeRecord &,
    const EscherIntegerShapeRecord &,EscherMemberPose,EscherMemberPose); //7951b0 ->794f80
struct EscherGeometryCatalog {
    std::vector<EscherEnumerationDescriptor> ordinary,restricted;
    int64_t minimumCellArea=INT64_MAX;
};
// The owner's address must remain stable while any returned descriptor is used.
EscherGeometryCatalog escherProduceGeometryCatalog(const EscherIntegerShapeRecord &,uint32_t candidateLimit); //7861b0
}
