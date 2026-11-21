#pragma once
#include "core.hpp"
#include <span>
namespace study::recovered {
struct AmoRasterResult {
    int32_t binCount=0,firstOffset=0;
    // Flattened [lowY,highY] pairs and binCount+1 prefix indices, as 7d7be0.
    std::vector<std::array<double,2>> intervals;
    std::vector<int32_t> indices;
};
// 7d7be0 -> 7d6ef0; input rings omit any repeated final closing vertex.
AmoRasterResult amoRasterStrips(std::span<const std::vector<Point>> contours,
    Point reference,double minimumX,double pitch,double mergeTolerance,
    double xTolerance,double interpolationWeight);
}
