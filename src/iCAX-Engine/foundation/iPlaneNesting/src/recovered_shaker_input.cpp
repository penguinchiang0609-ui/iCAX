#include "recovered_shaker_input.hpp"
#include "recovered_shaker_geometry_input.hpp"
#include <algorithm>
#include <bit>
#include <numbers>
#include <stdexcept>
#include <immintrin.h>

namespace study::recovered {
namespace {
double add(double a,double b){return _mm_cvtsd_f64(_mm_add_sd(_mm_set_sd(a),_mm_set_sd(b)));}
double sub(double a,double b){return _mm_cvtsd_f64(_mm_sub_sd(_mm_set_sd(a),_mm_set_sd(b)));}
double half(double a){return _mm_cvtsd_f64(_mm_mul_sd(_mm_set_sd(a),_mm_set_sd(.5)));}
void registerMovement(ShakerMovement &m,uintptr_t identity){
    if(m.parents.size()>=size_t(INT32_MAX))throw std::length_error("Shaker movement capacity");
    const auto index=int32_t(m.parents.size());m.parents.push_back(index);m.ranks.push_back(0);m.indices[identity]=index;
}
void appendRelation(ShakerRelations &relations,ShakerRelationPart &part,const ShakerInputComponent &component){
    const auto type=relations.types.index(component.type);
    if(part.values.contains(type))return;
    if(component.offsetPresent){
        for(const int32_t key:{-1,-5,-6,-7}){
            const auto base=relations.types.index(key);const auto found=part.values.find(base);
            if(found!=part.values.end()){
                part.values.emplace(type,ShakerRelationValue{type,found->second.category,add(component.offset,found->second.value)});return;
            }
        }
    }
    part.values.emplace(type,ShakerRelationValue{type,type,0});
}
}
size_t shakerCreatePart(ShakerInputScene &s,std::optional<size_t> shape,ShakerPoint position,double angle,std::array<uint8_t,2> mirror,uint8_t category){
    if(shape&&*shape>=s.shapes.size())throw std::out_of_range("Shaker input geometry");
    const size_t index=s.parts.size();const auto sequence=std::bit_cast<int32_t>(s.nextSequence++);
    ShakerPart p;p.position=position;p.reference50=position;p.angle=angle;p.mirror=mirror;p.category8=category;
    p.bounds={-1e15,1e15,-1e15,1e15};p.angleBounds={sub(angle,std::numbers::pi),add(angle,std::numbers::pi)};
    s.parts.push_back(p);s.inputParts.push_back({sequence,0,shape,position,angle,angle});
    ShakerRelationPart relation;relation.id=sequence;relation.identity=index+1;
    ShakerGeometryPart geometry;geometry.typeIdentity=shape?*shape+1:0;if(shape)geometry.reference=s.shapes[*shape].reference;
    s.geometry.parts.push_back(geometry);
    if(shape)for(const auto &source:s.shapes[*shape].components){
        const size_t component=s.geometry.components.size();s.geometry.parts.back().components.push_back(component);
        s.geometry.components.push_back({index,component+1,source.points,{}});
        shakerRefreshComponent(s.parts,s.geometry,component);appendRelation(s.relations,relation,source);
    }
    s.relationParts.push_back(std::move(relation));
    registerMovement(s.relations.x,index+1);registerMovement(s.relations.y,index+1);return index;
}
int32_t shakerSetMobility(ShakerPart &p,std::array<int32_t,3> locked,std::array<double,4> bounds,std::array<double,2> angles){
    for(size_t n=0;n<3;++n)p.locked[n]=uint8_t(locked[n]!=0);p.bounds=bounds;p.angleBounds=angles;return 0;
}
int32_t shakerSetPartLabel(ShakerInputScene &s,size_t part,int32_t label){
    if(s.labels.contains(label))return 7;s.inputParts.at(part).label=label;s.labels.emplace(label,part);return 0;
}
void shakerConfigureOrdinaryRelations(ShakerRelations &relations){
    relations.types={};
    for(int32_t key:{-1,-2,-3,-4,-8,-9}){
        const auto first=relations.types.index(-1),second=relations.types.index(key);
        relations.types.set(first,second,true);relations.types.set(second,first,true);
    }
}
std::vector<ShakerPoint> shakerRectanglePoints(double width,double height){
    const double lowX=half(-width),lowY=half(-height),highX=half(width),highY=half(height);
    return {{lowX,lowY},{highX,lowY},{highX,highY},{lowX,highY}};
}
bool shakerCreateBoundaryGeometry(ShakerInputScene &s,double maximumX,double maximumY,double allowance){
    s.boundaryShapes[0]=shakerCreateGeometry(s,{});s.shapes.back().components.push_back({-2,shakerRectanglePoints(maximumX,allowance),false,0});
    s.boundaryShapes[1]=shakerCreateGeometry(s,{});s.shapes.back().components.push_back({-2,shakerRectanglePoints(allowance,maximumY),false,0});
    s.maximumX=maximumX;s.maximumY=maximumY;s.boundaries.allowance=allowance;
    s.geometry.excludedSpanTypes={*s.boundaryShapes[0]+1,*s.boundaryShapes[1]+1};return true;
}
int32_t shakerSetBounds(ShakerInputScene &s,double xmin,double xmax,double ymin,double ymax){
    if(std::any_of(s.boundaries.parts.begin(),s.boundaries.parts.end(),[](auto p){return p.has_value();})||
        !s.boundaryShapes[0]||!s.boundaryShapes[1]||sub(xmax,xmin)>s.maximumX||sub(ymax,ymin)>s.maximumY)return 1;
    const double h=half(s.boundaries.allowance),x=half(add(xmin,xmax)),y=half(add(ymin,ymax));
    const std::array<ShakerPoint,4> positions{{{x,add(h,ymax)},{x,sub(ymin,h)},{sub(xmin,h),y},{add(h,xmax),y}}};
    for(size_t n=0;n<4;++n)s.boundaries.parts[n]=shakerCreatePart(s,s.boundaryShapes[n/2],positions[n],0,{},1);
    return 0;
}
}
