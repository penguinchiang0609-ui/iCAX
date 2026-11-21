#include "recovered_amo_input.hpp"
#include "recovered_amo_input_offset.hpp"
#include "recovered_floating_offset.hpp"
#include "recovered_rich_conversion.hpp"
#include "recovered_rich_sampling.hpp"
namespace study::recovered {
namespace {
constexpr double tolerance=1e-6;
RichNode &inputCurve(RichGraph &graph,std::span<const Point> ring){
    require(!ring.empty(),PN2S_INVALID,"Amo offset component requires its first point");
    auto &curve=richStartCurve(graph,{ring.front().x,ring.front().y},tolerance);
    for(const auto point:ring.subspan(1))richAppendLine(graph,curve,{point.x,point.y});
    richAppendLine(graph,curve,{ring.front().x,ring.front().y});richClose(curve);return curve;
}
std::shared_ptr<ConvolutionTopology> positivePath(const RichNode &curve,ConvolutionCurves &curves){
    auto path=richStraightPathToBoolean(curve,curves);
    if(path&&booleanStraightPathArea(*path,path->firstEdge,path->lastEdge)<=0.)convolutionReverse(*path);
    return path;
}
RichNode &sampleCurve(RichGraph &graph,const RichNode &source,double error){
    require(source.first&&source.last,PN2S_INVALID,"Amo offset sample requires its source contour");
    auto &result=richStartCurve(graph,source.first->point,source.tolerance);
    const auto append=[&](ShakerPoint point){return richAppendLine(graph,result,point);};
    richVisit(source,{[&](RichEvent event,const RichNode &edge){
        if(event!=RichEvent::Edge)return;
        require(edge.next!=nullptr,PN2S_INVALID,"Open Amo offset edge");
        if(!edge.circle){
            require(edge.line,PN2S_UNSUPPORTED,"Unsupported Amo offset curve");
            // The original visitor retains existing line geometry.
            if(auto written=append(edge.next->point)){written->endpoints=edge.endpoints;written->parameters=edge.parameters;written->attributes=edge.attributes;}
            return;
        }
        require(edge.parameters.has_value(),PN2S_INVALID,"Amo offset arc has no native parameters");
        const auto interval=*edge.parameters;double first=interval[0];
        // Native 8ac680/8acc50 builds each replacement from the segment's
        // evaluated endpoints. b83e40 then retains the old boundary vertices.
        // Their coordinates may differ by a few ULPs; constructing geometry
        // from those retained vertices changes the later Boolean input.
        auto &startVertex=*const_cast<RichNode *>(result.last);
        const auto originalStart=startVertex.point;startVertex.point=edge.endpoints[0];
        bool firstReplacement=true;
        const auto replacement=[&](ShakerPoint point){auto written=append(point);if(firstReplacement){startVertex.point=originalStart;firstReplacement=false;}return written;};
        const auto emit=[&](double last,ShakerPoint endpoint){
            for(const auto point:richDiscretizeArc(*edge.circle,{first,last},error))replacement(point);
            replacement(endpoint);first=last;
        };
        // 8ad220/8ad570 first split the actual circle at X/Y extrema. Native
        // segment construction preserves the circle and its parameter units.
        for(const double parameter:richCircleExtremaParameters(*edge.circle,interval[0],interval[1]))
            if(source.tolerance<std::abs(parameter-first)&&source.tolerance<std::abs(parameter-interval[1]))
                emit(parameter,richCircleSample(*edge.circle,parameter).point);
        emit(interval[1],edge.endpoints[1]);
        const_cast<RichNode *>(result.last)->point=edge.next->point;
    },{}});
    richClose(result);return result;
}
}
RichNode &amoTilingSampleOffsetInputCurve(RichGraph &graph,const RichNode &source,double halfDistance){
    return sampleCurve(graph,source,halfDistance/10.);
}
std::optional<std::vector<BooleanStraightPart>> amoTilingUnionOffsetInputPath(
    const ConvolutionTopology &path,ConvolutionCurves &curves){
    const BooleanEdgePair anchor{path.firstEdge,path.lastEdge};
    auto result=booleanArrangeStraightParts(path,std::span<const BooleanEdgePair>(&anchor,1),tolerance,curves,0,true,false);
    if(result)booleanFinishStraightParts(*result,tolerance,curves.edgeFactory(),curves.arcFactory());
    return result;
}
std::vector<Point> amoTilingOffsetInputRing(std::span<const Point> ring,double halfDistance,bool field58){
    if(halfDistance==0.)return {ring.begin(),ring.end()};
    RichGraph input;auto &curve=inputCurve(input,ring);ConvolutionCurves curves;
    auto path=positivePath(curve,curves);require(bool(path),PN2S_INVALID,"Amo offset source has no live path");
    const std::array source{BooleanStraightPart{path,{}}};
    auto offset=floatingOffsetParts(source,curves,field58?FloatingOffsetJoin::Bisector:FloatingOffsetJoin::Round,
        -halfDistance,field58?3.1101767270538954:1.5707963267948966,0.);
    require(offset.has_value(),PN2S_INVALID,"Amo input offset has a Boolean hierarchy conflict");
    booleanFinishStraightParts(*offset,tolerance,curves.edgeFactory(),curves.arcFactory());
    if(offset->empty())return {};
    RichGraph converted;auto &root=booleanStraightPartsToRich(converted,*offset,tolerance);
    if(root.children.empty()||!root.children.front()->outer)return {};
    RichGraph sampledGraph;const auto &sampled=amoTilingSampleOffsetInputCurve(sampledGraph,*root.children.front()->outer,halfDistance);
    auto sampledPath=positivePath(sampled,curves);if(!sampledPath)return {};
    // 8ba760/beb830/7d9ec0: actual destructive union against an empty tree.
    auto cleaned=amoTilingUnionOffsetInputPath(*sampledPath,curves);
    require(cleaned.has_value(),PN2S_INVALID,"Amo sampled input cleanup has a Boolean hierarchy conflict");
    RichGraph finalGraph;auto &finalRoot=booleanStraightPartsToRich(finalGraph,*cleaned,tolerance);std::vector<Point> output;
    if(!finalRoot.children.empty()&&finalRoot.children.front()->outer)
        richVisit(*finalRoot.children.front()->outer,{[&](RichEvent event,const RichNode &node){if(event==RichEvent::Vertex)output.push_back({node.point[0],node.point[1]});},{}});
    return output;
}
}
