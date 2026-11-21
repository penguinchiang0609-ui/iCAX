#pragma once
#include "recovered_escher.hpp"
#include "recovered_general_tiling.hpp"
#include "recovered_reverse.hpp"
namespace study::recovered {
struct EscherConvertedPose {PlacementOrientation orientation;Point position{};};
EscherConvertedPose escherProviderPose(const EscherPatternRecord &,Point sourceReference,double scale,
    Box originalBounds,bool symmetryX,bool symmetryY); //492f40 numerical conversion
struct EscherProviderConversionGeometry {
    uintptr_t part=0;double scale=1;Box originalBounds{};bool symmetryX=false,symmetryY=false;
    std::function<Point(uintptr_t)> sourceReference;
    // The actual Part variant's local bounds, as acquired by 54b3b0.
    std::function<Box(PlacementOrientation)> variantBounds;
};
void escherSortProviderPlacements(std::vector<TilingMember> &,size_t depth=SIZE_MAX); //4951e0 family
std::vector<TilingMember> escherConvertProviderLayout(std::span<const EscherPatternRecord>,
    const EscherProviderConversionGeometry &); //4939a0
}
