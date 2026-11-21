#include "recovered_fuzzy_polygon_vertex.hpp"
#include <algorithm>
#include <bit>
#include <set>
#include <stdexcept>
namespace study::recovered {
namespace {
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
struct DirectionRecord {EscherIntegerPoint direction{};int32_t index=0;};
// DirectionSort is the original MSVC three-way introsort, including equal-key
// swaps above the 32-record insertion threshold.
class DirectionSort {
    std::vector<DirectionRecord> &hits;
    static bool before(const DirectionRecord &a,const DirectionRecord &b){return std::bit_cast<int64_t>(uint64_t(a.direction[0])*uint64_t(b.direction[1])-uint64_t(a.direction[1])*uint64_t(b.direction[0]))>0;}
    void insertion(size_t first,size_t last){for(size_t n=first+1;n<last;++n){auto item=hits[n];size_t j=n;if(before(item,hits[first])){std::move_backward(hits.begin()+first,hits.begin()+n,hits.begin()+n+1);j=first;}else while(j>first&&before(item,hits[j-1])){hits[j]=hits[j-1];--j;}hits[j]=item;}}
    void median(size_t first,size_t middle,size_t last){if(before(hits[middle],hits[first]))std::swap(hits[middle],hits[first]);if(before(hits[last],hits[middle])){std::swap(hits[last],hits[middle]);if(before(hits[middle],hits[first]))std::swap(hits[middle],hits[first]);}}
    std::pair<size_t,size_t> partition(size_t first,size_t last){const auto mid=first+(last-first)/2;
        if(last-first>41){const auto step=(last-first)/8;median(first,first+step,first+2*step);median(mid-step,mid,mid+step);median(last-1-2*step,last-1-step,last-1);median(first+step,mid,last-1-step);}else median(first,mid,last-1);
        size_t pfirst=mid,plast=mid+1;while(first<pfirst&&!before(hits[pfirst-1],hits[pfirst])&&!before(hits[pfirst],hits[pfirst-1]))--pfirst;while(plast<last&&!before(hits[plast],hits[pfirst])&&!before(hits[pfirst],hits[plast]))++plast;
        size_t right=plast,left=pfirst;for(;;){for(;right<last;++right){if(before(hits[pfirst],hits[right]))continue;if(before(hits[right],hits[pfirst]))break;if(plast!=right)std::swap(hits[plast],hits[right]);++plast;}
            for(;first<left;--left){if(before(hits[left-1],hits[pfirst]))continue;if(before(hits[pfirst],hits[left-1]))break;--pfirst;if(pfirst!=left-1)std::swap(hits[pfirst],hits[left-1]);}
            if(left==first&&right==last)return {pfirst,plast};if(left==first){if(plast!=right)std::swap(hits[pfirst],hits[plast]);++plast;std::swap(hits[pfirst++],hits[right++]);}
            else if(right==last){--left;--pfirst;if(left!=pfirst)std::swap(hits[left],hits[pfirst]);std::swap(hits[pfirst],hits[--plast]);}else std::swap(hits[right++],hits[--left]);}
    }
    void down(size_t base,size_t hole,size_t count,DirectionRecord item){const auto top=hole,limit=(count-1)/2;while(hole<limit){size_t child=hole*2+1;if(!before(hits[base+child+1],hits[base+child]))++child;hits[base+hole]=hits[base+child];hole=child;}
        if(hole==limit&&count%2==0){hits[base+hole]=hits[base+count-1];hole=count-1;}while(top<hole){const auto parent=(hole-1)/2;if(!before(hits[base+parent],item))break;hits[base+hole]=hits[base+parent];hole=parent;}hits[base+hole]=item;}
    void heap(size_t first,size_t last){size_t count=last-first;for(size_t n=count/2;n>0;--n)down(first,n-1,count,hits[first+n-1]);while(count>1){auto item=hits[first+count-1];hits[first+count-1]=hits[first];down(first,0,--count,item);}}
    void sort(size_t first,size_t last,size_t depth){while(last-first>32){if(!depth){heap(first,last);return;}const auto [a,b]=partition(first,last);depth=(depth>>1)+(depth>>2);if(a-first<last-b){sort(first,a,depth);first=b;}else{sort(b,last,depth);last=a;}}if(first<last)insertion(first,last);}
public:explicit DirectionSort(std::vector<DirectionRecord> &input):hits(input){}void run(){sort(0,hits.size(),hits.size());}
};
}
std::vector<int32_t> fuzzyDirectionIndices(std::span<const EscherIntegerPoint> directions){
    std::array<std::vector<DirectionRecord>,2> groups;for(size_t n=0;n<directions.size();++n){const auto p=directions[n];const bool upper=p[1]>0||(p[1]==0&&p[0]>0);groups[upper?0:1].push_back({p,int32_t(n)});}
    std::vector<int32_t> result;result.reserve(directions.size());for(auto &group:groups){DirectionSort(group).run();for(auto entry:group)result.push_back(entry.index);}return result;
}
std::vector<int32_t> fuzzyAngularRelatives(std::span<const ConvolutionIntegerSegment> edges,
    std::span<const int32_t> levels,std::span<const FuzzyAngularQuery> queries){
    if(edges.empty()||edges.size()!=levels.size())throw std::invalid_argument("Fuzzy angular relative inputs");
    const auto center=edges.front()[0];std::vector<EscherIntegerPoint> directions,queryDirections;
    for(auto edge:edges){if(edge[0]!=center)throw std::invalid_argument("Fuzzy angular segment center");directions.push_back({sub(edge[1][0],center[0]),sub(edge[1][1],center[1])});}
    for(auto query:queries){if(query.segment[0]!=center)throw std::invalid_argument("Fuzzy angular query center");queryDirections.push_back({sub(query.segment[1][0],center[0]),sub(query.segment[1][1],center[1])});}
    const auto order=fuzzyDirectionIndices(directions),queryOrder=fuzzyDirectionIndices(queryDirections);std::set<std::array<int32_t,2>> pending;std::vector<int32_t> result(queries.size(),-1);size_t query=0;
    for(size_t n=0;n<order.size()*2;++n){const auto index=order[n%order.size()];const auto level=std::bit_cast<int32_t>(uint32_t(levels[size_t(index)])+1);
        const auto first=pending.lower_bound({level,-3});for(auto i=first;i!=pending.end();++i)result[size_t((*i)[1])]=index;pending.erase(first,pending.end());
        while(query<queries.size()&&queryDirections[size_t(queryOrder[query])]==directions[size_t(index)]){const auto id=queryOrder[query++];pending.insert({queries[size_t(id)].threshold,id});}
    }
    if(std::find(result.begin(),result.end(),-1)!=result.end())throw std::invalid_argument("Fuzzy angular query has no relative above its threshold");return result;
}
FuzzyFamilyRelation fuzzyVertexRelative(const FuzzyTopology &topology,
    const FuzzyEdgeRegionLabels &labels,EscherIntegerPoint point){
    auto edges=topology.incidentEdges.at(point);const ConvolutionIntegerSegment probe{point,{point[0],sub(point[1],1)}};
    if(std::find(edges.begin(),edges.end(),probe)==edges.end())edges.push_back(probe);
    const std::vector<int32_t> levels(edges.size());const std::array<FuzzyAngularQuery,1> queries{FuzzyAngularQuery{1,probe}};
    const auto selected=edges[size_t(fuzzyAngularRelatives(edges,levels,queries).front())];const auto forward=topology.segmentIndex.at(selected).forward;
    auto edge=selected;if(!labels.labels.contains(edge))std::swap(edge[0],edge[1]);const auto pair=labels.labels.at(edge);return {pair[forward?0:1],!forward};
}
}
