#include "recovered_shaker_configuration.hpp"
#include <algorithm>
#include <stdexcept>
#include <immintrin.h>
namespace study::recovered {
namespace {
bool complete(const ShakerBoundaryConfiguration &c){return std::all_of(c.parts.begin(),c.parts.end(),[](auto p){return p.has_value();});}
double add(double a,double b){return _mm_cvtsd_f64(_mm_add_sd(_mm_set_sd(a),_mm_set_sd(b)));}
double sub(double a,double b){return _mm_cvtsd_f64(_mm_sub_sd(_mm_set_sd(a),_mm_set_sd(b)));}
double mul(double a,double b){return _mm_cvtsd_f64(_mm_mul_sd(_mm_set_sd(a),_mm_set_sd(b)));}
ShakerPart &boundary(std::span<ShakerPart> p,const ShakerBoundaryConfiguration &c,size_t n){if(!c.parts[n]||*c.parts[n]>=p.size())throw std::invalid_argument("Shaker boundary part");return p[*c.parts[n]];}
void configure(ShakerPart &p,size_t axis,double objective){p.locked={1,1,1};p.locked[axis]=0;p.objective={};p.objective[axis]=objective;}
}
void shakerResetBoundaryObjectives(std::span<ShakerPart> p,const ShakerBoundaryConfiguration &c){
    for(size_t n=0;n<4;++n)boundary(p,c,n).locked={1,1,1};
    for(size_t n=0;n<4;++n)boundary(p,c,n).objective={};
}
int32_t shakerConfigureDefault(std::span<ShakerPart> p,const ShakerBoundaryConfiguration &c,int32_t direction){
    if(!complete(c))return direction==4?0:4;
    shakerResetBoundaryObjectives(p,c);
    switch(direction){
        case 0:configure(boundary(p,c,0),1,1);break;
        case 1:configure(boundary(p,c,1),1,-1);break;
        case 2:configure(boundary(p,c,3),0,1);break;
        case 3:configure(boundary(p,c,2),0,-1);break;
        case 4:break;
        default:return 2;
    }return 0;
}
int32_t shakerConfigureCompaction(std::span<ShakerPart> p,const ShakerBoundaryConfiguration &c,int32_t first,int32_t second){
    if(!complete(c))return 4;shakerResetBoundaryObjectives(p,c);
    const auto has=[&](int32_t d){return first==d||second==d;};
    if(has(0)){auto &v=boundary(p,c,0);const double limit=add(v.position[1],1e-5);configure(v,1,1);v.bounds[3]=limit;}
    if(has(1)){auto &v=boundary(p,c,1);const double limit=sub(v.position[1],1e-5);configure(v,1,-1);v.bounds[2]=limit;}
    if(has(3)){auto &v=boundary(p,c,2);const double limit=sub(v.position[0],1e-5);configure(v,0,-1);v.bounds[0]=limit;}
    if(has(2)){auto &v=boundary(p,c,3);const double limit=add(v.position[0],.01);configure(v,0,100);v.bounds[1]=limit;}
    const auto x=[](int32_t d){return d==2?1.:d==3?-1.:0.;};const auto y=[](int32_t d){return d==0?1.:d==1?-1.:0.;};
    const double objectiveX=add(mul(x(second),.25),x(first)),objectiveY=add(mul(y(second),.25),y(first));
    for(size_t n=0;n<p.size();++n)if(std::none_of(c.parts.begin(),c.parts.end(),[&](auto i){return i&&*i==n;}))p[n].objective={objectiveX,objectiveY,0};
    return 0;
}
int32_t shakerConfigureNeutral(std::span<ShakerPart> p,const ShakerBoundaryConfiguration &c){
    if(!complete(c))return 4;shakerResetBoundaryObjectives(p,c);for(auto &part:p)part.objective={};return 0;
}
double shakerSceneScale(std::span<ShakerPart> p,ShakerSceneGeometry &geometry,const ShakerBoundaryConfiguration &c){
    if(c.parts[0]&&c.parts[1])return sub(sub(boundary(p,c,0).position[1],boundary(p,c,1).position[1]),c.allowance);
    bool first=true;double low=0,high=0;
    for(size_t n=0;n<p.size();++n){shakerRefreshPart(p,geometry,n);for(auto index:geometry.parts.at(n).components){const auto &b=geometry.components.at(index).bounds.value;
        if(first){first=false;low=b[1];high=b[3];}else{if(b[1]<low)low=b[1];if(b[3]>high)high=b[3];}}}
    return first?0:sub(high,low);
}
int32_t shakerCommonRun(double seconds,const ShakerCommonSettings &settings,const ShakerCommonRuntime &runtime){
    const double scale=runtime.sceneScale(),factor=runtime.thresholdFactor();
    ShakerIterationOptions options;options.seconds=seconds;options.maximumIterations=10000;options.stallLimit=3;
    options.translationWindow=mul(scale,settings.translationScale);options.angularWindow=settings.angularWindow;
    options.improvementThreshold=mul(factor,scale);options.rotationWindow=mul(scale,settings.rotationScale);
    options.rotate=runtime.rotationEnabled();return runtime.iterate(options)?0:5;
}
}
