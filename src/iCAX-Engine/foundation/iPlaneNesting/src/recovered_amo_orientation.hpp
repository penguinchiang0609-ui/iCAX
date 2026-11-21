#pragma once
#include "recovered_amo_tiling.hpp"
#include "recovered_reverse.hpp"
namespace study::recovered {
struct AmoTilingOrientationGeometry {
    uintptr_t part=0;
    std::array<PlacementOrientation,2> base;
    Box originalBounds{};
    bool symmetryX=false,symmetryY=false;
    int32_t required=-1;
    int32_t maximumX=0,maximumY=0;
    std::function<Box(PlacementOrientation)> variantBounds;
};
std::vector<TilingPattern> amoTilingConvertDescriptors(std::span<const AmoTilingDescriptor>,
    const AmoTilingOrientationGeometry &); //4992a0 numeric conversion / 49c6c0
}
