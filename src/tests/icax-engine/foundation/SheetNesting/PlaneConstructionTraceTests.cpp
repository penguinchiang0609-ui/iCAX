#include "SheetNestingPlaneStrategies.h"
#include "SheetNestingConstructionTrace.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
bool SamePose(const Pose& a,const Pose& b)
{return a.X==b.X&&a.Y==b.Y&&a.AngleDegrees==b.AngleDegrees;}
bool SamePlacement(const Placement& a,const Placement& b)
{return a.PartID==b.PartID&&a.Instance==b.Instance&&SamePose(a.Transform,b.Transform)&&a.Group==b.Group&&a.GapFiller==b.GapFiller;}
bool SameLayouts(const std::vector<Layout>& a,const std::vector<Layout>& b)
{
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)
    {
        if(a[i].StockID!=b[i].StockID||a[i].StockInstance!=b[i].StockInstance||a[i].Width!=b[i].Width||a[i].Height!=b[i].Height||
            a[i].MaterialArea!=b[i].MaterialArea||a[i].Placements.size()!=b[i].Placements.size())return false;
        for(std::size_t j=0;j<a[i].Placements.size();++j)if(!SamePlacement(a[i].Placements[j],b[i].Placements[j]))return false;
    }
    return true;
}
Part Rectangle(std::string id,double w,double h,std::size_t quantity)
{
    Part p;p.ID=std::move(id);p.Quantity=quantity;p.FreeRotation=false;p.AllowedAnglesDegrees={90};
    p.Geometry.Outer={{0,0},{w,0},{w,h},{0,h}};return p;
}
Problem Input()
{
    Problem p;p.Parts={Rectangle("large-original",3,2,2),Rectangle("small-original",1,2,2)};
    p.Stocks={{"sheet",8,8,1}};p.EdgeMargin=.25;p.PartGap=.2;return p;
}
Options Limits()
{
    Options o;o.TimeLimitSeconds=20;o.MaxPlacementAttempts=1000000;o.EnableBasicCompaction=false;
    o.EnableCombinations=false;o.Ranking=RankingPolicy::LowThenLeft;return o;
}
void Check(PlaneStrategyKind kind,const char* route)
{
    auto problem=Input();
    const bool search=kind==PlaneStrategyKind::ArrayMultiSpecificationSearch;
    // Residual demand forces the search route beyond its feasible greedy
    // bootstrap, so replay checks real branching instead of assuming it ran.
    if(search){problem.Parts[0].Quantity=12;problem.Parts[1].Quantity=0;}
    auto options=Limits();PlaneStrategySettings settings;settings.Kind=kind;
    settings.EnableLayoutRefinement=false;settings.EnableContourSettling=false;
    settings.EnableSheetTemplateReuse=false;settings.MaxNodes=128;settings.BranchWidth=4;settings.BranchDecisions=3;
    const auto baseline=MakePlaneStrategyFactory(settings)()->Run(problem,options);
    const auto baselineCheck=Validate(problem,baseline);
    Require(baselineCheck.Valid&&baselineCheck.PlacedCount>0&&(search||baselineCheck.Complete),"unobserved representative construction lost the feasible original task");
    std::vector<ConstructionTraceEvent> events;
    options.ConstructionTraceRoute=route;options.ConstructionTraceScope="contract";
    options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
    options.ConstructionTrace=[&](const auto& event){events.push_back(event);};
    const auto traced=MakePlaneStrategyFactory(settings)()->Run(problem,options);
    const auto tracedCheck=Validate(problem,traced);
    Require(tracedCheck.Valid&&tracedCheck.PlacedCount>0&&(search||tracedCheck.Complete),"observation damaged the original-valid construction");
    Require(SameLayouts(baseline.Layouts,traced.Layouts),"observation changed deterministic construction with sufficient budget");
    Require(baseline.Stats.PlacementAttempts==traced.Stats.PlacementAttempts,"observation changed actual placement-attempt accounting");
    Require(!options.ConstructionObservation->Failed.load()&&options.ConstructionObservation->Omitted.load()==0,"complete trace silently omitted observation events");
    std::map<std::pair<std::string,std::uint64_t>,Layout> states;
    std::set<std::tuple<std::string,std::uint64_t,std::uint64_t>> candidates;
    bool sawCommit=false,sawRaw=false,sawFinal=false,sawGenerate=false,sawSearch=false,sawArrayProfile=false;
    bool sawRetainedCommit=false,sawRetainedRaw=false;
    std::array<std::size_t,2> pendingQueries{};
    std::array<double,2> pendingQuerySeconds{};
    for(const auto& event:events)
    {
        Require(event.Route==route,"native event lost its actual route identity");
        const auto measure=[&](const std::string& name)
        {
            for(const auto& [key,value]:event.Measures)if(key==name)return value;
            throw std::runtime_error("missing array observation measure: "+name);
        };
        if(event.Source=="array.original-query-result"||event.Source=="array.rigid-query-result")
        {
            const std::size_t queryKind=event.Source=="array.original-query-result"?0:1;
            ++pendingQueries[queryKind];pendingQuerySeconds[queryKind]+=measure("placementQuerySeconds");
        }
        if(event.Source=="array.query-profile")
        {
            sawArrayProfile=true;
            const auto wall=measure("arrayWallSeconds");
            Require(event.Stage==ConstructionTraceStage::Complete&&std::isfinite(wall)&&wall>=0,
                "array performance record lacks its complete call wall time");
            for(std::size_t queryKind=0;queryKind<2;++queryKind)
            {
                const std::string prefix=queryKind?"rigid":"single";
                Require(measure(prefix+"Queries")==double(pendingQueries[queryKind])&&
                    std::abs(measure(prefix+"QuerySeconds")-pendingQuerySeconds[queryKind])<1e-8,
                    "array cumulative record omitted or double-counted a completed contour query");
            }
            Require(wall+1e-6>=pendingQuerySeconds[0]+pendingQuerySeconds[1]&&
                measure("callerFitsSeconds")>=0&&measure("callerFitsSeconds")<=wall+1e-6&&
                measure("callerFitsCalls")>=0&&measure("phaseTimesNested")==1,
                "array performance record misrepresents overlapping query/material times as additive stages");
            pendingQueries={};pendingQuerySeconds={};
        }
        if(event.Stage==ConstructionTraceStage::Generate||event.Stage==ConstructionTraceStage::Score||event.Stage==ConstructionTraceStage::Filter)
            if(event.CandidateID)candidates.emplace(event.Scope,event.ProposalID,event.CandidateID);
        sawGenerate|=event.Stage==ConstructionTraceStage::Generate;
        sawSearch|=event.Stage==ConstructionTraceStage::Expand;
        if(event.Stage==ConstructionTraceStage::Commit)
        {
            sawCommit=true;
            Require(event.StateID!=0&&event.CandidateID!=0&&!event.Members.empty()&&event.Layouts.size()==1,
                "commit lacks original members, candidate identity or real state snapshot");
            Require(candidates.contains({event.Scope,event.ProposalID,event.CandidateID}),"committed candidate has no preceding native observation");
            const auto& board=event.Layouts[0];
            if(event.ParentStateID)
            {
                const auto parent=states.find({event.Scope,event.ParentStateID});
                Require(parent!=states.end(),"committed state refers to an unrecorded parent");
                Require(board.Placements.size()==parent->second.Placements.size()+event.Members.size(),"commit snapshot is not parent plus original delta");
                for(std::size_t i=0;i<parent->second.Placements.size();++i)
                    Require(SamePlacement(parent->second.Placements[i],board.Placements[i]),"commit changed an existing parent placement");
            }
            for(const auto& member:event.Members)
            {
                Require(member.PartIndex<problem.Parts.size()&&member.Instance<problem.Parts[member.PartIndex].Quantity,
                    "committed member does not identify an actual demanded original instance");
                Require(member.Transform.AngleDegrees==90,"trace or construction changed the only allowed original angle");
                const auto& id=problem.Parts[member.PartIndex].ID;
                const auto found=std::find_if(board.Placements.begin(),board.Placements.end(),[&](const auto& p)
                {return p.PartID==id&&p.Instance==member.Instance;});
                Require(found!=board.Placements.end()&&SamePose(found->Transform,member.Transform)&&found->Group==member.Group,
                    "commit member coordinates do not equal the native layout snapshot");
            }
            states[{event.Scope,event.StateID}]=board;
        }
        if(event.Source=="construction.path.commit")
        {
            sawRetainedCommit=true;
            const auto actual=std::find_if(events.begin(),events.end(),[&](const auto& before)
            {return before.Stage==ConstructionTraceStage::Commit&&before.Scope==event.Scope&&before.StateID==event.StateID;});
            Require(event.Stage==ConstructionTraceStage::Selected&&actual!=events.end()&&
                actual->ParentStateID==event.ParentStateID&&actual->CandidateID==event.CandidateID&&
                actual->ProposalID==event.ProposalID&&actual->Rank==event.Rank&&SameLayouts(actual->Layouts,event.Layouts)&&
                measure("originalCommitStep")==double(actual->Step)&&measure("originalCommitElapsedSeconds")==actual->ElapsedSeconds,
                "retained winning path was not an exact copy of its actual observed commit");
        }
        if(event.Source=="construction.path.raw")
        {
            sawRetainedRaw=true;
            const auto actual=states.find({event.Scope,event.StateID});
            Require(actual!=states.end()&&SameLayouts({actual->second},event.Layouts),
                "retained chosen board differs from the actual committed state");
        }
        if(event.Source=="board.raw-construction")sawRaw=true;
        if(event.Source=="strategy.result")
        {sawFinal=true;Require(event.Stage==ConstructionTraceStage::Selected&&event.Accepted&&SameLayouts(event.Layouts,traced.Layouts),
            "final replay snapshot does not equal actual native selected result");}
    }
    Require(sawGenerate&&sawCommit&&sawRaw&&sawFinal,"representative route omitted generation, real commits or final snapshots");
    Require(sawRetainedCommit&&sawRetainedRaw,"representative route omitted its protected authentic winning path");
    Require(pendingQueries==std::array<std::size_t,2>{},"a contour query lost its enclosing array performance record");
    if(kind==PlaneStrategyKind::ArrayQuantityDeferred||search)
        Require(sawArrayProfile,"array construction omitted its cumulative per-call performance record");
    if(kind==PlaneStrategyKind::ArrayMultiSpecificationSearch)Require(sawSearch,"search route did not expose real search expansion");
    for(const auto& layout:traced.Layouts)for(const auto& placed:layout.Placements)
        Require(placed.Transform.AngleDegrees==90,"tracing disabled or replaced the original allowed-angle constraint");
}
}
int main()
{
    int passed=0,failed=0;
    const std::vector<std::pair<PlaneStrategyKind,const char*>> cases={
        {PlaneStrategyKind::ArrayQuantityDeferred,"array"},
        {PlaneStrategyKind::Scanline,"scanline"},
        {PlaneStrategyKind::NfpClosest,"nfp"},
        {PlaneStrategyKind::ArrayMultiSpecificationSearch,"search"}};
    for(const auto& [kind,name]:cases)
    {
        try{Check(kind,name);++passed;std::cout<<"PASS "<<name<<" native construction replay contract\n";}
        catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    }
    std::cout<<"PlaneConstructionTraceTests: "<<passed<<" passed, "<<failed<<" failed\n";
    return failed?1:0;
}
