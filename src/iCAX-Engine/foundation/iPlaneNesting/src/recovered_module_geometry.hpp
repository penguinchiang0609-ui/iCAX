#pragma once
#include "recovered_reverse.hpp"
#include <optional>
#include <span>
namespace study::recovered {
//373880, shared by521160. Input coordinates stay double; only the separate
//module bbox offsets are quantized. Mirror preserves ring winding by reversal.
std::vector<Polygon> moduleTransformContours(std::span<const Polygon>,PlacementOrientation,
    Point reference={},bool reverseMirrored=true);
struct PreparedPartModuleGeometry {
    PlacementOrientation orientation;
    std::array<int32_t,2> standardLayers{0,1};
    std::vector<Polygon> primary,alternate;
    Box bounds{};
    std::array<int64_t,2> minimumOffset{},maximumOffset{};
    //3a47c0/3a4960: standard layer A/B mapping, reversed by slot3.
    std::span<const Polygon> layer(bool slot2,int32_t index)const;
};
//Geometry projection of3a41a0/3a3520 for ALREADY prepared Part+1a0/+200.
//It deliberately does not replace upstream simplification/normalization or
//invent protection geometry. Missing alternate uses the primary source.
PreparedPartModuleGeometry preparedPartModuleGeometry(std::span<const Polygon> primary,
    std::optional<std::span<const Polygon>> alternate,PlacementOrientation,
    std::array<int32_t,2> standardLayers={0,1});
}
