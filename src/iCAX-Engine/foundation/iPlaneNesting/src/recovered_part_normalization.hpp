#pragma once
#include "recovered_part_geometry.hpp"
#include "recovered_module_geometry.hpp"
namespace study::recovered {
//51db20 after51d9a0: exact double vertices, including across different rings.
bool partContoursHaveRepeatedVertices(std::span<const Polygon>);
//51dd90: original per-axis INT32_MAX scaling/quantization,84f950 split/edge
//duplicates, then the original double point population test. Finite rings
//with at least3 input points are the explicitly supported multi-ring domain.
bool partContoursAreClean(std::span<const Polygon>);
//51df30→51ed40 for actually clean multiple rings, including outer+holes.
//Non-clean multi-ring geometry needs original rich Boolean normalization and
//is explicitly rejected; it is never reduced to its first or exterior ring.
std::vector<Polygon> partNormalizeCleanContours(std::span<const Polygon>);
//Connects both normalized sources to already recovered PartModule projection.
PreparedPartModuleGeometry cleanPartModuleGeometry(std::span<const Polygon> primary,
    std::optional<std::span<const Polygon>> alternate,PlacementOrientation,
    std::array<int32_t,2> standardLayers={0,1});
}
