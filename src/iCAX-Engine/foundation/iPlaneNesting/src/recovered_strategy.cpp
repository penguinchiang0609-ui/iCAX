#include "recovered_strategy.hpp"
namespace study::recovered {
int32_t truncateInt32(double v) {
    if(!std::isfinite(v))return INT32_MIN;
    const double t=std::trunc(v);
    return t>=-2147483648. && t<2147483648. ? static_cast<int32_t>(t) : INT32_MIN;
}
Complexity drawComplexity(Random &rng,const ComplexityConfig &config) {
    Complexity c;
    if(rng.unit()<config.perimeterProbability)c.perimeter=rng.unit()*(config.perimeterScale-0.)+0.;
    c.alpha=rng.unit()*(config.alphaLimit-0.)+0.;
    c.gamma=rng.unit()*(config.gammaLimit-0.)+0.;
    c.useBBox=rng.unit()<.2;
    return c;
}
MultiFactoryResult multiFactory(Random &rng,const MultiFactoryInput &in,const std::vector<ValuePart> &parts,double totalArea) {
    MultiFactoryResult out;
    const double u=rng.unit();
    out.batch=truncateInt32(static_cast<double>(in.contextBatch238)*((u+u)+1.));
    out.complexity=drawComplexity(rng,in.config);
    out.evaluationValues=values(parts,totalArea,out.complexity);
    // The draw occurs even when the reference heights differ (or are NaN).
    const double choice=rng.unit()+0.;
    out.currentReference=in.currentReference;
    if(in.currentHeight==in.normalHeight) {
        out.reference=choice<.3?ReferenceKind::Wide:ReferenceKind::Normal;
        if(choice<.3){out.currentReference=ReferenceKind::Wide;out.updateCurrentReference=true;}
    } else out.reference=in.currentReference;
    out.flagD0=truncateInt32(in.config700)!=0;out.flagD8=truncateInt32(in.config710)!=0;out.managerId=in.managerId;
    return out;
}
std::vector<AliasEntry> aliasTable(std::span<const double> weights) {
    require(weights.size()<=INT32_MAX,PN2S_RESOURCE_LIMIT,"Alias table too large");
    std::vector<AliasEntry> out(weights.size()),small,large;
    if(weights.empty())return out;
    double sum=0;
    for(double w:weights){require(std::isfinite(w)&&w>=0,PN2S_INVALID,"Alias weights must be finite and nonnegative");sum+=w;}
    require(std::isfinite(sum)&&sum>0,PN2S_INVALID,"Alias weights need a positive finite sum");
    const double average=sum/static_cast<double>(weights.size());
    require(average>0,PN2S_INVALID,"Alias weight average underflow");
    for(size_t k=0;k<weights.size();++k) {
        AliasEntry e{weights[k]/average,static_cast<uint32_t>(k)};
        (e.threshold>=1.?large:small).push_back(e);
    }
    size_t s=0,l=0;
    while(s<small.size() && l<large.size()) {
        out[small[s].alias]={small[s].threshold,large[l].alias};
        large[l].threshold-=(1.-small[s].threshold);
        if(large[l].threshold>=1.)++s;
        else small[s]=large[l++];
    }
    for(;s<small.size();++s)out[small[s].alias].threshold=1.;
    for(;l<large.size();++l)out[large[l].alias].threshold=1.;
    return out;
}
uint32_t aliasDraw(Random &rng,std::span<const double> weights) {
    auto table=aliasTable(weights);
    if(table.empty())table.push_back({1.,0});
    const auto count=static_cast<uint32_t>(table.size());uint32_t column=0;
    if(count!=1) {
        uint32_t bucket=UINT32_MAX/count;
        if(UINT32_MAX%count==count-1)++bucket;
        do{column=rng.next()/bucket;}while(column>=count);
    }
    const double u=static_cast<double>(rng.next())*0x1p-32;
    return table[column].threshold>u?column:table[column].alias;
}
}
