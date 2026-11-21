#include "recovered_amo_convolution.hpp"
#include <bit>
namespace study::recovered {
std::vector<ConvolutionTopology> amoConvolveStraightPaths(const ConvolutionTopology &first,
    const ConvolutionTopology &second,ConvolutionCurves &curves){
    const auto a=convolutionPolygon(first),b=convolutionPolygon(second);
    auto pairs=convolutionSweep(a,b);convolutionLinkPairs(a,b,pairs);
    std::vector<uint8_t> visited(pairs.size());std::vector<ConvolutionTopology> result;
    const auto vertexPair=[&](int32_t index){
        if(index<0)return false;const auto &pair=pairs[size_t(index)];
        return a[pair.a].kind==ConvolutionEntity::Kind::vertex&&b[pair.b].kind==ConvolutionEntity::Kind::vertex&&a[pair.a].shortArc&&b[pair.b].shortArc;
    };
    const auto edgeFactory=curves.edgeFactory();
    for(size_t start=0;start<pairs.size();++start){
        if(visited[start])continue;std::optional<ConvolutionTopology> path;
        int32_t current=int32_t(start);
        while(current>=0&&!visited[size_t(current)]){
            visited[size_t(current)]=1;const auto &pair=pairs[size_t(current)];ShakerBoundary segment;
            if(convolutionSegment(a[pair.a],b[pair.b],segment)){
                const auto begin=segment.direction?segment.start:segment.end,end=segment.direction?segment.end:segment.start;
                int32_t edge=-1;
                if(!path){path=convolutionTopology(begin,end,edgeFactory);edge=path->firstEdge;}
                else if(path->vertices.at(path->edges.at(path->lastEdge).last).point!=end)edge=convolutionAppend(*path,end,edgeFactory);
                if(edge>=0){
                    auto &metadata=path->edges.at(edge).metadata;
                    metadata[4]=a[pair.a].identity;metadata[5]=b[pair.b].identity;
                    metadata[6]=(std::bit_cast<uint64_t>(segment.end[0])&~uint64_t(0xffff))|
                        uint64_t(vertexPair(pair.previous)||vertexPair(pair.next))|0x100;
                }
            }
            current=pair.next;
        }
        if(!path)continue;
        convolutionMergeClosure(*path);
        const auto firstNext=path->vertices.at(path->edges.at(path->firstEdge).last).outgoing;
        const auto lastNext=path->vertices.at(path->edges.at(path->lastEdge).last).outgoing;
        if(firstNext!=path->lastEdge||lastNext!=path->firstEdge)result.push_back(std::move(*path));
    }
    return result;
}
}
