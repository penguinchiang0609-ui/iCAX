#pragma once
#include <algorithm>
#include <atomic>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace iCAX::FiveAxisCAM::RapidParallel {
// One bounded queue per batch; all workers join before any result is published.
// Each action owns its output slot and mutable collision session. Geometry and
// the prepared obstacle meshes are shared read-only across the queue.
template<class Action>
unsigned Run(std::size_t count,unsigned budget,const std::function<bool()>& cancelled,Action action){
    if(!count)return 0;
    const auto workers=static_cast<unsigned>(std::min<std::size_t>(count,std::max(1u,budget)));
    std::atomic_size_t next=0;
    std::atomic_uint active=0,peak=0;
    std::atomic_bool ready=false,failed=false;
    std::mutex mutex;std::exception_ptr error;
    const auto stop=[&]{return failed.load()||(cancelled&&cancelled());};
    std::vector<std::jthread> threads;threads.reserve(workers);
    try{
        for(unsigned i=0;i<workers;++i)threads.emplace_back([&]{
            ready.wait(false);
            while(!stop()){
                const auto index=next.fetch_add(1);if(index>=count)break;
                const auto running=active.fetch_add(1)+1;auto old=peak.load();
                while(old<running&&!peak.compare_exchange_weak(old,running)){}
                try{action(index,stop);}
                catch(...){std::lock_guard lock(mutex);if(!error)error=std::current_exception();failed=true;}
                --active;
            }
        });
    }catch(...){failed=true;ready=true;ready.notify_all();throw;}
    ready=true;ready.notify_all();threads.clear();
    if(error)std::rethrow_exception(error);
    if(cancelled&&cancelled())throw std::runtime_error("TRAVEL_CANCELLED");
    return peak.load();
}
}
