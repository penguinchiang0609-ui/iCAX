#include "SheetNestingPipeline.h"
#include "SheetNestingPlaneStrategies.h"
#include "SheetNestingConstructionTrace.h"
#include "NestingExecution.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <functional>
#include <iostream>
#include <mutex>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <vector>

using namespace iCAX::SheetNesting;
namespace
{
using Clock=std::chrono::steady_clock;
using Function=std::function<Result(const Problem&,const Options&)>;
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Event
{
    std::mutex Mutex;std::condition_variable Changed;bool Set=false;
    void Signal(){std::lock_guard lock(Mutex);Set=true;Changed.notify_all();}
    bool Wait(){std::unique_lock lock(Mutex);return Changed.wait_for(lock,std::chrono::seconds(2),[&]{return Set;});}
};
struct Probe
{
    const std::thread::id Owner=std::this_thread::get_id();
    std::atomic<int> Active{0},Calls{0};std::atomic<bool> WrongThread{false},Concurrent{false};
    struct Visit
    {
        Probe& Value;
        explicit Visit(Probe& value):Value(value)
        {if(Value.Active.fetch_add(1)!=0)Value.Concurrent=true;++Value.Calls;if(std::this_thread::get_id()!=Value.Owner)Value.WrongThread=true;}
        ~Visit(){--Value.Active;}
    };
    void Check()const{Require(!WrongThread&&!Concurrent,"user callback/factory escaped serialized calling-thread execution");}
};
class Constructor final:public IStrategy
{
    Function Work;bool Composed;
public:
    Constructor(Function work,bool composed=false):Work(std::move(work)),Composed(composed){}
    Result Run(const Problem& p,const Options& o)override{return Work(p,o);}
    bool OwnsRefinementLifecycle()const noexcept override{return Composed;}
};
class PureComposite final:public IStrategy
{
    Function Work;
public:
    explicit PureComposite(Function work):Work(std::move(work)){}
    Result Run(const Problem& p,const Options& o)override{return Work(p,o);}
    bool OwnsRefinementLifecycle()const noexcept override{return true;}
    bool IsPureConstruction()const noexcept override{return true;}
};
StrategyFactory Factory(Function function,bool composed=false)
{return [function=std::move(function),composed]{return std::make_unique<Constructor>(function,composed);};}
Problem Input()
{
    Part p;p.ID="original";p.Material="steel";p.FreeRotation=false;p.AllowedAnglesDegrees={0};
    p.Geometry.Outer={{0,0},{2,0},{2,2},{0,2}};
    Problem input;input.Parts={p};input.Stocks={{"sheet",20,20,2,1,"steel"}};return input;
}
Options Budget(){Options o;o.TimeLimitSeconds=5;o.MaxPlacementAttempts=1000;o.Ranking=RankingPolicy::LowThenLeft;return o;}
Result At(const Problem& p,double x,double y,std::size_t attempts=1)
{
    Result r;r.State=Status::Complete;r.Stats.PlacementAttempts=attempts;
    Layout b{p.Stocks[0].ID,0,p.Stocks[0].Width,p.Stocks[0].Height,{},0};
    for(const auto& part:p.Parts)
    {
        const auto angle=part.AllowedAnglesDegrees.empty()?0:part.AllowedAnglesDegrees.front();
        const auto box=Detail::Bounds(part.Geometry,{0,0,angle});
        for(std::size_t i=0;i<part.Quantity;++i)
        {b.Placements.push_back({part.ID,i,{x-box.X,y-box.Y,angle},17,false});x+=box.Width+p.PartGap;b.MaterialArea+=Detail::Area(part.Geometry);}
    }
    r.Layouts={b};return r;
}
Result Empty(const Problem& p,Status state=Status::Partial)
{Result r;r.State=state;for(const auto& part:p.Parts)if(part.Quantity)r.Remaining.push_back({part.ID,part.Quantity});return r;}
JobResult Run(const Problem& p,const StrategyRegistry& constructors,std::vector<std::string> ids,
    ParallelConstructionSettings settings,Options options=Budget())
{
    StrategyRegistry execution;auto factory=MakeParallelConstructionPortfolioFactory(constructors,std::move(ids),std::move(settings));
    execution.Register("parallel",std::move(factory));return RunJob({"parallel","parallel",p,std::move(options)},execution);
}
void Complete(const JobResult& result)
{
    if(!result.Checked.Valid||!result.Checked.Complete)
    {for(const auto& s:result.Output.Diagnostics)std::cerr<<s<<'\n';for(const auto& s:result.Checked.Errors)std::cerr<<s<<'\n';}
    Require(result.Checked.Valid&&result.Checked.Complete,"parallel pool lost a complete original-valid result");
}
double X(const JobResult& result){return result.Output.Layouts.at(0).Placements.at(0).Transform.X;}
int passed=0,failed=0;
void Test(const char* name,const std::function<void()>& work)
{try{work();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
}

int main()
{
    Test("constructors genuinely overlap",[]
    {
        struct Rendezvous{std::mutex Mutex;std::condition_variable Changed;int Started=0,Active=0,Peak=0;bool TimedOut=false;std::set<std::thread::id> Threads;} shared;
        Probe cancel;StrategyRegistry registry;
        for(int i=0;i<2;++i)registry.Register("worker"+std::to_string(i),Factory([&,i](const Problem& p,const Options& o)
        {
            Require(!o.EnableBasicCompaction,"automatic refinement entered an initial constructor");
            if(o.Cancel)o.Cancel();
            {
                std::unique_lock lock(shared.Mutex);++shared.Started;++shared.Active;
                shared.Peak=std::max(shared.Peak,shared.Active);shared.Threads.insert(std::this_thread::get_id());shared.Changed.notify_all();
                if(!shared.Changed.wait_for(lock,std::chrono::seconds(2),[&]{return shared.Started==2;}))shared.TimedOut=true;
                --shared.Active;
            }
            return At(p,3+i,3+i);
        }));
        auto options=Budget();options.Cancel=[&]{Probe::Visit visit(cancel);return false;};
        ParallelConstructionSettings settings;settings.Workers=2;
        const auto result=Run(Input(),registry,{},settings,options);Complete(result);
        Require(!shared.TimedOut&&shared.Peak==2&&shared.Threads.size()==2,"independent constructor Run calls were serialized");
        cancel.Check();Require(cancel.Calls>0,"calling-thread cancellation was never polled");
    });

    Test("factories are serial and one attempt cap covers every worker",[]
    {
        Probe factories,cancel;std::array<std::size_t,5> quotas{},spent{};std::atomic<int> prepared{0},composedRuns{0};
        std::atomic<bool> preparationIncomplete{false};StrategyRegistry registry;std::vector<std::string> ids;
        for(std::size_t i=0;i<5;++i)
        {
            const auto id="bounded"+std::to_string(i);ids.push_back(id);
            registry.Register(id,[&,i]
            {
                Probe::Visit visit(factories);std::this_thread::sleep_for(std::chrono::milliseconds(1));++prepared;
                return std::make_unique<Constructor>([&,i,calls=0](const Problem& p,const Options& o)mutable
                {
                    Require(++calls==1&&!o.EnableBasicCompaction,"workers reused mutable constructor state or refined it");
                    if(prepared!=8)preparationIncomplete=true;
                    quotas[i]=o.MaxPlacementAttempts;
                    while(spent[i]<o.MaxPlacementAttempts){if(o.Cancel&&o.Cancel())break;++spent[i];}
                    const double position=2+static_cast<double>(i);
                    return At(p,position,position,spent[i]);
                });
            });
        }
        registry.Register("null",[&]()->std::unique_ptr<IStrategy>{Probe::Visit visit(factories);++prepared;return {};});
        registry.Register("factory-error",[&]()->std::unique_ptr<IStrategy>{Probe::Visit visit(factories);++prepared;throw std::runtime_error("factory failure");});
        registry.Register("composed",[&]{Probe::Visit visit(factories);++prepared;return std::make_unique<Constructor>([&](const Problem& p,const Options&){++composedRuns;return At(p,0,0);},true);});
        ids.insert(ids.end(),{"null","factory-error","composed","missing"});
        auto options=Budget();options.MaxPlacementAttempts=17;options.Cancel=[&]{Probe::Visit visit(cancel);return false;};
        ParallelConstructionSettings settings;settings.Workers=3;
        const auto result=Run(Input(),registry,ids,settings,options);Complete(result);
        Require(quotas==std::array<std::size_t,5>{4,4,3,3,3},"runnable routes did not share one finite attempt allowance");
        Require(std::accumulate(spent.begin(),spent.end(),std::size_t{})==17&&result.Output.Stats.PlacementAttempts==17,
            "parallel work exceeded or lost its total attempt accounting");
        Require(!preparationIncomplete&&composedRuns==0,"worker execution overlapped factory preparation or ran a composed strategy");
        factories.Check();cancel.Check();Require(factories.Calls==8,"a failed/null/composed factory was called on an unexpected path");
    });

    Test("pure composites are eligible independently of refinement ownership",[]
    {
        for(const bool parallel:{false,true})
        {
            std::atomic<int> pureRuns{0},refinementRuns{0};StrategyRegistry constructors;
            constructors.Register("pure-owner",[&]
            {return std::make_unique<PureComposite>([&](const Problem& p,const Options& o)
                {++pureRuns;Require(!o.EnableBasicCompaction,"automatic refinement entered a pure composite");return At(p,3,3);});});
            constructors.Register("refinement-owner",Factory([&](const Problem& p,const Options&)
                {++refinementRuns;return At(p,0,0);},true));
            ParallelConstructionSettings settings;settings.Workers=2;settings.FairScheduling=true;
            StrategyRegistry execution;
            execution.Register("construction",parallel?MakeParallelConstructionPortfolioFactory(constructors,{},settings):
                MakeConstructionPortfolioFactory(constructors));
            const auto result=RunJob({"pure-composite","construction",Input(),Budget()},execution);Complete(result);
            Require(pureRuns==1&&refinementRuns==0&&X(result)==3,
                "pure capability was confused with lifecycle ownership or admitted a refinement strategy");
        }
    });

    Test("fair local time slices let the final queued route run",[]
    {
        constexpr std::size_t count=6;std::array<std::size_t,count> runs{};std::array<double,count> slices{};
        std::atomic<int> active{0},peak{0};Probe cancel;StrategyRegistry registry;std::vector<std::string> ids;
        for(std::size_t i=0;i<count;++i)
        {
            const auto id="timed"+std::to_string(i);ids.push_back(id);
            registry.Register(id,Factory([&,i](const Problem& p,const Options& o)
            {
                ++runs[i];slices[i]=o.TimeLimitSeconds;Require(!o.EnableBasicCompaction,"fair construction entered refinement");
                const auto current=++active;auto previous=peak.load();
                while(current>previous&&!peak.compare_exchange_weak(previous,current)){}
                const auto began=Clock::now(),watchdog=began+std::chrono::seconds(2);
                while(Clock::now()<watchdog&&std::chrono::duration<double>(Clock::now()-began).count()<o.TimeLimitSeconds)
                {
                    if(o.Cancel&&o.Cancel())break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                --active;
                if(i+1==count)return At(p,2,2);
                auto result=Empty(p);result.Stats.PlacementAttempts=1;return result;
            }));
        }
        auto options=Budget();options.TimeLimitSeconds=1.2;options.MaxPlacementAttempts=120;
        options.Cancel=[&]{Probe::Visit visit(cancel);return false;};
        ParallelConstructionSettings settings;settings.Workers=2;settings.FairScheduling=true;
        const auto result=Run(Input(),registry,ids,settings,options);Complete(result);
        Require(std::all_of(runs.begin(),runs.end(),[](auto ran){return ran==1;})&&peak==2,
            "early constructors consumed the common deadline before the final queued route had a turn");
        Require(slices[0]>0&&slices[1]>0&&slices[0]<=.41&&slices[1]<=.41,
            "fair execution gave early routes the entire deadline instead of bounded local slices");
        Require(result.Output.State==Status::Complete&&X(result)==2&&result.Output.Stats.PlacementAttempts==count,
            "local time stops lost the last complete result, became user cancellation, or lost charged work");
        cancel.Check();
    });

    Test("fair leases refund unused attempts without a wave barrier",[]
    {
        constexpr std::size_t count=10;Event lastStarted;std::array<std::size_t,count> caps{},runs{};
        std::atomic<bool> waitingPassed{true};StrategyRegistry registry;std::vector<std::string> ids;
        for(std::size_t i=0;i<count;++i)
        {
            const auto id="refund"+std::to_string(i);ids.push_back(id);
            registry.Register(id,Factory([&,i](const Problem& p,const Options& o)
            {
                ++runs[i];caps[i]=o.MaxPlacementAttempts;
                Require(!o.EnableBasicCompaction,"fair lease execution refined a constructor");
                if(i==0&&!lastStarted.Wait())waitingPassed=false;
                if(i+1==count)
                {
                    lastStarted.Signal();Require(o.MaxPlacementAttempts>=45,
                        "many eligible routes divided the total attempt cap too thinly for a queued constructor");
                    return At(p,2,2,45);
                }
                return Empty(p);
            }));
        }
        auto options=Budget();options.MaxPlacementAttempts=120;
        ParallelConstructionSettings settings;settings.Workers=2;settings.FairScheduling=true;
        const auto result=Run(Input(),registry,ids,settings,options);Complete(result);
        Require(waitingPassed&&std::all_of(runs.begin(),runs.end(),[](auto ran){return ran==1;}),
            "a waiting first route imposed a wave barrier or unused leases starved queued constructors");
        Require(caps.back()>=45&&result.Output.Stats.PlacementAttempts==45&&X(result)==2,
            "unused attempt refunds were lost, double charged, or could not fund the final route");
    });

    Test("simultaneous fair leases preserve one actual global attempt cap",[]
    {
        Event headsEntered;std::atomic<int> heads{0},tailRuns{0};std::atomic<std::size_t> actualSpent{0};
        std::array<std::size_t,4> caps{};std::atomic<bool> waitingPassed{true};StrategyRegistry registry;std::vector<std::string> ids;
        for(int i=0;i<4;++i)
        {
            const auto id="charged"+std::to_string(i);ids.push_back(id);
            registry.Register(id,Factory([&,i](const Problem& p,const Options& o)
            {
                caps[i]=o.MaxPlacementAttempts;
                if(i>=2)++tailRuns;
                else {if(++heads==2)headsEntered.Signal();if(!headsEntered.Wait())waitingPassed=false;}
                actualSpent+=o.MaxPlacementAttempts;return At(p,2+i,2+i,o.MaxPlacementAttempts);
            }));
        }
        auto options=Budget();options.MaxPlacementAttempts=40;
        ParallelConstructionSettings settings;settings.Workers=2;settings.FairScheduling=true;
        const auto result=Run(Input(),registry,ids,settings,options);Complete(result);
        Require(waitingPassed&&heads==2&&tailRuns==2&&caps==std::array<std::size_t,4>{10,10,10,10}&&
            actualSpent==40&&result.Output.Stats.PlacementAttempts==40,
            "parallel fair leases consumed queued routes' reserved opportunity or oversubscribed the actual global cap");
    });

    Test("a queued fair route waits for a temporarily empty attempt pool",[]
    {
        std::atomic<int> runs{0};StrategyRegistry registry;
        for(int i=0;i<2;++i)registry.Register("tiny-cap"+std::to_string(i),Factory([&](const Problem& p,const Options& o)
        {
            Require(o.MaxPlacementAttempts==1,"a tiny global allowance was not leased as one whole attempt");
            if(runs.fetch_add(1)==0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));return Empty(p);
            }
            return At(p,2,2,1);
        }));
        auto options=Budget();options.MaxPlacementAttempts=1;
        ParallelConstructionSettings settings;settings.Workers=2;settings.FairScheduling=true;
        const auto result=Run(Input(),registry,{},settings,options);Complete(result);
        Require(runs==2&&X(result)==2&&result.Output.Stats.PlacementAttempts==1,
            "a temporarily empty fair pool permanently skipped a queued route instead of waiting for the unused lease");
    });

    Test("complete improvements publish before slow routes finish and never regress",[]
    {
        const auto p=Input();Event slowEntered,earlyPublished,bestPublished;
        std::atomic<bool> waitsPassed{true};Probe updates,cancel;std::mutex observedMutex;std::vector<JobResult> observed;
        StrategyRegistry registry;
        registry.Register("invalid",Factory([](const Problem& input,const Options&){return At(input,-2,0);}));
        registry.Register("early",Factory([&](const Problem& input,const Options& o)
        {Require(!o.EnableBasicCompaction,"outer compaction entered early construction");if(!slowEntered.Wait())waitsPassed=false;return At(input,8,8);}));
        registry.Register("better-late",Factory([&](const Problem& input,const Options&)
        {slowEntered.Signal();if(!earlyPublished.Wait())waitsPassed=false;return At(input,3,3);}));
        registry.Register("worse-last",Factory([&](const Problem& input,const Options&)
        {if(!bestPublished.Wait())waitsPassed=false;return At(input,6,6);}));
        registry.Register("partial",Factory([](const Problem& input,const Options&){return Empty(input);}));
        ParallelConstructionSettings settings;settings.Workers=3;
        settings.OnImprovement=[&](const JobResult& result)
        {
            Probe::Visit visit(updates);Require(Validate(p,result.Output).Complete&&result.Checked.Valid,"callback published partial/invalid geometry");
            {
                std::lock_guard lock(observedMutex);
                if(!observed.empty())Require(Detail::Quality(result,p,RankingPolicy::LowThenLeft)<Detail::Quality(observed.back(),p,RankingPolicy::LowThenLeft),"published best regressed or repeated");
                observed.push_back(result);
            }
            if(X(result)==8)earlyPublished.Signal();if(X(result)==3)bestPublished.Signal();
        };
        auto options=Budget();options.Cancel=[&]{Probe::Visit visit(cancel);return false;};
        const auto result=Run(p,registry,{"invalid","early","better-late","worse-last","partial"},settings,options);Complete(result);
        Require(waitsPassed&&X(result)==3&&observed.size()==2&&X(observed.front())==8&&X(observed.back())==3,
            "a complete result waited for all workers, lost its snapshot, or regressed to a late loser");
        Require(result.Output.State==Status::Complete,"ordinary completion was reported as cancellation");updates.Check();cancel.Check();
    });

    Test("user cancellation retains a late certified best without further callbacks",[]
    {
        Event slowEntered,published;std::atomic<bool> cancelRequested{false},slowStopped{false},waitsPassed{true};Probe cancel,updates,target;
        StrategyRegistry registry;
        registry.Register("early",Factory([&](const Problem& p,const Options&)
        {if(!slowEntered.Wait())waitsPassed=false;return At(p,7,7);}));
        registry.Register("cooperative",Factory([&](const Problem& p,const Options& o)
        {
            slowEntered.Signal();if(!published.Wait())waitsPassed=false;
            const auto end=Clock::now()+std::chrono::seconds(2);
            while(Clock::now()<end){if(o.Cancel&&o.Cancel()){slowStopped=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            auto result=At(p,2,2);result.State=Status::Cancelled;return result;
        }));
        ParallelConstructionSettings settings;settings.Workers=2;settings.OnImprovement=[&](const JobResult& result)
        {Probe::Visit visit(updates);Require(X(result)==7,"cancelled subscription received a late improvement");cancelRequested=true;published.Signal();};
        settings.StopWhen=[&](const Problem&,const JobResult&)
        {Probe::Visit visit(target);return false;};
        auto options=Budget();options.Cancel=[&]{Probe::Visit visit(cancel);return cancelRequested.load();};
        const auto result=Run(Input(),registry,{"early","cooperative"},settings,options);Complete(result);
        Require(waitsPassed&&slowStopped&&X(result)==2&&result.Output.State==Status::Cancelled,
            "user cancellation discarded a late certified better result or failed to stop a worker");
        Require(updates.Calls==1&&target.Calls==0,"confirmed user cancellation allowed another improvement or target callback");
        cancel.Check();updates.Check();target.Check();
    });

    Test("successful StopWhen stops workers without becoming user cancellation",[]
    {
        Event slowEntered;std::atomic<bool> waitsPassed{true},slowStopped{false};Probe target,cancel,updates;StrategyRegistry registry;
        registry.Register("target",Factory([&](const Problem& p,const Options&)
        {if(!slowEntered.Wait())waitsPassed=false;return At(p,4,4);}));
        registry.Register("waiting",Factory([&](const Problem& p,const Options& o)
        {
            slowEntered.Signal();const auto end=Clock::now()+std::chrono::seconds(2);
            while(Clock::now()<end){if(o.Cancel&&o.Cancel()){slowStopped=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            return Empty(p,Status::Cancelled);
        }));
        ParallelConstructionSettings settings;settings.Workers=2;
        settings.OnImprovement=[&](const JobResult& r){Probe::Visit visit(updates);Require(r.Checked.Complete,"target callback saw partial order");};
        settings.StopWhen=[&](const Problem& p,const JobResult& r)
        {Probe::Visit visit(target);Require(Validate(p,r.Output).Complete,"StopWhen received invalid or incomplete data");return true;};
        auto options=Budget();options.Cancel=[&]{Probe::Visit visit(cancel);return false;};
        const auto result=Run(Input(),registry,{"target","waiting"},settings,options);Complete(result);
        Require(waitsPassed&&slowStopped&&target.Calls==1&&result.Output.State==Status::Complete&&X(result)==4,
            "successful target stop was relabeled as user cancellation or lost best");
        target.Check();cancel.Check();updates.Check();
    });

    Test("one global deadline covers queued routes and retains complete best",[]
    {
        Event slowEntered;std::atomic<bool> waitsPassed{true};std::atomic<int> stopped{0},slowRuns{0};StrategyRegistry registry;
        registry.Register("early",Factory([&](const Problem& p,const Options&)
        {if(!slowEntered.Wait())waitsPassed=false;return At(p,5,5);}));
        for(int i=0;i<3;++i)registry.Register("slow"+std::to_string(i),Factory([&](const Problem& p,const Options& o)
        {
            ++slowRuns;slowEntered.Signal();const auto watchdog=Clock::now()+std::chrono::seconds(2);
            while(Clock::now()<watchdog){if(o.Cancel&&o.Cancel()){++stopped;break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            return Empty(p,Status::Cancelled);
        }));
        ParallelConstructionSettings settings;settings.Workers=2;
        auto options=Budget();options.TimeLimitSeconds=.15;
        const auto began=Clock::now();const auto result=Run(Input(),registry,{"early","slow0","slow1","slow2"},settings,options);
        const auto seconds=std::chrono::duration<double>(Clock::now()-began).count();Complete(result);
        Require(waitsPassed&&stopped>0&&slowRuns<=2&&seconds<1&&result.Output.Stats.ElapsedSeconds<1,
            "a queued constructor received a fresh wall-clock budget instead of the common deadline");
        Require(result.Output.State==Status::Complete&&X(result)==5,"deadline-proxy cancellation contaminated the retained complete result state");
    });

    Test("deadline-only cancelled child can supply the first complete incumbent",[]
    {
        Probe cancel;std::atomic<bool> deadlineObserved{false};StrategyRegistry registry;
        registry.Register("complete-on-deadline",Factory([&](const Problem& p,const Options& o)
        {
            const auto watchdog=Clock::now()+std::chrono::seconds(2);
            while(Clock::now()<watchdog)
            {
                if(o.Cancel&&o.Cancel()){deadlineObserved=true;break;}
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            // A cooperative constructor can retain a complete legal layout
            // while labeling its local stop Cancelled. There was no earlier
            // winner, so the final completed-worker collection must keep it.
            auto result=At(p,4,4,3);result.State=Status::Cancelled;return result;
        }));
        ParallelConstructionSettings settings;settings.Workers=1;
        auto options=Budget();options.TimeLimitSeconds=.1;
        options.Cancel=[&]{Probe::Visit visit(cancel);return false;};
        const auto result=Run(Input(),registry,{"complete-on-deadline"},settings,options);Complete(result);
        Require(deadlineObserved&&result.Output.State==Status::Complete&&X(result)==4&&
            result.Output.Stats.PlacementAttempts==3,
            "deadline-local Cancelled label hid a complete incumbent or became user cancellation");
        cancel.Check();
    });

    Test("worker and callback exceptions retain best and stop only failed callbacks",[]
    {
        Event slowEntered,published;std::atomic<bool> waitsPassed{true};Probe updates,target;StrategyRegistry registry;
        registry.Register("first",Factory([&](const Problem& p,const Options&)
        {if(!slowEntered.Wait())waitsPassed=false;return At(p,8,8);}));
        registry.Register("improve-after-callback-errors",Factory([&](const Problem& p,const Options&)
        {slowEntered.Signal();if(!published.Wait())waitsPassed=false;return At(p,2,2);}));
        registry.Register("worker-error",Factory([&](const Problem&,const Options&)->Result
        {if(!published.Wait())waitsPassed=false;throw std::runtime_error("worker failed");}));
        registry.Register("worker-nonstandard-error",Factory([&](const Problem&,const Options&)->Result
        {if(!published.Wait())waitsPassed=false;throw 7;}));
        ParallelConstructionSettings settings;settings.Workers=3;
        settings.OnImprovement=[&](const JobResult&){Probe::Visit visit(updates);published.Signal();throw std::runtime_error("subscriber failed");};
        settings.StopWhen=[&](const Problem&,const JobResult&)->bool{Probe::Visit visit(target);throw 9;};
        const auto result=Run(Input(),registry,{"first","improve-after-callback-errors","worker-error","worker-nonstandard-error"},settings);Complete(result);
        Require(waitsPassed&&X(result)==2&&result.Output.State==Status::Complete&&updates.Calls==1&&target.Calls==1,
            "an exception escaped, disabled both callbacks, discarded best, or stopped remaining constructors");
        Require(result.Output.Stats.PlacementAttempts<=Budget().MaxPlacementAttempts,"failed search did not stay within its globally reserved attempts");
        updates.Check();target.Check();
    });

    Test("parallel construction preserves original quantities angles and inventory",[]
    {
        auto p=Input();p.Parts[0].Quantity=2;p.Parts[0].AllowedAnglesDegrees={90};
        for(auto& q:p.Parts[0].Geometry.Outer){q.X-=7;q.Y+=11;}
        auto other=p.Parts[0];other.ID="other-original";other.Quantity=1;other.AllowedAnglesDegrees={0};p.Parts.push_back(other);
        p.PartGap=.25;p.EdgeMargin=.5;const auto original=p;StrategyRegistry registry;
        registry.Register("a",Factory([](const Problem& input,const Options& o){Require(!o.EnableBasicCompaction,"basic iteration entered constructor");return At(input,4,4);}));
        registry.Register("b",Factory([](const Problem& input,const Options&){return At(input,2,2);}));
        ParallelConstructionSettings settings;settings.Workers=2;const auto result=Run(p,registry,{},settings);Complete(result);
        std::set<std::pair<std::string,std::size_t>> ids;
        for(const auto& b:result.Output.Layouts)for(const auto& q:b.Placements)
        {
            Require(ids.emplace(q.PartID,q.Instance).second,"parallel selection duplicated an original instance");
            Require(q.Transform.AngleDegrees==(q.PartID==p.Parts[0].ID?90:0),"parallel selection relaxed a finite angle domain");
        }
        Require(ids.size()==3&&p.Stocks[0].Quantity==original.Stocks[0].Quantity&&p.PartGap==original.PartGap&&p.EdgeMargin==original.EdgeMargin,
            "parallel selection changed original demand or stock constraints");
        for(std::size_t i=0;i<p.Parts.size();++i)
            Require(p.Parts[i].Quantity==original.Parts[i].Quantity&&p.Parts[i].AllowedAnglesDegrees==original.Parts[i].AllowedAnglesDegrees&&
                std::equal(p.Parts[i].Geometry.Outer.begin(),p.Parts[i].Geometry.Outer.end(),original.Parts[i].Geometry.Outer.begin(),
                    [](Point a,Point b){return a.X==b.X&&a.Y==b.Y;}),"parallel execution mutated original contours or angle/quantity data");
    });

    Test("real NFP scanline and array workers overlap inside candidate generation",[]
    {
        auto p=Input();p.Parts[0].Quantity=3;p.Parts[0].AllowMirror=true;
        p.Parts[0].Geometry.Outer={{0,0},{4,0},{4,2},{0,2}};
        p.Parts[0].AllowedAnglesDegrees={0,90};p.Stocks[0].Quantity=1;
        p.PartGap=.2;p.EdgeMargin=.5;const auto original=p;
        const std::array<std::string,3> ids{{"plane.nfp-closest","plane.scanline","plane.array-deferred"}};
        StrategyRegistry production,routes;RegisterPlaneStrategies(production);
        struct Rendezvous
        {
            std::mutex Mutex;std::condition_variable Changed;
            std::size_t Entered=0,Active=0,Peak=0;std::set<std::thread::id> Threads;
        } shared;
        Event nfpCommitted,arrayPublished;
        std::atomic<bool> waitsPassed{true},nfpReturned{false},publishedBeforeNfpReturn{false};
        std::array<JobResult,3> native;
        std::array<std::vector<ConstructionTraceEvent>,3> evidence;
        Probe factories,updates;
        for(std::size_t i=0;i<ids.size();++i)routes.Register(ids[i],[&,i]
        {
            Probe::Visit visit(factories);
            auto child=std::shared_ptr<IStrategy>(production.Create(ids[i]));
            Require(child&&child->IsPureConstruction(),"a real canonical mechanism is unavailable");
            return std::make_unique<PureComposite>([&,i,child](const Problem& input,const Options& supplied)
            {
                Require(input.Parts.size()==1&&input.Parts[0].ID==original.Parts[0].ID&&
                    input.Parts[0].Quantity==3&&input.Parts[0].AllowMirror&&
                    input.Parts[0].AllowedAnglesDegrees==original.Parts[0].AllowedAnglesDegrees&&
                    input.Stocks[0].Quantity==1&&input.PartGap==.2&&input.EdgeMargin==.5&&
                    !supplied.EnableBasicCompaction&&bool(supplied.PublishConstructionCandidate),
                    "parallel mechanisms received different originals, permissions or lifecycle options");
                auto work=supplied;bool generated=false,committed=false;
                // Intercept the production leaf before forwarding to the
                // scheduler's serialized observer. Waiting inside the shared
                // observer itself would manufacture an observation deadlock.
                work.ConstructionTrace=[&](const ConstructionTraceEvent& event)
                {
                    evidence[i].push_back(event);
                    if(!generated&&event.Stage==ConstructionTraceStage::Generate&&event.CandidateID)
                    {
                        generated=true;std::unique_lock lock(shared.Mutex);
                        ++shared.Entered;++shared.Active;shared.Peak=std::max(shared.Peak,shared.Active);
                        shared.Threads.insert(std::this_thread::get_id());shared.Changed.notify_all();
                        if(!shared.Changed.wait_for(lock,std::chrono::seconds(2),[&]{return shared.Entered==ids.size();}))
                            waitsPassed=false;
                        --shared.Active;
                    }
                    if(!committed&&event.Source=="constructor.commit"&&!event.Members.empty())
                    {
                        committed=true;
                        if(i==0){nfpCommitted.Signal();if(!arrayPublished.Wait())waitsPassed=false;}
                        if(i==2&&!nfpCommitted.Wait())waitsPassed=false;
                    }
                    if(supplied.ConstructionTrace)supplied.ConstructionTrace(event);
                };
                work.PublishConstructionCandidate=[&](const Result& candidate)
                {
                    if(i!=2&&Validate(input,candidate).Complete&&!arrayPublished.Wait())waitsPassed=false;
                    supplied.PublishConstructionCandidate(candidate);
                };
                native[i].Output=child->Run(input,work);Detail::AssessCandidate(input,native[i]);
                if(i!=2&&!arrayPublished.Wait())waitsPassed=false;
                if(i==0)nfpReturned=true;
                return native[i].Output;
            });
        });
        auto options=Budget();options.TimeLimitSeconds=10;options.MaxPlacementAttempts=1000000;
        options.EnableCombinations=false;options.EnableGapFill=false;options.EnableBasicCompaction=false;
        options.Ranking=RankingPolicy::Utilization;
        options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
        options.ConstructionTrace=[](const ConstructionTraceEvent&){};
        ParallelConstructionSettings settings;settings.Workers=3;settings.FairScheduling=true;
        settings.ContinuousTimeRoutes.assign(ids.begin(),ids.end());
        settings.OnImprovement=[&](const JobResult& candidate)
        {
            Probe::Visit visit(updates);
            Require(candidate.Checked.Valid&&candidate.Checked.Complete,"a real worker published an invalid or partial layout");
            if(candidate.StrategyID==ids[2])
            {publishedBeforeNfpReturn=!nfpReturned.load();arrayPublished.Signal();}
        };
        const auto result=Run(p,routes,std::vector<std::string>(ids.begin(),ids.end()),settings,options);Complete(result);
        Require(waitsPassed&&shared.Entered==3&&shared.Peak==3&&shared.Threads.size()==3&&publishedBeforeNfpReturn,
            "real candidate generation was serialized or a complete array waited for the NFP worker to return");
        const auto utilization=[&](const Result& value)
        {
            double material=0,section=0;
            for(const auto& board:value.Layouts)
            {
                double top=0;
                for(const auto& placed:board.Placements)
                {
                    const auto box=Detail::Bounds(p.Parts[0].Geometry,placed.Transform);
                    top=std::max(top,box.Y+box.Height);material+=Detail::Area(p.Parts[0].Geometry);
                }
                section+=board.Width*std::clamp(top+p.EdgeMargin,0.,board.Height);
            }
            return material/section;
        };
        std::size_t attempts=0;const double best=utilization(result.Output);
        for(std::size_t i=0;i<ids.size();++i)
        {
            Require(native[i].Checked.Valid&&native[i].Checked.Complete&&best+1e-12>=utilization(native[i].Output),
                "global selection lost an independently completed worker's true original-material utilization");
            attempts+=native[i].Output.Stats.PlacementAttempts;
            std::set<std::tuple<std::string,std::uint64_t,std::uint64_t>> generatedIDs;
            std::uint64_t parent=0;std::size_t commits=0,gridProposals=0;bool batch=false;
            for(const auto& event:evidence[i])
            {
                if(event.Stage==ConstructionTraceStage::Generate&&event.CandidateID)
                    generatedIDs.emplace(event.Scope,event.ProposalID,event.CandidateID);
                if(i==1&&event.Stage==ConstructionTraceStage::Generate&&event.Source=="scanline.grid")
                {
                    ++gridProposals;
                    Require(event.Members.size()==1,"scanline generated a compound array instead of a real grid pose");
                    const auto box=Detail::Bounds(p.Parts[0].Geometry,event.Members[0].Transform);
                    const double gx=box.X-p.EdgeMargin,gy=box.Y-p.EdgeMargin;
                    Require(std::abs(gx-std::round(gx))<1e-7&&std::abs(gy-std::round(gy))<1e-7,
                        "scanline-labelled proposals did not lie on its physical one-mm scan lattice");
                }
                if(event.Source!="constructor.commit")continue;
                ++commits;batch|=event.Members.size()>1;
                Require(event.StateID&&event.ParentStateID==parent&&event.Layouts.size()==1&&
                    generatedIDs.contains({event.Scope,event.ProposalID,event.CandidateID}),
                    "a real worker commit lacked its generated candidate or independent parent state");
                if(i<2)Require(event.Members.size()==1,"single-original NFP or scanning was replaced with the array controller");
                parent=event.StateID;
            }
            Require(commits>0&&(i!=2||batch)&&(i!=1||gridProposals>3),
                "a strategy name hid the same single-placement flow instead of real array or scanline work");
        }
        Require(attempts==result.Output.Stats.PlacementAttempts&&attempts<=options.MaxPlacementAttempts&&
            !options.ConstructionObservation->Failed.load(),
            "parallel production attempts or trace health were corrupted by streamed candidates");
        factories.Check();updates.Check();
        Require(factories.Calls==3&&updates.Calls>0&&p.Parts[0].Quantity==original.Parts[0].Quantity&&
            p.Stocks[0].Quantity==original.Stocks[0].Quantity,"parallel factories or original inventory changed");
    });

    Test("real ordered array publishes while independent ordered NFP is still running",[]
    {
        auto p=Input();p.Parts[0].ID="short";p.Parts[0].Quantity=2;
        p.Parts[0].Geometry.Outer={{0,0},{4,0},{4,2},{0,2}};
        p.Parts[0].AllowedAnglesDegrees={0,90,180,270,23.7,203.7};p.Parts[0].AllowMirror=true;
        auto tall=p.Parts[0];tall.ID="tall";tall.Quantity=1;tall.Geometry.Outer={{0,0},{1,0},{1,6},{0,6}};
        auto middle=p.Parts[0];middle.ID="middle";middle.Quantity=1;middle.Geometry.Outer={{0,0},{3,0},{3,2},{0,2}};
        middle.FreeRotation=true;p.Parts.push_back(tall);p.Parts.push_back(middle);
        p.Stocks[0].Quantity=1;p.PartGap=.2;p.EdgeMargin=.5;p.AllowCommonEdges=true;const auto original=p;
        auto options=Budget();options.Ranking=RankingPolicy::Utilization;options.MaxPlacementAttempts=200000;
        options.EnableCombinations=false;options.EnableBasicCompaction=false;
        Event slowNativeEntered,fastPublished;std::atomic<bool> slowReturned{false},waitsPassed{true},fastBeforeSlow{false};
        Probe factories,updates;std::array<JobResult,2> native;std::array<std::thread::id,2> threads;
        std::array<std::atomic<bool>,2> started{};
        const std::array<std::string,2> ids{{"plane.order-ultra","plane.order-array"}};
        StrategyRegistry production,routes;RegisterPlaneStrategies(production);
        const auto requireOriginal=[&](const Problem& input)
        {
            Require(input.Parts.size()==original.Parts.size()&&input.Stocks.size()==original.Stocks.size()&&
                input.PartGap==original.PartGap&&input.EdgeMargin==original.EdgeMargin&&
                input.AllowCommonEdges==original.AllowCommonEdges&&input.Tolerance==original.Tolerance,
                "an independent mechanism received a different problem or spacing policy");
            for(std::size_t i=0;i<input.Parts.size();++i)
            {
                const auto& part=input.Parts[i];const auto& source=original.Parts[i];
                Require(part.ID==source.ID&&part.Quantity==source.Quantity&&part.Material==source.Material&&
                    part.FreeRotation==source.FreeRotation&&part.AllowMirror==source.AllowMirror&&
                    part.AllowedAnglesDegrees==source.AllowedAnglesDegrees&&
                    part.Geometry.Outer.size()==source.Geometry.Outer.size()&&part.Geometry.Holes.empty()&&
                    part.Geometry.Radius==source.Geometry.Radius&&part.Geometry.Center.X==source.Geometry.Center.X&&
                    part.Geometry.Center.Y==source.Geometry.Center.Y&&part.Geometry.CommonEdgeRings==source.Geometry.CommonEdgeRings&&
                    std::equal(part.Geometry.Outer.begin(),part.Geometry.Outer.end(),source.Geometry.Outer.begin(),
                        [](Point a,Point b){return a.X==b.X&&a.Y==b.Y;}),
                    "an independent mechanism lost original demand, geometry or orientation permissions");
            }
            for(std::size_t i=0;i<input.Stocks.size();++i)
            {
                const auto& stock=input.Stocks[i];const auto& source=original.Stocks[i];
                Require(stock.ID==source.ID&&stock.Width==source.Width&&stock.Height==source.Height&&
                    stock.Quantity==source.Quantity&&stock.Cost==source.Cost&&stock.Material==source.Material,
                    "an independent mechanism changed the original stock inventory");
            }
        };
        for(std::size_t i=0;i<ids.size();++i)routes.Register(ids[i],[&,i]
        {
            Probe::Visit visit(factories);
            auto child=std::shared_ptr<IStrategy>(production.Create(ids[i]));
            Require(bool(child)&&child->IsPureConstruction(),"a real ordered construction mechanism is unavailable");
            return std::make_unique<PureComposite>([&,i,child](const Problem& input,const Options& o)
            {
                requireOriginal(input);started[i]=true;threads[i]=std::this_thread::get_id();
                Require(o.OrientationProposals==OrientationProposalPolicy::AllowedReflections&&
                    !o.EnableBasicCompaction&&bool(o.PublishConstructionCandidate),
                    "an independent mechanism lost proposal permissions or candidate transport");
                auto childOptions=o;bool firstComplete=true;
                if(i==0)childOptions.PublishConstructionCandidate=[&](const Result& candidate)
                {
                    if(firstComplete&&Validate(input,candidate).Complete)
                    {
                        firstComplete=false;slowNativeEntered.Signal();
                        if(!fastPublished.Wait())waitsPassed=false;
                    }
                    o.PublishConstructionCandidate(candidate);
                };
                else if(!slowNativeEntered.Wait())waitsPassed=false;
                native[i].Output=child->Run(input,childOptions);Detail::AssessCandidate(input,native[i]);
                if(i==0)slowReturned=true;
                return native[i].Output;
            });
        });
        std::mutex traceMutex;std::map<std::string,bool> complete;
        std::map<std::string,double> completedUtilization;std::map<std::string,std::set<double>> policies;
        options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
        {
            std::lock_guard lock(traceMutex);std::map<std::string,double> measures(event.Measures.begin(),event.Measures.end());
            if(event.Source=="scheduler.route-result")complete[event.Route]=measures.at("complete")==1;
            if(event.Source=="scheduler.quality"&&complete[event.Route])
                completedUtilization[event.Route]=measures.at("usedSectionUtilization");
            if(event.Source=="order.permutation")policies[event.Route].insert(measures.at("orientationProposalPolicy"));
        };
        ParallelConstructionSettings settings;settings.Workers=2;settings.FairScheduling=true;
        settings.ContinuousTimeRoutes.assign(ids.begin(),ids.end());
        settings.OnImprovement=[&](const JobResult& candidate)
        {
            Probe::Visit visit(updates);
            if(candidate.StrategyID==ids[1]&&candidate.Checked.Complete)
            {if(!slowReturned.load())fastBeforeSlow=true;fastPublished.Signal();}
        };
        const auto result=Run(p,routes,std::vector<std::string>(ids.begin(),ids.end()),settings,options);Complete(result);
        Require(waitsPassed&&started[0]&&started[1]&&threads[0]!=threads[1]&&fastBeforeSlow,
            "the real ordered array waited for ordered NFP completion or did not publish on its own worker");
        const auto finalUtilization=-std::get<5>(Detail::Quality(result,p,options.Ranking));
        for(std::size_t i=0;i<ids.size();++i)
        {
            Require(native[i].Checked.Valid&&native[i].Checked.Complete&&complete.at(ids[i])&&
                policies.at(ids[i])==std::set<double>{static_cast<double>(OrientationProposalPolicy::AllowedReflections)},
                "an ordered mechanism lost complete original demand or legal orientation proposals");
            Require(finalUtilization+1e-12>=completedUtilization.at(ids[i])&&
                finalUtilization+1e-12>=-std::get<5>(Detail::Quality(native[i],p,options.Ranking)),
                "the global result is worse than an independently completed mechanism's true utilization");
        }
        Require(result.Output.Stats.PlacementAttempts<=options.MaxPlacementAttempts&&
            result.Output.Stats.PlacementAttempts==native[0].Output.Stats.PlacementAttempts+native[1].Output.Stats.PlacementAttempts,
            "independent ordered mechanisms exceeded the attempt cap or charged streamed snapshots twice");
        factories.Check();updates.Check();Require(factories.Calls==2&&updates.Calls>0,
            "real mechanism factories or improvement callbacks escaped the calling thread");requireOriginal(p);
    });

    Test("default race schedules canonical mechanisms without historical source or order duplicates",[]
    {
        auto p=Input();p.Parts[0].AllowMirror=true;p.Stocks[0].Quantity=1;const auto original=p;
        auto options=Budget();options.TimeLimitSeconds=5;options.MaxPlacementAttempts=1000000;
        options.Ranking=RankingPolicy::Utilization;options.EnableCombinations=false;options.Seed=71;
        options.ContinuousConstructionTime=true;
        options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
        const std::set<std::string> expected{"plane.envelope-initial","plane.array-layered","plane.array-deferred",
            "plane.array-immediate","plane.array-random","plane.scanline","plane.nfp-initial","plane.nfp-closest",
            "plane.nfp-directional","plane.nfp-strip","plane.nfp-array","plane.nfp-multi-array",
            "plane.array-search-multi","plane.mix","plane.rect-block-search","plane.rect-contact",
            "plane.single-product","plane.single-column","plane.four-block"};
        std::vector<std::string> queued;std::map<std::string,double> completedUtilization,leases;
        std::map<std::string,bool> complete;std::map<std::string,std::size_t> commits;
        std::mutex traceMutex;
        options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
        {
            std::lock_guard lock(traceMutex);
            std::map<std::string,double> measures(event.Measures.begin(),event.Measures.end());
            if(event.Source=="scheduler.queue")
            {queued.push_back(event.Route);}
            if(event.Source=="scheduler.lease")
            {
                leases[event.Route]=measures.at("localTimeSeconds");
            }
            if(event.Source=="scheduler.route-result")complete[event.Route]=measures.at("complete")==1;
            if(event.Source=="scheduler.quality"&&complete[event.Route])
                completedUtilization[event.Route]=measures.at("usedSectionUtilization");
            if(event.Source=="constructor.commit")commits[event.Route]+=event.Members.size();
        };
        ParallelConstructionSettings settings;settings.Workers=4;
        const auto result=MakePlaneInitialFactory(settings)()->Run(p,options);const auto checked=Validate(p,result);
        Require(checked.Valid&&checked.Complete&&!options.ConstructionObservation->Failed.load()&&
            options.ConstructionObservation->Omitted.load()==0,"proposal lanes failed the real initial construction or trace contract");
        Require(queued.size()==expected.size()&&std::set<std::string>(queued.begin(),queued.end())==expected&&
            std::vector<std::string>(queued.begin(),queued.begin()+4)==std::vector<std::string>{
                "plane.envelope-initial","plane.array-layered","plane.nfp-initial","plane.nfp-array"},
            "the default race omitted a canonical mechanism or duplicated historical source/order/quantity variants");
        Require(complete.size()==expected.size()&&leases.size()==expected.size()&&options.ContinuousConstructionTime,
            "an admitted canonical mechanism did not actually run or execution changed the caller's timing policy");
        for(const auto& id:expected)
            Require(complete.at(id)&&leases.at(id)>0,"a canonical constructor never returned the complete tiny original task");
        for(const auto& id:{"plane.array-layered","plane.array-deferred","plane.array-immediate","plane.nfp-initial"})
            Require(commits[id]>0,"an admitted production mechanism returned without any original-instance construction");
        double material=0,usedArea=0;
        for(const auto& board:result.Layouts)
        {
            double top=0;
            for(const auto& item:board.Placements)
            {const auto box=Detail::Bounds(p.Parts[0].Geometry,item.Transform);top=std::max(top,box.Y+box.Height);material+=Detail::Area(p.Parts[0].Geometry);}
            usedArea+=board.Width*std::clamp(top+p.EdgeMargin,0.,board.Height);
        }
        const auto finalUtilization=material/usedArea;
        for(const auto& [id,value]:completedUtilization)
            Require(expected.contains(id)&&finalUtilization+1e-12>=value,
                "the portfolio discarded a completed canonical mechanism's better true utilization");
        Require(completedUtilization.size()==expected.size()&&result.Stats.PlacementAttempts<=options.MaxPlacementAttempts,
            "default mechanism quality evidence or global attempt accounting was omitted");
        Require(p.Parts[0].AllowMirror&&p.Parts[0].Quantity==original.Parts[0].Quantity&&
            p.Parts[0].Geometry.Outer.size()==original.Parts[0].Geometry.Outer.size()&&
            std::equal(p.Parts[0].Geometry.Outer.begin(),p.Parts[0].Geometry.Outer.end(),original.Parts[0].Geometry.Outer.begin(),
                [](Point a,Point b){return a.X==b.X&&a.Y==b.Y;})&&p.Stocks[0].Quantity==original.Stocks[0].Quantity,
            "canonical mechanism execution mutated hard original geometry, mirror permission, demand or stock");
    });

    Test("streamed checked candidates publish before the route ends and remain the global best",[]
    {
        const auto p=Input();Event posted,published,probesDone;Probe updates;
        std::atomic<int> probes{0};std::atomic<bool> waitsPassed{true},optionsInherited{true};
        StrategyRegistry registry;
        registry.Register("streaming",[&]
        {
            return std::make_unique<PureComposite>([&](const Problem& input,const Options& options)
            {
                if(!options.ContinuousConstructionTime||options.TimeLimitSeconds<.7||!options.PublishConstructionCandidate)
                    optionsInherited=false;
                auto snapshot=At(input,0,0,2);
                options.PublishConstructionCandidate(snapshot);
                snapshot.Layouts[0].Placements[0].Transform.X=100; // Receiver must already own an immutable copy.
                options.PublishConstructionCandidate(At(input,-2,0,2));
                posted.Signal();
                if(!published.Wait()||!probesDone.Wait())waitsPassed=false;
                auto final=Empty(input);final.Stats.PlacementAttempts=7;return final;
            });
        });
        std::vector<std::string> ids{"streaming"};
        for(int i=0;i<5;++i)
        {
            const auto id="probe"+std::to_string(i);ids.push_back(id);
            registry.Register(id,Factory([&,i](const Problem& input,const Options& options)
            {
                if(options.ContinuousConstructionTime)optionsInherited=false;
                if(!posted.Wait())waitsPassed=false;
                if(++probes==5)probesDone.Signal();
                return At(input,3+i,3+i);
            }));
        }
        auto options=Budget();options.TimeLimitSeconds=.9;options.MaxPlacementAttempts=120;
        options.Ranking=RankingPolicy::Utilization;
        ParallelConstructionSettings settings;settings.Workers=2;settings.FairScheduling=true;
        settings.ContinuousTimeRoutes={"streaming"};
        settings.OnImprovement=[&](const JobResult& candidate)
        {Probe::Visit visit(updates);if(candidate.StrategyID=="streaming"&&X(candidate)==0)published.Signal();};
        const auto result=Run(p,registry,ids,settings,options);Complete(result);
        Require(waitsPassed&&optionsInherited&&probes==5&&updates.Calls>=1&&X(result)==0&&
            result.Output.Stats.PlacementAttempts==12&&p.Parts[0].Quantity==1&&p.Stocks[0].Quantity==2,
            "streamed candidates waited for route completion, skipped original validation/copying, regressed quality or double-charged attempts");
        updates.Check();
    });

    std::cout<<"ParallelConstructionTests: "<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}
