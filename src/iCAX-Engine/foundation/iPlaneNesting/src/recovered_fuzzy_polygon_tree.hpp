#pragma once
#include "recovered_fuzzy_polygon_build.hpp"
#include <set>
namespace study::recovered {
struct FuzzyFamilyRelation {
    int32_t relative=-1;
    bool contained=true;
    bool operator==(const FuzzyFamilyRelation &)const=default;
};
struct FuzzyFamilyGraph {
    std::vector<std::vector<int32_t>> children;
    std::vector<int32_t> roots;
    bool operator==(const FuzzyFamilyGraph &)const=default;
};
FuzzyFamilyGraph fuzzyFamilyGraph(std::span<const FuzzyFamilyRelation>); //826a50/824ad0
std::map<int32_t,FuzzyFamilyRelation> fuzzyMergeFamilyRelations(
    const std::map<int32_t,FuzzyFamilyRelation> &,std::span<const EscherIntegerPoint>); //831760
std::vector<std::array<int32_t,2>> fuzzyUnpaintedIntervals(
    std::set<std::array<int32_t,2>> &,int32_t begin,int32_t end); //842ff0
struct FuzzyDirectedRegionLabels {
    std::vector<ConvolutionIntegerSegment> order;
    std::map<ConvolutionIntegerSegment,int32_t> labels;
};
FuzzyDirectedRegionLabels fuzzyDirectedRegionLabels(const FuzzyTopology &,
    std::span<const FuzzyRegionGeometry>); //844830
struct FuzzyEdgeRegionLabels {
    std::vector<ConvolutionIntegerSegment> order;
    std::map<ConvolutionIntegerSegment,std::array<int32_t,2>> labels;
};
FuzzyEdgeRegionLabels fuzzyEdgeRegionLabels(const FuzzyTopology &); //832530
std::map<int32_t,FuzzyFamilyRelation> fuzzyInitialFamilyRelations(
    const FuzzyTopology &,const FuzzyEdgeRegionLabels &); //831200
std::shared_ptr<FuzzyPolygonRegion> fuzzyAssembleRegionTree(
    std::span<const FuzzyRegionGeometry>,std::span<const double>,
    std::span<const FuzzyFamilyRelation>); //832e30/832c20/832430
}
