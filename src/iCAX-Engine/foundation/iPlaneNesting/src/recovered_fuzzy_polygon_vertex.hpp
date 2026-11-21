#pragma once
#include "recovered_fuzzy_polygon_tree.hpp"
namespace study::recovered {
struct FuzzyAngularQuery {int32_t threshold=1;ConvolutionIntegerSegment segment{};};
std::vector<int32_t> fuzzyDirectionIndices(std::span<const EscherIntegerPoint>); //8490f0
std::vector<int32_t> fuzzyAngularRelatives(std::span<const ConvolutionIntegerSegment>,
    std::span<const int32_t>,std::span<const FuzzyAngularQuery>); //82dc40
FuzzyFamilyRelation fuzzyVertexRelative(const FuzzyTopology &,
    const FuzzyEdgeRegionLabels &,EscherIntegerPoint); //82d660
}
