#include "recovered_escher_nester.hpp"
#include <bit>
namespace study::recovered {
namespace {
int32_t add(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));}
int64_t subtract(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t round64(double value){const double shifted=value<0.?value-.5:value+.5;return shifted>=-0x1p63&&shifted<0x1p63?int64_t(shifted):INT64_MIN;}
}
std::vector<EscherNesterPriority> escherNesterPriorities(std::span<const EscherNesterPart> parts,const std::function<int32_t(uintptr_t)> &available){
    require(bool(available),PN2S_INVALID,"Escher nester requires live unplaced quantity queries");std::vector<EscherNesterPriority> result;result.reserve(parts.size());
    for(const auto &part:parts){const auto value=part.optional?0.:part.overrideValue220<0.?part.area58:part.overrideValue220;
        result.push_back({double(available(part.key))*std::bit_cast<double>(std::bit_cast<uint64_t>(value)^0x8000000000000000ULL),part.key});}
    return result;
}
std::vector<uintptr_t> escherNesterOrder(std::span<const EscherNesterPriority> priorities,int32_t trial,Random &random){
    std::vector<EscherNesterPriority> sorted(priorities.begin(),priorities.end());
    // Finite weights and unique Part identities give the original pair a total
    // order; equal weights compare the unsigned Part pointer.
    for(const auto &item:sorted)require(std::isfinite(item.weight),PN2S_INVALID,"Escher nester priority is not finite");
    std::sort(sorted.begin(),sorted.end(),[](const auto &a,const auto &b){return a.weight<b.weight||(a.weight==b.weight&&a.key<b.key);});
    std::vector<uintptr_t> result;result.reserve(sorted.size());for(const auto &item:sorted)result.push_back(item.key);
    if(trial>1)for(size_t index=1;index<result.size();++index)if(random.integer(1,99)>75)std::swap(result[index-1],result[index]);return result;
}
int32_t escherOrdinaryNester(const EscherNesterInput &input,Random &random,EscherNesterHost &host){
    const auto priorities=escherNesterPriorities(input.parts,[&](uintptr_t key){return host.available(key);});
    // The early repeated-part gate precedes the optional perturbation draws.
    auto ordered=escherNesterOrder(priorities,0,random);int32_t repeated=0,beforeRepeated=0;
    for(const auto key:ordered){const auto part=std::find_if(input.parts.begin(),input.parts.end(),[&](const auto &value){return value.key==key;});require(part!=input.parts.end(),PN2S_INTERNAL,"Escher nester lost its source Part identity");
        if(!part->optional&&part->required>=8)++repeated;else if(!repeated)++beforeRepeated;if(beforeRepeated>=5)return 0;}
    if(input.trial13bc>1)for(size_t index=1;index<ordered.size();++index)if(random.integer(1,99)>75)std::swap(ordered[index-1],ordered[index]);
    host.prepareGreedy(ordered,subtract(input.maximumRight,round64(input.leftTrim70*1e6)));
    int32_t result=0;double gap=0.;bool fillBeforeNext=false;
    for(const auto key:ordered){if(host.remainingTime()<=0.||host.stopped())return result;
        if(fillBeforeNext)result=add(result,host.greedy());
        const auto part=std::find_if(input.parts.begin(),input.parts.end(),[&](const auto &value){return value.key==key;});if(gap<part->gap210)gap=part->gap210;
        const auto placed=host.pattern(key,gap);result=add(result,placed);fillBeforeNext=placed>0&&input.fallback90;}
    if(host.hasRemaining()&&input.fallback90)result=add(result,host.fallback());return result;
}
}
