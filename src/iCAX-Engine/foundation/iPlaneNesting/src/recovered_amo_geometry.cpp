#include "recovered_amo_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <bit>
namespace study::recovered {
namespace {
constexpr AmoTilingMatrix identity{1,0,0,1,0,0,1},opposite{1,0,0,1,0,0,-1};
std::vector<Point> points(const ConvolutionTopology &path){
    std::vector<Point> result;if(path.firstEdge<0)return result;
    auto current=path.firstEdge;
    for(;;){const auto &edge=path.edges.at(current);if(!edge.curve.line)throw std::invalid_argument("Amo map point visitor requires straight curves");const auto p=edge.curve.endpoints[0];result.push_back({p[0],p[1]});if(current==path.lastEdge)break;current=path.vertices.at(edge.last).outgoing;}
    return result;
}
int32_t integer(double value){return value>=-0x1p31&&value<0x1p31?int32_t(value):INT32_MIN;}
}
ConvolutionTopology amoTilingBooleanRing(std::span<const Point> ring,const AmoTilingMatrix &matrix,ConvolutionCurves &curves){
    if(ring.empty())throw std::invalid_argument("Amo Boolean ring requires at least one point");
    const auto first=amoTilingTransformPoint(ring.front(),matrix);ConvolutionTopology path;path.cursor={first.x,first.y};
    const auto factory=curves.edgeFactory();
    for(const auto p:ring.subspan(1)){const auto point=amoTilingTransformPoint(p,matrix);convolutionAppend(path,{point.x,point.y},factory);}
    convolutionAppend(path,{first.x,first.y},factory);convolutionMergeClosure(path);return path;
}
std::vector<BooleanStraightPart> amoTilingBooleanShape(const AmoTilingShape &shape,const AmoTilingMatrix &matrix,ConvolutionCurves &curves){
    std::vector<BooleanStraightPart> result;
    for(const auto &ring:shape.rings){auto path=std::make_shared<ConvolutionTopology>(amoTilingBooleanRing(ring,matrix,curves));
        if(booleanStraightPathArea(*path,path->firstEdge,path->lastEdge)<=0.)convolutionReverse(*path);
        result.push_back({std::move(path),{}});
    }
    return result;
}
std::vector<Point> amoTilingSampleContour(std::span<const Point> input,double threshold,bool reverse){
    std::vector<Point> result;
    for(const auto point:input){
        if(!result.empty()){
            const auto previous=result.back();const double dy=previous.y-point.y,dx=previous.x-point.x;
            const int32_t count=integer(std::sqrt(dy*dy+dx*dx)/threshold);
            if(count>0){const double divisor=double(std::bit_cast<int32_t>(uint32_t(count)+1U)),stepX=(point.x-previous.x)/divisor,stepY=(point.y-previous.y)/divisor;
                for(int64_t n=1;n<=count;++n)result.push_back({double(n)*stepX+previous.x,double(n)*stepY+previous.y});
            }
        }
        result.push_back(point);
    }
    if(reverse)std::reverse(result.begin(),result.end());return result;
}
void amoTilingBuildSimplifiedMap(AmoTilingContourMap &map,ConvolutionCurves &curves){
    if(!map.first.shape||!map.second.shape)throw std::invalid_argument("Amo map requires two shapes");
    map.simplified.clear();
    const auto first=amoTilingBooleanShape(*map.first.shape,identity,curves),second=amoTilingBooleanShape(*map.second.shape,opposite,curves);
    auto parts=amoConvolveStraightParts(first,second,curves);
    const auto a=map.first.shape->bounds.box,b=map.second.shape->bounds.box;
    double maximum=std::fabs(b.y1-b.y0);maximum=std::max(maximum,std::fabs(b.x1-b.x0));maximum=std::max(maximum,std::fabs(a.y1-a.y0));maximum=std::max(maximum,std::fabs(a.x1-a.x0));
    const double threshold=(maximum+maximum)/50.;map.usable=false;
    if(parts)for(const auto &part:*parts){if(part.outer){map.usable=true;const auto contour=points(*part.outer);map.simplified.push_back(amoTilingSampleContour(contour,threshold));}
        for(const auto &hole:part.holes){map.usable=true;const auto contour=points(*hole);map.simplified.push_back(amoTilingSampleContour(contour,threshold,true));}}
}
void amoTilingBuildDetailedMap(AmoTilingContourMap &map,ConvolutionCurves &curves){
    if(!map.first.shape||!map.second.shape)throw std::invalid_argument("Amo map requires two shapes");
    map.detailed.clear();const auto second=amoTilingTransformShape(*map.second.shape,amoTilingReverseMatrix()).geometry;
    for(const auto &a:map.first.shape->rings)for(const auto &b:second.rings){
        const auto firstPath=amoTilingBooleanRing(a,identity,curves),secondPath=amoTilingBooleanRing(b,identity,curves);
        auto paths=amoConvolveStraightPaths(firstPath,secondPath,curves);for(const auto &path:paths)map.detailed.push_back(points(path));
    }
}
AmoTilingMapGeometry amoTilingFloatingMapGeometry(){
    auto curves=std::make_shared<ConvolutionCurves>();
    return {[curves](AmoTilingContourMap &map){amoTilingBuildSimplifiedMap(map,*curves);},
        [curves](AmoTilingContourMap &map){amoTilingBuildDetailedMap(map,*curves);}};
}
}
