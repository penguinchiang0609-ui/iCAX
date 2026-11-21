#include "recovered_shaker_scene.hpp"
#include <bit>
#include <cmath>
#include <stdexcept>
namespace study::recovered {
namespace {
double neg(double v){return std::bit_cast<double>(std::bit_cast<uint64_t>(v)^0x8000000000000000ULL);}
double quiet(double v){return std::bit_cast<double>(std::bit_cast<uint64_t>(v)|0x8000000000000ULL);}
// Scalar SSE arithmetic keeps the destination NaN payload/sign. Ordinary C++
// commutative expressions may swap operands even under /fp:precise.
double add(double a,double b){if(std::isnan(a))return quiet(a);if(std::isnan(b))return quiet(b);return a+b;}
double subtract(double a,double b){if(std::isnan(a))return quiet(a);if(std::isnan(b))return quiet(b);return a-b;}
double multiply(double a,double b){if(std::isnan(a))return quiet(a);if(std::isnan(b))return quiet(b);return a*b;}
void include(ShakerGeometryBox &box,ShakerPoint p){
    if(box.empty){box.value={p[0],p[1],p[0],p[1]};box.empty=false;return;}
    if(p[0]<box.value[0])box.value[0]=p[0];if(p[1]<box.value[1])box.value[1]=p[1];
    if(p[0]>box.value[2])box.value[2]=p[0];if(p[1]>box.value[3])box.value[3]=p[1];
}
ShakerTransform transform(std::span<const ShakerPart> parts,const ShakerSceneGeometry &scene,size_t part){
    const auto &p=parts[part];return {p.position,scene.parts.at(part).reference,p.angle,std::cos(p.angle),std::sin(p.angle),p.mirror,1};
}
ShakerPoint normalized(ShakerPoint v){const double length=std::sqrt(v[1]*v[1]+v[0]*v[0]);return !(1e-9>length)?ShakerPoint{v[0]*(1/length),v[1]*(1/length)}:ShakerPoint{1,0};}
}
ShakerPoint shakerTransformPoint(ShakerPoint p,const ShakerTransform &t){
    double x=subtract(p[0],t.reference[0]),y=subtract(p[1],t.reference[1]);
    if(t.angle!=0){const double term=multiply(t.sine,y);y=add(multiply(t.cosine,y),multiply(t.sine,x));x=subtract(multiply(x,t.cosine),term);}
    if(t.mirror[0])y=neg(y);if(t.mirror[1])x=neg(x);
    return {multiply(add(x,t.position[0]),t.scale),multiply(add(y,t.position[1]),t.scale)};
}
ShakerGeometryBox shakerGeometryBounds(std::span<const ShakerPoint> points,const ShakerTransform &t){ShakerGeometryBox box;for(auto p:points)include(box,shakerTransformPoint(p,t));return box;}
ShakerGeometryBox shakerProjectedBounds(std::span<const ShakerPoint> points,const ShakerTransform &t,const std::array<double,4> &m){
    ShakerGeometryBox box;for(auto p:points){p=shakerTransformPoint(p,t);include(box,{add(multiply(p[1],m[1]),multiply(p[0],m[0])),add(multiply(p[0],m[2]),multiply(p[1],m[3]))});}return box;
}
void shakerRefreshComponent(std::span<const ShakerPart> parts,ShakerSceneGeometry &scene,size_t index){auto &c=scene.components.at(index);c.bounds=shakerGeometryBounds(c.points,transform(parts,scene,c.part));}
void shakerRefreshPart(std::span<ShakerPart> parts,ShakerSceneGeometry &scene,size_t index){
    auto &p=parts[index];if(p.geometryValid30)return;for(auto component:scene.parts.at(index).components)shakerRefreshComponent(parts,scene,component);p.geometryValid30=1;
}
void shakerRefreshScene(std::span<ShakerPart> parts,ShakerSceneGeometry &scene){for(size_t n=0;n<parts.size();++n)shakerRefreshPart(parts,scene,n);}
double shakerSceneSpan(std::span<ShakerPart> parts,ShakerSceneGeometry &scene){
    bool first=true;double low=0,high=0;
    for(size_t n=0;n<parts.size();++n){shakerRefreshPart(parts,scene,n);const auto &p=scene.parts.at(n);
        if(p.typeIdentity==scene.excludedSpanTypes[0]||p.typeIdentity==scene.excludedSpanTypes[1])continue;
        for(auto index:p.components){const auto &box=scene.components.at(index).bounds.value;
            if(first){first=false;low=box[1];high=box[3];}else{if(box[1]<low)low=box[1];if(box[3]>high)high=box[3];}}
    }
    return high-low;
}
ShakerGeometryBox shakerPartProjection(std::span<const ShakerPart> parts,const ShakerSceneGeometry &scene,size_t index,ShakerPoint first,ShakerPoint second){
    ShakerGeometryBox result;
    for(auto component:scene.parts.at(index).components){const auto &c=scene.components.at(component);const auto a=normalized(first),b=normalized(second);const double determinant=1/(b[1]*a[0]-b[0]*a[1]);
        const std::array<double,4> matrix{determinant*b[1],neg(b[0])*determinant,neg(a[1])*determinant,determinant*a[0]};
        const auto box=shakerProjectedBounds(c.points,transform(parts,scene,c.part),matrix);
        // The original merges all four values even for an empty component.
        if(result.empty){result.value=box.value;result.empty=false;}else{
            if(box.value[0]<result.value[0])result.value[0]=box.value[0];if(box.value[1]<result.value[1])result.value[1]=box.value[1];
            if(box.value[2]>result.value[2])result.value[2]=box.value[2];if(box.value[3]>result.value[3])result.value[3]=box.value[3];}
    }
    return result;
}
}
