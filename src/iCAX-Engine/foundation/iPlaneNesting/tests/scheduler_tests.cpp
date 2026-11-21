#include "recovered_scheduler.hpp"
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <bit>
using namespace study;using namespace study::recovered;
namespace {
size_t checks=0;
void check(bool v,const char *why){++checks;if(!v)throw std::runtime_error(why);}
bool bits(double a,double b){return std::bit_cast<uint64_t>(a)==std::bit_cast<uint64_t>(b);}
struct Owner {PN2_Session value=PN2_CreateSession();~Owner(){PN2_DeleteSession(value);}};
}
int main(){try {
    Owner owner,other;auto s=owner.value;
    const PN2S_StrategyEntry entries[]{{8,250,2,11},{3,17,1,7}};
    PN2S_ScheduleOptions options{sizeof(PN2S_ScheduleOptions),23,2,0,80,1,30,6,2.5};
    size_t selections=0;
    for(unsigned seed=0;seed<32;++seed) {
        options.seed=seed;auto schedule=PN2S_CreateStrategySchedule(s,entries,2,&options);
        check(!PN2_IsObjectError(schedule),"Public schedule creation failed");
        PN2S_StrategyEntry e{};double weight=0;
        check(PN2S_GetScheduleEntry(s,schedule,0,&e,&weight)==0 && e.strategy_id==3 && bits(e.accumulated_budget,17.) && bits(weight,1./17.),"Budget must use stored W, sampling must use calibration");
        check(PN2S_GetScheduleEntry(s,schedule,1,&e,&weight)==0 && e.strategy_id==8 && bits(e.accumulated_budget,31.) && bits(weight,2./250.),"Sorted preset order differs");
        int id=-1,trials=-1,capacity=-1;
        check(PN2S_NextScheduledStrategy(s,schedule,nullptr,&trials,&capacity)==PN2S_INVALID,"Null output must fail atomically");
        check(PN2S_NextScheduledStrategy(other.value,schedule,&id,&trials,&capacity)==PN2S_INVALID,"Cross-session schedule accepted");
        Random rng(seed);ScheduleState state{0,80,1,2.5};const std::array<double,2> weights{1./17.,2./250.};
        for(int k=0;k<84;++k) {
            auto selected=nextStrategy(rng,state,weights);auto status=PN2S_NextScheduledStrategy(s,schedule,&id,&trials,&capacity);
            check(status==(selected?1:0) && id==(selected?(*selected==0?3:8):0) && trials==state.trials && capacity==state.poolLimit,"Public scheduler state differs from recovered core");
            if(selected)++selections;
        }
        check(PN2S_DeleteStrategySchedule(s,schedule)==0 && PN2S_DeleteStrategySchedule(s,schedule)==PN2S_INVALID,"Schedule ownership/release differs");
        check(PN2S_NextScheduledStrategy(s,schedule,&id,&trials,&capacity)==PN2S_INVALID,"Expired schedule accepted");
    }
    auto bad=options;bad.struct_size--;check(PN2_ErrorCodeFromObject(PN2S_CreateStrategySchedule(s,entries,2,&bad))==PN2S_INVALID,"Bad schedule ABI accepted");
    check(PN2_ErrorCodeFromObject(PN2S_CreateStrategySchedule(s,nullptr,0,&options))==PN2S_INVALID,"Empty schedule accepted");
    for(auto entry:std::array{PN2S_StrategyEntry{3,0,1,0},PN2S_StrategyEntry{3,17,0,0},PN2S_StrategyEntry{3,17,-1,0},PN2S_StrategyEntry{3,17,1e308,1e308}})
        check(PN2_ErrorCodeFromObject(PN2S_CreateStrategySchedule(s,&entry,1,&options))==PN2S_INVALID,"Invalid schedule arithmetic accepted");
    PN2S_StrategyEntry duplicates[]{{3,17,1,0},{3,250,1,0}};
    check(PN2_ErrorCodeFromObject(PN2S_CreateStrategySchedule(s,duplicates,2,&options))==PN2S_INVALID,"Duplicate manager ID accepted");
    const PN2S_StrategyResult pool[]{{100,3},{100,8},{100,3},{100,0},{100,3},{100,8},{100,3},{100.1,8}};
    int count=-1,prefix=-1,selected=-1;
    check(PN2S_SelectStrategyWeights(pool,8,12,2,2,nullptr,0,&count,&prefix,&selected)==0 && count==2 && prefix==7 && selected==7,"Result prefix/selected count differs");
    PN2S_StrategyWeight output[2]{{-9,-9},{-9,-9}};
    count=prefix=selected=-1;
    check(PN2S_SelectStrategyWeights(pool,8,12,2,2,output,1,&count,&prefix,&selected)==PN2S_RESOURCE_LIMIT && count==-1 && prefix==-1 && selected==-1 && output[0].strategy_id==-9,"Short result buffer must leave all outputs unchanged");
    check(PN2S_SelectStrategyWeights(pool,8,12,2,2,output,2,&count,&prefix,&selected)==0 && output[0].strategy_id==3 && output[0].weight==4 && output[1].strategy_id==8 && output[1].weight==2,"Positive source frequencies differ");
    check(PN2S_SelectStrategyWeights(pool,8,0,65536,65536,nullptr,0,&count,&prefix,&selected)==PN2S_INVALID,"Wrapped zero divisor accepted");
    check(PN2S_SelectStrategyWeights(nullptr,0,1,1,1,nullptr,0,&count,&prefix,&selected)==PN2S_INVALID,"Empty result pool accepted");
    size_t resultCases=0;
    for(int test=0;test<600;++test) {
        std::vector<PN2S_StrategyResult> input;std::vector<StrategyResult> raw;
        for(int j=0;j<1+test%17;++j){const double metric=(test%5==0?0.:100.)*(1.+(j>test%7?1e-6:0));const int id=j%5-1;input.push_back({metric,id});raw.push_back({metric,id});}
        auto expected=selectResultWeights(raw,test,2.75,4.9);std::vector<PN2S_StrategyWeight> results(20);
        check(PN2S_SelectStrategyWeights(input.data(),static_cast<int>(input.size()),test,2.75,4.9,results.data(),20,&count,&prefix,&selected)==0,"Public result reweight failed");
        check(prefix==expected.equalPrefix && selected==expected.selectedCount && static_cast<size_t>(count)==expected.weights.size(),"Public result reweight metadata differs");
        size_t k=0;for(auto [id,weight]:expected.weights){check(results[k].strategy_id==id && bits(results[k].weight,weight),"Public result frequency differs");++k;}++resultCases;
    }
    // The original selector holds a mutex around draw, increment and capacity
    // update. Verify the public adapter has the same serialized state behavior.
    options.seed=42;options.max_trials=257;
    auto schedule=PN2S_CreateStrategySchedule(s,entries,2,&options);check(!PN2_IsObjectError(schedule),"Concurrent setup failed");
    struct Ticket {int trial,id,capacity;};std::vector<Ticket> tickets;std::mutex recordMutex;std::atomic<int> failures=0;std::vector<std::thread> workers;
    for(int worker=0;worker<8;++worker)workers.emplace_back([&]{for(;;){int id,trial,capacity;const int status=PN2S_NextScheduledStrategy(s,schedule,&id,&trial,&capacity);if(status<=0){if(status<0)++failures;break;}std::lock_guard lock(recordMutex);tickets.push_back({trial,id,capacity});}});
    for(auto &worker:workers)worker.join();
    check(failures==0 && tickets.size()==257,"Concurrent scheduler lost or duplicated trials");
    std::sort(tickets.begin(),tickets.end(),[](auto a,auto b){return a.trial<b.trial;});Random rng(42);ScheduleState state{0,257,1,2.5};
    for(const auto &ticket:tickets){auto selectedIndex=nextStrategy(rng,state,std::array<double,2>{1./17.,2./250.});check(ticket.trial==state.trials && ticket.capacity==state.poolLimit && ticket.id==(*selectedIndex==0?3:8),"Concurrent state is not a serial original-compatible draw sequence");}
    PN2S_DeleteStrategySchedule(s,schedule);
    std::cout<<"{\"pass\":true,\"checks\":"<<checks<<",\"publicSelections\":"<<selections<<",\"resultCases\":"<<resultCases<<",\"concurrentTickets\":"<<tickets.size()<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
