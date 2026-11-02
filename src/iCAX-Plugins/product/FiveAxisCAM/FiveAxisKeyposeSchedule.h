#pragma once
#include <algorithm>
#include <atomic>
#include <exception>
#include <functional>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace iCAX::FiveAxisCAM::KeyposeCollision {
// Samples describe the original TCP curve; they are not collision jobs.
// One interpolation job owns the entire interval between adjacent keyposes.
struct Interval {
    std::size_t First=0,Last=0;
    std::vector<std::size_t> Rows;
};
inline std::vector<Interval> Intervals(const std::vector<std::size_t>& keys,
    const std::vector<std::size_t>& requestedSamples) {
    if(keys.empty())throw std::invalid_argument("CAM_HEAD_KEYPOSES_REQUIRED");
    if(keys.size()==1){Interval interval{keys.front(),keys.front(),{}};for(std::size_t i=0;i<requestedSamples.size();++i)interval.Rows.push_back(i);return {interval};}
    std::vector<Interval> result;
    std::map<std::size_t,std::size_t> owners;
    for(std::size_t row=0;row<requestedSamples.size();++row){
        const auto upper=std::upper_bound(keys.begin(),keys.end(),requestedSamples[row]);
        if(upper==keys.begin()||upper==keys.end())throw std::invalid_argument("CAM_HEAD_KEYPOSE_RANGE_INVALID");
        const auto first=*std::prev(upper),last=*upper;
        auto [owner,inserted]=owners.emplace(first,result.size());
        if(inserted)result.push_back({first,last,{}});
        result[owner->second].Rows.push_back(row);
    }
    return result;
}

// All callbacks finish (and all workers join) before returning to the
// interpolation phase, including cancellation and exceptional exits.
inline void ParallelKeyposes(std::size_t count,unsigned workers,
    const std::function<bool()>& cancelled,const std::function<void(std::size_t)>& check) {
    if(!count)return;
    workers=static_cast<unsigned>(std::min<std::size_t>(count,std::max(1u,workers)));
    std::atomic_size_t next=0;std::atomic_bool failed=false;
    std::mutex mutex;std::exception_ptr error;
    const auto run=[&]{
        try{for(;;){
            if(failed.load()||(cancelled&&cancelled()))return;
            const auto index=next.fetch_add(1);if(index>=count)return;check(index);
        }}catch(...){std::lock_guard lock(mutex);if(!error)error=std::current_exception();failed.store(true);}
    };
    std::vector<std::jthread> pool;
    try{for(unsigned i=1;i<workers;++i)pool.emplace_back(run);run();}
    catch(...){failed.store(true);for(auto& worker:pool)worker.join();throw;}
    for(auto& worker:pool)worker.join();
    if(error)std::rethrow_exception(error);
    if(cancelled&&cancelled())throw std::runtime_error("MOTION_CANCELLED");
}
}
