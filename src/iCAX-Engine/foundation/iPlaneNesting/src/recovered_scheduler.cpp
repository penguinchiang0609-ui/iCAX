#include "recovered_scheduler.hpp"
#include <bit>
namespace study::recovered {
std::vector<double> strategyWeights(std::span<const StrategyEntry> entries) {
    std::vector<double> out;out.reserve(entries.size());
    for(const auto &e:entries)out.push_back(e.weight/static_cast<double>(e.calibration));
    return out;
}
void addStrategyBudgets(std::span<StrategyEntry> entries,int workers,double time,double totalWeight) {
    const double workerTime=static_cast<double>(workers)*time;
    for(auto &e:entries)e.budget=((workerTime*e.weight)/totalWeight)+e.budget;
}
std::optional<size_t> nextStrategy(Random &rng,ScheduleState &state,std::span<const double> weights) {
    if(state.trialLimit>=0 && state.trials>=state.trialLimit)return std::nullopt;
    require(!weights.empty(),PN2S_INVALID,"A scheduler needs at least one strategy");
    const size_t selected=aliasDraw(rng,weights);
    state.trials=std::bit_cast<int32_t>(static_cast<uint32_t>(state.trials)+1U);
    const int32_t capacity=truncateInt32(std::sqrt(static_cast<double>(state.trials))*state.poolGrowth);
    state.poolLimit=std::max(state.poolLimit,capacity);
    return selected;
}
ResultWeights selectResultWeights(std::span<const StrategyResult> pool,int32_t trials,double config198,double config190) {
    require(!pool.empty() && pool.size()<=INT32_MAX,PN2S_INVALID,"Result weighting requires a nonempty ordered pool");
    ResultWeights out;
    const double first=pool.front().metric;
    for(const auto &result:pool) {
        const double difference=result.metric-first;
        const double absolute=std::bit_cast<double>(std::bit_cast<uint64_t>(difference)&0x7fffffffffffffffULL);
        if(!(absolute/first<1e-7))break;
        ++out.equalPrefix;
    }
    const uint32_t a=static_cast<uint32_t>(truncateInt32(config198)),b=static_cast<uint32_t>(truncateInt32(config190)),n=static_cast<uint32_t>(trials);
    const uint64_t denominator=static_cast<uint64_t>(static_cast<int64_t>(std::bit_cast<int32_t>(b*a+n)));
    require(denominator!=0,PN2S_INVALID,"Result weight divisor is zero after original 32-bit arithmetic");
    const uint64_t numerator=static_cast<uint64_t>(static_cast<int64_t>(std::bit_cast<int32_t>(a+n)))*static_cast<uint64_t>(pool.size());
    uint32_t count=static_cast<uint32_t>(numerator/denominator);
    if(count==0)count=static_cast<uint32_t>(pool.size());
    out.selectedCount=std::min(out.equalPrefix,std::bit_cast<int32_t>(count));
    for(int k=0;k<out.selectedCount;++k)if(pool[k].sourceId>0)out.weights[pool[k].sourceId]+=1.;
    return out;
}
}
