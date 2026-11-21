#pragma once
#include "recovered_placement.hpp"
namespace study::recovered {
struct RectangleFallbackInput {
    int32_t components7c = 1, required254 = 0;
    bool optional3c1 = false, specialPreNest = false, forceB0 = false;
    double remaining = 0, minimumX = 0, minimumY = 0, maximumX = 0, maximumY = 0, area68 = 0;
};
// Branches in 53df40; the fallback is not the repeated-part tiling solver.
bool rectangleUsePrimitiveFallback(const RectangleFallbackInput &);
bool rectangleAdmitPrimitiveFallback(const RectangleFallbackInput &);
struct PrimitiveTilingInput {
    PlacementOrientation orientation;
    bool symmetryX = false, symmetryY = false, variantPresent = false, protectedGeometry200 = false;
    double gap210 = 0;
    // Variant floating bounding box 340/348/350/358.
    std::array<double, 4> bounds{};
    int64_t left3e8 = 0, right3f8 = 0;
    int32_t bottom3f0 = 0, top400 = 0;
};
struct PrimitiveTilingPlan {
    PlacementOrientation orientation;
    Point u{}, v{};
    int32_t style = 1;
};
// 4a8750: canonical orientation, variant-cache presence and actual lattice
// vectors. Variant generation and the 49b880 object builder are separate.
std::optional<PrimitiveTilingPlan> primitiveTilingPlan(const PrimitiveTilingInput &);
}
