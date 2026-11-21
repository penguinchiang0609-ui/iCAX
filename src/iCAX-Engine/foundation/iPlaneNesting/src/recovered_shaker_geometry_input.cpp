#include "recovered_shaker_geometry_input.hpp"
#include <algorithm>
#include <bit>
#include <immintrin.h>

namespace study::recovered {
namespace {
double plus(double a,double b){return _mm_cvtsd_f64(_mm_add_sd(_mm_set_sd(a),_mm_set_sd(b)));}
double minus(double a,double b){return _mm_cvtsd_f64(_mm_sub_sd(_mm_set_sd(a),_mm_set_sd(b)));}
double times(double a,double b){return _mm_cvtsd_f64(_mm_mul_sd(_mm_set_sd(a),_mm_set_sd(b)));}
double distance(ShakerPoint a,ShakerPoint b,ShakerPoint c){
    const double x=minus(b[0],a[0]),y=minus(b[1],a[1]);
    const double cross=minus(times(minus(c[1],b[1]),x),times(minus(c[0],b[0]),y));
    const double square=plus(times(y,y),times(x,x));
    const auto length=_mm_sqrt_sd(_mm_setzero_pd(),_mm_set_sd(square));
    const double value=_mm_cvtsd_f64(_mm_div_sd(_mm_set_sd(cross),length));
    return std::bit_cast<double>(std::bit_cast<uint64_t>(value)&0x7fffffffffffffffULL);
}
}
std::vector<ShakerPoint> shakerCoordinatePolygon(std::span<const ShakerPoint> input,bool outer){
    std::vector<ShakerPoint> points(input.begin(),input.end());double area=0;
    for(size_t n=0;n<points.size();++n){const auto &a=points[n],&b=points[(n+1)%points.size()];area=plus(area,minus(times(b[1],a[0]),times(a[1],b[0])));}
    area=times(area,.5);
    if((area<0&&outer)||(area>0&&!outer))std::reverse(points.begin(),points.end());
    return points;
}
std::vector<ShakerPolygonInput> shakerPreparePolygons(std::span<const ShakerSdkPolygon> records,int32_t type,std::optional<double> offset,const ShakerInputDiagnostic &diagnostic){
    std::vector<ShakerPolygonInput> groups;
    for(size_t n=0;n<records.size();++n){const auto &r=records[n];
        if(!(r.classification<=0)){groups.push_back({r.points,{},type,offset});}
        else if(groups.empty()){if(diagnostic)diagnostic(n);}
        else groups.back().holes.emplace_back(r.points.rbegin(),r.points.rend());
    }
    for(auto &g:groups){g.outer=shakerCoordinatePolygon(g.outer,true);for(auto &h:g.holes)h=shakerCoordinatePolygon(h,false);}
    return groups;
}
std::vector<ShakerPoint> shakerCleanComponentPoints(std::span<const ShakerPoint> input){
    std::vector<ShakerPoint> output;output.reserve(input.size());
    for(const auto p:input){const size_t n=output.size();if(n>=2&&distance(output[n-2],output[n-1],p)<1e-7)output.pop_back();output.push_back(p);}
    if(output.size()>2&&distance(output.back(),output[0],output[1])<1e-7)output.erase(output.begin());
    if(output.size()>2&&distance(output[output.size()-2],output.back(),output.front())<1e-7)output.pop_back();
    return output;
}
std::vector<double> shakerHoleCutLevels(std::span<const std::array<double,4>> bounds){
    std::vector<double> levels;
    for(const auto &b:bounds){bool found=false;for(double level:levels)if(minus(level,b[1])>1e-6&&minus(b[3],level)>1e-6){found=true;break;}
        if(!found)levels.push_back(times(plus(b[3],b[1]),.5));}
    return levels;
}
size_t shakerCreateGeometry(ShakerInputScene &scene,ShakerPoint reference){
    const size_t index=scene.shapes.size();ShakerInputGeometry g;g.reference=reference;g.sequence=std::bit_cast<int32_t>(scene.nextGeometry++);scene.shapes.push_back(std::move(g));return index;
}
size_t shakerAppendVisitedComponent(ShakerInputScene &scene,size_t shape,int32_t type,std::span<const ShakerPoint> points,double area,std::optional<double> offset){
    auto &g=scene.shapes.at(shape);ShakerInputComponent c;c.type=type;c.points=shakerCleanComponentPoints(points);c.offsetPresent=offset.has_value();c.offset=offset.value_or(0);c.sequence=std::bit_cast<int32_t>(scene.nextComponent++);
    const size_t index=g.components.size();g.components.push_back(std::move(c));g.area=plus(area,g.area);return index;
}
}
