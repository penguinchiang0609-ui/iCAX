#pragma once
#include "recovered_fuzzy_polygon_vertex.hpp"
namespace study::recovered {
std::vector<FuzzyFamilyRelation> fuzzyCompleteFamilyRelations(const FuzzyTopology &,
    const FuzzyEdgeRegionLabels &,const std::map<int32_t,FuzzyFamilyRelation> &); //8302b0
std::shared_ptr<FuzzyPolygonRegion> fuzzyBuildRegionTree(const FuzzyTopology &); //832760 after 82ef40
FuzzyPolygonPrepared fuzzyPrepareIndexedPolygon(const ConvolutionIntegerPolygon &,
    std::span<const double>,std::shared_ptr<FuzzyPolygonRegion>); //82a7c0, explicit prepared region provider
FuzzyPolygonPrepared fuzzyPrepareIndexedPolygon(const ConvolutionIntegerPolygon &,
    std::span<const double>,std::shared_ptr<FuzzyPolygonRegion>,const FuzzyWeightedBuild &);
}
