#pragma once
#include "recovered_context.hpp"
namespace study::recovered {
// 5ce6d0 invokes386550 with rotation18000000 for the reverse problem.
// This rewrites allowed orientation records; it does not reflect Part geometry.
std::vector<PlacementOrientation> reversePartOrientationCache(
    std::span<const PlacementOrientation>,bool symmetryX,bool symmetryY); //386550
struct CurrentPartModule {
    uintptr_t part=0;
    PlacementOrientation orientation;
    uintptr_t module=0;
};
std::optional<uintptr_t> currentPartModule(std::span<const CurrentPartModule>,
    uintptr_t part,PlacementOrientation requested,bool symmetryX,bool symmetryY); //54ec10
}
