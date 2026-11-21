#pragma once
#include "recovered_fuzzy_polygon_build.hpp"
namespace study::recovered {
EscherIntegerPoint fuzzyTopologyContinuation(const ConvolutionIntegerSegment &,
    std::span<const EscherIntegerPoint>); //8296b0, original wrapped primitive-vector tests
std::vector<int32_t> fuzzyTopologyPathOrder(std::span<const int32_t> components,
    std::span<const EscherIntegerPoint> summits); //82e650
struct FuzzyTopologySummitLocation {
    int32_t region=-1,range=-1,offset=-1;
};
struct FuzzyTopologySummits {
    std::vector<EscherIntegerPoint> points;
    std::vector<FuzzyTopologySummitLocation> locations;
};
FuzzyTopologySummits fuzzyTopologySummits(const FuzzyTopology &); //82ea10
}
