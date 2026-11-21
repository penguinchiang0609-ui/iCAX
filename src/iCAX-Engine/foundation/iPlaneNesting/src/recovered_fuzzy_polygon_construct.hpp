#pragma once
#include "recovered_fuzzy_polygon_regions.hpp"
namespace study::recovered {
FuzzyTopology fuzzyReconstituteTopology(std::span<const FuzzyWeightedSegment>); //82ef40
FuzzyPolygonPrepared fuzzyPreparePolygon(const ConvolutionIntegerPolygon &,
    std::span<const double>); //82a7c0 ->82d3d0 ->832760, actual complete construction
}
