#include "recovered_floating_offset.hpp"
#include <bit>
#include <cmath>
#include <stdexcept>

namespace study::recovered {
namespace {
double negative(double v) { return std::bit_cast<double>(std::bit_cast<uint64_t>(v) ^ (uint64_t(1) << 63)); }
double distance(ShakerPoint a, ShakerPoint b) {
    const double x=a[0]-b[0], y=a[1]-b[1];
    return std::sqrt(x*x+y*y);
}
ShakerPoint normal(const ConvolutionCurveSegment &c, double parameter) {
    auto n=c.circle?convolutionCircleNormal(*c.circle,parameter):c.line->normal;
    if (!(c.parameters[1]>c.parameters[0])) n={negative(n[0]),negative(n[1])};
    return n;
}
ShakerPoint shifted(ShakerPoint p, ShakerPoint n, double radius) {
    return {p[0]+n[0]*radius,p[1]+n[1]*radius};
}
void source(ConvolutionEdge &e, uint32_t value) {
    e.metadata[3]=(e.metadata[3]&0xffffffff00000000ULL)|value;
}
void line(ConvolutionTopology &path, ConvolutionCurves &curves, ShakerPoint p,
          uint32_t label, bool inform) {
    const auto i=convolutionAppend(path,p,curves.edgeFactory());
    source(path.edges.at(i),0xffffffffU);
    if(inform)source(path.edges.at(i),label);
}
void arc(ConvolutionTopology &path, ConvolutionCurves &curves, ShakerPoint p,
         double radius, uint32_t label, bool inform) {
    const auto i=convolutionAppend(path,p,[&](ShakerPoint a,ShakerPoint b){
        return curves.copy(curves.createRadiusArc(a,b,radius));
    });
    source(path.edges.at(i),0xffffffffU);
    if(inform)source(path.edges.at(i),label);
}
}

void floatingOffsetInsertAngle(ConvolutionTopology &path, ConvolutionCurves &curves,
    FloatingOffsetJoin join,double radius,double maximumAngle,ShakerPoint vertex,
    ShakerPoint previousPoint,ShakerPoint previousNormal,ShakerPoint nextPoint,
    ShakerPoint nextNormal,uint32_t vertexSource,bool informVertex) {
    if(join==FloatingOffsetJoin::Round){
        arc(path,curves,nextPoint,negative(radius),vertexSource,informVertex);return;
    }
    double bx=previousPoint[1]-nextPoint[1],by=nextPoint[0]-previousPoint[0];
    const double length=std::sqrt(bx*bx+by*by);
    if(!(length<1e-9)){const double factor=1./length;bx*=factor;by*=factor;}
    else {bx=1.;by=0.;}
    const double dot=join==FloatingOffsetJoin::Regular?
        previousNormal[1]*by+previousNormal[0]*bx:
        previousNormal[0]*bx+previousNormal[1]*by;
    if(dot<0.){line(path,curves,nextPoint,vertexSource,informVertex);return;}
    const double cross=nextNormal[1]*previousNormal[0]-nextNormal[0]*previousNormal[1];
    const double d=join==FloatingOffsetJoin::Regular?
        nextNormal[1]*previousNormal[1]+nextNormal[0]*previousNormal[0]:
        nextNormal[0]*previousNormal[0]+nextNormal[1]*previousNormal[1];
    const double angle=std::atan2(cross,d),absolute=std::abs(angle);
    if(absolute>maximumAngle||absolute>3.141592652589793){
        if(join==FloatingOffsetJoin::Regular){
            const double signedRadius=cross>=0.?std::abs(radius):negative(std::abs(radius));
            arc(path,curves,nextPoint,signedRadius,vertexSource,informVertex);
        }else{
            const ShakerPoint middle{vertex[0]+radius*bx,vertex[1]+radius*by},n{bx,by};
            floatingOffsetInsertAngle(path,curves,join,radius,maximumAngle,vertex,
                previousPoint,previousNormal,middle,n,vertexSource,informVertex);
            floatingOffsetInsertAngle(path,curves,join,radius,maximumAngle,vertex,
                middle,n,nextPoint,nextNormal,vertexSource,informVertex);
        }
        return;
    }
    double tangent=std::tan(angle*.5);
    ShakerPoint point;
    if(join==FloatingOffsetJoin::Regular){
        tangent*=radius;
        point={previousPoint[0]-tangent*previousNormal[1],previousPoint[1]+tangent*previousNormal[0]};
    }else{
        point={previousPoint[0]-(tangent*radius)*previousNormal[1],
               previousPoint[1]+(tangent*radius)*previousNormal[0]};
    }
    line(path,curves,point,vertexSource,informVertex);
    line(path,curves,nextPoint,vertexSource,informVertex);
}

double floatingOffsetVertexAngle(const ConvolutionTopology &path,int32_t vertex){
    if(vertex<0)return 0.;
    const auto &v=path.vertices.at(vertex);
    if(v.incoming<0||v.outgoing<0)return 0.;
    const auto &a=path.edges.at(v.incoming).curve,&b=path.edges.at(v.outgoing).curve;
    const auto n=normal(a,a.parameters[1]),m=normal(b,b.parameters[0]);
    return m[1]*n[0]-m[0]*n[1];
}

std::vector<ConvolutionTopology> floatingOffsetPath(const ConvolutionTopology &input,
    ConvolutionCurves &curves,FloatingOffsetJoin join,double radius,double maximumAngle,
    double minimumLength,bool outer,bool informVertex){
    if(input.firstEdge<0||input.lastEdge<0)throw std::invalid_argument("Offset requires a nonempty path");
    const auto &first=input.edges.at(input.firstEdge),&last=input.edges.at(input.lastEdge);
    if(first.first>=0&&first.first==last.last&&outer==(radius>0.)){
        if(std::abs(booleanStraightPathArea(input,input.firstEdge,input.lastEdge))<radius*3.141592653589793*radius)return {};
        const auto box=booleanPathBounds(input,{input.firstEdge,input.lastEdge});
        double dimension=box[2]-box[0];if(box[3]-box[1]<=dimension)dimension=box[3]-box[1];
        if(dimension<=std::abs(radius)+std::abs(radius))return {};
    }
    std::vector<ConvolutionTopology> output(1);
    auto current=input.firstEdge;
    auto startNormal=normal(first.curve,first.curve.parameters[0]);
    ShakerPoint begin=shifted(first.curve.endpoints[0],startNormal,radius);
    output.back().cursor=begin;
    uint64_t sequence=1;
    for(;;){
        const auto &edge=input.edges.at(current);const auto &c=edge.curve;
        const auto endNormal=normal(c,c.parameters[1]);
        const auto end=shifted(c.endpoints[1],endNormal,radius);
        const bool forward=c.parameters[1]>c.parameters[0];
        if(distance(end,begin)>minimumLength){
            if(!c.circle)line(output.back(),curves,end,uint32_t(edge.metadata[3]),true);
            else {
                const double r=forward?std::abs(c.circle->radius-radius):negative(std::abs(radius+c.circle->radius));
                arc(output.back(),curves,end,r,uint32_t(edge.metadata[3]),true);
            }
            output.back().edges.at(output.back().lastEdge).curve.source=std::bit_cast<int64_t>(sequence++);
        }
        const auto next=edge.last<0?-1:input.vertices.at(edge.last).outgoing;
        if(next<0)return output;
        const auto &nextEdge=input.edges.at(next);const auto &nextCurve=nextEdge.curve;
        startNormal=normal(nextCurve,nextCurve.parameters[0]);
        begin=shifted(nextCurve.endpoints[0],startNormal,radius);
        // OutlineOffseter+40 is true in 7e2850; it retains every corner,
        // including the last-to-first join of a closed source path.
        if(distance(end,begin)>minimumLength){
            const auto &v=input.vertices.at(nextEdge.first);
            floatingOffsetInsertAngle(output.back(),curves,join,radius,maximumAngle,
                nextCurve.endpoints[0],end,endNormal,begin,startNormal,uint32_t(v.metadata[3]),informVertex);
        }
        current=next;
        const auto finish=last.last<0?-1:input.vertices.at(last.last).outgoing;
        if(current==finish)return output;
    }
}
}
