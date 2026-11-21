#include "recovered_convolution_integer.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <list>
#include <map>
#include <stdexcept>
namespace study::recovered {
namespace {
uint64_t mix(uint64_t v){v=(v^(v>>32))*0xe9846af9b1a615dULL;v=(v^(v>>32))*0xe9846af9b1a615dULL;return v^(v>>28);}
uint64_t pointHash(ConvolutionIntegerPoint p){return mix(mix(uint64_t(p[0])+0x9e3779b9)+0x9e3779b9+uint64_t(p[1]));}
ConvolutionIntegerPoint difference(ConvolutionIntegerPoint a,ConvolutionIntegerPoint b){return {std::bit_cast<int64_t>(uint64_t(a[0])-uint64_t(b[0])),std::bit_cast<int64_t>(uint64_t(a[1])-uint64_t(b[1]))};}
ConvolutionIntegerPoint primitive(ConvolutionIntegerPoint p){const auto divisor=std::bit_cast<int64_t>(convolutionIntegerGcd(p));if(!divisor)throw std::invalid_argument("Trace requires nonzero integer edges");return {p[0]/divisor,p[1]/divisor};}
int turn(ConvolutionIntegerPoint a,ConvolutionIntegerPoint b){const auto cross=std::bit_cast<int64_t>(uint64_t(a[0])*uint64_t(b[1])-uint64_t(a[1])*uint64_t(b[0]));return (cross>0)-(cross<0);}
bool choose(ConvolutionIntegerPoint back,ConvolutionIntegerPoint best,ConvolutionIntegerPoint candidate){
    const auto bc=turn(back,candidate),sc=turn(best,candidate);const auto boundary=[&](){return bc==0&&sc==-1;};
    if(candidate==back||candidate==best||back==best)return boundary();
    if(turn(back,best)==1)return bc==1?sc!=1:boundary();return bc==1||sc!=1?true:boundary();
}
}
uint64_t convolutionIntegerEdgeHash(const ConvolutionIntegerSegment &edge){return mix(mix(pointHash(edge[0])+0x9e3779b9)+pointHash(edge[1])+0x9e3779b9);}
std::vector<ConvolutionIntegerSegment> convolutionIntegerEdgeOrder(std::span<const ConvolutionIntegerSegment> input){
    std::vector<ConvolutionIntegerSegment> nodes;std::map<ConvolutionIntegerSegment,size_t> indices;std::list<size_t> order;using Iterator=std::list<size_t>::iterator;std::vector<Iterator> buckets(8,order.end());
    const auto rehash=[&](size_t capacity){buckets.assign(capacity,order.end());for(auto i=order.begin();i!=order.end();){const auto next=std::next(i);auto &first=buckets[convolutionIntegerEdgeHash(nodes[*i])&(capacity-1)];if(first!=order.end())order.splice(first,order,i);first=i;i=next;}};
    for(auto edge:input){if(indices.contains(edge))continue;const auto required=size_t(std::ceil(float(nodes.size()+1)));if(buckets.size()<required){size_t desired=std::max<size_t>(8,required);if(buckets.size()<512)desired=std::max(desired,buckets.size()*8);rehash(std::bit_ceil(desired));}
        const auto index=nodes.size();nodes.push_back(edge);indices.emplace(edge,index);auto &first=buckets[convolutionIntegerEdgeHash(edge)&(buckets.size()-1)];first=order.insert(first,index);
    }
    std::vector<ConvolutionIntegerSegment> result;result.reserve(nodes.size());for(auto n:order)result.push_back(nodes[n]);return result;
}
ConvolutionIntegerPolygon convolutionTraceIntegerSegments(std::span<const ConvolutionIntegerSegment> input){
    for(auto edge:input)if(edge[0]==edge[1])throw std::invalid_argument("Trace requires nonzero integer edges");
    const auto ordered=convolutionIntegerEdgeOrder(input);std::map<ConvolutionIntegerSegment,int> remaining;std::map<ConvolutionIntegerPoint,ConvolutionIntegerRing> adjacency;
    for(auto edge:ordered){remaining[edge]=1;adjacency[edge[0]].push_back(edge[1]);}
    ConvolutionIntegerPolygon result;
    for(size_t start=0;start<ordered.size();){if(remaining[ordered[start]]<=0){++start;continue;}std::vector<ConvolutionIntegerSegment> path{ordered[start]};std::map<ConvolutionIntegerSegment,size_t> visited{{ordered[start],0}};
        while(true){const auto previous=path.back();const auto &neighbors=adjacency[previous[1]];if(neighbors.empty())break;
            const auto back=primitive(difference(previous[0],previous[1]));size_t first=0;while(first+1<neighbors.size()&&neighbors[first]==previous[0])++first;auto selected=neighbors[first],best=primitive(difference(selected,previous[1]));
            for(auto candidate:neighbors){if(candidate==previous[0])continue;const auto direction=primitive(difference(candidate,previous[1]));if(choose(back,best,direction)){selected=candidate;best=direction;}}
            const ConvolutionIntegerSegment next{previous[1],selected};if(remaining[next]==0)break;
            const auto found=visited.find(next);if(found!=visited.end()){ConvolutionIntegerRing ring;for(size_t n=found->second;n<path.size();++n)ring.push_back(path[n][0]);if(ring.size()>2)result.push_back(std::move(ring));break;}
            visited.emplace(next,path.size());path.push_back(next);
        }
        for(auto edge:path)--remaining[edge];
    }return result;
}
ConvolutionIntegerPolygon convolutionIntegerConvolutionContours(std::span<const ConvolutionIntegerPoint> a,std::span<const ConvolutionIntegerPoint> b){
    auto edges=convolutionSplitDirectedIntegerSegments(convolutionIntegerRingEdges(a,b));convolutionSortIntegerSegments(edges);convolutionFilterIntegerCycles(edges);convolutionSortIntegerSegments(edges);
    auto result=convolutionTraceIntegerSegments(edges);for(auto &ring:result)convolutionNormalizeIntegerRing(ring);convolutionRemoveEmptyIntegerRings(result);return result;
}
}
