#include "recovered_amo_raster.hpp"
#include <bit>
namespace study::recovered {
namespace {
int32_t trunc32(double v) {
    return std::isfinite(v)&&v>=double(INT32_MIN)&&v<double(INT32_MAX)+1?
        static_cast<int32_t>(v):INT32_MIN;
}
int32_t add32(int32_t a,int32_t b) {return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));}
double minimum(double a,double b) {return a<b?a:b;}
double maximum(double a,double b) {return a>b?a:b;}
bool turn(const std::vector<Point> &p,size_t k) {
    const auto n=p.size();const auto a=p[(k+n-1)%n],b=p[k%n],c=p[(k+1)%n];
    const double u=c.x-b.x,v=b.x-a.x;
    if((u>0&&v>0)||(u<0&&v<0))return false;
    if((u>0&&v<0)||(u<0&&v>0))return true;
    const double iy=b.y-a.y;
    if(u>0)return iy<0;
    if(u<0)return iy>0;
    const double oy=c.y-b.y;
    if(v>0)return oy<0;
    if(v<0)return oy>0;
    return (oy>0)==(iy<0);
}
bool openingEnd(const std::vector<Point> &p,size_t k) {
    const auto n=p.size();const auto a=p[(k+n-1)%n],b=p[k%n],c=p[(k+1)%n];
    const double u=c.x-b.x,v=b.x-a.x;
    if(u>0||v<0)return false;
    if(u<0) {if(v>0)return true;return b.y>a.y;}
    if(v>0)return c.y<b.y;
    return b.y>a.y&&c.y<b.y;
}
struct Event {bool opening;double y;};
}
AmoRasterResult amoRasterStrips(std::span<const std::vector<Point>> contours,Point reference,
    double minimumX,double pitch,double mergeTolerance,double xTolerance,double weight) {
    const Point *maxPoint=nullptr;
    for(const auto &ring:contours)for(const auto &point:ring)
        if(!maxPoint||maxPoint->x<point.x)maxPoint=&point;
    if(!maxPoint||!(pitch>0)||!std::isfinite(pitch))throw std::invalid_argument("Invalid Amo strip raster input");
    AmoRasterResult result;
    result.firstOffset=trunc32(std::floor(((reference.x-minimumX)-xTolerance)/pitch+.5));
    const int32_t second=trunc32(std::floor(((maxPoint->x-reference.x)-xTolerance)/pitch+.5));
    result.binCount=add32(add32(second,1),result.firstOffset);
    if(result.binCount<0||result.binCount>10000000)throw std::length_error("Amo strip raster count");
    const double origin=reference.x-(double(result.firstOffset)+.5)*pitch;
    std::vector<std::vector<Event>> events(size_t(result.binCount));
    for(const auto &ring:contours) {
        if(ring.empty())throw std::invalid_argument("Empty Amo raster contour");
        size_t k=0;
        while(k<ring.size()&&!turn(ring,k))++k;
        if(k==ring.size())continue;
        while(k<ring.size()) {
            std::vector<Point> chain{ring[k%ring.size()]};
            do {++k;chain.push_back(ring[k%ring.size()]);}while(!turn(ring,k));
            const bool opening=openingEnd(ring,k);
            if(!opening)std::reverse(chain.begin(),chain.end());
            const int32_t lo=trunc32(std::floor(((chain.front().x-origin)+xTolerance)/pitch));
            const int32_t last=add32(trunc32(std::floor(((chain.back().x-origin)-xTolerance)/pitch)),1);
            const int32_t begin=std::max(0,lo),end=std::min(result.binCount,std::max(lo,last));
            if(begin>end)throw std::out_of_range("Amo raster monotone-chain bins");
            size_t previous=0,next=1;double rawY=chain.front().y;
            for(int32_t i=begin;i!=end;++i) {
                const double upper=double(add32(i,1))*pitch+origin;
                double low=rawY,high=rawY,value=rawY;
                bool interpolated=false;
                while(next<chain.size()) {
                    const auto a=chain[previous],b=chain[next];
                    const double tolerance=opening?-xTolerance:xTolerance;
                    if(upper<=tolerance+b.x) {
                        double y=a.y,dx=b.x-a.x;
                        if(dx>xTolerance)y=((b.y-y)/dx)*(upper-a.x)+y;
                        const double lowEndpoint=b.y<a.y?b.y:a.y;
                        const double highEndpoint=b.y>a.y?b.y:a.y;
                        y=maximum(y,lowEndpoint);y=minimum(y,highEndpoint);
                        rawY=y;
                        low=minimum(y,low);high=maximum(y,high);
                        const double exterior=opening?low:high,interior=opening?high:low;
                        value=(1.-weight)*exterior+interior*weight;
                        interpolated=true;break;
                    }
                    low=minimum(b.y,low);high=maximum(b.y,high);
                    const double exterior=opening?low:high,interior=opening?high:low;
                    value=(1.-weight)*exterior+weight*interior;
                    previous=next++;
                }
                if(!interpolated)rawY=value;
                events[size_t(i)].push_back({opening,value});
            }
        }
    }
    for(auto &row:events) {
        std::sort(row.begin(),row.end(),[](const Event &a,const Event &b) {
            if(a.y==b.y&&a.opening!=b.opening)return !a.opening;
            return a.y<b.y;
        });
        result.indices.push_back(int32_t(result.intervals.size()));
        bool first=true;int32_t winding=0;double start=0;
        for(const auto &event:row) {
            if(event.opening) {winding=add32(winding,1);if(winding==1)start=event.y;}
            else {
                winding=add32(winding,-1);
                if(winding!=0)continue;
                const double end=event.y-reference.y;
                if(!first&&start-(reference.y+result.intervals.back()[1])<mergeTolerance)
                    result.intervals.back()[1]=end;
                else {first=false;result.intervals.push_back({start-reference.y,end});}
            }
        }
    }
    result.indices.push_back(int32_t(result.intervals.size()));
    return result;
}
}
