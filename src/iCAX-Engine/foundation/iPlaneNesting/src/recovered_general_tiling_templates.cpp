#include "recovered_general_tiling_templates.hpp"
#include <bit>
#include <set>
namespace study::recovered {
namespace {
int32_t integer(double value){return value>=-0x1p31&&value<0x1p31?int32_t(value):INT32_MIN;}
int64_t absolute(int64_t value){return value<0?std::bit_cast<int64_t>(uint64_t(0)-uint64_t(value)):value;}
int8_t comparePoint(Point a,Point b){
    if(b.x>a.x||(b.x==a.x&&b.y>a.y))return -1;
    if(a.x>b.x||(a.x==b.x&&a.y>b.y))return 1;
    return 0;
}
struct Before {bool operator()(const GeneralTilingTemplate &a,const GeneralTilingTemplate &b)const{return generalTilingCompareTemplates(a,b)<0;}};
int64_t integer64(double value){return value>=-0x1p63&&value<0x1p63?int64_t(value):INT64_MIN;}
Point quantized(Point point){const auto x=point.x*1000000.,y=point.y*1000.;return {double(integer64(x<0?x-.5:x+.5))/1000000.,double(integer(y<0?y-.5:y+.5))/1000.};}
}
GeneralTilingTemplate generalTilingTemplate(std::shared_ptr<TilingPattern> pattern){
    require(bool(pattern),PN2S_INVALID,"A supplemental template needs its pattern");
    GeneralTilingTemplate result{std::move(pattern),{}};result.memberIdentities.reserve(result.pattern->members.size());
    for(const auto &member:result.pattern->members)result.memberIdentities.push_back(reinterpret_cast<uintptr_t>(&member));
    return result;
}
int8_t generalTilingCompareTemplates(const GeneralTilingTemplate &a,const GeneralTilingTemplate &b){
    require(a.pattern&&b.pattern,PN2S_INVALID,"Template comparison needs both patterns");
    require(a.memberIdentities.size()==a.pattern->members.size()&&b.memberIdentities.size()==b.pattern->members.size(),PN2S_INVALID,"Template placement handles do not match members");
    if(const auto order=comparePoint(a.pattern->u,b.pattern->u))return order;
    if(const auto order=comparePoint(a.pattern->v,b.pattern->v))return order;
    for(size_t n=0;n<std::min(a.memberIdentities.size(),b.memberIdentities.size());++n){
        if(a.memberIdentities[n]<b.memberIdentities[n])return -1;
        if(a.memberIdentities[n]>b.memberIdentities[n])return 1;
    }
    return a.memberIdentities.size()<b.memberIdentities.size()?-1:a.memberIdentities.size()>b.memberIdentities.size()?1:0;
}
std::vector<GeneralTilingTemplate> generalTilingSupplementalTemplates(int32_t target,Point usable,
    double seed,const GeneralTilingTemplateProvider &produce){
    require(bool(produce),PN2S_INVALID,"Supplemental templates need the actual Escher provider");
    EscherProviderRequest request;request.width=usable.x;request.height=usable.y;request.singleMode=true;request.useCountCache=true;
    auto initial=produce(request);require(bool(initial.pattern),PN2S_INVALID,"Supplemental provider omitted its converted pattern");
    std::set<GeneralTilingTemplate,Before> tree;
    if(initial.pattern->members.empty())return {};
    tree.insert(std::move(initial));
    Random random(uint32_t(integer(seed))+1993U);
    // The native counter advances for every sampled request, including empty
    // layouts and duplicate keys; it is not the evolving tree size.
    for(int32_t attempt=int32_t(tree.size());attempt<target;++attempt){
        request.width=(random.unit()*.7+.3)*usable.x;request.height=(random.unit()*.7+.3)*usable.y;request.useCountCache=false;
        auto candidate=produce(request);require(bool(candidate.pattern),PN2S_INVALID,"Supplemental provider omitted its sampled pattern");
        if(!candidate.pattern->members.empty())tree.insert(std::move(candidate));
    }
    return {tree.begin(),tree.end()};
}
TilingPattern generalTilingConvertEscherPattern(const EscherPatternLayout &layout,
    const GeneralTilingTemplateGeometry &geometry){
    TilingPattern pattern;pattern.u=pattern.v={1.,0.};
    if(layout.primary.empty())return pattern;
    const auto &source=layout.secondary;
    require(source.size()==1||source.size()==2,PN2S_UNSUPPORTED,"Only mono and bimodules accepted");
    const auto &conversion=geometry.conversion;
    require(std::isfinite(conversion.scale)&&conversion.scale>0,PN2S_INVALID,"Template conversion needs the actual positive Escher scale");
    int64_t width=layout.basis[0],height=layout.basis[1];
    if(width==0){width=layout.basis[2];height=layout.basis[1];}else height=layout.basis[3];
    pattern.u={double(absolute(width))/conversion.scale,0./conversion.scale};
    pattern.v={0./conversion.scale,double(absolute(height))/conversion.scale};
    pattern.maximumX=geometry.maximumX;pattern.maximumY=geometry.maximumY;
    std::vector<TilingMember> converted;converted.reserve(source.size());
    for(const auto &record:source){const std::span<const EscherPatternRecord> one(&record,1);auto placement=escherConvertProviderLayout(one,conversion);converted.push_back(std::move(placement.front()));}
    const auto origin=converted.front().position;
    for(size_t n=0;n<converted.size();++n){auto &member=converted[n];
        member.position=quantized(n?Point{member.position.x-origin.x,member.position.y-origin.y}:Point{});
        auto box=conversion.variantBounds(member.orientation);member.box={box.x0+member.position.x,box.y0+member.position.y,box.x1+member.position.x,box.y1+member.position.y};member.required=geometry.required;
        pattern.members.push_back(std::move(member));
    }
    for(auto &matrix:pattern.commonCuts)matrix.assign(source.size(),std::vector<double>(source.size(),0.));
    return pattern;
}
}
