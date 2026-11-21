#include "recovered_shaker_terrain.hpp"
#include <bit>
#include <numeric>

namespace study::recovered {
namespace {
int32_t sub32(int32_t a,int32_t b) { return std::bit_cast<int32_t>(uint32_t(a)-uint32_t(b)); }
int32_t add32(int32_t a,int32_t b) { return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b)); }
int64_t add64(int64_t a,int64_t b) { return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b)); }
int64_t quantize(double v) {
    v = v < 0. ? v-.5 : v+.5;
    if (!(v >= -9223372036854775808. && v < 9223372036854775808.)) return INT64_MIN;
    return static_cast<int64_t>(v);
}
void sumTerrain(ShakerTerrain &t) {
    t.area80=0;
    for (int32_t height:t.heights) t.area80=add64(t.area80,height);
}
}
ShakerTerrain shakerTerrainFromFreeRows(const Rows &rows,int32_t lower,int32_t upper,int32_t minimum) {
    require(rows.size()<=size_t(INT32_MAX),PN2S_RESOURCE_LIMIT,"Shaker terrain strip count");
    ShakerTerrain out; out.lower94=lower;out.upper98=upper;out.minimum90=minimum;
    int32_t last=int32_t(rows.size())-1;
    while(last>=0 && rows[size_t(last)].empty()) --last;
    out.last88=last;out.first8c=add32(last,1);
    int32_t height=sub32(upper,lower);
    for(int32_t index=last;index>=0;--index) {
        std::optional<int32_t> top;
        for(const auto &interval:rows[size_t(index)]) {
            const auto lo=int32_t(interval[0]),hi=int32_t(interval[1]);
            const auto available=sub32(hi,lo);
            if(hi==upper && available>=minimum) { top=available;break; }
        }
        if(!top) break;
        if(*top<height)height=*top;
        if(height==0)break;
        out.heights.push_back(height);out.first8c=index;
    }
    sumTerrain(out);return out;
}
void shakerUpdateTerrain(ShakerTerrain &t,const Rows &rows,int32_t strip,int32_t y,int32_t layers) {
    require(rows.size()<=size_t(INT32_MAX),PN2S_RESOURCE_LIMIT,"Shaker terrain raster strip count");
    int32_t running=sub32(t.upper98,t.lower94);
    int32_t current=add32(strip,int32_t(rows.size())-1);
    if(t.last88<current)current=t.last88;
    for(;current>=t.first8c;--current) {
        const auto offset=size_t(sub32(t.last88,current));
        require(offset<t.heights.size(),PN2S_INTERNAL,"Shaker terrain index invariant");
        const int32_t row=sub32(current,strip);
        if(row>=0) {
            const auto &intervals=rows.at(size_t(row));
            size_t end=intervals.size();
            if(layers>1)while(end && int32_t(intervals[end-1][0])>t.upper98)--end;
            if(!end) { if(layers>1)continue;throw Failure(PN2S_UNSUPPORTED,"Empty occupied terrain row"); }
            const int32_t candidate=sub32(sub32(t.upper98,int32_t(intervals[end-1][1])),y);
            if(candidate<t.minimum90 || candidate==0) {
                t.heights.resize(offset);t.first8c=add32(current,1);break;
            }
            if(candidate<running)running=candidate;
        }
        if(running<t.heights[offset])t.heights[offset]=running;
        if(current==INT32_MIN)break;
    }
    sumTerrain(t);
}
int64_t shakerTerrainLoss(const ShakerTerrain &t,const Rows &rows,int32_t strip,int32_t y,int32_t layers) {
    require(rows.size()<=size_t(INT32_MAX),PN2S_RESOURCE_LIMIT,"Shaker terrain loss raster count");
    int32_t running=sub32(t.upper98,t.lower94),current=add32(strip,int32_t(rows.size())-1);
    if(t.last88<current)current=t.last88;
    int64_t loss=0;
    for(;current>=t.first8c;--current) {
        const auto offset=size_t(sub32(t.last88,current));
        require(offset<t.heights.size(),PN2S_INTERNAL,"Shaker terrain loss index invariant");
        const int32_t row=sub32(current,strip),height=t.heights[offset];
        if(row>=0) {
            const auto &intervals=rows.at(size_t(row));size_t end=intervals.size();
            if(layers>1)while(end && int32_t(intervals[end-1][0])>t.upper98)--end;
            if(!end){if(layers>1)continue;throw Failure(PN2S_UNSUPPORTED,"Empty occupied loss row");}
            const auto candidate=sub32(sub32(t.upper98,int32_t(intervals[end-1][1])),y);
            if(candidate<t.minimum90){running=0;loss=add64(loss,height);continue;}
            if(candidate<running)running=candidate;
        }
        if(running<height)loss=add64(loss,sub32(height,running));
        if(current==INT32_MIN)break;
    }
    return loss;
}
std::vector<ShakerTerrainPoint> shakerTerrainProfile(const ShakerTerrain &t,double pitch) {
    if(t.heights.empty())return {};
    const int64_t step=quantize(pitch*1000000.);
    int64_t x=std::bit_cast<int64_t>(uint64_t(int64_t(t.first8c))*uint64_t(step));
    std::vector<ShakerTerrainPoint> points{{x,t.upper98}};
    int32_t previous=t.heights.back();
    for(auto i=t.heights.rbegin();i!=t.heights.rend();++i) {
        if(*i!=previous) { points.push_back({x,sub32(t.upper98,previous)});previous=*i; }
        x=add64(x,step);
    }
    points.push_back({x,t.lower94});return points;
}
std::vector<Polygon> shakerTerrainGuides(const ShakerTerrain &t,double pitch) {
    const auto profile=shakerTerrainProfile(t,pitch);if(profile.empty())return {};
    const double right=double(profile.back().x)/1000000.;
    double previous=double(profile.front().y)/1000.;std::vector<Polygon> out;
    for(size_t index=0;index+1<profile.size();++index) {
        const double left=double(profile[index].x)/1000000.;
        const double next=double(profile[index+1].y)/1000.;
        out.push_back({{left,next},{right,next},{right,previous},{left,previous}});previous=next;
    }
    return out;
}
}
