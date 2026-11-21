#include "recovered_fuzzy_polygon_build.hpp"
#include <bit>
#include <cmath>
#include <list>
#include <set>
#include <stdexcept>
namespace study::recovered {
void fuzzyNormalizeSegmentWeights(std::vector<ConvolutionIntegerSegment> &segments,
    std::vector<double> &weights){
    if(segments.size()!=weights.size())throw std::invalid_argument("Fuzzy segment weights size");
    for(size_t n=0;n<segments.size();++n)if(segments[n][0]<segments[n][1]){
        std::swap(segments[n][0],segments[n][1]);weights[n]*=-1.;
    }
}
FuzzyMergeInput fuzzyPrepareMerge(std::vector<ConvolutionIntegerSegment> segments,
    std::vector<double> weights){
    fuzzyNormalizeSegmentWeights(segments,weights);
    const auto split=convolutionSplitIntegerSegments(segments);FuzzyMergeInput result;
    result.segments.reserve(split.size());result.weights.reserve(split.size());result.sources.reserve(split.size());
    for(const auto &part:split){const auto index=size_t(part.source);auto edge=part.segment;
        const auto &source=segments[index];
        const auto dx=uint64_t(edge[1][0])-uint64_t(edge[0][0]),dy=uint64_t(edge[1][1])-uint64_t(edge[0][1]);
        const auto sx=uint64_t(source[1][0])-uint64_t(source[0][0]),sy=uint64_t(source[1][1])-uint64_t(source[0][1]);
        const bool aligned=std::bit_cast<int64_t>(dx*sx+dy*sy)>=0;
        if(!aligned)std::swap(edge[0],edge[1]);
        result.segments.push_back(edge);result.weights.push_back(weights[index]);
        result.sources.push_back({int32_t(index),aligned});
    }return result;
}
FuzzyMergeOutput fuzzyMergeSegments(const FuzzyMergeInput &input){
    if(input.segments.size()!=input.weights.size()||input.segments.size()!=input.sources.size())
        throw std::invalid_argument("Fuzzy merge input size");
    struct Node {ConvolutionIntegerSegment segment;double weight=0.;std::vector<FuzzySegmentReference> sources;};
    std::list<Node> order;using Iterator=std::list<Node>::iterator;
    std::map<ConvolutionIntegerSegment,Iterator> indices;std::vector<Iterator> buckets(8,order.end());
    const auto rehash=[&](size_t capacity){buckets.assign(capacity,order.end());
        for(auto i=order.begin();i!=order.end();){const auto next=std::next(i);
            auto &first=buckets[convolutionIntegerEdgeHash(i->segment)&(capacity-1)];
            if(first!=order.end())order.splice(first,order,i);first=i;i=next;
        }
    };
    for(size_t n=0;n<input.segments.size();++n){const auto &edge=input.segments[n];auto found=indices.find(edge);Iterator node;
        if(found==indices.end()){
            const auto required=size_t(std::ceil(float(order.size()+1)));
            if(buckets.size()<required){size_t desired=std::max<size_t>(8,required);
                if(buckets.size()<512)desired=std::max(desired,buckets.size()*8);
                rehash(std::bit_ceil(desired));
            }
            auto &first=buckets[convolutionIntegerEdgeHash(edge)&(buckets.size()-1)];
            node=order.insert(first,Node{edge});first=node;indices.emplace(edge,node);
        }else node=found->second;
        node->weight+=input.weights[n];node->sources.push_back(input.sources[n]);
        if(node->weight==0.){
            auto &first=buckets[convolutionIntegerEdgeHash(edge)&(buckets.size()-1)];
            if(first==node){const auto next=std::next(node);first=next!=order.end()&&
                (convolutionIntegerEdgeHash(next->segment)&(buckets.size()-1))==
                (convolutionIntegerEdgeHash(edge)&(buckets.size()-1))?next:order.end();}
            indices.erase(edge);order.erase(node);
        }
    }
    FuzzyMergeOutput result;result.segments.reserve(order.size());result.sources.reserve(order.size());
    for(auto &node:order){result.segments.push_back({node.segment,node.weight});result.sources.push_back(std::move(node.sources));}
    return result;
}
namespace {
FuzzyWeightedBuild build(const ConvolutionIntegerPolygon &polygons,std::span<const double> weights,size_t first,size_t last){
    FuzzyWeightedBuild result;if(first==last)return result;
    if(last-first==1){const auto &ring=polygons[first];result.segments.reserve(ring.size());result.sources.reserve(ring.size());
        for(size_t n=0;n<ring.size();++n){result.segments.push_back({{ring[n],ring[(n+1)%ring.size()]},weights[first]});result.sources.push_back({int32_t(first)});}return result;
    }
    const auto middle=(first+last)/2;auto left=build(polygons,weights,first,middle),right=build(polygons,weights,middle,last);
    std::vector<ConvolutionIntegerSegment> edges;std::vector<double> values;
    edges.reserve(left.segments.size()+right.segments.size());values.reserve(edges.capacity());
    for(const auto &part:left.segments){edges.push_back(part.segment);values.push_back(part.weight);}
    for(const auto &part:right.segments){edges.push_back(part.segment);values.push_back(part.weight);}
    auto merged=fuzzyMergeSegments(fuzzyPrepareMerge(std::move(edges),std::move(values)));
    result.segments=std::move(merged.segments);result.sources.resize(merged.sources.size());
    for(size_t n=0;n<merged.sources.size();++n)for(auto ref:merged.sources[n]){
        const size_t index=size_t(ref.index);const auto &source=index<left.sources.size()?left.sources[index]:right.sources[index-left.sources.size()];
        result.sources[n].insert(result.sources[n].end(),source.begin(),source.end());
    }return result;
}
}
FuzzyWeightedBuild fuzzyBuildWeightedSegments(const ConvolutionIntegerPolygon &polygons,std::span<const double> weights){
    if(polygons.size()!=weights.size())throw std::invalid_argument("Fuzzy polygon weights size");
    return build(polygons,weights,0,polygons.size());
}
std::vector<ConvolutionIntegerSegment> fuzzyRegionBoundary(
    std::span<const FuzzyGeometryRange> geometry,std::span<const FuzzyRegionGeometry> children){
    const auto edges=[](std::span<const FuzzyGeometryRange> ranges){ConvolutionIntegerRing points;
        for(const auto &range:ranges)for(int32_t n=range.begin;n<range.end;++n)points.push_back((*range.segments)[size_t(n)][0]);
        std::vector<ConvolutionIntegerSegment> result;result.reserve(points.size());
        for(size_t n=0;n<points.size();++n)result.push_back({points[n],points[(n+1)%points.size()]});return result;
    };
    auto result=convolutionIntegerEdgeOrder(edges(geometry));std::set<ConvolutionIntegerSegment> removed;
    for(const auto &child:children)for(auto edge:edges(child))removed.insert(edge);
    std::erase_if(result,[&](const auto &edge){return removed.contains(edge);});return result;
}
}
