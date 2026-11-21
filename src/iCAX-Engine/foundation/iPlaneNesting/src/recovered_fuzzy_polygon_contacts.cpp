#include "recovered_fuzzy_polygon_contacts.hpp"
#include <bit>
#include <list>
#include <numeric>
#include <stdexcept>
namespace study::recovered {
namespace {
FuzzyTopologySegmentIndex locate(const FuzzyTopology &topology,const FuzzyGeometryRange &range){const auto found=topology.segmentIndex.at(range.segments->at(size_t(range.begin)));if(!found.forward)throw std::invalid_argument("Fuzzy contact range is not forward indexed");return found;}
EscherIntegerPoint sourcePoint(const FuzzyTopology &topology,int32_t path,int32_t position){const auto &edges=*topology.paths.at(size_t(path));return size_t(position)==edges.size()?edges.back()[1]:edges.at(size_t(position))[0];}
uint64_t pointHash(EscherIntegerPoint p){const auto mix=[](uint64_t v){v=(v^(v>>32))*0xe9846af9b1a615dULL;v=(v^(v>>32))*0xe9846af9b1a615dULL;return v^(v>>28);};return mix(mix(uint64_t(p[0])+0x9e3779b9)+0x9e3779b9+uint64_t(p[1]));}
std::vector<EscherIntegerPoint> pointOrder(std::span<const EscherIntegerPoint> points){std::list<EscherIntegerPoint> order;using Iterator=std::list<EscherIntegerPoint>::iterator;std::set<EscherIntegerPoint> present;std::vector<Iterator> buckets(8,order.end());
    const auto rehash=[&](size_t count){buckets.assign(count,order.end());for(auto i=order.begin();i!=order.end();){const auto next=std::next(i);auto &first=buckets[pointHash(*i)&(count-1)];if(first!=order.end())order.splice(first,order,i);first=i;i=next;}};
    for(auto point:points){if(!present.insert(point).second)continue;if(buckets.size()<present.size()){auto capacity=std::max<size_t>(8,present.size());if(buckets.size()<512)capacity=std::max(capacity,buckets.size()*8);rehash(std::bit_ceil(capacity));}auto &first=buckets[pointHash(point)&(buckets.size()-1)];first=order.insert(first,point);}return {order.begin(),order.end()};
}
std::vector<FuzzyTopologySummitLocation> fallback(const FuzzyTopology &topology,std::span<const FuzzySegmentContactQuery> queries){std::vector<size_t> order(queries.size());std::iota(order.begin(),order.end(),0);std::sort(order.begin(),order.end(),[&](size_t a,size_t b){return queries[a].region<queries[b].region;});std::vector<FuzzySegmentContactQuery> sorted;for(auto n:order)sorted.push_back(queries[n]);const auto locations=fuzzySegmentContactLocations(topology,sorted);if(locations.size()!=queries.size())throw std::invalid_argument("Fuzzy fallback queries require distinct region ids");std::vector<FuzzyTopologySummitLocation> result(queries.size());for(size_t n=0;n<order.size();++n)result[order[n]]=locations[n];return result;}
}
EscherIntegerPoint fuzzyGeometryRangePoint(const FuzzyRegionGeometry &geometry,int32_t range,int32_t offset){const auto &source=geometry.at(size_t(range));const auto position=size_t(source.begin+offset);return position==source.segments->size()?source.segments->back()[1]:source.segments->at(position)[0];}
ConvolutionIntegerSegment fuzzyGeometryPrecedingEdge(const FuzzyRegionGeometry &geometry,int32_t range,int32_t offset){if(offset>0){const auto &source=geometry.at(size_t(range));return source.segments->at(size_t(source.begin+offset-1));}const auto &source=geometry.at(range>0?size_t(range-1):geometry.size()-1);return source.segments->at(size_t(source.end-1));}
std::vector<FuzzyRangeContactLocation> fuzzyDirectContactLocations(const FuzzyTopology &topology){
    std::vector<std::vector<int32_t>> labels;std::vector<std::set<std::array<int32_t,2>>> painted(topology.paths.size());for(const auto &path:topology.paths)labels.emplace_back(path->size(),-1);std::vector<FuzzyRangeContactLocation> result;
    for(size_t n=0;n<topology.regions.size();++n){const auto &geometry=topology.regions[n].geometry;FuzzyRangeContactLocation selected;const auto &lastRange=geometry.back();const auto last=topology.segmentIndex.at(lastRange.segments->at(size_t(lastRange.end-1)));bool previousPainted=labels.at(size_t(last.path)).at(size_t(last.position))>=0;
        for(size_t r=0;r<geometry.size();++r){const auto &range=geometry[r];const auto location=locate(topology,range);const auto end=location.position+(range.end-range.begin);const auto gaps=fuzzyUnpaintedIntervals(painted.at(size_t(location.path)),location.position,end);
            if(!selected.present&&(gaps.size()>1||(gaps.size()==1&&(gaps.back()[0]>location.position||previousPainted))))selected={int32_t(r),gaps.back()[0]-location.position,true};
            previousPainted=gaps.empty()||gaps.back()[1]<end;for(auto gap:gaps)for(int32_t k=gap[0];k<gap[1];++k)labels.at(size_t(location.path)).at(size_t(k))=int32_t(n);
        }result.push_back(selected);
    }return result;
}
std::vector<FuzzyRangeContactLocation> fuzzyVertexContactLocations(const FuzzyTopology &topology,std::span<const int32_t> eligible){
    const std::set<int32_t> requested(eligible.begin(),eligible.end());std::vector<std::set<std::array<int32_t,2>>> painted(topology.paths.size());std::set<EscherIntegerPoint> points;std::vector<FuzzyRangeContactLocation> result;
    for(size_t n=0;n<topology.regions.size();++n){const bool search=requested.contains(int32_t(n));FuzzyRangeContactLocation selected;const auto &geometry=topology.regions[n].geometry;
        for(size_t r=0;r<geometry.size();++r){const auto &range=geometry[r];const auto location=locate(topology,range);const auto end=location.position+(range.end-range.begin);const auto gaps=fuzzyUnpaintedIntervals(painted.at(size_t(location.path)),location.position,end);
            if(search&&!selected.present)for(int32_t k=location.position;k<=end;++k)if(points.contains(sourcePoint(topology,location.path,k))){selected={int32_t(r),k-location.position,true};break;}
            for(auto gap:gaps)for(int32_t k=gap[0];k<gap[1];++k){const auto &edge=topology.paths.at(size_t(location.path))->at(size_t(k));points.insert(edge[0]);points.insert(edge[1]);}
        }if(search)result.push_back(selected);
    }return result;
}
std::vector<FuzzyTopologySummitLocation> fuzzySegmentContactLocations(const FuzzyTopology &topology,
    std::span<const FuzzySegmentContactQuery> queries){
    std::map<int32_t,ConvolutionIntegerSegment> requested;for(auto query:queries)requested[query.region]=query.segment;
    std::vector<std::vector<FuzzyTopologySummitLocation>> labels;for(const auto &path:topology.paths)labels.emplace_back(path->size());std::vector<FuzzyTopologySummitLocation> result;
    for(size_t n=0;n<topology.regions.size();++n){if(auto query=requested.find(int32_t(n));query!=requested.end()){const auto location=topology.segmentIndex.at(query->second);result.push_back(labels.at(size_t(location.path)).at(size_t(location.position)));}
        const auto &geometry=topology.regions[n].geometry;for(size_t r=0;r<geometry.size();++r){const auto &range=geometry[r];const auto location=locate(topology,range);for(int32_t k=0;k<range.end-range.begin;++k)labels.at(size_t(location.path)).at(size_t(location.position+k))={int32_t(n),int32_t(r),k};}
    }return result;
}
FuzzyTopologyContactLocations fuzzyTopologyContactLocations(const FuzzyTopology &topology){
    FuzzyTopologyContactLocations result;const auto direct=fuzzyDirectContactLocations(topology);std::vector<int32_t> unresolved;std::vector<FuzzySegmentContactQuery> queries;
    for(size_t n=0;n<direct.size();++n)if(!direct[n].present)unresolved.push_back(int32_t(n));else queries.push_back({fuzzyGeometryPrecedingEdge(topology.regions[n].geometry,direct[n].range,direct[n].offset),int32_t(n)});
    const auto targets=fuzzySegmentContactLocations(topology,queries);for(size_t n=0;n<queries.size();++n)result.direct.push_back({queries[n].region,targets[n].region,true,{}});
    const auto vertices=fuzzyVertexContactLocations(topology,unresolved);for(size_t n=0;n<unresolved.size();++n)if(vertices[n].present)result.sharedVertices.push_back({unresolved[n],vertices[n].range,vertices[n].offset});return result;
}
std::vector<FuzzyTopologyContact> fuzzyTopologyContacts(const FuzzyTopology &topology,const FuzzyTopologySummits &summits){
    const auto initial=fuzzyTopologyContactLocations(topology);std::vector<FuzzyRegionGeometry> geometries;for(const auto &region:topology.regions)geometries.push_back(region.geometry);const auto labels=fuzzyDirectedRegionLabels(topology,geometries);
    std::map<EscherIntegerPoint,int32_t> pointLabels;for(auto [edge,region]:labels.labels)for(auto point:edge){auto [entry,inserted]=pointLabels.try_emplace(point,region);if(!inserted)entry->second=std::min(entry->second,region);}
    std::map<EscherIntegerPoint,std::vector<FuzzyAngularQuery>> groups;std::vector<EscherIntegerPoint> pointKeys;
    for(auto location:initial.sharedVertices){if(pointLabels.at(summits.points.at(size_t(location.region)))<location.region)location=summits.locations.at(size_t(location.region));const auto &geometry=topology.regions.at(size_t(location.region)).geometry;const auto point=fuzzyGeometryRangePoint(geometry,location.range,location.offset);auto edge=fuzzyGeometryPrecedingEdge(geometry,location.range,location.offset);std::swap(edge[0],edge[1]);if(!groups.contains(point))pointKeys.push_back(point);groups[point].push_back({location.region,edge});}
    std::vector<FuzzyTopologyContact> result=initial.direct;std::vector<FuzzySegmentContactQuery> queries;std::vector<size_t> positions;
    for(auto point:pointOrder(pointKeys)){const auto &edges=topology.incidentEdges.at(point);std::vector<int32_t> levels;for(auto edge:edges){if(!labels.labels.contains(edge))std::swap(edge[0],edge[1]);levels.push_back(labels.labels.at(edge));}const auto &probes=groups.at(point);const auto selected=fuzzyAngularRelatives(edges,levels,probes);
        for(size_t n=0;n<probes.size();++n){const auto edge=edges.at(size_t(selected[n]));positions.push_back(result.size());result.push_back({probes[n].threshold,levels[size_t(selected[n])],false,edge});queries.push_back({edge,probes[n].threshold});}}
    const auto relatives=fallback(topology,queries);for(size_t n=0;n<positions.size();++n)result[positions[n]].relative=relatives[n].region;return result;
}
}
