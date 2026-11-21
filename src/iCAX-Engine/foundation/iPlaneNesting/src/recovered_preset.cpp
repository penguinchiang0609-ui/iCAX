#include "recovered_preset.hpp"
#include <bit>
#include <list>
namespace study::recovered {
void addPreset(Preset &preset,StrategyEntry entry) {
    if(entry.weight==0.)return;
    preset.entries[entry.id]=entry;
    preset.totalWeight=entry.weight+preset.totalWeight;
}
void decimalPreset(Preset &preset,int64_t encoded,const std::function<std::optional<StrategyEntry>(int)> &factory) {
    if(encoded<=0)return;
    require(encoded<=9999999999LL,PN2S_UNSUPPORTED,"Preset references a manager beyond original IDs 1..10");
    int id=1;
    for(;encoded;encoded/=10,++id) {
        const int weight=static_cast<int>(encoded%10);
        if(weight)if(auto entry=factory(id)){entry->weight=static_cast<double>(weight);addPreset(preset,*entry);}
    }
}
int32_t strategyCalibration(int id,int32_t offsetD8,double columnConfig2b0) {
    switch(id) {
    case 1:return offsetD8;
    case 2:return std::bit_cast<int32_t>(static_cast<uint32_t>(truncateInt32(columnConfig2b0))*35U)/100;
    case 3:return 17;case 4:return 1;case 5:return 13;case 6:return 20;
    case 7:return 3;case 8:return 250;case 9:return 20;case 10:return 15;
    default:throw Failure(PN2S_UNSUPPORTED,"Unknown original strategy manager");
    }
}
bool mapEligible(MapEligibilityState &state,const MapEligibilityInput &input) {
    if(state.status!=3)return state.status==2;
    require(input.parts.size()<=INT32_MAX,PN2S_RESOURCE_LIMIT,"Eligibility type count exceeds signed original field");
    if(static_cast<int32_t>(input.parts.size())>truncateInt32(input.typeLimit188))state.status=1;
    else {
        double sum=0;
        const double pitch=static_cast<double>(input.pitch230)/1000000.;
        for(const auto &p:input.parts)sum+=((p.maxY-p.minY)+(p.maxX-p.minX))/pitch;
        const double scale=static_cast<double>(input.problemScale);
        const double predicted=(sum/static_cast<double>(input.parts.size()))*((scale*.00002969*scale+scale*.00571)-.2569);
        if(predicted>input.memoryLimitD0*.25)state.status=1;
        else if(state.costLimit<0.)state.status=2;
        else {
            require(static_cast<bool>(input.estimateCost),PN2S_INVALID,"Missing map cost provider");
            state.status=input.estimateCost()>state.costLimit?0:2;
        }
    }
    return state.status==2;
}
bool columnEligible(bool map,int32_t population88,int32_t scale,bool special) {
    const auto product=std::bit_cast<int32_t>(static_cast<uint32_t>(population88)*static_cast<uint32_t>(scale));
    return map && product<20001 && !special;
}
bool sheetIsSpecial(size_t segmentCount,std::array<double,4> margins) {
    if(segmentCount==0)return false;
    size_t positive=0;for(double margin:margins)if(margin>0.)++positive;
    return segmentCount!=positive;
}
bool preNestMixed(std::span<const int32_t> keys) {
    for(size_t k=1;k<keys.size();++k)if(keys[k]!=keys[k-1])return true;
    return false;
}
bool preNestCompatible(const PreNestInput &input) {
    bool compatible=true;
    if(input.mode90>0) {
        require(input.finalized2c0,PN2S_INVALID,"Pre-nest constraints must be finalized");
        compatible=input.flag2ba && input.flag2b2;
    }
    if(input.mode90>1)return compatible;
    if(input.constraintCount8 || input.constraintCount30)return false;
    for(auto tileable:input.constraintTileable)if(!tileable)return false;
    return compatible;
}
bool escherEligible(int32_t &cache,const EligibilityInput &input) {
    if(cache!=2)return cache==1;
    bool tileable=input.hasTiles28;
    if(input.hasPreNest && (!input.preTileable || input.preMixed))tileable=false;
    bool repeated=false;
    for(const auto &p:input.parts) {
        if(!p.optional && p.required>=8)repeated=true;
        if(p.geometry7c>1 || !p.descriptor238){cache=0;return false;}
    }
    cache=tileable && repeated?1:0;return cache==1;
}
bool rectangleEligible(const EligibilityInput &input) {
    if(input.hasPreNest && (!input.preTileable || input.preMixed || input.preFlag4c || input.preCount50>=1))return false;
    std::vector<uint64_t> insertions;std::map<uint64_t,size_t> typeIndices;
    for(const auto &group:input.rectangleGroups) {
        if(group.empty())continue;
        bool same=true;for(auto index:group)if(index!=group.front())same=false;
        if(!same)continue;
        require(group.front()<input.parts.size(),PN2S_INVALID,"Rectangle group references missing type");
        const auto &p=input.parts[group.front()];
        if(input.rectangleForceB0 || (p.maxY-p.minY)*(p.maxX-p.minX)*.95<p.area || (!p.optional && p.required>4)) {
            const uint64_t identity=p.identity?p.identity:reinterpret_cast<uint64_t>(&p);
            insertions.push_back(identity);typeIndices[identity]=group.front();
        }
    }
    const auto weighted=[](const EligibilityPart &p) {return p.optional?0.:static_cast<double>(p.required)*(p.overrideValue<0.?p.value:p.overrideValue);};
    double total=0,selected=0;int32_t count=0;
    for(const auto &p:input.parts)total+=weighted(p);
    for(auto identity:rectangleTypeOrder(insertions)){const auto &p=input.parts[typeIndices.at(identity)];selected=weighted(p)+selected;count=std::bit_cast<int32_t>(static_cast<uint32_t>(count)+static_cast<uint32_t>(p.optional?0:p.required));}
    return total*.1<selected || truncateInt32(input.rectangleCount998)<=count || .34<selected/(input.sheetHeight*input.sheetHeight);
}
std::vector<uint64_t> rectangleTypeOrder(std::span<const uint64_t> insertions) {
    // MSVC pointer hash (FNV-1a over the eight little-endian identity bytes),
    // 408ca0 insert and 4099b0/409e30 growth. The list order determines floating
    // accumulation order in 408190, so a sorted set is not interchangeable.
    auto hash=[](uint64_t value){uint64_t h=0xcbf29ce484222325ULL;for(int k=0;k<8;++k){h^=(value>>(8*k))&255;h*=0x100000001b3ULL;}return h;};
    std::list<uint64_t> order;
    std::vector<std::list<uint64_t>::iterator> first(8,order.end());
    std::set<uint64_t> present;
    auto insert=[&](uint64_t value){auto &at=first[hash(value)&(first.size()-1)];at=order.insert(at,value);};
    for(auto value:insertions) {
        if(!present.insert(value).second)continue;
        if(static_cast<float>(order.size()+1)/static_cast<float>(first.size())>1.f) {
            size_t wanted=std::max<size_t>(8,static_cast<size_t>(std::ceil(static_cast<float>(order.size()+1))));
            if(first.size()<512 && first.size()*8>=wanted)wanted=first.size()*8;
            wanted=std::bit_ceil(wanted);
            std::vector<uint64_t> old(order.begin(),order.end());order.clear();first.assign(wanted,order.end());
            for(auto item:old)insert(item);
        }
        insert(value);
    }
    return {order.begin(),order.end()};
}
bool multiheadsEligible(const EligibilityInput &input) {return input.heads48>=2 && (!input.hasPreNest || input.preMultiheadsCompatible);}
bool unplacedEligible(std::span<const uint8_t> flags) {for(auto flag:flags)if(flag)return true;return false;}
}
