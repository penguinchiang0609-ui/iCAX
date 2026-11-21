#include "recovered_floating_offset.hpp"
#include <cmath>
#include <limits>

namespace study::recovered {
namespace {
int32_t next(const ConvolutionTopology &p,int32_t edge){
    return edge<0||p.edges.at(edge).last<0?-1:p.vertices.at(p.edges.at(edge).last).outgoing;
}
double length(const ConvolutionEdge &e){return std::abs(e.curve.parameters[1]-e.curve.parameters[0]);}
double projectionError(const ConvolutionCurveSegment &c,ShakerPoint p){
    const auto parameter=booleanStraightEdgePointParameter(c,p);
    const auto point=convolutionCurveEvaluate(c,parameter);
    const double x=p[0]-point[0],y=p[1]-point[1];return std::sqrt(y*y+x*x);
}
int32_t appendReplacement(ConvolutionTopology &p,int32_t a,int32_t b,ConvolutionEdge edge){
    edge.first=a;edge.last=b;const auto index=int32_t(p.edges.size());p.edges.push_back(std::move(edge));
    p.vertices.at(a).outgoing=index;p.vertices.at(b).incoming=index;return index;
}
}

bool floatingOffsetSimplifyPath(ConvolutionTopology &p,ConvolutionCurves &curves,
    double tolerance,double maximumLength){
    if(p.firstEdge==p.lastEdge)return true;
    const auto firstVertex=p.edges.at(p.firstEdge).first,lastVertex=p.edges.at(p.lastEdge).last;
    if(firstVertex>=0&&firstVertex==lastVertex){
        if(next(p,p.firstEdge)==p.lastEdge)return false;
        int32_t longest=-1;auto edge=p.firstEdge,finish=next(p,p.lastEdge);
        do {if(longest<0||length(p.edges.at(edge))>length(p.edges.at(longest)))longest=edge;edge=next(p,edge);}while(edge!=finish);
        p.firstEdge=longest;p.lastEdge=p.vertices.at(p.edges.at(longest).first).incoming;
    }
    bool discarded=false,done=false;
    auto start=p.firstEdge;
    do {
        int64_t group=p.edges.at(start).curve.source;
        bool first=true,newFirst=true;
        auto end=start,acceptedEnd=start;
        std::optional<ConvolutionEdge> accepted;
        for(;;){
            if(next(p,end)==start){discarded=true;done=true;break;}
            if(end!=start){
                int32_t longest=-1,metadata=-1;
                auto edge=start;const auto stop=next(p,end);
                do {
                    if(longest<0||length(p.edges.at(edge))>length(p.edges.at(longest)))longest=edge;
                    if(metadata<0)metadata=edge;
                    else {
                        const auto chosen=p.edges.at(metadata).curve.source,source=p.edges.at(edge).curve.source;
                        if(chosen==-1?(source!=-1||length(p.edges.at(edge))>length(p.edges.at(metadata))):
                            chosen==source&&length(p.edges.at(edge))>length(p.edges.at(metadata)))metadata=edge;
                    }
                    edge=next(p,edge);
                }while(edge!=stop);
                const auto a=p.vertices.at(p.edges.at(start).first).point,b=p.vertices.at(p.edges.at(end).last).point;
                ConvolutionEdge candidate=p.edges.at(metadata);
                const auto &longCurve=p.edges.at(longest).curve;
                if(longCurve.circle){const auto middle=convolutionCurveEvaluate(longCurve,(longCurve.parameters[1]+longCurve.parameters[0])*.5);
                    candidate.curve=curves.copy(curves.createArc(a,middle,b));}
                else candidate.curve=curves.copy(curves.create(a,b));
                candidate.first=candidate.last=-1;
                if(!candidate.curve.line&&!candidate.curve.circle){if(end==p.firstEdge)done=true;break;}
                candidate.curve.source=-1;edge=start;
                do {if(p.edges.at(edge).curve.source!=-1){candidate.curve.source=p.edges.at(edge).curve.source;break;}edge=next(p,edge);}while(edge!=stop);
                double error=projectionError(candidate.curve,a);edge=start;
                do {
                    const auto &c=p.edges.at(edge).curve;
                    const auto middle=convolutionCurveEvaluate(c,(c.parameters[1]+c.parameters[0])*.5);
                    const double midpointError=projectionError(candidate.curve,middle);
                    const double endpointError=projectionError(candidate.curve,p.vertices.at(p.edges.at(edge).last).point);
                    const double current=midpointError<=error?error:midpointError;
                    error=endpointError<=current?current:endpointError;edge=next(p,edge);
                }while(edge!=stop);
                if(error<=tolerance){accepted=std::move(candidate);acceptedEnd=end;first=newFirst;}
                else {if(end==p.firstEdge)done=true;break;}
            }
            if(end==p.firstEdge&&!first){if(start==p.firstEdge)discarded=true;done=true;break;}
            end=next(p,end);first=false;newFirst=false;
            if(end<0){done=true;break;}
            if(length(p.edges.at(end))>maximumLength){if(end==p.firstEdge)done=true;break;}
            const auto source=p.edges.at(end).curve.source;
            if(source!=group&&source!=-1&&group!=-1){if(end==p.firstEdge)done=true;break;}
            if(group==-1)group=source;
        }
        const auto last=p.edges.at(acceptedEnd).last,following=next(p,acceptedEnd);
        if(!discarded&&start!=acceptedEnd){
            const auto first=p.edges.at(start).first;
            const auto incoming=p.vertices.at(first).incoming;
            auto edge=start;
            while(edge!=acceptedEnd){const auto e=next(p,edge),v=p.edges.at(edge).last;
                p.vertices.at(v).alive=false;p.vertices.at(v).incoming=p.vertices.at(v).outgoing=-1;
                p.edges.at(edge).first=p.edges.at(edge).last=-1;edge=e;}
            p.edges.at(acceptedEnd).first=p.edges.at(acceptedEnd).last=-1;
            const auto replacement=accepted?appendReplacement(p,first,last,std::move(*accepted)):-1;
            if(replacement<0){p.vertices.at(first).outgoing=-1;p.vertices.at(last).incoming=-1;}
            if(acceptedEnd==p.firstEdge){p.firstEdge=replacement;p.lastEdge=incoming;}
            else if(start==p.firstEdge)p.firstEdge=replacement;
            else if(acceptedEnd==p.lastEdge)p.lastEdge=replacement;
        }
        start=following;
    }while(!done);
    return discarded;
}

void floatingOffsetSplitLargeArcs(ConvolutionTopology &p,ConvolutionCurves &curves){
    auto edge=p.firstEdge;
    do {
        const auto following=next(p,edge);const auto source=p.edges.at(edge);
        const auto &c=source.curve;
        if(c.circle&&c.circle->radius*3.*.7853981633974483<std::abs(c.parameters[0]-c.parameters[1])){
            const double middle=(c.parameters[0]+c.parameters[1])*.5;
            const auto point=convolutionCurveEvaluate(c,middle);
            const auto vertex=int32_t(p.vertices.size());
            p.vertices.push_back({point});
            ConvolutionEdge a=source,b=source;a.curve=curves.range(c,c.parameters[0],middle);b.curve=curves.range(c,middle,c.parameters[1]);
            const auto first=appendReplacement(p,source.first,vertex,std::move(a));
            const auto last=appendReplacement(p,vertex,source.last,std::move(b));
            if(p.firstEdge==edge)p.firstEdge=first;if(p.lastEdge==edge)p.lastEdge=last;
            p.edges.at(edge).first=p.edges.at(edge).last=-1;
        }
        edge=following;
    }while(edge!=next(p,p.lastEdge));
}

void floatingOffsetFinishParts(std::vector<BooleanStraightPart> &parts,ConvolutionCurves &curves,
    double tolerance,double maximumLength){
    std::vector<BooleanStraightPart> kept;
    for(auto &part:parts){
        if(part.outer&&floatingOffsetSimplifyPath(*part.outer,curves,tolerance,maximumLength))continue;
        if(part.outer)floatingOffsetSplitLargeArcs(*part.outer,curves);
        std::vector<std::shared_ptr<ConvolutionTopology>> holes;
        for(auto &hole:part.holes)if(!floatingOffsetSimplifyPath(*hole,curves,tolerance,maximumLength)){
            floatingOffsetSplitLargeArcs(*hole,curves);holes.push_back(std::move(hole));
        }
        part.holes=std::move(holes);kept.push_back(std::move(part));
    }
    parts=std::move(kept);
}

std::optional<std::vector<BooleanStraightPart>> floatingOffsetParts(
    std::span<const BooleanStraightPart> parts,ConvolutionCurves &curves,FloatingOffsetJoin join,
    double radius,double maximumAngle,double tolerance){
    ConvolutionTopology graph;std::vector<BooleanEdgePair> anchors;
    if(tolerance==0.){
        // 7d9ae0 takes the largest path dimension across all source parts.
        std::vector<const ConvolutionTopology*> paths;
        for(const auto &part:parts){if(part.outer)paths.push_back(part.outer.get());for(const auto &hole:part.holes)paths.push_back(hole.get());}
        double scale=1.;
        if(!paths.empty()){
            auto bounds=booleanPathBounds(*paths.front(),{paths.front()->firstEdge,paths.front()->lastEdge});
            for(const auto *p:std::span(paths).subspan(1)){const auto box=booleanPathBounds(*p,{p->firstEdge,p->lastEdge});
                for(size_t axis=0;axis<2;++axis){if(box[axis]<bounds[axis])bounds[axis]=box[axis];if(bounds[axis+2]<box[axis+2])bounds[axis+2]=box[axis+2];}}
            const auto width=bounds[2]-bounds[0],height=bounds[3]-bounds[1];scale=height>width?height:width;
        }
        tolerance=scale*1e-7;
    }
    int32_t winding=0;
    const auto append=[&](const ConvolutionTopology &p,bool outer){
        auto input=p;
        for(auto edge=input.firstEdge;;edge=next(input,edge)){
            const auto &e=input.edges.at(edge);auto &v=input.vertices.at(e.first);
            v.metadata[3]=(v.metadata[3]&0xffffffff00000000ULL)|uint32_t(e.metadata[3]);if(edge==input.lastEdge)break;
        }
        auto generated=floatingOffsetPath(input,curves,join,radius,maximumAngle,tolerance/3.,outer);
        for(auto &path:generated){const auto eo=int32_t(graph.edges.size()),vo=int32_t(graph.vertices.size());
            anchors.push_back({path.firstEdge+eo,path.lastEdge+eo});
            for(auto vertex:path.vertices){if(vertex.incoming>=0)vertex.incoming+=eo;if(vertex.outgoing>=0)vertex.outgoing+=eo;graph.vertices.push_back(vertex);}
            for(auto edge:path.edges){if(edge.first>=0)edge.first+=vo;if(edge.last>=0)edge.last+=vo;graph.edges.push_back(std::move(edge));}
        }
    };
    for(const auto &part:parts){if(part.outer)append(*part.outer,true);else ++winding;for(const auto &hole:part.holes)append(*hole,false);}
    if(parts.empty())return std::vector<BooleanStraightPart>{};
    auto result=booleanArrangeStraightParts(std::move(graph),anchors,tolerance,curves,winding,radius<=0.,true);
    if(!result)return std::nullopt;
    floatingOffsetFinishParts(*result,curves,tolerance);
    booleanFinishStraightParts(*result,0.,curves.edgeFactory(),curves.arcFactory());
    return result;
}
}
