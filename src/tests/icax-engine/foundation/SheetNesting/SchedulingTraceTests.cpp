#include "SheetNestingPipeline.h"
#include "SheetNestingConstructionTrace.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
using Work = std::function<Result(const Problem&, const Options&)>;
class Constructor final : public IStrategy
{
    Work Function;
public:
    explicit Constructor(Work function) : Function(std::move(function)) {}
    Result Run(const Problem& p, const Options& o) override { return Function(p, o); }
};
StrategyFactory Factory(Work work)
{ return [work=std::move(work)] { return std::make_unique<Constructor>(work); }; }
Problem Input()
{
    Part part; part.ID="part"; part.Material="steel"; part.FreeRotation=false; part.AllowedAnglesDegrees={0};
    part.Geometry.Outer={{0,0},{2,0},{2,2},{0,2}};
    Problem p; p.Parts={part}; p.Stocks={{"sheet",20,20,1,1,"steel"}}; return p;
}
Result At(const Problem& p, double x, double y)
{
    Result r; r.State=Status::Complete; r.Stats.PlacementAttempts=1;
    r.Layouts={{p.Stocks[0].ID,0,20,20,{{p.Parts[0].ID,0,{x,y,0},1,false}},4}};
    return r;
}
Options Limits()
{ Options o; o.TimeLimitSeconds=5; o.MaxPlacementAttempts=10000; o.EnableBasicCompaction=false; o.Ranking=RankingPolicy::LowThenLeft; return o; }
JobResult Run(const StrategyRegistry& routes, std::vector<std::string> ids,
    ParallelConstructionSettings settings, Options options)
{
    StrategyRegistry registry;
    registry.Register("race",MakeParallelConstructionPortfolioFactory(routes,std::move(ids),std::move(settings)));
    return RunJob({"trace","race",Input(),std::move(options)},registry);
}
bool Named(const std::vector<std::pair<std::string,double>>& values,const char* name)
{ return std::any_of(values.begin(),values.end(),[&](const auto& item){return item.first==name;}); }
int passed=0, failed=0;
void Test(const char* name,const std::function<void()>& test)
{try {test();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}}
}
int main()
{
    Test("parallel observations preserve all route results and actual selection",[]
    {
        StrategyRegistry routes; std::atomic<int> entered{0};
        for (int i=0;i<2;++i) routes.Register(i?"low":"high",Factory([&,i](const Problem& p,const Options& o)
        {
            ++entered;
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            while(entered.load()<2 && std::chrono::steady_clock::now()<until)std::this_thread::yield();
            Require(entered.load()==2,"trace accidentally serialized independent constructor execution");
            for(std::uint64_t j=1;j<=64;++j)
            {
                ConstructionTraceEvent e; e.Stage=ConstructionTraceStage::Candidate; e.Source="native-test";
                e.Route=o.ConstructionTraceRoute; e.Scope="inner"; e.Step=j; e.CandidateID=j;
                if(o.ConstructionTrace)o.ConstructionTrace(e);
            }
            return At(p,i?1:8,i?1:8);
        }));
        std::vector<ConstructionTraceEvent> events;std::atomic<int> active{0};std::atomic<bool> concurrent{false};
        auto options=Limits(); options.ConstructionTrace=[&](const auto& event)
        {
            if(active.fetch_add(1)!=0)concurrent=true;
            std::this_thread::yield();events.push_back(event);--active;
        };
        ParallelConstructionSettings settings;settings.Workers=2;settings.FairScheduling=true;
        const auto result=Run(routes,{"high","low"},settings,options);
        Require(result.Checked.Valid&&result.Checked.Complete,"tracing damaged original-valid complete result");
        Require(result.Output.Layouts[0].Placements[0].Transform.X==1,"tracing changed actual shared-quality winner");
        Require(!concurrent,"user trace callback was invoked concurrently");
        std::uint64_t previous=0;std::size_t native=0,finals=0,scores=0;bool deciding=false,selected=false;
        for(const auto& e:events)
        {
            Require(e.Step>previous,"portfolio trace sequence is not strictly increasing");previous=e.Step;
            Require(Named(e.Measures,"portfolioWallSeconds"),"event lost portfolio wall time");
            if(e.Source=="native-test")
            {++native;Require((e.Route=="high"||e.Route=="low")&&e.Scope=="inner"&&Named(e.Measures,"nativeStep"),"route/scope/native step did not penetrate observation forwarding");}
            if(e.Source=="scheduler.route-result")
            {++finals;Require(e.Layouts.size()==1&&e.Layouts[0].Placements.size()==1,"nonwinner original layout was not retained");
                const double expected=e.Route=="low"?1:8;Require(e.Layouts[0].Placements[0].Transform.X==expected,"recorded route layout is not the actual returned geometry");}
            if(e.Source=="scheduler.quality")
            {++scores;Require(Named(e.Rank,"missing")&&Named(e.Rank,"boards")&&Named(e.Rank,"topSum")&&Named(e.Rank,"meanX")&&Named(e.Measures,"frontUtilizationCount"),"shared quality key was partially recorded");
                deciding|=e.Reason.find("deciding-field=topSum")!=std::string::npos;}
            if(e.Source=="scheduler.final-selection")
            {selected=true;Require(e.Route=="low"&&e.Layouts.size()==1&&e.Accepted,"final recorded winner disagrees with returned result");}
        }
        Require(native==128&&finals==2&&scores==2&&deciding&&selected,"trace omitted native evolution, a route result, or selection evidence");
    });
    Test("explicit target stop and failed factories remain distinguishable",[]
    {
        StrategyRegistry routes;
        routes.Register("bad",[]{throw std::runtime_error("factory-proof");return std::unique_ptr<IStrategy>{};});
        routes.Register("first",Factory([](const Problem& p,const Options&){return At(p,1,1);}));
        routes.Register("waiting",Factory([](const Problem& p,const Options& o)
        {
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            while(!(o.Cancel&&o.Cancel())&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
            Result r;r.State=Status::Cancelled;r.Remaining={{p.Parts[0].ID,1}};return r;
        }));
        routes.Register("last",Factory([](const Problem& p,const Options&){return At(p,8,8);}));
        auto options=Limits();std::vector<ConstructionTraceEvent> events;
        options.ConstructionTrace=[&](const auto& e){events.push_back(e);};
        ParallelConstructionSettings settings;settings.Workers=1;settings.StopWhen=[](const Problem&,const JobResult&){return true;};
        const auto result=Run(routes,{"bad","first","waiting","last"},settings,options);
        Require(result.Checked.Valid&&result.Checked.Complete&&result.Output.State==Status::Complete,"successful target stop damaged final state");
        bool factory=false,notStarted=false,target=false;
        for(const auto& e:events)
        {
            factory|=e.Source=="scheduler.factory"&&e.Route=="bad"&&!e.Accepted&&e.Reason=="factory-proof";
            notStarted|=e.Source=="scheduler.not-started"&&e.Route=="last"&&!e.Accepted&&e.Reason=="successful target stop";
            target|=e.Source=="scheduler.final-selection"&&e.Reason=="explicit-target-predicate";
        }
        Require(factory&&notStarted&&target,"trace confused factory failure, unstarted route, and explicit successful stop");
    });
    Test("throwing observer cannot fail or replace solver geometry",[]
    {
        StrategyRegistry routes;routes.Register("only",Factory([](const Problem& p,const Options&){return At(p,3,3);}));
        auto options=Limits();int calls=0;options.ConstructionTrace=[&](const auto&){++calls;throw std::runtime_error("observer-only");};
        options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
        ParallelConstructionSettings settings;settings.Workers=1;
        const auto result=Run(routes,{"only"},settings,options);
        Require(result.Checked.Valid&&result.Checked.Complete&&result.Output.Layouts[0].Placements[0].Transform.X==3,"observer failure changed solver result");
        Require(calls==1,"failed observer was repeatedly invoked");
        Require(options.ConstructionObservation->Failed.load()&&options.ConstructionObservation->Omitted.load()>0,
            "native observation loss was not visible to the trace writer");
        Require(std::any_of(result.Output.Diagnostics.begin(),result.Output.Diagnostics.end(),[](const auto& s){return s.find("observation callback failed")!=std::string::npos;}),"omitted observations were not disclosed");
    });
    Test("general portfolio also serializes the shared observer",[]
    {
        StrategyRegistry routes;
        for(int i=0;i<3;++i)routes.Register("route"+std::to_string(i),Factory([i](const Problem& p,const Options& o)
        {
            for(std::uint64_t j=1;j<=64;++j)
            {ConstructionTraceEvent e;e.Source="native-general";e.Route=o.ConstructionTraceRoute;e.Step=j;if(o.ConstructionTrace)o.ConstructionTrace(e);}
            return At(p,1+i,1+i);
        }));
        std::atomic<int> active{0};std::atomic<bool> concurrent{false};std::vector<ConstructionTraceEvent> events;
        PortfolioOptions options;options.PerJob=Limits();options.Workers=3;options.Ranking=RankingPolicy::LowThenLeft;
        options.PerJob.ConstructionTrace=[&](const auto& e)
        {if(active.fetch_add(1)!=0)concurrent=true;std::this_thread::yield();events.push_back(e);--active;};
        const auto result=RunPortfolio(Input(),options,routes);
        Require(!concurrent&&result.BestJob<result.Jobs.size(),"general portfolio did not serialize observation or lost winner");
        Require(result.Jobs[result.BestJob].Output.Layouts[0].Placements[0].Transform.X==1,"general portfolio trace changed ranking");
        Require(std::count_if(events.begin(),events.end(),[](const auto& e){return e.Source=="scheduler.portfolio-route-result";})==3,"general portfolio omitted a route final layout");
    });
    Test("validated construction snapshots publish before route exit without double charging",[]
    {
        const auto caller=std::this_thread::get_id();
        std::atomic<bool> delivered{false},exited{false};int improvements=0,relayed=0;
        StrategyRegistry routes;routes.Register("streaming",Factory([&](const Problem& p,const Options& o)
        {
            Require(bool(o.PublishConstructionCandidate),"worker has no construction snapshot transport");
            auto forbidden=At(p,1,1);forbidden.Layouts[0].Placements[0].Transform.Mirrored=true;
            o.PublishConstructionCandidate(forbidden);
            Result partial;partial.State=Status::Partial;partial.Remaining={{p.Parts[0].ID,1}};
            o.PublishConstructionCandidate(partial);
            auto good=At(p,1,1);good.Stats.PlacementAttempts=3;o.PublishConstructionCandidate(good);
            good.Layouts[0].Placements[0].Transform={12,12,0}; // The transport owns its earlier snapshot.
            auto worse=At(p,8,8);worse.Stats.PlacementAttempts=4;o.PublishConstructionCandidate(worse);
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            while(!delivered.load()&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
            Require(delivered.load(),"caller waited for route exit instead of collecting its complete snapshot");
            exited=true;worse.Stats.PlacementAttempts=7;return worse;
        }));
        auto options=Limits();options.MaxPlacementAttempts=20;
        options.PublishConstructionCandidate=[&](const Result& candidate)
        {
            Require(std::this_thread::get_id()==caller&&!exited.load(),"enclosing publisher ran on worker or after route exit");
            Require(candidate.Layouts[0].Placements[0].Transform.X==1,"enclosing publisher received an invalid or worse snapshot");
            ++relayed;
        };
        std::vector<ConstructionTraceEvent> events;options.ConstructionTrace=[&](const auto& event){events.push_back(event);};
        ParallelConstructionSettings settings;settings.Workers=1;settings.FairScheduling=true;
        settings.OnImprovement=[&](const JobResult& candidate)
        {
            Require(std::this_thread::get_id()==caller&&!exited.load(),"improvement did not arrive on caller before constructor exit");
            Require(candidate.Checked.Valid&&candidate.Checked.Complete&&candidate.Output.Layouts[0].Placements[0].Transform.X==1,
                "invalid, incomplete or worse publication replaced the certified best");
            ++improvements;delivered=true;
        };
        const auto result=Run(routes,{"streaming"},settings,options);
        Require(result.Checked.Valid&&result.Checked.Complete&&result.Output.Layouts[0].Placements[0].Transform.X==1,
            "worse terminal route result overwrote its previously published complete winner");
        Require(improvements==1&&relayed==1&&exited.load()&&result.Output.Stats.PlacementAttempts==7,
            "snapshot was counted as route completion, repeated publication, or additional attempts");
        Require(std::count_if(events.begin(),events.end(),[](const auto& e)
            {return e.Source=="scheduler.published-candidate"&&e.Accepted;})==1&&
            std::count_if(events.begin(),events.end(),[](const auto& e)
            {return e.Source=="scheduler.published-candidate"&&!e.Accepted;})==2&&
            std::count_if(events.begin(),events.end(),[](const auto& e)
            {return e.Source=="scheduler.route-result";})==1,
            "trace confused rejected snapshots, early publication and the one terminal route result");
        Require(std::any_of(events.begin(),events.end(),[](const auto& e)
            {return e.Source=="scheduler.incumbent"&&e.Reason=="new-streamed-incumbent";}),
            "early original-validated incumbent lost its distinct scheduling evidence");
        const auto early=std::find_if(events.begin(),events.end(),[](const auto& e)
            {return e.Source=="scheduler.published-candidate"&&e.Accepted;});
        Require(early!=events.end()&&Named(early->Measures,"snapshotAssessmentSeconds")&&
            Named(early->Measures,"snapshotPublishToParentSeconds")&&Named(early->Measures,"snapshotValidatedCount"),
            "snapshot original-input assessment or caller delivery cost was not recorded");
    });
    Test("nested construction publication forwards early and isolates the enclosing publisher",[]
    {
        const auto caller=std::this_thread::get_id();std::atomic<bool> delivered{false},innerExited{false};
        StrategyRegistry inner;inner.Register("inner",Factory([&](const Problem& p,const Options& o)
        {
            auto good=At(p,1,1);good.Stats.PlacementAttempts=2;o.PublishConstructionCandidate(good);
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            while(!delivered.load()&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
            Require(delivered.load(),"nested portfolio swallowed its enclosing construction publisher");
            innerExited=true;auto worse=At(p,8,8);worse.Stats.PlacementAttempts=6;return worse;
        }));
        ParallelConstructionSettings inside;inside.Workers=1;inside.FairScheduling=true;
        StrategyRegistry routes;routes.Register("nested",MakeParallelConstructionPortfolioFactory(inner,{"inner"},inside));
        auto options=Limits();options.MaxPlacementAttempts=20;int relayCalls=0,improvements=0;bool relayOnCaller=false;
        options.PublishConstructionCandidate=[&](const Result&)
        {++relayCalls;relayOnCaller=std::this_thread::get_id()==caller;throw std::runtime_error("publisher-only");};
        ParallelConstructionSettings settings;settings.Workers=1;settings.FairScheduling=true;
        settings.OnImprovement=[&](const JobResult& candidate)
        {
            Require(std::this_thread::get_id()==caller&&!innerExited.load()&&candidate.Checked.Valid&&candidate.Checked.Complete,
                "nested snapshot was delayed until exit or notified a worker-thread subscriber");
            ++improvements;delivered=true;
        };
        const auto result=Run(routes,{"nested"},settings,options);
        Require(result.Checked.Valid&&result.Checked.Complete&&result.Output.Layouts[0].Placements[0].Transform.X==1&&
            result.Output.Stats.PlacementAttempts==6&&relayCalls==1&&relayOnCaller&&improvements==1&&innerExited.load(),
            "nested forwarding or a failed enclosing publisher damaged the winner, subscriber or attempt accounting");
        Require(std::any_of(result.Output.Diagnostics.begin(),result.Output.Diagnostics.end(),[](const auto& text)
            {return text.find("candidate publisher failed: publisher-only")!=std::string::npos;}),
            "isolated enclosing publisher failure was not disclosed");
    });
    Test("cancelling after an early snapshot preserves it and settles the route lease once",[]
    {
        const auto caller=std::this_thread::get_id();std::atomic<bool> cancel{false},stopped{false};int improvements=0;
        StrategyRegistry routes;routes.Register("cancel-stream",Factory([&](const Problem& p,const Options& o)
        {
            auto good=At(p,1,1);good.Stats.PlacementAttempts=2;o.PublishConstructionCandidate(good);
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            while(!(o.Cancel&&o.Cancel())&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
            stopped=o.Cancel&&o.Cancel();
            auto worse=At(p,8,8);worse.State=Status::Cancelled;worse.Stats.PlacementAttempts=5;return worse;
        }));
        auto options=Limits();options.MaxPlacementAttempts=20;options.Cancel=[&]
        {Require(std::this_thread::get_id()==caller,"original cancellation escaped the caller thread");return cancel.load();};
        ParallelConstructionSettings settings;settings.Workers=1;settings.FairScheduling=true;
        settings.OnImprovement=[&](const JobResult&)
        {Require(std::this_thread::get_id()==caller,"streamed improvement escaped caller thread");++improvements;cancel=true;};
        const auto result=Run(routes,{"cancel-stream"},settings,options);
        Require(stopped.load()&&improvements==1&&result.Output.State==Status::Cancelled&&result.Checked.Valid&&result.Checked.Complete&&
            result.Output.Layouts[0].Placements[0].Transform.X==1&&result.Output.Stats.PlacementAttempts==5,
            "cancellation discarded a certified snapshot, notified twice or double-charged its attempts");
    });
    Test("continuous route time preserves fair attempt leases and ordinary wave slices",[]
    {
        StrategyRegistry routes;std::vector<double> times;std::vector<std::size_t> caps;std::vector<bool> continuous;
        for(const auto& name:{"continuous","ordinary","last"})routes.Register(name,Factory([&](const Problem& p,const Options& o)
        {times.push_back(o.TimeLimitSeconds);caps.push_back(o.MaxPlacementAttempts);continuous.push_back(o.ContinuousConstructionTime);return At(p,1,1);}));
        auto options=Limits();options.TimeLimitSeconds=2;options.MaxPlacementAttempts=90;
        ParallelConstructionSettings settings;settings.Workers=1;settings.FairScheduling=true;settings.ContinuousTimeRoutes={"continuous"};
        auto result=Run(routes,{"continuous","ordinary","last"},settings,options);
        Require(result.Checked.Valid&&result.Checked.Complete&&times.size()==3&&times[0]>1.5&&times[1]<1.5&&
            continuous[0]&&!continuous[1]&&!continuous[2]&&caps[0]==30&&caps[1]==45&&caps[2]==88&&
            result.Output.Stats.PlacementAttempts==3,
            "continuous construction changed fair attempt reclamation or removed ordinary wave scheduling");
        times.clear();caps.clear();continuous.clear();settings.ContinuousTimeRoutes.clear();options.ContinuousConstructionTime=true;
        result=Run(routes,{"continuous","ordinary","last"},settings,options);
        Require(times.size()==3&&std::all_of(times.begin(),times.end(),[](double time){return time>1.5;})&&
            std::all_of(continuous.begin(),continuous.end(),[](bool value){return value;})&&
            caps[0]==30&&caps[1]==45&&caps[2]==88&&result.Output.Stats.PlacementAttempts==3,
            "explicit caller continuous-time intent was lost or changed the shared attempt budget");
    });
    Test("automatic worker count uses available hardware and scheduled routes",[]
    {
        const auto expected=std::min<std::size_t>(6,std::max<std::size_t>(1,std::thread::hardware_concurrency()));
        StrategyRegistry routes;std::vector<std::string> ids;std::atomic<std::size_t> entered{0};
        for(std::size_t i=0;i<6;++i)
        {
            ids.push_back("automatic-"+std::to_string(i));routes.Register(ids.back(),Factory([&](const Problem& p,const Options&)
            {
                ++entered;const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
                while(entered.load()<expected&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
                Require(entered.load()>=expected,"automatic pool retained an artificial four-worker cap");return At(p,1,1);
            }));
        }
        std::size_t actual=0;auto options=Limits();options.ConstructionTrace=[&](const auto& event)
        {if(event.Source=="scheduler.workers")for(const auto& [name,value]:event.Measures)if(name=="workersActual")actual=static_cast<std::size_t>(value);};
        ParallelConstructionSettings settings;settings.Workers=0;settings.FairScheduling=true;
        const auto result=Run(routes,ids,settings,options);
        Require(actual==expected&&entered.load()==6&&result.Checked.Valid&&result.Checked.Complete,
            "automatic concurrency did not follow hardware and the scheduled route count");
    });
    std::cout<<"SchedulingTraceTests: "<<passed<<" passed, "<<failed<<" failed\n";
    return failed?1:0;
}
