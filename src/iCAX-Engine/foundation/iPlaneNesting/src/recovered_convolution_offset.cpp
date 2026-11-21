#include "recovered_convolution_offset.hpp"
#include "recovered_convolution_minkowski.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace study::recovered {
namespace {
int64_t sub(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t add(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t mul(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)*uint64_t(b));}
ConvolutionIntegerPoint difference(ConvolutionIntegerPoint a,ConvolutionIntegerPoint b){return {sub(a[0],b[0]),sub(a[1],b[1])};}
int64_t dot(ConvolutionIntegerPoint a,ConvolutionIntegerPoint b){return add(mul(a[0],b[0]),mul(a[1],b[1]));}
int64_t cross(ConvolutionIntegerPoint a,ConvolutionIntegerPoint b){return sub(mul(a[0],b[1]),mul(a[1],b[0]));}
int64_t trunc64(double x){return x>=-0x1p63&&x<0x1p63?int64_t(x):INT64_MIN;}
int32_t trunc32(double x){return x>=-0x1p31&&x<0x1p31?int32_t(x):INT32_MIN;}
int64_t nearest(double x){return trunc64(x+(x<0?-0.5:0.5));}
double squared(ConvolutionIntegerPoint p){return double(p[1])*double(p[1])+double(p[0])*double(p[0]);}
double length(ConvolutionIntegerPoint p){return std::sqrt(squared(p));}
}
ConvolutionIntegerPoint convolutionOffsetClosestPoint(ConvolutionIntegerPoint p,const ConvolutionIntegerSegment &edge,bool endpointsForCollinear){
    const auto a=edge[0],b=edge[1],ab=difference(b,a);const auto side=cross(difference(a,p),difference(b,p));
    if(side){const ConvolutionIntegerPoint q{sub(p[0],ab[1]),add(ab[0],p[1])};
        const double x=double(a[0])-double(b[0]),y=double(a[1])-double(b[1]),xx=double(p[0])-double(q[0]),yy=double(p[1])-double(q[1]);
        const double c=double(b[1])*double(a[0])-double(a[1])*double(b[0]),cc=double(q[1])*double(p[0])-double(p[1])*double(q[0]),den=yy*x-y*xx;
        const ConvolutionIntegerPoint hit{nearest((xx*c-x*cc)/den),nearest((yy*c-y*cc)/den)};
        if(dot(difference(hit,a),ab)>0&&dot(difference(hit,b),difference(a,b))>0)return hit;
    }
    if(!endpointsForCollinear&&!side){const auto d=dot(ab,difference(p,a));if(d>=0&&double(d)<=squared(difference(a,b)))return p;}
    return squared(difference(p,a))<=squared(difference(p,b))?a:b;
}
double convolutionOffsetDistance(ConvolutionIntegerPoint p,const ConvolutionIntegerPolygon &rings){double best=std::numeric_limits<double>::max();for(const auto &ring:rings){if(ring.empty())throw std::invalid_argument("Offset distance requires nonempty rings");auto a=ring.back();for(auto b:ring){auto delta=difference(p,convolutionOffsetClosestPoint(p,{a,b},true));const auto d=squared(delta);best=best<=d?best:d;a=b;}}return std::sqrt(best);}
ConvolutionIntegerBox convolutionOffsetBounds(const ConvolutionHierarchy &node){ConvolutionIntegerBox out{INT64_MAX,INT64_MAX,INT64_MIN,INT64_MIN};if(node.ring)for(auto p:*node.ring)for(size_t k=0;k<2;++k){out[k]=std::min(out[k],p[k]);out[k+2]=std::max(out[k+2],p[k]);}for(const auto &child:node.children){const auto box=convolutionOffsetBounds(*child);for(size_t k=0;k<2;++k){out[k]=std::min(out[k],box[k]);out[k+2]=std::max(out[k+2],box[k+2]);}}return out;}
std::vector<ConvolutionIntegerSegment> convolutionOffsetArc(ConvolutionIntegerPoint center,ConvolutionIntegerPoint a,ConvolutionIntegerPoint b,int32_t resolution,int64_t radius){
    if(a==b||a==center||b==center)return {};constexpr double tau=6.2831853071795864769;const auto da=difference(a,center),db=difference(b,center);const double start=std::atan2(double(da[1]),double(da[0]));double end=std::atan2(double(db[1]),double(db[0]));while(end<start)end+=tau;const double sweep=end-start;if(sweep>3.141602653589793)throw std::invalid_argument("Offset arc exceeds original half-circle limit");
    const double byRadius=(double(radius)*sweep)/3.0,byResolution=(double(resolution)*sweep)/tau;const int32_t count=std::max<int32_t>(1,trunc32(std::round(byRadius<byResolution?byRadius:byResolution)));const double half=sweep/double(std::bit_cast<int32_t>(uint32_t(count)*2)),extended=double(radius)/std::cos(half),first=half+start;
    ConvolutionIntegerRing points{a};for(int32_t n=0;n<count;++n){const double angle=double(std::bit_cast<int32_t>(uint32_t(n)*2))*half+first;const auto y=trunc64(std::round(std::sin(angle)*extended)),x=trunc64(std::round(std::cos(angle)*extended));points.push_back({add(x,center[0]),add(y,center[1])});}points.push_back(b);std::vector<ConvolutionIntegerSegment> out;for(size_t n=1;n<points.size();++n)out.push_back({points[n-1],points[n]});return out;
}
std::vector<ConvolutionIntegerSegment> convolutionOffsetEdges(std::span<const ConvolutionIntegerPoint> ring,int32_t resolution,int64_t radius,const ConvolutionHierarchyDiagnostic &diagnostic){
    std::vector<ConvolutionIntegerSegment> out,joins;const size_t count=ring.size();
    for(size_t n=0;n<count;++n){auto a=ring[n],b=ring[(n+1)%count];const auto delta=difference(b,a);const double nx=double(delta[1]),ny=double(sub(a[0],b[0])),scale=double(radius)/std::sqrt(ny*ny+nx*nx);const ConvolutionIntegerPoint normal{nearest(nx*scale),nearest(scale*ny)};out.push_back({ConvolutionIntegerPoint{add(a[0],normal[0]),add(a[1],normal[1])},ConvolutionIntegerPoint{add(b[0],normal[0]),add(b[1],normal[1])}});}
    for(size_t n=0;n<count;++n){const size_t previous=(n+count-1)%count;const auto p=ring[n],before=difference(p,ring[previous]),after=difference(ring[(n+1)%count],p);const auto a=out[previous][1],b=out[n][0];
        if(cross(before,after)>0){const auto begin=joins.size();auto arc=convolutionOffsetArc(p,a,b,resolution,radius);joins.insert(joins.end(),arc.begin(),arc.end());if(joins.size()>begin+1){out[previous][1]=joins[begin][1];out[n][0]=joins.back()[0];std::swap(joins[begin],joins[joins.size()-2]);joins.resize(joins.size()-2);}continue;}
        const double l0=length(before),l1=length(after),cosine=double(dot(before,after))/(l1*l0);if(cosine<=0){if(a!=b)joins.push_back({a,b});continue;}
        const double ux=double(before[0])/l0,uy=double(before[1])/l0,vx=double(after[0])/l1,vy=double(after[1])/l1;
        const double bx=(vy+uy)+(vx-ux),by=(-ux-vx)+(vy-uy),bl=std::sqrt(by*by+bx*bx);
        if(bl<0.1&&diagnostic)diagnostic("numerical precision issue in bissector angle calculation. ");
        const double factor=double(radius)/std::sqrt((cosine+1.0)*0.5);const ConvolutionIntegerPoint hit{add(nearest(factor*(bx/bl)),p[0]),add(nearest(factor*(by/bl)),p[1])};
        const auto d0=difference(out[previous][1],out[previous][0]),h0=difference(hit,out[previous][0]),d1=difference(out[n][1],out[n][0]),h1=difference(out[n][1],hit);
        const bool inside=dot(d0,h0)>0&&dot(h1,d1)>0&&length(d0)+2.0>=length(h0)&&length(d1)+2.0>=length(h1);
        const auto close=convolutionOffsetClosestPoint(hit,{a,b},false),distance=difference(hit,close);const bool merge=squared(distance)<1&&cosine>0.999;
        if(inside){out[previous][1]=hit;out[n][0]=hit;}else if(merge){out[previous][1]=out[n][1];out[n][0]=out[previous][0];}else if(a!=b)joins.push_back({a,b});
    }
    out.erase(std::unique(out.begin(),out.end()),out.end());out.insert(out.end(),joins.begin(),joins.end());return out;
}
ConvolutionIntegerPolygon convolutionOffsetFilter(const ConvolutionHierarchy &operand,const ConvolutionHierarchy &offset,const ConvolutionIntegerPolygon &rings){const auto bounds=convolutionOffsetBounds(offset);const auto radius=sub(bounds[2],bounds[0])/2;const auto flat=convolutionFlattenHierarchy(operand).rings;ConvolutionIntegerPolygon out;for(const auto &ring:rings){if(ring.empty())throw std::invalid_argument("Offset filter requires nonempty rings");if(double(radius)<convolutionOffsetDistance(ring.front(),flat)+2.8284271247461903)out.push_back(ring);}return out;}

