#include "SheetNestingAxisCompaction.h"
#include "NestingExecution.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace iCAX::SheetNesting;
namespace
{
    int Passed=0,Failed=0;
    void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
    void Test(const char* name,const std::function<void()>& body)
    {try{body();++Passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& error){++Failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}}
    using ConstructorFunction=std::function<Result(const Problem&,const Options&)>;
    using ProcessorFunction=std::function<Result(const Problem&,const Result&,const Options&)>;
    class Constructor final:public IStrategy
    {
        ConstructorFunction Function;
    public:
        explicit Constructor(ConstructorFunction function):Function(std::move(function)){}
        Result Run(const Problem& p,const Options& o)override{return Function(p,o);}
    };
    class Processor final:public IPostProcessor
    {
        ProcessorFunction Function;
    public:
        explicit Processor(ProcessorFunction function):Function(std::move(function)){}
        Result Run(const Problem& p,const Result& r,const Options& o)override{return Function(p,r,o);}
    };
    StrategyFactory Construct(ConstructorFunction function)
    {return [function]{return std::make_unique<Constructor>(function);};}
    PostProcessStage Post(std::string id,ProcessorFunction function)
    {return {std::move(id),[function]{return std::make_unique<Processor>(function);}};}
    Problem Input(std::size_t quantity=1)
    {
        Part a;a.ID="piece";a.Material="steel";a.Quantity=quantity;a.FreeRotation=false;a.AllowedAnglesDegrees={0};
        a.Geometry.Outer={{0,0},{10,0},{10,10},{0,10}};
        Problem p;p.Parts={a};p.Stocks={{"sheet",100,100,2,1,"steel"}};return p;
    }
    Result Floating(const Problem& p,double x=30,double y=40)
    {
        Result r;r.State=Status::Complete;r.Layouts={{p.Stocks[0].ID,0,p.Stocks[0].Width,p.Stocks[0].Height,{},0}};
        for(std::size_t i=0;i<p.Parts[0].Quantity;++i)
            r.Layouts[0].Placements.push_back({p.Parts[0].ID,i,{x+15*double(i),y,0},7,true});
        return r;
    }
    Options Budget(){Options o;o.TimeLimitSeconds=2;o.MaxPlacementAttempts=10000;return o;}
    JobResult Solve(const Problem& p,StrategyFactory factory,Options options=Budget())
    {StrategyRegistry registry;registry.Register("generic",std::move(factory));return RunJob({"job","generic",p,options},registry);}
    void Complete(const JobResult& r){Require(r.Checked.Valid&&r.Checked.Complete,"whole original job must remain complete and valid");}
    void Closed(const Result& r)
    {const auto report=GetAxisCompactionReport(r);Require(report&&report->Closed&&report->GeometryValidated,"returned actual poses must have their own closed report");}
}
int main()
{
    Test("ordinary constructors use the shared lifecycle without an algorithm ID branch",[]
    {
        auto p=Input();Options observed;
        auto r=Solve(p,Construct([&](const Problem& q,const Options& o){observed=o;return Floating(q);}));
        Complete(r);Closed(r.Output);
        const auto& pose=r.Output.Layouts[0].Placements[0];
        Require(std::abs(pose.Transform.X)<1e-5&&std::abs(pose.Transform.Y)<1e-5,"floating result must settle to the usable corner");
        Require(pose.PartID=="piece"&&pose.Instance==0&&pose.Group==7&&pose.GapFiller,"identity and original provenance must survive");
        Require(!observed.EnableBasicCompaction,"only the outer lifecycle may reserve basic work");
        Require(observed.TimeLimitSeconds<2&&observed.MaxPlacementAttempts==10000,"construction reserves time while preserving its full attempt cap");
    });
    Test("construction keeps its attempt cap and refinement shares the unused balance",[]
    {
        auto p=Input();auto options=Budget();options.MaxPlacementAttempts=1000;std::size_t allocated=0;
        auto r=Solve(p,Construct([&](const Problem& q,const Options& o)
        {allocated=o.MaxPlacementAttempts;auto r=Floating(q);r.Stats.PlacementAttempts=10;return r;}),options);
        Complete(r);Closed(r.Output);
        Require(allocated==1000,"basic refinement must not reduce the constructor's attempt search space");
        Require(r.Output.Stats.PlacementAttempts>10&&r.Output.Stats.PlacementAttempts<=1000,"all constructor and normalization attempts share one cap");
    });
    Test("an exhausted global attempt cap retains the original result without a closure claim",[]
    {
        auto p=Input();auto options=Budget();options.MaxPlacementAttempts=1000;
        auto r=Solve(p,Construct([](const Problem& q,const Options& o)
        {auto r=Floating(q);r.Stats.PlacementAttempts=o.MaxPlacementAttempts;return r;}),options);
        Complete(r);
        Require(r.Output.Stats.PlacementAttempts==1000&&!GetAxisCompactionReport(r.Output),"exhausted shared attempts must prevent extra unreported normalization");
        Require(r.Output.Layouts[0].Placements[0].Transform.X==30,"budget exhaustion must preserve the last safe construction");
    });
    Test("explicit pipelines settle construction and repaired results before the next stage",[]
    {
        auto p=Input(2);bool sawInitial=false,sawRepaired=false;StrategyPipeline pipeline;
        pipeline.Construct=Construct([](const Problem& q,const Options&)
        {auto r=Floating(q);r.Layouts[0].Placements.resize(1);r.State=Status::Partial;r.Remaining={{q.Parts[0].ID,1}};return r;});
        pipeline.PostProcessors.push_back(Post("insert",[&](const Problem& q,const Result& old,const Options&)
        {
            sawInitial=GetAxisCompactionReport(old).has_value()&&old.Layouts[0].Placements[0].Transform.Y<1e-5;
            auto r=old;r.Stats={};r.State=Status::Complete;r.Remaining.clear();
            r.Layouts[0].Placements.push_back({q.Parts[0].ID,1,{60,60,0},0,false});return r;
        }));
        pipeline.PostProcessors.push_back(Post("observe",[&](const Problem&,const Result& old,const Options&)
        {
            sawRepaired=GetAxisCompactionReport(old).has_value()&&old.Layouts[0].Placements.size()==2;
            for(const auto& q:old.Layouts[0].Placements)sawRepaired=sawRepaired&&q.Transform.Y<1e-5;
            auto r=old;r.Stats={};return r;
        }));
        auto r=Solve(p,MakePipelineFactory(std::move(pipeline)));Complete(r);Closed(r.Output);
        Require(sawInitial&&sawRepaired,"basic refinement must execute between actual stage boundaries");
    });
    Test("nested composed constructors do not reserve or normalize twice",[]
    {
        auto p=Input();std::size_t observed=0;double seconds=0;StrategyPipeline inner,outer;
        inner.Construct=Construct([&](const Problem& q,const Options& o){observed=o.MaxPlacementAttempts;seconds=o.TimeLimitSeconds;return Floating(q);});
        outer.Construct=MakePipelineFactory(std::move(inner));
        auto r=Solve(p,MakePipelineFactory(std::move(outer)));Complete(r);Closed(r.Output);
        Require(observed==10000&&seconds>1.5&&seconds<=1.7,"nested constructors must receive one time deduction and the original attempt cap");
        const auto scans=std::count_if(r.Output.Diagnostics.begin(),r.Output.Diagnostics.end(),[](const std::string& text)
        {return text.find("pipeline.basic=after-construction: accepted")!=std::string::npos;});
        Require(scans==1,"one lifecycle owner must normalize this unchanged construction boundary once");
    });
    Test("an explicit raw-construction measurement retains its original budget and poses",[]
    {
        auto p=Input();auto options=Budget();options.EnableBasicCompaction=false;std::size_t observed=0;
        auto r=Solve(p,Construct([&](const Problem& q,const Options& o){observed=o.MaxPlacementAttempts;return Floating(q);}),options);
        Complete(r);Require(observed==10000&&!GetAxisCompactionReport(r.Output),"raw mode must neither reserve nor normalize");
        Require(r.Output.Layouts[0].Placements[0].Transform.X==30,"raw constructor evidence must remain untouched");
    });
    Test("legacy closure without the current source-invariance guard is rechecked",[]
    {
        auto p=Input();auto first=Solve(p,Construct([](const Problem& q,const Options&){return Floating(q);}));
        Complete(first);Closed(first.Output);
        auto legacy=first.Output;bool removed=false;
        const std::string guard="guard=nominal-changes-source-invariants-v2";
        for(auto& diagnostic:legacy.Diagnostics)
            if(const auto position=diagnostic.find(guard);position!=std::string::npos)
            {diagnostic.erase(position,guard.size());removed=true;}
        Require(removed,"current closure evidence must identify its nominal material protection");
        Require(!GetAxisCompactionReport(p,legacy),"a tolerance-only historical certificate cannot skip nominal geometry checks");
        auto checked=Solve(p,Construct([legacy](const Problem&,const Options&){return legacy;}));
        Complete(checked);Closed(checked.Output);
    });
    Test("a latched cancellation retains the safe result and starts no refinement",[]
    {
        auto p=Input();auto options=Budget();bool cancelled=false;options.Cancel=[&]{return cancelled;};
        auto r=Solve(p,Construct([&](const Problem& q,const Options&){cancelled=true;return Floating(q);}),options);
        Require(r.Checked.Valid&&r.Output.State==Status::Cancelled,"cancellation must remain visible after a valid constructor returns");
        Require(!GetAxisCompactionReport(r.Output)&&r.Output.Layouts[0].Placements[0].Transform.X==30,"cancelled construction must not secretly move its retained result");
    });
    Test("invalid construction is rejected before normalization",[]
    {
        auto p=Input();auto r=Solve(p,Construct([](const Problem& q,const Options&){return Floating(q,-20,0);}));
        Require(!r.Checked.Valid&&r.Output.State==Status::Failed&&!GetAxisCompactionReport(r.Output),"forged invalid input cannot receive a closure report");
    });
    Test("all accepted refinements preserve the full original quality order",[]
    {
        for(int i=0;i<16;++i)
        {
            auto p=Input(2);p.PartGap=double(i%3)*.2;p.EdgeMargin=.2;
            auto raw=Floating(p,10+double(i),20+double(i));
            JobResult baseline;baseline.Output=raw;Detail::AssessCandidate(p,baseline);Complete(baseline);
            auto r=Solve(p,Construct([raw](const Problem&,const Options&){return raw;}));Complete(r);
            Require(Detail::Quality(r,p,RankingPolicy::RegularRemnant)<=Detail::Quality(baseline,p,RankingPolicy::RegularRemnant),
                "pressure-driven refinement must never overrule certified-tail-first acceptance");
            if(const auto proof=GetAxisCompactionReport(r.Output);proof&&proof->Closed)
                Require(proof->GeometryValidated,"closure certificate must bind the retained valid output");
        }
    });
    Test("bounded pose restoration cannot replace the larger certified remnant",[]
    {
        auto p=Input();p.Parts[0].ID="payload";
        Part frame;frame.ID="frame";frame.Material="steel";frame.FreeRotation=false;frame.AllowedAnglesDegrees={0};
        frame.Geometry.Outer={{0,0},{100,0},{100,60},{0,60}};
        frame.Geometry.Holes={{{10,10},{10,50},{50,50},{50,10}},{{60,10},{60,50},{90,50},{90,10}}};
        p.Parts.push_back(frame);p.Stocks[0].Height=60;
        StrategyPipeline pipeline;
        pipeline.Construct=Construct([](const Problem& q,const Options&)
        {
            Result r;r.State=Status::Complete;
            r.Layouts={{q.Stocks[0].ID,0,100,60,{{"frame",0,{0,0,0}},{"payload",0,{60,10,0}}},0}};
            return r;
        });
        auto restoration=Post("bounded-restore",[](const Problem&,const Result& source,const Options&)
        {
            auto r=source;r.Stats={};
            for(auto& q:r.Layouts[0].Placements)if(q.PartID=="payload")q.Transform.X=10;
            return r;
        });
        restoration.MaxPrecisionAxisShift=50;
        pipeline.PostProcessors.push_back(std::move(restoration));
        auto r=Solve(p,MakePipelineFactory(std::move(pipeline)));Complete(r);Closed(r.Output);
        const auto& placements=r.Output.Layouts[0].Placements;
        const auto payload=std::find_if(placements.begin(),placements.end(),[](const Placement& q){return q.PartID=="payload";});
        Require(payload!=placements.end()&&std::abs(payload->Transform.X-60)<1e-5,"the public best must retain the empty larger left cavity");
        Require(r.Remnants.Measured&&r.Remnants.LastSheetRegularArea>1599,"bounded restoration must still obey the user's certified-remnant priority");
    });
    Test("parallel independent jobs own their own refinement state",[]
    {
        auto p=Input(2);std::atomic<int> constructors=0;StrategyRegistry registry;
        for(int i=0;i<4;++i)registry.Register("independent-"+std::to_string(i),Construct([&](const Problem& q,const Options&){++constructors;return Floating(q);}));
        PortfolioOptions options;options.Workers=4;options.PerJob=Budget();
        auto portfolio=RunPortfolio(p,options,registry);
        Require(constructors==4&&portfolio.Jobs.size()==4,"each job must independently construct its own original input");
        for(const auto& r:portfolio.Jobs){Complete(r);Closed(r.Output);}
    });
    Test("tail-remnant priority does not discard useful preceding-sheet compaction",[]
    {
        Part front;front.ID="front";front.Material="steel";front.FreeRotation=false;front.AllowedAnglesDegrees={0};
        front.Geometry.Outer={{0,0},{80,0},{80,80},{0,80}};
        Part tail=front;tail.ID="tail";
        tail.Geometry.Outer={{0,0},{100,0},{100,100},{0,100},{0,90},{80,90},{80,10},{0,10}};
        Problem p;p.Parts={front,tail};p.Stocks={{"sheet",200,100,2,1,"steel"}};
        auto r=Solve(p,Construct([](const Problem&,const Options&)
        {
            Result r;r.State=Status::Complete;
            r.Layouts={{"sheet",0,200,100,{{"front",0,{60,10,0}}},0},
                {"sheet",1,200,100,{{"tail",0,{100,0,0}}},0}};
            return r;
        }));
        Complete(r);
        for(const auto& board:r.Output.Layouts)for(const auto& q:board.Placements)
            if(q.PartID=="front")Require(std::abs(q.Transform.X)<1e-5&&std::abs(q.Transform.Y)<1e-5,"preceding sheet must retain its safe down-left improvement");
            else Require(q.Transform.X==100&&q.Transform.Y==0,"the larger certified logical-tail remnant must be retained");
        Require(r.Remnants.LastSheetRegularArea>14399,"front-sheet progress cannot diminish the retained tail certificate");
        Require(!GetAxisCompactionReport(p,r.Output),"a rejected full-candidate closure report must not label the mixed retained layout");
    });
    Test("a fractional stage preserves unused time and the remaining shared attempts for its successor",[]
    {
        auto p=Input();auto options=Budget();options.EnableBasicCompaction=false;
        options.TimeLimitSeconds=10;options.MaxPlacementAttempts=100;
        double firstSeconds=0,secondSeconds=0;std::size_t firstAttempts=0,secondAttempts=0;
        StrategyPipeline pipeline;
        pipeline.Construct=Construct([](const Problem& q,const Options&)
        {auto result=Floating(q);result.Stats={};return result;});
        auto first=Post("fractional-stage",[&](const Problem&,const Result& source,const Options& stage)
        {
            firstSeconds=stage.TimeLimitSeconds;firstAttempts=stage.MaxPlacementAttempts;
            auto result=source;result.Stats={};result.Stats.PlacementAttempts=stage.MaxPlacementAttempts;return result;
        });
        first.BudgetFraction=.5;pipeline.PostProcessors.push_back(std::move(first));
        pipeline.PostProcessors.push_back(Post("following-stage",[&](const Problem&,const Result& source,const Options& stage)
        {
            secondSeconds=stage.TimeLimitSeconds;secondAttempts=stage.MaxPlacementAttempts;
            auto result=source;result.Stats={};result.Stats.PlacementAttempts=stage.MaxPlacementAttempts;return result;
        }));
        const auto result=Solve(p,MakePipelineFactory(std::move(pipeline)),options);Complete(result);
        Require(firstAttempts==50&&secondAttempts==50,"stage quotas must share the actual remaining 100-attempt budget");
        Require(firstSeconds>0&&firstSeconds<=options.TimeLimitSeconds*.5+1e-6,
            "fractional stage received more than half the available time");
        Require(secondSeconds>firstSeconds*1.5&&secondSeconds<=options.TimeLimitSeconds,
            "unused fractional-stage time was withheld from the following stage");
        Require(result.Output.Stats.PlacementAttempts==100,
            "rejected or unchanged fractional trials lost work or exceeded the common attempt budget");
    });
    Test("stage budget fractions reject invalid definitions and preserve live cancellation",[]
    {
        auto p=Input();
        for(const double fraction:{0.,std::numeric_limits<double>::quiet_NaN(),1.01})
        {
            bool constructed=false,rejected=false;StrategyPipeline pipeline;
            pipeline.Construct=Construct([&](const Problem& q,const Options&){constructed=true;return Floating(q);});
            auto stage=Post("invalid-fraction",[](const Problem&,const Result& source,const Options&){return source;});
            stage.BudgetFraction=fraction;pipeline.PostProcessors.push_back(std::move(stage));
            try{auto factory=MakePipelineFactory(std::move(pipeline));(void)factory;}
            catch(const std::invalid_argument&){rejected=true;}
            Require(rejected&&!constructed,"invalid per-stage budget fraction must reject the pipeline definition before work");
        }
        bool cancelled=false,firstRan=false,secondRan=false;auto options=Budget();
        options.EnableBasicCompaction=false;options.MaxPlacementAttempts=100;options.Cancel=[&]{return cancelled;};
        StrategyPipeline pipeline;pipeline.Construct=Construct([](const Problem& q,const Options&){return Floating(q);});
        auto first=Post("cancel-fractional-stage",[&](const Problem&,const Result& source,const Options& stage)
        {
            firstRan=true;Require(stage.MaxPlacementAttempts==50,"cancelled fractional stage did not receive its own cap");
            Require(stage.Cancel&&!stage.Cancel(),"fractional stage lost the original cancellation callback");
            cancelled=true;Require(stage.Cancel(),"fractional stage did not observe live cancellation");
            auto result=source;result.Stats={};return result;
        });
        first.BudgetFraction=.5;pipeline.PostProcessors.push_back(std::move(first));
        pipeline.PostProcessors.push_back(Post("must-not-run",[&](const Problem&,const Result& source,const Options&)
        {secondRan=true;return source;}));
        const auto result=Solve(p,MakePipelineFactory(std::move(pipeline)),options);
        Require(firstRan&&!secondRan&&result.Checked.Valid&&result.Output.State==Status::Cancelled,
            "live cancellation must latch and prevent the next budgeted stage");
    });
    Test("invalid refinement budget is rejected before the constructor runs",[]
    {
        auto p=Input();auto options=Budget();options.BasicCompactionBudgetFraction=1;bool called=false;
        auto r=Solve(p,Construct([&](const Problem& q,const Options&){called=true;return Floating(q);}),options);
        Require(!called&&r.Output.State==Status::InvalidInput,"invalid reserve must not launch a constructor");
    });
    std::cout<<"refinement lifecycle: "<<Passed<<" passed, "<<Failed<<" failed\n";
    return Failed?1:0;
}
