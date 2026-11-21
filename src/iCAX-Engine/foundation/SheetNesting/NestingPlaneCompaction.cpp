#include "SheetNestingPlaneCompaction.h"
#include "SheetNestingPlaneStrategies.h"
#include "SheetNestingAxisCompaction.h"
#include "SheetNestingRotationCompaction.h"
#include "SheetNestingReassembly.h"
#include "SheetNestingPlaneSheetMix.h"
#include "NestingAxisSafety.h"
#include "NestingInternal.h"
#include "NestingExecution.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>

namespace iCAX::SheetNesting
{
namespace
{
using Clock=std::chrono::steady_clock;
double Seconds(Clock::time_point start){return std::chrono::duration<double>(Clock::now()-start).count();}
struct Measure{double Area=0,U=0;Rect Box;};
Measure Evaluate(const Problem& p,const Result& r)
{
    Measure m;bool first=true;
    for(const auto& l:r.Layouts)for(const auto& placed:l.Placements)
    {
        const auto part=std::find_if(p.Parts.begin(),p.Parts.end(),[&](const Part& q){return q.ID==placed.PartID;});
        if(part==p.Parts.end())return {};
        auto b=Detail::Bounds(part->Geometry,placed.Transform);m.Area+=Detail::Area(part->Geometry);
        if(first){m.Box=b;first=false;}
        else{double x=std::min(m.Box.X,b.X),y=std::min(m.Box.Y,b.Y);m.Box={x,y,std::max(m.Box.X+m.Box.Width,b.X+b.Width)-x,std::max(m.Box.Y+m.Box.Height,b.Y+b.Height)-y};}
    }
    if(m.Box.Width>0&&m.Box.Height>0)m.U=m.Area/(m.Box.Width*m.Box.Height);
    return m;
}
bool SameInstances(const Result& a,const Result& b)
{
    if(a.Layouts.size()!=1||b.Layouts.size()!=1)return false;
    const auto& x=a.Layouts[0];const auto& y=b.Layouts[0];
    if(x.StockID!=y.StockID||x.StockInstance!=y.StockInstance||x.Width!=y.Width||x.Height!=y.Height)return false;
    const auto ids=[](const Layout& l){std::set<std::pair<std::string,std::size_t>> s;for(const auto& q:l.Placements)s.emplace(q.PartID,q.Instance);return s;};
    return x.Placements.size()==y.Placements.size()&&ids(x)==ids(y);
}
bool SettingsValid(const PlaneCompactionSettings& s)
{return std::isfinite(s.TargetUtilization)&&s.TargetUtilization>=0&&s.TargetUtilization<=1&&std::isfinite(s.SignificantImprovement)&&s.SignificantImprovement>=0&&std::isfinite(s.StageTimeSeconds)&&s.StageTimeSeconds>0&&
    std::isfinite(s.MaxRelativeEnvelopeGrowth)&&s.MaxRelativeEnvelopeGrowth>=0&&s.MaxRelativeEnvelopeGrowth<=100&&
    (!s.EnablePerturbation||(s.PerturbEvery&&s.MaxPerturbationAttempts&&s.MaxPerturbationAttempts<=100000));}
bool NominalSafe(const Problem& p,const Result& result,const std::function<bool()>& stop={})
{
    std::map<std::string,const Part*> parts;for(const auto& part:p.Parts)parts.emplace(part.ID,&part);
    for(const auto& board:result.Layouts)
    for(std::size_t i=0;i<board.Placements.size();++i)
    {
        if(stop&&stop())return false;
        const auto& placed=board.Placements[i];const auto a=AxisSafety::Make(parts.at(placed.PartID)->Geometry,placed.Transform);
        if(AxisSafety::Containment(a,board.Width,board.Height,p.EdgeMargin)!=AxisSafety::Verdict::Safe)return false;
        for(std::size_t j=0;j<i;++j)
        {
            if(stop&&stop())return false;
            if(AxisSafety::Pair(a,AxisSafety::Make(parts.at(board.Placements[j].PartID)->Geometry,
                board.Placements[j].Transform),p.PartGap,p.AllowCommonEdges,p.Tolerance)!=AxisSafety::Verdict::Safe)return false;
        }
    }
    return true;
}
class Shrink final:public IPostProcessor
{
    StrategyFactory Construct;std::size_t Probes;
public:
    Shrink(StrategyFactory f,std::size_t probes):Construct(std::move(f)),Probes(probes){}
    Result Run(const Problem& p,const Result& input,const Options& options)override
    {
        const auto start=Clock::now();auto checked=Validate(p,input);
        if(!checked.Valid||!checked.Complete||input.Layouts.empty())return input;
        if(!std::isfinite(options.TimeLimitSeconds)||options.TimeLimitSeconds<=0||!options.MaxPlacementAttempts)
        {auto skipped=input;skipped.Diagnostics.push_back("plane.shrink skipped: invalid budget");return skipped;}
        if(input.Layouts.size()>1)
        {
            // Isolate the tail's original identities. Earlier physical sheets
            // and their placements are never passed to the reconstruction.
            const auto& tail=input.Layouts.back();Problem local=p;
            const auto stock=std::find_if(p.Stocks.begin(),p.Stocks.end(),[&](const Stock& s){return s.ID==tail.StockID;});
            local.Stocks={*stock};local.Stocks[0].Quantity=1;
            std::map<std::string,std::vector<std::size_t>> originalIDs;
            for(const auto& q:tail.Placements)originalIDs[q.PartID].push_back(q.Instance);
            for(auto& [id,ids]:originalIDs)std::sort(ids.begin(),ids.end());
            for(auto& part:local.Parts)part.Quantity=originalIDs[part.ID].size();
            Result seed;seed.State=Status::Complete;seed.Layouts={tail};seed.Layouts[0].StockInstance=0;
            for(auto& q:seed.Layouts[0].Placements)
            {const auto& ids=originalIDs.at(q.PartID);q.Instance=std::lower_bound(ids.begin(),ids.end(),q.Instance)-ids.begin();}
            auto remaining=options;remaining.TimeLimitSeconds-=Seconds(start);
            auto refined=Run(local,seed,remaining);
            if(!Validate(local,refined).Complete||!SameInstances(seed,refined))return input;
            auto candidate=input;auto board=refined.Layouts[0];board.StockInstance=tail.StockInstance;
            for(auto& q:board.Placements)q.Instance=originalIDs.at(q.PartID).at(q.Instance);
            candidate.Layouts.back()=std::move(board);candidate.Stats=refined.Stats;candidate.Stats.ElapsedSeconds=Seconds(start);
            candidate.Diagnostics.insert(candidate.Diagnostics.end(),refined.Diagnostics.begin(),refined.Diagnostics.end());
            if(refined.State==Status::Cancelled)candidate.State=Status::Cancelled;
            const auto verified=Validate(p,candidate);return verified.Valid&&verified.Complete?candidate:input;
        }
        Result best=input;std::size_t attempts=0,accepted=0,probes=0;bool cancelled=false;
        const auto stop=[&]{if(!cancelled&&options.Cancel&&options.Cancel())cancelled=true;return cancelled||Seconds(start)>=options.TimeLimitSeconds||attempts>=options.MaxPlacementAttempts;};
        const auto original=input.Layouts[0];
        const auto stock=std::find_if(p.Stocks.begin(),p.Stocks.end(),[&](const Stock& s){return s.ID==original.StockID;});
        std::map<std::string,std::vector<std::size_t>> ids;
        for(const auto& q:original.Placements)ids[q.PartID].push_back(q.Instance);
        for(auto& [id,list]:ids)std::sort(list.begin(),list.end());
        for(int axis=0;axis<2&&!stop();++axis)
        {
            auto measured=Evaluate(p,best);const bool width=(axis==0)==options.BasicCompactionXFirst;
            const auto other=(width?original.Height:original.Width)-2*p.EdgeMargin;
            if(other<=0)continue;
            double lo=2*p.EdgeMargin+measured.Area/other;
            double hi=(width?measured.Box.Width:measured.Box.Height)+2*p.EdgeMargin;
            for(std::size_t probe=0;probe<Probes&&!stop()&&hi-lo>p.Tolerance;++probe)
            {
                const double mid=(lo+hi)/2;Problem local=p;local.Stocks={*stock};local.Stocks[0].Quantity=1;
                if(width)local.Stocks[0].Width=mid;else local.Stocks[0].Height=mid;
                for(auto& part:local.Parts)part.Quantity=ids[part.ID].size();
                auto q=options;q.EnableBasicCompaction=false;q.Ranking=RankingPolicy::CompactArea;
                q.TimeLimitSeconds=(options.TimeLimitSeconds-Seconds(start))/static_cast<double>(2*Probes-probes);
                q.MaxPlacementAttempts=(options.MaxPlacementAttempts-attempts)/std::max<std::size_t>(1,2*Probes-probes);q.Cancel=stop;
                if(!q.MaxPlacementAttempts)break;
                auto solver=Construct();if(!solver)break;
                auto trial=solver->Run(local,q);attempts+=std::min(trial.Stats.PlacementAttempts,options.MaxPlacementAttempts-attempts);++probes;
                auto valid=Validate(local,trial);
                if(!valid.Valid||!valid.Complete||trial.Layouts.size()!=1){lo=mid;continue;}
                auto& board=trial.Layouts[0];board.Width=original.Width;board.Height=original.Height;board.StockInstance=original.StockInstance;
                for(auto& placed:board.Placements)placed.Instance=ids.at(placed.PartID).at(placed.Instance);
                valid=Validate(p,trial);
                if(valid.Valid&&valid.Complete&&SameInstances(input,trial))
                {
                    hi=mid;
                    if(Evaluate(p,trial).U>Evaluate(p,best).U){best=std::move(trial);++accepted;}
                }
                else lo=mid;
            }
        }
        best.Stats={};best.Stats.PlacementAttempts=attempts;best.Stats.ElapsedSeconds=Seconds(start);
        if(cancelled)best.State=Status::Cancelled;
        best.Diagnostics.push_back("plane.shrink probes="+std::to_string(probes)+" accepted="+std::to_string(accepted)+"; fixed original instances, heuristic bounds");
        return best;
    }
};
class Optimizer final:public IStrategy
{
    StrategyFactory Construct;PlaneCompactionSettings Settings;double Fraction;
public:
    Optimizer(StrategyFactory f,PlaneCompactionSettings s,double fraction):Construct(std::move(f)),Settings(s),Fraction(fraction){}
    bool OwnsRefinementLifecycle()const noexcept override{return true;}
    Result Run(const Problem& p,const Options& options)override
    {
        const auto start=Clock::now();auto q=options;q.EnableBasicCompaction=false;q.TimeLimitSeconds*=Fraction;
        if(options.MaxPlacementAttempts)q.MaxPlacementAttempts=std::max<std::size_t>(1,
            static_cast<std::size_t>(static_cast<long double>(options.MaxPlacementAttempts)*Fraction));
        auto constructor=Construct();if(!constructor){Result r;r.Diagnostics.push_back("Missing initial constructor.");return r;}
        auto initial=constructor->Run(p,q);const auto v=Validate(p,initial);
        if(!v.Valid||!v.Complete||initial.Layouts.empty())
        {initial.Diagnostics.push_back("plane.optimize initial complete layout not found");return initial;}
        auto rest=options;rest.TimeLimitSeconds=std::max(0.,options.TimeLimitSeconds-Seconds(start));
        rest.MaxPlacementAttempts-=std::min(rest.MaxPlacementAttempts,initial.Stats.PlacementAttempts);
        if(rest.TimeLimitSeconds<=0||!rest.MaxPlacementAttempts)return initial;
        auto refined=MakePlaneOptimizationStage(Settings,Construct).Factory()->Run(p,initial,rest);
        refined.Stats.PlacementAttempts+=initial.Stats.PlacementAttempts;
        refined.Stats.ElapsedSeconds=Seconds(start);return refined;
    }
};
class MultiOptimizer final:public IPostProcessor
{
    PlaneCompactionSettings Settings;StrategyFactory Construct;
public:
    MultiOptimizer(PlaneCompactionSettings settings,StrategyFactory construct):Settings(settings),Construct(std::move(construct)){}
    Result Run(const Problem& p,const Result& initial,const Options& options)override
    {
        const auto started=Clock::now();auto kept=initial;Statistics work;work.VerificationSeconds=work.EvaluationSeconds=0;
        bool cancelled=false,callbackFailed=false;std::size_t refinedBoards=0,cachedBoards=0,accepted=0,checkpoints=0;
        const auto cancel=[&]
        {
            if(!cancelled&&!callbackFailed&&options.Cancel)
            {try{if(options.Cancel())cancelled=true;}catch(...){callbackFailed=true;}}
            return cancelled||callbackFailed;
        };
        const auto stop=[&]{return cancel()||Seconds(started)>=options.TimeLimitSeconds||work.PlacementAttempts>=options.MaxPlacementAttempts;};
        const auto finish=[&](const std::string& reason)
        {
            const auto combinations=kept.Stats.CombinationGroups,circles=kept.Stats.CircleLatticeGroups,fillers=kept.Stats.GapFilledParts;
            kept.Stats=work;kept.Stats.CombinationGroups=combinations;kept.Stats.CircleLatticeGroups=circles;kept.Stats.GapFilledParts=fillers;
            kept.Stats.ElapsedSeconds=kept.Stats.PostProcessingSeconds=Seconds(started);
            if(cancelled)kept.State=Status::Cancelled;
            kept.Diagnostics.push_back("plane.optimize-job: "+reason+" refined-boards="+std::to_string(refinedBoards)+
                " reused-refinements="+std::to_string(cachedBoards)+" local-checkpoints="+std::to_string(checkpoints)+" accepted="+std::to_string(accepted)+
                "; complete original identities, fixed-board compaction, optional cross-board refill and joint recipe mix");return kept;
        };
        if(!SettingsValid(Settings)||!std::isfinite(options.TimeLimitSeconds)||options.TimeLimitSeconds<=0||!options.MaxPlacementAttempts)
            return finish("invalid settings or exhausted budget; incumbent retained");
        if(stop())return finish("cancelled or exhausted budget; incumbent retained");
        const auto validationStarted=Clock::now();const auto checked=Validate(p,initial);
        work.VerificationSeconds+=Seconds(validationStarted);
        if(!checked.Valid||!checked.Complete||!initial.Remaining.empty()||initial.Layouts.empty())
            return finish("requires complete original-valid incumbent");
        JobResult best;best.Output=initial;best.Output.Stats.VerificationSeconds=best.Output.Stats.EvaluationSeconds=0;
        Detail::AssessCandidate(p,best);
        work.VerificationSeconds+=std::max(0.,best.Output.Stats.VerificationSeconds);
        work.EvaluationSeconds+=std::max(0.,best.Output.Stats.EvaluationSeconds);
        const auto assess=[&](Result candidate)
        {
            if(stop())return;
            candidate.Stats.VerificationSeconds=candidate.Stats.EvaluationSeconds=0;
            JobResult trial;trial.Output=std::move(candidate);Detail::AssessCandidate(p,trial);
            work.VerificationSeconds+=std::max(0.,trial.Output.Stats.VerificationSeconds);
            work.EvaluationSeconds+=std::max(0.,trial.Output.Stats.EvaluationSeconds);
            if(!trial.Checked.Valid||!trial.Checked.Complete||stop()||!NominalSafe(p,trial.Output,stop))return;
            if(Detail::Eligible(trial)&&Detail::Quality(trial,p,options.Ranking)<Detail::Quality(best,p,options.Ranking))
            {best=std::move(trial);kept=best.Output;++accepted;}
        };
        // The assessment routine may reorder layouts. Iterate immutable
        // physical identities and publish each completed board independently;
        // a later timeout must not discard an earlier accepted improvement.
        const auto originalBoards=initial.Layouts;
        std::map<std::string,Result> refinedRecipes;
        const auto crossStages=std::size_t{Settings.RefillAcrossSheets}+std::size_t{Settings.MixSheetRecipes};
        for(std::size_t bi=0;bi<originalBoards.size()&&!stop();++bi)
        {
            const auto original=originalBoards[bi];
            const auto stock=std::find_if(p.Stocks.begin(),p.Stocks.end(),[&](const Stock& s){return s.ID==original.StockID;});
            if(stock==p.Stocks.end())continue;
            std::map<std::string,std::vector<std::size_t>> ids;
            std::map<std::pair<std::string,std::size_t>,std::pair<std::size_t,bool>> provenance;
            for(const auto& member:original.Placements)
            {ids[member.PartID].push_back(member.Instance);provenance.emplace(std::pair{member.PartID,member.Instance},std::pair{member.Group,member.GapFiller});}
            // Canonicalize equal-specification local identities by their
            // actual source pose. Sparse/global numbering does not change
            // geometry and must not defeat current-job refinement reuse.
            for(auto& [id,values]:ids)
            {
                std::vector<const Placement*> members;
                for(const auto& member:original.Placements)if(member.PartID==id)members.push_back(&member);
                std::stable_sort(members.begin(),members.end(),[](const auto* a,const auto* b)
                {return std::tie(a->Transform.AngleDegrees,a->Transform.Mirrored,a->Transform.Y,a->Transform.X,a->Instance)<
                        std::tie(b->Transform.AngleDegrees,b->Transform.Mirrored,b->Transform.Y,b->Transform.X,b->Instance);});
                values.clear();for(const auto* member:members)values.push_back(member->Instance);
            }
            Problem local=p;local.Stocks={*stock};local.Stocks[0].Quantity=1;
            local.Parts.erase(std::remove_if(local.Parts.begin(),local.Parts.end(),[&](const Part& part){return !ids.contains(part.ID);}),local.Parts.end());
            for(auto& part:local.Parts)part.Quantity=ids.at(part.ID).size();
            Result seed;seed.State=Status::Complete;seed.Layouts={original};seed.Layouts[0].StockInstance=0;seed.GeometryValidated=true;
            for(auto& member:seed.Layouts[0].Placements)
            {const auto& values=ids.at(member.PartID);member.Instance=std::find(values.begin(),values.end(),member.Instance)-values.begin();}
            std::stable_sort(seed.Layouts[0].Placements.begin(),seed.Layouts[0].Placements.end(),[](const auto& a,const auto& b)
            {return std::tie(a.PartID,a.Instance)<std::tie(b.PartID,b.Instance);});
            std::ostringstream signature;signature.precision(17);signature<<original.StockID<<'\n';
            std::map<std::size_t,std::size_t> groups;std::size_t nextGroup=1;
            for(const auto& member:seed.Layouts[0].Placements)
            {
                std::size_t group=0;if(member.Group){auto [where,inserted]=groups.emplace(member.Group,nextGroup);if(inserted)++nextGroup;group=where->second;}
                signature<<member.PartID.size()<<':'<<member.PartID<<':'<<member.Instance<<':'<<member.Transform.X<<':'<<member.Transform.Y<<':'
                    <<member.Transform.AngleDegrees<<':'<<member.Transform.Mirrored<<':'<<group<<':'<<member.GapFiller<<';';
            }
            Result localBest;
            const auto cached=refinedRecipes.find(signature.str());
            if(cached!=refinedRecipes.end())
            {
                bool charged=true;
                for(std::size_t i=0;i<original.Placements.size();++i)
                {if(stop()){charged=false;break;}++work.PlacementAttempts;}
                if(!charged)break;
                localBest=cached->second;++cachedBoards;
            }
            else
            {
                const auto units=originalBoards.size()-bi+crossStages+1;
                auto q=options;q.EnableBasicCompaction=false;q.Cancel=cancel;
                q.TimeLimitSeconds=std::max(0.,options.TimeLimitSeconds-Seconds(started))/static_cast<double>(units);
                q.MaxPlacementAttempts=(options.MaxPlacementAttempts-work.PlacementAttempts)/units;
                if(!q.MaxPlacementAttempts||q.TimeLimitSeconds<=0)break;
                auto settings=Settings;settings.RefillAcrossSheets=settings.MixSheetRecipes=false;
                auto report=OptimizePlaneLayout(local,seed,q,settings);
                work.PlacementAttempts+=std::min(report.Output.Stats.PlacementAttempts,q.MaxPlacementAttempts);
                localBest=std::move(report.Output);++refinedBoards;
                if(!stop()&&Validate(local,localBest).Valid&&Validate(local,localBest).Complete&&SameInstances(seed,localBest)&&NominalSafe(local,localBest,stop))
                {localBest.Stats={};refinedRecipes.emplace(signature.str(),localBest);}
            }
            if(stop()||!Validate(local,localBest).Valid||!Validate(local,localBest).Complete||!SameInstances(seed,localBest)||!NominalSafe(local,localBest,stop))continue;
            if(Evaluate(local,localBest).U<=Evaluate(local,seed).U)continue;
            auto board=localBest.Layouts[0];board.StockID=original.StockID;board.StockInstance=original.StockInstance;
            for(auto& member:board.Placements)
            {
                member.Instance=ids.at(member.PartID).at(member.Instance);
                const auto& origin=provenance.at({member.PartID,member.Instance});member.Group=origin.first;member.GapFiller=origin.second;
            }
            auto checkpoint=kept;
            const auto target=std::find_if(checkpoint.Layouts.begin(),checkpoint.Layouts.end(),[&](const Layout& value)
            {return value.StockID==original.StockID&&value.StockInstance==original.StockInstance;});
            if(target==checkpoint.Layouts.end())continue;
            *target=std::move(board);
            ++checkpoints;assess(std::move(checkpoint));
        }
        if(Settings.RefillAcrossSheets&&!stop()&&kept.Layouts.size()>1)
        {
            const auto units=std::size_t{1}+std::size_t{Settings.MixSheetRecipes};
            auto q=options;q.EnableBasicCompaction=false;q.Cancel=cancel;
            q.TimeLimitSeconds=std::max(0.,options.TimeLimitSeconds-Seconds(started))/static_cast<double>(units);
            q.MaxPlacementAttempts=(options.MaxPlacementAttempts-work.PlacementAttempts)/units;
            if(q.MaxPlacementAttempts&&q.TimeLimitSeconds>0)
            {
                auto candidate=MakeNfpRefillStage().Factory()->Run(p,kept,q);
                work.PlacementAttempts+=std::min(candidate.Stats.PlacementAttempts,q.MaxPlacementAttempts);
                if(!stop())assess(std::move(candidate));
            }
        }
        if(Settings.MixSheetRecipes&&!stop())
        {
            auto q=options;q.EnableBasicCompaction=false;q.Cancel=cancel;
            q.TimeLimitSeconds=std::max(0.,options.TimeLimitSeconds-Seconds(started));
            q.MaxPlacementAttempts=options.MaxPlacementAttempts-work.PlacementAttempts;
            if(q.MaxPlacementAttempts&&q.TimeLimitSeconds>0)
            {
                auto candidate=OptimizePlaneSheetMix(p,kept,q,Construct);
                work.PlacementAttempts+=std::min(candidate.Stats.PlacementAttempts,q.MaxPlacementAttempts);
                if(!stop())assess(std::move(candidate));
            }
        }
        return finish(cancelled?"cancelled":callbackFailed?"callback failed safely":Seconds(started)>=options.TimeLimitSeconds?"time limit":
            work.PlacementAttempts>=options.MaxPlacementAttempts?"attempt limit":"bounded optimization completed");
    }
};
}
PlaneOptimizationResult OptimizePlaneLayout(const Problem& p,const Result& initial,const Options& options,PlaneCompactionSettings settings)
{
    const auto start=Clock::now();PlaneOptimizationResult report;report.Output=initial;
    if(!SettingsValid(settings)||!std::isfinite(options.TimeLimitSeconds)||options.TimeLimitSeconds<0)
    {report.Output.State=Status::InvalidInput;report.Output.Diagnostics.push_back("plane.optimize invalid settings or budget");return report;}
    const auto checked=Validate(p,initial);
    if(!checked.Valid||!checked.Complete||initial.Layouts.size()!=1||initial.Layouts[0].Placements.empty())
    {report.Stop=PlaneOptimizationStop::InitialNotFound;if(!checked.Valid)report.Output.State=Status::InvalidInput;report.Output.Diagnostics.push_back("plane.optimize needs a complete nonempty single-sheet initial layout");return report;}
    const auto measure=Evaluate(p,initial);report.InitialUtilization=report.BestUtilization=measure.U;report.MaterialArea=measure.Area;report.BestEnvelope=measure.Box;
    double stableReference=measure.U;std::size_t attempts=0;bool cancelled=false;
    Result current=initial;double currentU=measure.U;
    std::mt19937_64 random(options.Seed);
    std::uniform_real_distribution<double> unit(-1.,1.);
    std::map<std::string,const Part*> originals;for(const auto& part:p.Parts)originals.emplace(part.ID,&part);
    std::vector<PostProcessStage> stages{MakeAxisCompactionStage()};
    if(settings.Rotate)stages.push_back(MakeRotationCompactionStage());
    if(settings.Reassemble)stages.push_back(MakeNfpReassemblyStage());
    if(settings.ShrinkRegion)stages.push_back(MakePlaneShrinkStage(MakePlaneStrategyFactory(),4));
    bool callbackFailed=false;
    const auto poll=[&]
    {
        if(!cancelled&&!callbackFailed&&options.Cancel)
        {try{if(options.Cancel())cancelled=true;}catch(...){callbackFailed=true;}}
        return cancelled||callbackFailed;
    };
    const auto interrupted=[&]{return poll()||Seconds(start)>=options.TimeLimitSeconds;};
    while(true)
    {
        if(poll()){report.Stop=cancelled?PlaneOptimizationStop::Cancelled:PlaneOptimizationStop::InvalidInput;break;}
        if(settings.TargetUtilization>0&&report.BestUtilization>=settings.TargetUtilization){report.Stop=PlaneOptimizationStop::Target;break;}
        if(Seconds(start)>=options.TimeLimitSeconds){report.Stop=PlaneOptimizationStop::Time;break;}
        if(attempts>=options.MaxPlacementAttempts){report.Stop=PlaneOptimizationStop::Attempts;break;}
        if(settings.StableIterations&&report.StableCount>=settings.StableIterations){report.Stop=PlaneOptimizationStop::Stable;break;}
        auto q=options;q.EnableBasicCompaction=false;q.Ranking=RankingPolicy::CompactArea;
        // With one immutable physical board and fixed instances, CompactArea's
        // completeness/consolidation prefixes are constants: its area term is U.
        q.TimeLimitSeconds=std::min(settings.StageTimeSeconds,options.TimeLimitSeconds-Seconds(start));
        q.MaxPlacementAttempts=options.MaxPlacementAttempts-attempts;q.Cancel=poll;
        Result trial;
        try{trial=stages[report.Iterations%stages.size()].Factory()->Run(p,current,q);}
        catch(const std::exception& e){trial=current;trial.Stats={};report.Output.Diagnostics.push_back(std::string("plane.optimize operator failed: ")+e.what());}
        attempts+=std::min(trial.Stats.PlacementAttempts,options.MaxPlacementAttempts-attempts);++report.Iterations;
        const auto valid=Validate(p,trial);
        if(valid.Valid&&valid.Complete&&SameInstances(initial,trial)&&!interrupted()&&NominalSafe(p,trial,interrupted))
        {
            const auto m=Evaluate(p,trial);
            if(m.U>report.BestUtilization)
            {report.Output=trial;report.BestUtilization=m.U;report.BestEnvelope=m.Box;++report.Improvements;}
            if(m.U>=currentU||(settings.EnablePerturbation&&m.U>=report.BestUtilization/(1+settings.MaxRelativeEnvelopeGrowth)))
            {if(m.U<currentU)++report.AcceptedWorse;current=std::move(trial);currentU=m.U;}
        }
        if(report.BestUtilization>stableReference+settings.SignificantImprovement){stableReference=report.BestUtilization;report.StableCount=0;}else ++report.StableCount;
        if(settings.EnablePerturbation&&report.StableCount&&report.StableCount%settings.PerturbEvery==0)
        {
            // current is disposable working state. A feasible temporary growth
            // can release a blocking contact; every published result remains
            // the best complete original instance set encountered so far.
            for(std::size_t probe=0;probe<settings.MaxPerturbationAttempts;++probe)
            {
                if(poll()||Seconds(start)>=options.TimeLimitSeconds||attempts>=options.MaxPlacementAttempts)break;
                auto draft=current;auto& board=draft.Layouts.front();
                const auto chosen=static_cast<std::size_t>(random()%board.Placements.size());
                auto& placed=board.Placements[chosen];const auto& part=*originals.at(placed.PartID);
                const auto old=Detail::Bounds(part.Geometry,placed.Transform);
                auto angles=Detail::CandidateAngles(part,p.Tolerance);
                if(probe%3==0&&!angles.empty())placed.Transform.AngleDegrees=angles[random()%angles.size()];
                if(probe%5==0&&part.FreeRotation)placed.Transform.AngleDegrees=(unit(random)+1)*180;
                const auto local=Detail::Bounds(part.Geometry,{0,0,placed.Transform.AngleDegrees,placed.Transform.Mirrored});
                const auto dx=unit(random)*report.BestEnvelope.Width*settings.MaxRelativeEnvelopeGrowth*.5;
                const auto dy=unit(random)*report.BestEnvelope.Height*settings.MaxRelativeEnvelopeGrowth*.5;
                const auto lx=p.EdgeMargin-local.X,rx=board.Width-p.EdgeMargin-local.X-local.Width;
                const auto ly=p.EdgeMargin-local.Y,ty=board.Height-p.EdgeMargin-local.Y-local.Height;
                if(rx<lx||ty<ly)continue;
                placed.Transform.X=std::clamp(old.X+old.Width/2-local.X-local.Width/2+dx,lx,rx);
                placed.Transform.Y=std::clamp(old.Y+old.Height/2-local.Y-local.Height/2+dy,ly,ty);
                ++attempts;++report.Perturbations;
                const auto verified=Validate(p,draft);
                if(!verified.Valid||!verified.Complete||!SameInstances(initial,draft)||interrupted()||!NominalSafe(p,draft,interrupted))continue;
                const auto m=Evaluate(p,draft);
                if(m.U<report.BestUtilization/(1+settings.MaxRelativeEnvelopeGrowth))continue;
                if(m.U>report.BestUtilization)
                {report.Output=draft;report.BestUtilization=m.U;report.BestEnvelope=m.Box;++report.Improvements;}
                if(m.U<currentU)++report.AcceptedWorse;
                current=std::move(draft);currentU=m.U;break;
            }
        }
        if(report.BestUtilization>stableReference+settings.SignificantImprovement)
        {stableReference=report.BestUtilization;report.StableCount=0;}
    }
    report.ElapsedSeconds=Seconds(start);report.Output.Stats.PlacementAttempts=attempts;report.Output.Stats.ElapsedSeconds=report.ElapsedSeconds;
    if(cancelled)report.Output.State=Status::Cancelled;
    std::ostringstream message;message.precision(17);message<<"plane.optimize initialU="<<report.InitialUtilization<<" bestU="<<report.BestUtilization<<" iterations="<<report.Iterations<<" improvements="<<report.Improvements<<" perturbations="<<report.Perturbations<<" accepted-worse="<<report.AcceptedWorse<<" stable="<<report.StableCount<<" stop="<<static_cast<int>(report.Stop);
    report.Output.Diagnostics.push_back(message.str());return report;
}
PostProcessStage MakePlaneShrinkStage(StrategyFactory construct,std::size_t probes)
{if(!construct||!probes||probes>64)throw std::invalid_argument("Invalid plane shrink configuration.");return {"plane.shrink",[construct=std::move(construct),probes]{return std::make_unique<Shrink>(construct,probes);}};}
StrategyFactory MakePlaneOptimizationFactory(StrategyFactory construct,PlaneCompactionSettings settings,double fraction)
{
    if(!construct||!SettingsValid(settings)||!std::isfinite(fraction)||fraction<=0||fraction>=1)throw std::invalid_argument("Invalid plane optimization configuration.");
    return [construct=std::move(construct),settings,fraction]{return std::make_unique<Optimizer>(construct,settings,fraction);};
}
PostProcessStage MakePlaneOptimizationStage(PlaneCompactionSettings settings,StrategyFactory reconstruct)
{
    if(!SettingsValid(settings))throw std::invalid_argument("Invalid plane optimization-stage settings.");
    if(!reconstruct)reconstruct=MakePlaneStrategyFactory();
    return {"plane.optimize-job",[settings,reconstruct=std::move(reconstruct)]{return std::make_unique<MultiOptimizer>(settings,reconstruct);}};
}
}