ConvolutionIntegerRing convolutionIntegerOffsetCircle(int64_t radius, int32_t pointCount) {
    ConvolutionIntegerRing circle;
    constexpr double pi = 3.14159265358979323846;
    for (int32_t index = 0; index < pointCount; ++index) {
        const auto numerator = std::bit_cast<int32_t>(uint32_t(index) * 2);
        const double angle = (double(numerator) * pi) / double(pointCount);
        circle.push_back({nearest(std::cos(angle) * double(radius)),
                          nearest(std::sin(angle) * double(radius))});
    }
    return circle;
}

ConvolutionHierarchyFlattened convolutionIntegerOffset(
    const ConvolutionIntegerPolygon &rings, int64_t radius, int32_t resolution,
    const ConvolutionHierarchyDiagnostic &diagnostic) {
    // The original signed absolute-value instruction also takes this branch
    // for INT64_MIN, whose negation wraps to itself.
    if (radius == 0 || radius == INT64_MIN || rings.empty())
        return {rings, {}};
    if (radius < 0) {
        auto reversed = rings;
        for (auto &ring : reversed)
            std::reverse(ring.begin(), ring.end());
        auto result = convolutionIntegerOffset(reversed, -radius, resolution, diagnostic);
        for (auto &ring : result.rings)
            std::reverse(ring.begin(), ring.end());
        return result;
    }

    const auto pointCount = std::bit_cast<int32_t>(uint32_t(resolution) * 8);
    auto circle = convolutionIntegerOffsetCircle(radius, pointCount);
    convolutionNormalizeIntegerRing(circle);
    if (circle.empty())
        throw std::invalid_argument("Integer offset requires a nondegenerate circle");
    auto result = convolutionIntegerMinkowski(rings, {circle}, {}, {resolution, true}, diagnostic);
    if (result.rings.empty()) {
        constexpr double tau = 6.2831853071795864769;
        const double scale = 12.0 - std::cos(tau / double(pointCount)) * 11.0;
        for (auto &point : circle)
            for (auto &coordinate : point)
                coordinate = nearest(double(coordinate) * scale);
        // Retry uses ordinary polygon convolution, with both offset flags clear.
        result = convolutionIntegerMinkowski(rings, {circle}, diagnostic);
        if (!result.rings.empty() && diagnostic)
            diagnostic("Circle desoffsetting failed , producing empty result");
    }
    return result;
}
}
