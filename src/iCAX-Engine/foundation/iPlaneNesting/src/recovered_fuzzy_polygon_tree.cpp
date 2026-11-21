#include "recovered_fuzzy_polygon_tree.hpp"
#include <functional>
#include <stdexcept>
namespace study::recovered {
std::vector<std::array<int32_t,2>> fuzzyUnpaintedIntervals(
    std::set<std::array<int32_t,2>> &painted,int32_t begin,int32_t end){
    std::vector<std::array<int32_t,2>> result;
    for(auto interval:painted){if(end<=begin)break;if(begin<interval[0]){
            if(end<=interval[0])break;result.push_back({begin,interval[0]});begin=interval[1];
        }else begin=std::max(begin,interval[1]);}
    if(begin<end)result.push_back({begin,end});for(auto interval:result)painted.insert(interval);return result;
}
FuzzyDirectedRegionLabels fuzzyDirectedRegionLabels(const FuzzyTopology &topology,
    std::span<const FuzzyRegionGeometry> geometries){
    std::vector<std::vector<int32_t>> labels;std::vector<std::set<std::array<int32_t,2>>> painted(topology.paths.size());
    for(const auto &path:topology.paths)labels.emplace_back(path->size(),-1);
    for(size_t n=0;n<geometries.size();++n)for(const auto &range:geometries[n]){
        const auto &edge=range.segments->at(size_t(range.begin));const auto location=topology.segmentIndex.at(edge);
        if(!location.forward)throw std::invalid_argument("Fuzzy region range is not forward indexed");
        for(auto interval:fuzzyUnpaintedIntervals(painted.at(size_t(location.path)),location.position,
            location.position+(range.end-range.begin)))for(int32_t k=interval[0];k<interval[1];++k)labels.at(size_t(location.path)).at(size_t(k))=int32_t(n);
    }
    FuzzyDirectedRegionLabels result;std::vector<ConvolutionIntegerSegment> edges;
    for(size_t n=0;n<topology.paths.size();++n)for(size_t k=0;k<topology.paths[n]->size();++k){const auto edge=(*topology.paths[n])[k];edges.push_back(edge);result.labels[edge]=labels[n][k];}
    result.order=convolutionIntegerEdgeOrder(edges);return result;
}
FuzzyEdgeRegionLabels fuzzyEdgeRegionLabels(const FuzzyTopology &topology){
    std::vector<FuzzyRegionGeometry> geometries;for(const auto &region:topology.regions)geometries.push_back(region.geometry);
    const auto forward=fuzzyDirectedRegionLabels(topology,geometries);std::reverse(geometries.begin(),geometries.end());
    const auto reverse=fuzzyDirectedRegionLabels(topology,geometries);FuzzyEdgeRegionLabels result;result.order=convolutionIntegerEdgeOrder(forward.order);
    for(auto edge:forward.order)result.labels[edge]={forward.labels.at(edge),int32_t(geometries.size())-reverse.labels.at(edge)-1};return result;
}
std::map<int32_t,FuzzyFamilyRelation> fuzzyInitialFamilyRelations(
    const FuzzyTopology &topology,const FuzzyEdgeRegionLabels &labels){
    std::map<int32_t,FuzzyFamilyRelation> result;
    for(size_t n=0;n<topology.regions.size();++n){const auto &region=topology.regions[n];if(!region.relativePresent)continue;
        if(region.relativeDirect){result[int32_t(n)]={region.relativeRegion,true};continue;}
        auto edge=region.relativeSegment;const auto location=topology.segmentIndex.at(edge);
        if(!location.forward)std::swap(edge[0],edge[1]);
        const auto relative=location.forward?labels.labels.at(edge)[0]:region.relativeRegion;
        if(relative==int32_t(n))throw std::invalid_argument("Fuzzy reflexive family relationship");
        result[int32_t(n)]={relative,!location.forward};
    }return result;
}
std::map<int32_t,FuzzyFamilyRelation> fuzzyMergeFamilyRelations(
    const std::map<int32_t,FuzzyFamilyRelation> &relations,std::span<const EscherIntegerPoint> summits){
    const auto count=summits.size();std::vector<std::vector<int32_t>> graph(count);std::vector<int32_t> pending;
    std::map<int32_t,FuzzyFamilyRelation> result;
    const auto valid=[&](int32_t id){if(id<0||size_t(id)>=count)throw std::invalid_argument("Fuzzy relation merge index");};
    for(auto [id,relation]:relations){valid(id);valid(relation.relative);
        if(relation.contained){pending.push_back(id);result.emplace(id,relation);}
        else {graph[size_t(id)].push_back(relation.relative);graph[size_t(relation.relative)].push_back(id);}}
    std::vector<bool> blocked(count),remaining(count,true);
    while(!pending.empty()){const auto id=pending.back();pending.pop_back();if(blocked[size_t(id)])continue;
        blocked[size_t(id)]=true;pending.insert(pending.end(),graph[size_t(id)].begin(),graph[size_t(id)].end());}
    for(size_t n=0;n<count;++n)if(remaining[n]&&!blocked[n]){
        std::vector<int32_t> component;pending={int32_t(n)};
        while(!pending.empty()){const auto id=pending.back();pending.pop_back();if(!remaining[size_t(id)])continue;
            remaining[size_t(id)]=false;component.push_back(id);pending.insert(pending.end(),graph[size_t(id)].begin(),graph[size_t(id)].end());}
        int32_t representative=component.front();for(auto id:component)if(summits[size_t(representative)][1]<summits[size_t(id)][1]||
            (summits[size_t(representative)][1]==summits[size_t(id)][1]&&id<representative))representative=id;
        for(auto id:component)if(id!=representative)result[id]={representative,false};
    }
    for(auto [id,relation]:relations)if(!relation.contained&&blocked[size_t(id)])result[id]=relation;
    return result;
}
FuzzyFamilyGraph fuzzyFamilyGraph(std::span<const FuzzyFamilyRelation> relations){
    const auto size=relations.size();std::vector<std::vector<int32_t>> nested(size),siblings(size);
    std::vector<int32_t> roots;
    for(size_t n=0;n<size;++n){const auto relation=relations[n];if(relation.relative==-1)roots.push_back(int32_t(n));
        else {if(relation.relative<0||size_t(relation.relative)>=size)throw std::invalid_argument("Fuzzy family relative index");
            (relation.contained?nested:siblings)[size_t(relation.relative)].push_back(int32_t(n));}}
    FuzzyFamilyGraph result;result.children.resize(size);std::vector<bool> visiting(size);
    const std::function<void(int32_t,int32_t)> visit=[&](int32_t node,int32_t parent){
        if(visiting[size_t(node)])throw std::invalid_argument("Fuzzy family relation cycle");visiting[size_t(node)]=true;
        if(parent==-1)result.roots.push_back(node);else result.children[size_t(parent)].push_back(node);
        for(auto child:siblings[size_t(node)])visit(child,parent);
        for(auto child:nested[size_t(node)])visit(child,node);
        visiting[size_t(node)]=false;
    };
    for(auto root:roots){result.roots.push_back(root);for(auto child:siblings[size_t(root)])visit(child,-1);
        for(auto child:nested[size_t(root)])visit(child,root);}
    return result;
}
std::shared_ptr<FuzzyPolygonRegion> fuzzyAssembleRegionTree(
    std::span<const FuzzyRegionGeometry> geometries,std::span<const double> weights,
    std::span<const FuzzyFamilyRelation> relations){
    if(geometries.size()!=weights.size()||geometries.size()!=relations.size())throw std::invalid_argument("Fuzzy family input size");
    const auto graph=fuzzyFamilyGraph(relations);if(graph.roots.empty())throw std::invalid_argument("Fuzzy family has no root");
    const std::function<std::shared_ptr<FuzzyPolygonRegion>(int32_t)> node=[&](int32_t id){
        auto result=std::make_shared<FuzzyPolygonRegion>();result->geometryPresent=true;result->weight=weights[size_t(id)];
        std::vector<FuzzyRegionGeometry> childGeometry;
        for(auto child:graph.children[size_t(id)]){result->children.push_back(node(child));childGeometry.push_back(geometries[size_t(child)]);}
        result->boundary=fuzzyRegionBoundary(geometries[size_t(id)],childGeometry);return result;
    };
    if(graph.roots.size()==1)return node(graph.roots.front());
    auto result=std::make_shared<FuzzyPolygonRegion>();for(auto root:graph.roots)result->children.push_back(node(root));return result;
}
}
