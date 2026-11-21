#include "recovered_shaker_relations.hpp"
#include <cmath>
#include <stdexcept>

namespace study::recovered {
int32_t ShakerRelationTypes::append(){
    if(rows.size()>=size_t(INT32_MAX))throw std::length_error("Too many Shaker relation types");
    const auto previous=int32_t(rows.size());
    rows.emplace_back((rows.size()+31)/32,0);
    const size_t count=rows.size(),words=(count+31)/32;
    for(auto &row:rows){row.resize(words,0);row[size_t(previous)/32]&=~(uint32_t(1)<<(previous&31));}
    return previous;
}
int32_t ShakerRelationTypes::index(int32_t key){
    const auto found=types.find(key);if(found!=types.end())return found->second;
    const int32_t value=append();types.emplace(key,value);return value;
}
bool ShakerRelationTypes::related(int32_t a,int32_t b) const {
    return a>=0&&b>=0&&size_t(a)<rows.size()&&size_t(b)<rows.size()&&
        ((rows[size_t(a)][size_t(b)/32]>>(b&31))&1U)!=0;
}
void ShakerRelationTypes::set(int32_t a,int32_t b,bool value){
    if(a<0||b<0||size_t(a)>=rows.size()||size_t(b)>=rows.size())throw std::out_of_range("Shaker relation type");
    auto &word=rows[size_t(a)][size_t(b)/32];const auto mask=uint32_t(1)<<(b&31);
    if(value)word|=mask;else word&=~mask;
}
int32_t ShakerMovement::root(int32_t index){
    if(index<0||size_t(index)>=parents.size())throw std::out_of_range("Shaker movement index");
    auto current=index;size_t remaining=parents.size();
    while(parents[size_t(current)]!=current){
        if(!remaining--)throw std::invalid_argument("Cyclic Shaker movement parents");
        current=parents[size_t(current)];
        if(current<0||size_t(current)>=parents.size())throw std::out_of_range("Shaker movement parent");
    }
    while(index!=current){const auto next=parents[size_t(index)];parents[size_t(index)]=current;index=next;}
    return current;
}
bool ShakerMovement::common(uintptr_t first,uintptr_t second){
    const auto b=indices.try_emplace(second,0).first->second;
    const auto a=indices.try_emplace(first,0).first->second;
    const auto ar=root(a),br=root(b);return ar==br;
}
bool shakerRelationDominated(const ShakerRelationTypes &types,const ShakerRelationMap &a,
    const ShakerRelationMap &b,int32_t ta,int32_t tb){
    if(!types.related(ta,tb)||!a.contains(ta)||!b.contains(tb))return true;
    const auto &baseA=a.at(ta),&baseB=b.at(tb);
    // Original MAXSD returns its second operand (+0) for NaN or equal inputs.
    const auto positive=[](double v){return v>0?v:0.;};
    for(int32_t i=0;i<int32_t(types.rows.size());++i)for(int32_t j=0;j<int32_t(types.rows.size());++j){
        const auto ai=a.find(i),bj=b.find(j);
        if(ai==a.end()||bj==b.end()||!types.related(i,j)||(i==ta&&j==tb))continue;
        if(ai->second.category!=baseA.category||bj->second.category!=baseB.category)continue;
        const double lossA=positive(baseA.value-ai->second.value),lossB=positive(baseB.value-bj->second.value);
        const double gainA=positive(ai->second.value-baseA.value),gainB=positive(bj->second.value-baseB.value);
        const double difference=(gainB+gainA)-(lossA+lossB);
        if(difference>1e-7)return true;
        if(std::abs(difference)<1e-7&&(i<ta||(i==ta&&j<tb)))return true;
    }
    return false;
}
bool ShakerRelations::excluded(bool rotation,const ShakerRelationPart &a,const ShakerRelationPart &b,int32_t ka,int32_t kb){
    if(a.identity==b.identity)return true;
    const int32_t ta=types.index(ka),tb=types.index(kb);
    if(!types.related(ta,tb))return true;
    if((rotation&&(!a.locked[2]||!b.locked[2]))||a.force143||b.force143)return false;
    if(((!a.locked[0]||!b.locked[0])&&!x.common(a.identity,b.identity))||
       ((!a.locked[1]||!b.locked[1])&&!y.common(a.identity,b.identity))){
        if(a.id<b.id)return shakerRelationDominated(types,a.values,b.values,ta,tb);
        return shakerRelationDominated(types,b.values,a.values,tb,ta);
    }
    return true;
}
}
