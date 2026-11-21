#include "recovered_convolution_hierarchy.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
namespace study::recovered {
namespace {
int64_t add(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t mul(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)*uint64_t(b));}
int64_t magnitude(int64_t n){return n<0?sub(0,n):n;}
int productSign(int64_t a,int64_t b){return (a<0?-1:1)*(b<0?-1:1);}
std::array<ConvolutionIntegerPoint,2> bounds(std::span<const ConvolutionIntegerPoint> ring){
    if(ring.empty())throw std::invalid_argument("Hierarchy contact requires nonempty rings");std::array<ConvolutionIntegerPoint,2> box{ring.front(),ring.front()};
    for(auto p:ring)for(size_t axis=0;axis<2;++axis){box[0][axis]=std::min(box[0][axis],p[axis]);box[1][axis]=std::max(box[1][axis],p[axis]);}return box;
}
enum class Location {outside,inside,boundary};
Location location(std::span<const ConvolutionIntegerPoint> ring,ConvolutionIntegerPoint p){
    if(ring.size()<2)return Location::outside;auto a=ring.front();bool inside=false;
    for(size_t n=1;n<=ring.size();++n){const auto b=ring[n%ring.size()];if(a==b)continue;if(p==b)return Location::boundary;
        const auto low=std::min(a[0],b[0]),high=std::max(a[0],b[0]);
        if(p[0]>=low&&p[0]<high){const auto side=convolutionHierarchyPointSide(p,{a,b});if(!side)return Location::boundary;if(side==1)inside=!inside;}
        else if(p[0]==high){if(low==high){if(std::min(a[1],b[1])<=p[1]&&p[1]<=std::max(a[1],b[1]))return Location::boundary;}else if(!convolutionHierarchyPointSide(p,{a,b}))return Location::boundary;}
        a=b;
    }
    return inside?Location::inside:Location::outside;
}
int64_t roundInteger(double value){const auto rounded=std::round(value);return rounded>=-0x1p63&&rounded<0x1p63?int64_t(rounded):INT64_MIN;}
}
int convolutionHierarchyPointSide(ConvolutionIntegerPoint p,const ConvolutionIntegerSegment &edge){
    if(p==edge[0]||p==edge[1])return 0;int64_t ax=sub(edge[0][0],p[0]),ay=sub(edge[0][1],p[1]),bx=sub(edge[1][0],p[0]),by=sub(edge[1][1],p[1]);
    const auto left=mul(magnitude(bx),magnitude(ay)),right=mul(magnitude(ax),magnitude(by));
    if(sub(left,right)==0&&(productSign(bx,ay)==productSign(ax,by)||left==0))return 0;
    bool upper=true;
    if(ax!=0){if(ax<0){ax=sub(0,ax);ay=sub(0,ay);}if(bx<0){bx=sub(0,bx);by=sub(0,by);}else if(bx==0){upper=ax==0;}
        if(bx!=0){const int ls=productSign(ay,bx),rs=productSign(ax,by);if(ls<rs)upper=false;else if(ls==rs){const auto l=mul(magnitude(ay),magnitude(bx)),r=mul(magnitude(ax),magnitude(by));upper=ls==-1?uint64_t(r)>=uint64_t(l):uint64_t(l)>=uint64_t(r);}}}
    int result=upper?1:-1;if(edge[1]<edge[0])result=-result;const auto low=std::min(edge[0],edge[1]),high=std::max(edge[0],edge[1]);return low<p&&p<high?result:-result;
}
bool convolutionHierarchyContains(std::span<const ConvolutionIntegerPoint> outer,std::span<const ConvolutionIntegerRing> holes,ConvolutionIntegerPoint p){
    if(location(outer,p)!=Location::inside)return false;for(const auto &hole:holes)if(location(hole,p)!=Location::outside)return false;return true;
}
bool convolutionHierarchyReference(std::span<const ConvolutionIntegerPoint> ring,ConvolutionIntegerPoint &point){
    if(ring.size()<3)return false;const auto box=bounds(ring);if(convolutionIntegerOrientation(ring)!=-1){point={add(box[1][0],8),add(box[1][1],8)};return true;}
    ConvolutionIntegerRing reversed(ring.rbegin(),ring.rend());auto previous=ring.back();
    for(const auto current:ring){const auto dx=sub(current[0],previous[0]),dy=sub(previous[1],current[1]);
        if(uint64_t(add(magnitude(sub(previous[0],current[0])),magnitude(dy)))>=8){
            const double x=double(dx),y=double(dy),length=std::sqrt(x*x+y*y),scale=2.121320355/length,ox=scale*y,oy=scale*x;
            point={roundInteger((double(add(previous[0],current[0]))-(ox+ox))*0.5),roundInteger((double(add(previous[1],current[1]))-(oy+oy))*0.5)};
            if(convolutionHierarchyContains(reversed,{},point))return true;previous=current;
        }
    }
    point={add(box[0][0],box[1][0])/2,add(box[0][1],box[1][1])/2};return convolutionHierarchyContains(reversed,{},point);
}
std::array<bool,2> convolutionHierarchyRingContacts(std::span<const ConvolutionIntegerPoint> a,std::span<const ConvolutionIntegerPoint> b){
    const auto ba=bounds(a),bb=bounds(b);const bool lower=bb[1][1]<ba[1][1];
    if(bb[0][0]<ba[0][0]||bb[0][1]<ba[0][1]||ba[1][0]<bb[1][0]||ba[1][1]<bb[1][1])return {false,lower};
    if(magnitude(convolutionIntegerArea(b))<magnitude(convolutionIntegerArea(a)))return {false,lower};
    ConvolutionIntegerRing current(b.begin(),b.end()),other(a.begin(),a.end());if(convolutionIntegerOrientation(current)!=-1)std::reverse(current.begin(),current.end());if(convolutionIntegerOrientation(other)==-1)std::reverse(other.begin(),other.end());
    ConvolutionIntegerPoint point{};convolutionHierarchyReference(current,point);return {convolutionHierarchyContains(other,{},point),lower};
}
}
