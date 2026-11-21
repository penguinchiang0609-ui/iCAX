#include "recovered_fuzzy_polygon_construct.hpp"
#include "recovered_fuzzy_polygon_topology.hpp"
#include "recovered_fuzzy_polygon_contacts.hpp"
#include "recovered_fuzzy_polygon_iterator.hpp"
#include <bit>
namespace study::recovered {
FuzzyTopology fuzzyReconstituteTopology(std::span<const FuzzyWeightedSegment> segments){
    FuzzyReconstitutor state(segments);FuzzyPathIterator iterator(state);FuzzyTopology result;
    for(const auto &[point,destinations]:state.adjacency())
        for(const auto destination:destinations)result.incidentEdges[point].push_back({point,destination});
    std::vector<int32_t> components;int32_t component=0;
    while(state.pendingCount()!=0||!state.empty()){
        ConvolutionIntegerSegment next{};
        if(!iterator.empty()){
            const auto previous=iterator.back();
            if(state.hasHot(previous[1]))next=state.clockwiseHot(previous);
            else next={previous[1],fuzzyTopologyContinuation(previous,state.neighbors(previous[1]))};
        }else{next=state.nextSeed();component=std::bit_cast<int32_t>(uint32_t(component)+1U);}
        std::optional<FuzzyRegionGeometry> closed;
        if(const auto source=state.lookup(next)){
            const auto end=state.rangeEnd(source->path,source->position);
            closed=iterator.appendRange(state.paths().at(size_t(source->path)),source->position,end);
        }else closed=iterator.append(next);
        if(closed){FuzzyTopologyRegion region;region.geometry=std::move(*closed);region.weight=state.withdraw(region.geometry);result.regions.push_back(std::move(region));components.push_back(component);}
    }
    result.paths=state.paths();
    for(const auto &path:result.paths)for(auto edge:*path)for(unsigned reverse=0;reverse<2;++reverse){
        if(reverse)std::swap(edge[0],edge[1]);
        const auto index=state.lookup(edge);require(bool(index),PN2S_INVALID,"Completed fuzzy path has no segment index");result.segmentIndex[edge]=*index;
    }
    auto summits=fuzzyTopologySummits(result);const auto order=fuzzyTopologyPathOrder(components,summits.points);
    std::vector<FuzzyTopologyRegion> sorted;FuzzyTopologySummits sortedSummits;
    for(const auto source:order){const auto n=size_t(source);auto region=std::move(result.regions[n]);region.summit=summits.points[n];sorted.push_back(std::move(region));sortedSummits.points.push_back(summits.points[n]);auto location=summits.locations[n];location.region=int32_t(sortedSummits.locations.size());sortedSummits.locations.push_back(location);}
    result.regions=std::move(sorted);
    for(const auto &contact:fuzzyTopologyContacts(result,sortedSummits)){
        auto &region=result.regions.at(size_t(contact.region));region.relativePresent=true;region.relativeRegion=contact.relative;region.relativeDirect=contact.direct;region.relativeSegment=contact.segment;
    }
    return result;
}
FuzzyPolygonPrepared fuzzyPreparePolygon(const ConvolutionIntegerPolygon &polygons,std::span<const double> weights){
    const auto weighted=fuzzyBuildWeightedSegments(polygons,weights);
    const auto topology=fuzzyReconstituteTopology(weighted.segments);
    return fuzzyPrepareIndexedPolygon(polygons,weights,fuzzyBuildRegionTree(topology),weighted);
}
}
