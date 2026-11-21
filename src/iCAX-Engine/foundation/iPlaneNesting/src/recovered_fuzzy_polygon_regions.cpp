#include "recovered_fuzzy_polygon_regions.hpp"
#include "recovered_convolution_index.hpp"
#include <bit>
namespace study::recovered {
namespace {
int64_t negate(int64_t value){return std::bit_cast<int64_t>(uint64_t(0)-uint64_t(value));}
int64_t add(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
bool shortDifference(int64_t a,int64_t b){const auto value=std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));return (value<0?negate(value):value)<2;}
}
std::vector<FuzzyFamilyRelation> fuzzyCompleteFamilyRelations(const FuzzyTopology &topology,
    const FuzzyEdgeRegionLabels &labels,const std::map<int32_t,FuzzyFamilyRelation> &known){
    ConvolutionEdgeIndex index;for(auto edge:labels.order)index.insert(edge);
    std::vector<FuzzyFamilyRelation> result(topology.regions.size(),FuzzyFamilyRelation{-1,true});std::map<EscherIntegerPoint,FuzzyFamilyRelation> vertexCache;
    for(size_t n=0;n<topology.regions.size();++n){if(auto found=known.find(int32_t(n));found!=known.end()){result[n]=found->second;continue;}
        const auto point=topology.regions[n].summit;const ConvolutionIntegerSegment ray{point,{point[0],add(point[1],1)}};std::vector<ConvolutionHierarchyRayHit> hits;
        for(auto edge:index.query(ray)){ConvolutionHierarchyRayHit hit;hit.segment=edge;convolutionClassifyHierarchyRay({0,1},{0,-1},{negate(point[0]),negate(point[1])},hit,true);if(hit.distanceSquared>=0.)hits.push_back(hit);}
        convolutionSortHierarchyRayHits(hits);std::reverse(hits.begin(),hits.end());
        for(const auto &hit:hits)if(hit.crossing!=0&&hit.distanceSquared!=0.){
            const auto pair=labels.labels.at(hit.segment);
            if(uint32_t(hit.crossing)*uint32_t(hit.crossing)!=4U){auto [entry,inserted]=vertexCache.try_emplace(hit.point);if(inserted)entry->second=fuzzyVertexRelative(topology,labels,hit.point);result[n]=entry->second;}
            else result[n]={pair[hit.crossing==2?1:0],hit.crossing==2};
            break;
        }
    }return result;
}
std::shared_ptr<FuzzyPolygonRegion> fuzzyBuildRegionTree(const FuzzyTopology &topology){
    if(topology.regions.empty())return std::make_shared<FuzzyPolygonRegion>();
    const auto labels=fuzzyEdgeRegionLabels(topology);const auto initial=fuzzyInitialFamilyRelations(topology,labels);std::vector<EscherIntegerPoint> summits;std::vector<FuzzyRegionGeometry> geometry;std::vector<double> weights;
    for(const auto &region:topology.regions){summits.push_back(region.summit);geometry.push_back(region.geometry);weights.push_back(region.weight);}
    const auto merged=fuzzyMergeFamilyRelations(initial,summits);return fuzzyAssembleRegionTree(geometry,weights,fuzzyCompleteFamilyRelations(topology,labels,merged));
}
FuzzyPolygonPrepared fuzzyPrepareIndexedPolygon(const ConvolutionIntegerPolygon &polygons,
    std::span<const double> weights,std::shared_ptr<FuzzyPolygonRegion> root){
    const auto build=fuzzyBuildWeightedSegments(polygons,weights);
    return fuzzyPrepareIndexedPolygon(polygons,weights,std::move(root),build);
}
FuzzyPolygonPrepared fuzzyPrepareIndexedPolygon(const ConvolutionIntegerPolygon &polygons,
    std::span<const double> weights,std::shared_ptr<FuzzyPolygonRegion> root,const FuzzyWeightedBuild &build){
    FuzzyPolygonPrepared result;result.polygons=polygons;result.weights.assign(weights.begin(),weights.end());result.root=std::move(root);
    result.segmentSources=build.sources;
    for(size_t n=0;n<build.segments.size();++n){const auto edge=build.segments[n].segment;result.incidentSegments[edge[0]].push_back(int32_t(n));result.incidentSegments[edge[1]].push_back(int32_t(n));
        if(shortDifference(edge[0][0],edge[1][0])&&shortDifference(edge[0][1],edge[1][1])){result.adjacency[edge[0]].push_back(edge[1]);result.adjacency[edge[1]].push_back(edge[0]);}}
    return result;
}
}
