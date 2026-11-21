#include "SheetNestingPlaneDomains.h"
#include "SheetNestingPlaneStrategies.h"
#include "SheetNestingPlaneCompaction.h"
#include "NestingPlaneDomainGeometry.h"
#include "NestingNfpPlacement.h"
#include "NestingClearancePlacement.h"
#include "NestingSheetTemplateReuse.h"
#include "SheetNestingNfp.h"
#include <chrono>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <tuple>

namespace iCAX::SheetNesting
{
namespace
{
using namespace Detail;
using Clock=std::chrono::steady_clock;
double Seconds(Clock::time_point start){return std::chrono::duration<double>(Clock::now()-start).count();}
std::vector<std::string> DomainErrors(const PlaneDomainTask& task)
{
    auto errors=CheckProblem(task.Input);std::set<std::string> seen;
    if(!std::isfinite(task.MinimumSheetReuseUtilization)||task.MinimumSheetReuseUtilization<0||task.MinimumSheetReuseUtilization>1)
        errors.push_back("Domain sheet-template utilization gate must be finite and in [0,1].");
    if(!task.MaxInnerRectangles||task.MaxInnerRectangles>64||!task.MaxBoardCandidates||task.MaxBoardCandidates>128||
        !std::isfinite(task.MaxRigidShift)||task.MaxRigidShift<0||task.RigidShiftSteps>32||task.InnerShrinkProbes>4)
        errors.push_back("Invalid bounded domain candidate or rigid-shift configuration.");
    for(const auto& domain:task.Domains)
    {
        const auto stock=std::find_if(task.Input.Stocks.begin(),task.Input.Stocks.end(),[&](const Stock& s){return s.ID==domain.StockID;});
        std::string error;
        if(stock==task.Input.Stocks.end()||!seen.insert(domain.StockID).second){errors.push_back("Unknown/duplicate stock domain.");continue;}
        if(domain.Boundary.Radius>0||!ShapeValid(domain.Boundary,task.Input.Tolerance,error)||!InsideSheet(domain.Boundary,{},stock->Width,stock->Height,0,task.Input.Tolerance))errors.push_back("Invalid polygon stock domain: "+domain.StockID);
        for(const auto& obstacle:domain.Obstacles)if(obstacle.Radius>0||!ShapeValid(obstacle,task.Input.Tolerance,error))errors.push_back("Invalid polygon fixed obstacle.");
    }
    return errors;
}
struct DomainBudget
{
    const Options& O;Clock::time_point Start=Clock::now();
    std::size_t Attempts=0;bool Cancelled=false,CallbackFailed=false;
    double Seconds()const{return std::chrono::duration<double>(Clock::now()-Start).count();}
    bool Stop()
    {
        if(!Cancelled&&!CallbackFailed&&O.Cancel)
        {
            try{Cancelled=O.Cancel();}catch(...){CallbackFailed=true;}
        }
        return Cancelled||CallbackFailed||Seconds()>=O.TimeLimitSeconds||Attempts>=O.MaxPlacementAttempts;
    }
    bool Charge(){if(Stop())return false;++Attempts;return true;}
    void Add(std::size_t attempts){Attempts+=std::min(attempts,O.MaxPlacementAttempts-Attempts);}
};
struct DomainPhase
{
    DomainBudget& B;double End;std::size_t AttemptEnd;
    bool Stop(){return B.Stop()||B.Seconds()>=End||B.Attempts>=AttemptEnd;}
    Options Remaining(double fraction=1.)
    {
        auto q=B.O;q.EnableBasicCompaction=false;
        q.TimeLimitSeconds=std::max(0.,std::min(End,B.O.TimeLimitSeconds)-B.Seconds())*fraction;
        q.MaxPlacementAttempts=static_cast<std::size_t>((std::min(AttemptEnd,B.O.MaxPlacementAttempts)-B.Attempts)*fraction);
        q.Cancel=[this]{return Stop();};return q;
    }
};
struct DomainState
{
    Layout Board;std::vector<std::size_t> Used;std::size_t Group=1;
};
std::optional<Rect> DomainBounds(const Problem& p,const Layout& board)
{
    std::optional<Rect> bounds;
    for(const auto& placed:board.Placements)
    {
        const auto part=std::find_if(p.Parts.begin(),p.Parts.end(),[&](const Part& value){return value.ID==placed.PartID;});
        if(part==p.Parts.end())return {};
        const auto r=Bounds(part->Geometry,placed.Transform);
        if(!bounds)bounds=r;
        else
        {
            const auto x=std::min(bounds->X,r.X),y=std::min(bounds->Y,r.Y);
            bounds=Rect{x,y,std::max(bounds->X+bounds->Width,r.X+r.Width)-x,
                std::max(bounds->Y+bounds->Height,r.Y+r.Height)-y};
        }
    }
    return bounds;
}
auto DomainRank(const Problem& p,const Layout& board)
{
    const auto box=DomainBounds(p,board).value_or(Rect{});
    return std::tuple(-board.MaterialArea,-static_cast<double>(board.Placements.size()),
        box.Width*box.Height,box.Y+box.Height,box.X+box.Width,box.Y,box.X);
}
class DomainConstructor
{
    const PlaneDomainTask& Task;const Problem& P;const Stock& StockValue;DomainBudget& B;
    std::map<std::string,std::size_t> Index;
    Problem Shadow;std::vector<Placement> Obstacles;std::set<std::string> NeutralIDs;
    std::unique_ptr<ClearanceNfpPlacement> Generator;
public:
    Clipper2Lib::PathsD Allowed;std::vector<Rect> Rectangles;
    std::size_t RectangleTrials=0,JointTrials=0,PrefillTrials=0,ShiftTrials=0,PeripheralParts=0,CompressionTrials=0;
    bool NfpReady=false;
    DomainConstructor(const PlaneDomainTask& task,const Stock& stock,DomainBudget& budget,
        const std::shared_ptr<NfpCache>& cache):Task(task),P(task.Input),StockValue(stock),B(budget)
    {
        for(std::size_t i=0;i<P.Parts.size();++i)Index.emplace(P.Parts[i].ID,i);
        Shape boundary;boundary.Outer={{0,0},{stock.Width,0},{stock.Width,stock.Height},{0,stock.Height}};
        const auto found=std::find_if(task.Domains.begin(),task.Domains.end(),[&](const auto& d){return d.StockID==stock.ID;});
        Allowed=found==task.Domains.end()?PlaneDomain::Allowed(boundary,{},P.EdgeMargin):
            PlaneDomain::Allowed(found->Boundary,found->Obstacles,P.EdgeMargin);
        const auto slabs=PlaneDomain::Decompose(Allowed,stock.Width,stock.Height,P.Tolerance);
        Rectangles=slabs.Inner;
        if(found!=task.Domains.end())Rectangles.insert(Rectangles.end(),found->InnerRectangles.begin(),found->InnerRectangles.end());
        // Merge adjacent certified strips. The resulting rectangles are still
        // proposals and receive real-domain containment checks below.
        const auto base=slabs.Inner;
        for(std::size_t i=0;i<base.size()&&!B.Stop();++i)
        {
            auto merged=base[i];
            for(std::size_t j=i+1;j<base.size()&&!B.Stop();++j)
                if(std::abs(merged.X+merged.Width-base[j].X)<=P.Tolerance&&
                    std::abs(merged.Y-base[j].Y)<=P.Tolerance&&std::abs(merged.Height-base[j].Height)<=P.Tolerance)
                {merged.Width+=base[j].Width;Rectangles.push_back(merged);}
        }
        Rectangles.erase(std::remove_if(Rectangles.begin(),Rectangles.end(),[](Rect rect)
        {return !std::isfinite(rect.X)||!std::isfinite(rect.Y)||!std::isfinite(rect.Width)||!std::isfinite(rect.Height)||rect.Width<=0||rect.Height<=0;}),Rectangles.end());
        std::stable_sort(Rectangles.begin(),Rectangles.end(),[](Rect a,Rect b)
        {return std::tuple(-a.Width*a.Height,a.Y,a.X,a.Width)<std::tuple(-b.Width*b.Height,b.Y,b.X,b.Width);});
        std::vector<Rect> certified;
        for(const auto rect:Rectangles)
        {
            if(B.Stop())break;
            if(!std::isfinite(rect.X)||!std::isfinite(rect.Y)||!std::isfinite(rect.Width)||!std::isfinite(rect.Height)||rect.Width<=0||rect.Height<=0)continue;
            if(std::any_of(certified.begin(),certified.end(),[&](Rect r)
                {return std::abs(r.X-rect.X)<=P.Tolerance&&std::abs(r.Y-rect.Y)<=P.Tolerance&&
                    std::abs(r.Width-rect.Width)<=P.Tolerance&&std::abs(r.Height-rect.Height)<=P.Tolerance;}))continue;
            Shape box;box.Outer={{0,0},{rect.Width,0},{rect.Width,rect.Height},{0,rect.Height}};
            if(PlaneDomain::Inside(box,{rect.X,rect.Y,0},Allowed,P.Tolerance))certified.push_back(rect);
            if(certified.size()>=Task.MaxInnerRectangles)break;
        }
        Rectangles=std::move(certified);
        Shadow=P;Shadow.Stocks={stock};Shadow.EdgeMargin=0;
        std::set<std::string> reserved;for(const auto& part:P.Parts)reserved.insert(part.ID);
        for(const auto& blocker:slabs.Blockers)
        {
            Part part;part.ID="__plane_obstacle_"+std::to_string(Shadow.Parts.size());
            while(reserved.contains(part.ID))part.ID+='_';reserved.insert(part.ID);
            part.Geometry=blocker;part.Quantity=0;part.Material=stock.Material;part.FreeRotation=false;part.AllowedAnglesDegrees={0};
            NeutralIDs.insert(part.ID);Obstacles.push_back({part.ID,0,{},0,false});Shadow.Parts.push_back(std::move(part));
        }
        // Domain obstacles already encode real edge/defect clearance. Only
        // production originals carry PartGap; expanding the domain again would
        // incorrectly apply the inter-part distance to a material boundary.
        if(!B.Stop())Generator=std::make_unique<ClearanceNfpPlacement>(Shadow,stock,cache,[this]{return B.Stop();},NeutralIDs);
        NfpReady=Generator&&Generator->Ready();
    }
    bool Fits(const Layout& board,std::size_t pi,Pose pose,std::size_t ignore=std::numeric_limits<std::size_t>::max())const
    {
        const auto& part=P.Parts[pi];
        if(!PoseAllowed(part,pose,P.Tolerance))return false;
        if(!PlaneDomain::Inside(part.Geometry,pose,Allowed,P.Tolerance)||
            !InsideSheet(part.Geometry,pose,board.Width,board.Height,P.EdgeMargin,P.Tolerance))return false;
        const auto geometry=AxisSafety::Make(part.Geometry,pose);
        if(AxisSafety::Containment(geometry,board.Width,board.Height,P.EdgeMargin)!=AxisSafety::Verdict::Safe)return false;
        for(std::size_t i=0;i<board.Placements.size();++i)if(i!=ignore)
        {
            const auto& fixed=board.Placements[i];const auto& other=P.Parts[Index.at(fixed.PartID)];
            if(ShapesConflict(part.Geometry,pose,other.Geometry,fixed.Transform,P.PartGap,P.Tolerance,P.AllowCommonEdges)||
                AxisSafety::Pair(geometry,AxisSafety::Make(other.Geometry,fixed.Transform),P.PartGap,P.AllowCommonEdges,P.Tolerance)!=AxisSafety::Verdict::Safe)return false;
        }
        return true;
    }
    void Seed(DomainState& state,Rect rect,DomainPhase& phase)
    {
        if(phase.Stop())return;++RectangleTrials;
        Problem local=P;local.Stocks={StockValue};local.Stocks[0].Width=rect.Width;local.Stocks[0].Height=rect.Height;local.Stocks[0].Quantity=1;local.EdgeMargin=0;
        bool demand=false;
        for(std::size_t i=0;i<P.Parts.size();++i){local.Parts[i].Quantity-=state.Used[i];demand|=local.Parts[i].Quantity&&local.Parts[i].Material==StockValue.Material;}
        if(!demand)return;
        auto q=phase.Remaining(.5);if(q.TimeLimitSeconds<=0||!q.MaxPlacementAttempts)return;
        const auto seed=MakePlaneStrategyFactory()()->Run(local,q);B.Add(seed.Stats.PlacementAttempts);
        if(!Validate(local,seed).Valid||seed.Layouts.empty())return;
        std::map<std::size_t,std::size_t> groups;
        for(auto placed:seed.Layouts.front().Placements)
        {
            if(phase.Stop()||!B.Charge())break;
            const auto pi=Index.at(placed.PartID);if(state.Used[pi]>=P.Parts[pi].Quantity)continue;
            placed.Transform=ComposePose({rect.X,rect.Y,0},placed.Transform);
            if(!Fits(state.Board,pi,placed.Transform))continue;
            placed.Instance=state.Used[pi]++;
            if(placed.Group)
            {const auto [where,inserted]=groups.emplace(placed.Group,state.Group);if(inserted)++state.Group;placed.Group=where->second;}
            state.Board.MaterialArea+=Area(P.Parts[pi].Geometry);state.Board.Placements.push_back(placed);
        }
    }
    void Fill(DomainState& state,DomainPhase& phase)
    {
        if(!Task.EnablePeripheralFill||!NfpReady)return;
        const bool supplemental=!state.Board.Placements.empty();
        while(!phase.Stop())
        {
            Layout environment=state.Board;environment.Placements.insert(environment.Placements.end(),Obstacles.begin(),Obstacles.end());
            bool placed=false;
            for(std::size_t i=0;i<P.Parts.size()&&!placed&&!phase.Stop();++i)
            {
                const auto& part=P.Parts[i];if(state.Used[i]>=part.Quantity||part.Material!=StockValue.Material)continue;
                for(const auto orientation:CandidateOrientations(part,P.Tolerance,B.O.OrientationProposals))
                {
                    if(phase.Stop())break;
                    NfpPlacementLimits limits;limits.MaxAttempts=std::min(phase.AttemptEnd,B.O.MaxPlacementAttempts)-B.Attempts;
                    limits.Mirrored=orientation.Mirrored;
                    auto query=Generator->Find(environment,i,orientation.AngleDegrees,limits,[&]{return phase.Stop();});B.Add(query.Attempts);
                    for(auto pose:query.Fits)
                    {
                        if(phase.Stop()||!B.Charge())break;
                        if(!Fits(state.Board,i,pose))continue;
                        state.Board.Placements.push_back({part.ID,state.Used[i]++,pose,state.Group++,supplemental});
                        state.Board.MaterialArea+=Area(part.Geometry);++PeripheralParts;placed=true;break;
                    }
                    if(placed)break;
                }
            }
            if(!placed)break;
        }
    }
    bool Shift(DomainState& state,std::size_t begin,DomainPhase& phase,std::size_t end=std::numeric_limits<std::size_t>::max())
    {
        end=std::min(end,state.Board.Placements.size());
        if(begin>=end||!Task.RigidShiftSteps)return false;
        bool moved=false;
        for(const bool xAxis:{false,true})
        {
            if(phase.Stop())break;
            const auto original=state.Board;
            double materialLow=std::numeric_limits<double>::infinity(),groupLow=materialLow;
            for(const auto& ring:Allowed)for(const auto& point:ring)materialLow=std::min(materialLow,xAxis?point.x:point.y);
            for(std::size_t i=begin;i<end;++i)
            {const auto& item=original.Placements[i];const auto box=Bounds(P.Parts[Index.at(item.PartID)].Geometry,item.Transform);groupLow=std::min(groupLow,xAxis?box.X:box.Y);}
            const double derived=groupLow-materialLow;
            if(!std::isfinite(derived)||derived<=P.Tolerance)continue;
            const auto legal=[&](double distance,Layout& trial)
            {
                trial=original;
                for(std::size_t i=begin;i<end;++i)
                {if(phase.Stop()||!B.Charge())return false;auto& pose=trial.Placements[i].Transform;if(xAxis)pose.X-=distance;else pose.Y-=distance;}
                for(std::size_t i=begin;i<end;++i)
                    if(!Fits(trial,Index.at(trial.Placements[i].PartID),trial.Placements[i].Transform,i))return false;
                return true;
            };
            double lo=0,hi=Task.MaxRigidShift>0?std::min(derived,Task.MaxRigidShift):derived;Layout accepted,trial;
            if(legal(hi,trial)){lo=hi;accepted=std::move(trial);}
            else for(std::size_t step=0;step<Task.RigidShiftSteps&&!phase.Stop()&&hi-lo>P.Tolerance;++step)
            {const auto middle=(lo+hi)/2;if(legal(middle,trial)){lo=middle;accepted=std::move(trial);}else hi=middle;}
            if(lo>P.Tolerance){state.Board=std::move(accepted);moved=true;}
        }
        if(moved)++ShiftTrials;return moved;
    }
    bool Compress(DomainState& state,Rect rect,std::size_t begin,std::size_t end,DomainPhase& phase)
    {
        if(!Task.InnerShrinkProbes||begin>=end||end>state.Board.Placements.size()||phase.Stop())return false;
        Problem local=P;local.Stocks={StockValue};local.Stocks[0].Width=rect.Width;local.Stocks[0].Height=rect.Height;
        local.Stocks[0].Quantity=1;local.EdgeMargin=0;
        std::map<std::string,std::vector<Placement>> identities;
        for(std::size_t i=begin;i<end;++i)identities[state.Board.Placements[i].PartID].push_back(state.Board.Placements[i]);
        for(auto& [id,values]:identities){(void)id;std::sort(values.begin(),values.end(),[](const Placement& a,const Placement& b){return a.Instance<b.Instance;});}
        for(auto& part:local.Parts)part.Quantity=identities[part.ID].size();
        local.Parts.erase(std::remove_if(local.Parts.begin(),local.Parts.end(),[](const Part& part){return !part.Quantity;}),local.Parts.end());
        Result seed;seed.State=Status::Complete;Layout board{StockValue.ID,0,rect.Width,rect.Height,{},0};
        for(const auto& [id,values]:identities)for(std::size_t i=0;i<values.size();++i)
        {
            auto item=values[i];item.Instance=i;item.Transform.X-=rect.X;item.Transform.Y-=rect.Y;
            board.MaterialArea+=Area(P.Parts[Index.at(id)].Geometry);board.Placements.push_back(item);
        }
        seed.Layouts={board};const auto sourceCheck=Validate(local,seed);if(!sourceCheck.Valid||!sourceCheck.Complete)return false;
        auto q=phase.Remaining();if(q.TimeLimitSeconds<=0||!q.MaxPlacementAttempts)return false;
        const auto stage=MakePlaneShrinkStage(MakePlaneStrategyFactory(),Task.InnerShrinkProbes);
        auto shrunk=stage.Factory()->Run(local,seed,q);B.Add(shrunk.Stats.PlacementAttempts);++CompressionTrials;
        const auto checked=Validate(local,shrunk);
        if(!checked.Valid||!checked.Complete||shrunk.Layouts.size()!=1||shrunk.Layouts[0].Placements.size()!=end-begin)return false;
        const auto oldBox=DomainBounds(local,seed.Layouts[0]),newBox=DomainBounds(local,shrunk.Layouts[0]);
        if(!oldBox||!newBox||newBox->Width*newBox->Height>=oldBox->Width*oldBox->Height)return false;
        auto trial=state;
        for(std::size_t i=0;i<end-begin;++i)
        {
            auto item=shrunk.Layouts[0].Placements[i];
            const auto& original=identities.at(item.PartID).at(item.Instance);
            item.Instance=original.Instance;item.Group=original.Group;item.GapFiller=original.GapFiller;
            item.Transform.X+=rect.X;item.Transform.Y+=rect.Y;trial.Board.Placements[begin+i]=item;
        }
        for(std::size_t i=begin;i<end;++i)
        {if(phase.Stop()||!B.Charge())return false;const auto& item=trial.Board.Placements[i];if(!Fits(trial.Board,Index.at(item.PartID),item.Transform,i))return false;}
        state=std::move(trial);return true;
    }
    DomainState Construct(DomainState initial)
    {
        if(!Task.UseInnerRectangle||Rectangles.empty())
        {DomainPhase phase{B,B.O.TimeLimitSeconds,B.O.MaxPlacementAttempts};Fill(initial,phase);return initial;}
        struct Plan{std::size_t Main;int Mode;};std::vector<Plan> plans;
        for(std::size_t i=0;i<Rectangles.size()&&plans.size()<Task.MaxBoardCandidates;++i)
        {
            plans.push_back({i,0});
            if(Task.CombineInnerRectangles&&Rectangles.size()>1&&plans.size()<Task.MaxBoardCandidates)plans.push_back({i,1});
            if(Task.EnableInnerPrefill&&Rectangles.size()>1&&plans.size()<Task.MaxBoardCandidates)plans.push_back({i,2});
        }
        auto best=initial;std::optional<DomainState> compressionSeed;Rect compressionRect;
        std::size_t compressionBegin=0,compressionEnd=0;
        const double allowedArea=std::abs(Clipper2Lib::Area(Allowed));
        for(std::size_t n=0;n<plans.size()&&!B.Stop();++n)
        {
            const auto slots=plans.size()-n;
            DomainPhase phase{B,B.Seconds()+std::max(0.,B.O.TimeLimitSeconds-B.Seconds())/slots,
                B.Attempts+(B.O.MaxPlacementAttempts-B.Attempts)/slots};
            if(phase.Stop())continue;
            auto state=initial;const auto plan=plans[n];std::size_t mainBegin=0;
            if(plan.Mode==2)
            {
                ++PrefillTrials;
                // Smaller certified material regions are prefill domains, not
                // the forbidden holes of the material boundary.
                const auto main=Rectangles[plan.Main];
                for(std::size_t j=Rectangles.size();j>0&&!phase.Stop();--j)if(j-1!=plan.Main)
                {
                    const auto rect=Rectangles[j-1];
                    const bool disjoint=rect.X+rect.Width<=main.X||main.X+main.Width<=rect.X||rect.Y+rect.Height<=main.Y||main.Y+main.Height<=rect.Y;
                    if(disjoint)Seed(state,rect,phase);
                }
                mainBegin=state.Board.Placements.size();
            }
            Seed(state,Rectangles[plan.Main],phase);
            const auto mainEnd=state.Board.Placements.size();
            if(plan.Mode==1)
            {++JointTrials;for(std::size_t j=0;j<Rectangles.size()&&!phase.Stop();++j)if(j!=plan.Main)Seed(state,Rectangles[j],phase);}
            if(mainEnd>mainBegin&&(!compressionSeed||DomainRank(P,state.Board)<DomainRank(P,compressionSeed->Board)))
            {compressionSeed=state;compressionRect=Rectangles[plan.Main];compressionBegin=mainBegin;compressionEnd=mainEnd;}
            auto shifted=state;const bool moved=Shift(shifted,mainBegin,phase);
            // Preserve the unshifted baseline; a legal translation alone is not
            // an improvement in internal density and may hinder peripheral fill.
            auto plainPhase=phase;
            if(moved){plainPhase.End=B.Seconds()+std::max(0.,phase.End-B.Seconds())*.5;plainPhase.AttemptEnd=B.Attempts+(phase.AttemptEnd-B.Attempts)/2;}
            Fill(state,plainPhase);
            if(DomainRank(P,state.Board)<DomainRank(P,best.Board))best=std::move(state);
            if(moved&&!phase.Stop())
            {Fill(shifted,phase);if(DomainRank(P,shifted.Board)<DomainRank(P,best.Board))best=std::move(shifted);}
            if(best.Board.MaterialArea>=allowedArea-P.Tolerance*P.Tolerance)break;
            bool done=true;for(std::size_t i=0;i<P.Parts.size();++i)if(P.Parts[i].Material==StockValue.Material&&best.Used[i]<P.Parts[i].Quantity){done=false;break;}
            if(done)break;
        }
        if(compressionSeed&&Task.InnerShrinkProbes&&!B.Stop())
        {
            // Pay this optional compression once for the selected main plan,
            // never once per candidate. The uncompressed best remains intact.
            DomainPhase compression{B,B.Seconds()+std::max(0.,B.O.TimeLimitSeconds-B.Seconds())/8,
                B.Attempts+(B.O.MaxPlacementAttempts-B.Attempts)/8};
            if(Compress(*compressionSeed,compressionRect,compressionBegin,compressionEnd,compression))
            {
                DomainPhase peripheral{B,B.O.TimeLimitSeconds,B.O.MaxPlacementAttempts};
                Shift(*compressionSeed,compressionBegin,peripheral,compressionEnd);
                Fill(*compressionSeed,peripheral);
                if(DomainRank(P,compressionSeed->Board)<DomainRank(P,best.Board))best=std::move(*compressionSeed);
            }
        }
        return best;
    }
};
}
Validation ValidatePlaneDomainLayout(const PlaneDomainTask& task,const Result& result)
{
    auto checked=Validate(task.Input,result);auto errors=DomainErrors(task);checked.Errors.insert(checked.Errors.end(),errors.begin(),errors.end());
    if(!checked.Errors.empty()){checked.Valid=false;return checked;}
    for(const auto& layout:result.Layouts)
    {
        auto domain=std::find_if(task.Domains.begin(),task.Domains.end(),[&](const auto& d){return d.StockID==layout.StockID;});
        if(domain==task.Domains.end())continue;
        const auto allowed=PlaneDomain::Allowed(domain->Boundary,domain->Obstacles,task.Input.EdgeMargin);
        for(const auto& placed:layout.Placements)
        {
            const auto part=std::find_if(task.Input.Parts.begin(),task.Input.Parts.end(),[&](const auto& p){return p.ID==placed.PartID;});
            if(!PlaneDomain::Inside(part->Geometry,placed.Transform,allowed,task.Input.Tolerance))checked.Errors.push_back("Part leaves real stock domain or intersects a fixed obstacle: "+placed.PartID);
        }
    }
    checked.Valid=checked.Errors.empty();return checked;
}
Result RunPlaneDomainTask(const PlaneDomainTask& task,const Options& options)
{
    DomainBudget budget{options};const auto& p=task.Input;Result result;
    result.Diagnostics=DomainErrors(task);
    if(!std::isfinite(options.TimeLimitSeconds)||options.TimeLimitSeconds<=0||!options.MaxPlacementAttempts)result.Diagnostics.push_back("Invalid domain solve budget.");
    if(!result.Diagnostics.empty()){result.State=Status::InvalidInput;return result;}
    std::vector<std::size_t> used(p.Parts.size());std::size_t group=1,reusedSheets=0,reusedParts=0;
    std::size_t rectangleTrials=0,jointTrials=0,prefillTrials=0,shiftTrials=0,peripheralParts=0,compressionTrials=0;
    auto cache=std::make_shared<NfpCache>();
    for(const auto& stock:p.Stocks)
    {
        if(budget.Stop())break;
        bool needed=false;for(std::size_t i=0;i<p.Parts.size();++i)needed|=p.Parts[i].Material==stock.Material&&used[i]<p.Parts[i].Quantity;
        if(!needed||!stock.Quantity)continue;
        DomainConstructor solver(task,stock,budget,cache);
        const double allowedArea=std::abs(Clipper2Lib::Area(solver.Allowed));
        for(std::size_t sheet=0;sheet<stock.Quantity&&!budget.Stop();++sheet)
        {
            DomainState initial;initial.Board={stock.ID,sheet,stock.Width,stock.Height,{},0};initial.Used=used;initial.Group=group;
            auto state=solver.Construct(std::move(initial));
            if(state.Board.Placements.empty())break;
            // Candidates do not mutate the committed counters. Publish only a
            // whole verified board, preserving the best legal baseline on stop.
            Result one;one.Layouts={state.Board};std::vector<std::size_t> need(p.Parts.size());
            for(const auto& placed:state.Board.Placements)
            {
                const auto found=std::find_if(p.Parts.begin(),p.Parts.end(),[&](const Part& value){return value.ID==placed.PartID;});
                if(found!=p.Parts.end())++need[static_cast<std::size_t>(found-p.Parts.begin())];
            }
            for(std::size_t i=0;i<p.Parts.size();++i)if(need[i]<p.Parts[i].Quantity)one.Remaining.push_back({p.Parts[i].ID,p.Parts[i].Quantity-need[i]});
            one.State=one.Remaining.empty()?Status::Complete:Status::Partial;
            if(!ValidatePlaneDomainLayout(task,one).Valid)
            {result.Diagnostics.push_back("Domain candidate rejected by original/typed material validation; committed boards retained.");break;}
            used=std::move(state.Used);group=state.Group;
            result.Stats.GapFilledParts+=static_cast<std::size_t>(std::count_if(state.Board.Placements.begin(),state.Board.Placements.end(),[](const Placement& value){return value.GapFiller;}));
            result.Layouts.push_back(std::move(state.Board));
            if(task.EnableSheetTemplateReuse&&!budget.Stop()&&std::isfinite(allowedArea)&&allowedArea>0)
            {
                auto q=options;q.EnableBasicCompaction=false;
                q.TimeLimitSeconds=std::max(0.,options.TimeLimitSeconds-budget.Seconds());
                q.MaxPlacementAttempts=options.MaxPlacementAttempts-budget.Attempts;
                q.Cancel=[&]
                {
                    budget.Stop();
                    if(budget.CallbackFailed)throw std::runtime_error("Domain cancellation callback failed.");
                    return budget.Cancelled;
                };
                std::size_t nextSheet=sheet+1;
                const auto repeated=AppendSheetTemplateCopies(p,result.Layouts.back(),stock,nextSheet,used,group,result,q,
                    task.MinimumSheetReuseUtilization*allowedArea/(stock.Width*stock.Height));
                budget.Add(repeated.Attempts);reusedSheets+=repeated.Copies;reusedParts+=repeated.Parts;budget.Cancelled|=repeated.Cancelled;
                sheet=nextSheet-1;
            }
        }
        rectangleTrials+=solver.RectangleTrials;jointTrials+=solver.JointTrials;prefillTrials+=solver.PrefillTrials;
        shiftTrials+=solver.ShiftTrials;peripheralParts+=solver.PeripheralParts;compressionTrials+=solver.CompressionTrials;
        if(task.EnablePeripheralFill&&!solver.NfpReady)result.Diagnostics.push_back("Domain NFP unavailable for this geometry/specification/obstacle limit; legal inner-rectangle baseline retained.");
    }
    result.Remaining.clear();
    for(std::size_t i=0;i<p.Parts.size();++i)if(used[i]<p.Parts[i].Quantity)result.Remaining.push_back({p.Parts[i].ID,p.Parts[i].Quantity-used[i]});
    result.State=budget.Cancelled?Status::Cancelled:result.Remaining.empty()?Status::Complete:Status::Partial;
    result.Stats.PlacementAttempts=budget.Attempts;result.Stats.ElapsedSeconds=budget.Seconds();
    const auto checked=ValidatePlaneDomainLayout(task,result);result.GeometryValidated=checked.Valid;
    if(!checked.Valid){result.State=Status::Failed;result.Diagnostics.insert(result.Diagnostics.end(),checked.Errors.begin(),checked.Errors.end());}
    if(budget.CallbackFailed)result.Diagnostics.push_back("Domain cancellation callback failed; committed legal boards retained.");
    result.Diagnostics.push_back("domain.sheet-template-reuse: enabled="+std::to_string(task.EnableSheetTemplateReuse)+
        " copies="+std::to_string(reusedSheets)+" parts="+std::to_string(reusedParts)+
        "; same stock/domain identity; optional utilization gate uses actual allowed material area");
    result.Diagnostics.push_back("plane.domain: rectangle-trials="+std::to_string(rectangleTrials)+" joint-plans="+std::to_string(jointTrials)+
        " prefill-plans="+std::to_string(prefillTrials)+" rigid-shifts="+std::to_string(shiftTrials)+" main-compressions="+std::to_string(compressionTrials)+" peripheral-trial-parts="+std::to_string(peripheralParts)+
        "; current-task independent candidates; original/typed domain validation; material boundary clearance distinct from PartGap");
    return result;
}
Result OptimizePlaneDomainLayout(const PlaneDomainTask& task,const Result& initial,const Options& options,
    PlaneDomainOptimizationSettings settings)
{
    DomainBudget budget{options};Result best=initial;const auto& p=task.Input;
    const auto originalCheck=ValidatePlaneDomainLayout(task,initial);
    if(!originalCheck.Valid||!settings.MaxPasses||settings.MaxPasses>64||!settings.MaxCandidatesPerPart||settings.MaxCandidatesPerPart>1024||
        !std::isfinite(settings.ReconstructionTimeSeconds)||settings.ReconstructionTimeSeconds<=0||
        !std::isfinite(options.TimeLimitSeconds)||options.TimeLimitSeconds<=0||!options.MaxPlacementAttempts)
    {best.Diagnostics.push_back("plane.domain.optimize: invalid typed initial layout, settings or budget; no replacement published");return best;}
    std::map<std::string,std::size_t> index;
    for(std::size_t i=0;i<p.Parts.size();++i)index.emplace(p.Parts[i].ID,i);
    for(auto& board:best.Layouts)
    {board.MaterialArea=0;for(const auto& placed:board.Placements)board.MaterialArea+=Area(p.Parts[index.at(placed.PartID)].Geometry);}
    auto cache=std::make_shared<NfpCache>();std::size_t accepted=0,reconstructions=0;
    for(std::size_t pass=0;pass<settings.MaxPasses&&!budget.Stop();++pass)
    {
        bool improved=false;
        for(std::size_t bi=0;bi<best.Layouts.size()&&!budget.Stop();++bi)
        {
            const auto stock=std::find_if(p.Stocks.begin(),p.Stocks.end(),[&](const Stock& value){return value.ID==best.Layouts[bi].StockID;});
            if(stock==p.Stocks.end())continue;
            DomainConstructor solver(task,*stock,budget,cache);
            auto board=best.Layouts[bi];
            for(std::size_t pi=0;pi<board.Placements.size()&&!budget.Stop();++pi)
            for(const bool xAxis:{false,true})
            {
                if(budget.Stop())break;
                const auto& placed=board.Placements[pi];const auto partIndex=index.at(placed.PartID);
                const auto local=Bounds(p.Parts[partIndex].Geometry,{0,0,placed.Transform.AngleDegrees,placed.Transform.Mirrored});
                const double current=xAxis?placed.Transform.X:placed.Transform.Y;
                const double low=xAxis?local.X:local.Y,span=xAxis?local.Width:local.Height;
                std::vector<double> targets;
                for(const auto& ring:solver.Allowed)for(const auto& v:ring)
                {const auto coordinate=xAxis?v.x:v.y;targets.push_back(coordinate-low);targets.push_back(coordinate-low-span);}
                const double clearance=p.PartGap>0?p.PartGap+std::max(1e-7,4*p.Tolerance):0;
                for(std::size_t j=0;j<board.Placements.size();++j)if(j!=pi)
                {
                    const auto& fixed=board.Placements[j];const auto other=Bounds(p.Parts[index.at(fixed.PartID)].Geometry,fixed.Transform);
                    targets.push_back((xAxis?other.X+other.Width:other.Y+other.Height)+clearance-low);
                    targets.push_back((xAxis?other.X:other.Y)-clearance-low-span);
                }
                targets.erase(std::remove_if(targets.begin(),targets.end(),[&](double value){return !std::isfinite(value)||value>=current-p.Tolerance;}),targets.end());
                std::sort(targets.begin(),targets.end());targets.erase(std::unique(targets.begin(),targets.end(),[&](double a,double b){return std::abs(a-b)<=p.Tolerance;}),targets.end());
                if(targets.size()>settings.MaxCandidatesPerPart)targets.resize(settings.MaxCandidatesPerPart);
                for(const auto target:targets)
                {
                    if(!budget.Charge())break;
                    auto pose=board.Placements[pi].Transform;if(xAxis)pose.X=target;else pose.Y=target;
                    if(!solver.Fits(board,partIndex,pose,pi))continue;
                    auto candidate=board;candidate.Placements[pi].Transform=pose;
                    if(DomainRank(p,candidate)<DomainRank(p,board))board=std::move(candidate);
                }
            }
            if(DomainRank(p,board)<DomainRank(p,best.Layouts[bi]))
            {
                auto candidate=best;candidate.Layouts[bi]=board;
                if(ValidatePlaneDomainLayout(task,candidate).Valid){best=std::move(candidate);++accepted;improved=true;}
            }
            if(settings.Reconstruct&&!budget.Stop())
            {
                // Rebuild only this board's fixed original set. The typed real
                // stock domain is retained; the stock bounding box is never
                // substituted for the actual material during reconstruction.
                const auto source=best.Layouts[bi];PlaneDomainTask local=task;
                local.Input.Stocks={*stock};local.Input.Stocks[0].Quantity=1;local.EnableSheetTemplateReuse=false;
                local.Domains.erase(std::remove_if(local.Domains.begin(),local.Domains.end(),[&](const PlaneStockDomain& value){return value.StockID!=stock->ID;}),local.Domains.end());
                std::map<std::string,std::vector<Placement>> originals;
                for(const auto& item:source.Placements)originals[item.PartID].push_back(item);
                for(auto& [id,items]:originals){(void)id;std::sort(items.begin(),items.end(),[](const Placement& a,const Placement& b){return a.Instance<b.Instance;});}
                for(auto& part:local.Input.Parts)part.Quantity=originals[part.ID].size();
                local.Input.Parts.erase(std::remove_if(local.Input.Parts.begin(),local.Input.Parts.end(),[](const Part& part){return !part.Quantity;}),local.Input.Parts.end());
                auto q=options;q.EnableBasicCompaction=false;
                q.TimeLimitSeconds=std::min(settings.ReconstructionTimeSeconds,std::max(0.,options.TimeLimitSeconds-budget.Seconds()));
                q.MaxPlacementAttempts=(options.MaxPlacementAttempts-budget.Attempts)/2;
                q.Cancel=[&]{return budget.Stop();};
                if(q.TimeLimitSeconds>0&&q.MaxPlacementAttempts)
                {
                    auto rebuilt=RunPlaneDomainTask(local,q);budget.Add(rebuilt.Stats.PlacementAttempts);++reconstructions;
                    const auto checked=ValidatePlaneDomainLayout(local,rebuilt);
                    if(checked.Valid&&checked.Complete&&rebuilt.Layouts.size()==1)
                    {
                        auto candidate=best;auto replacement=rebuilt.Layouts.front();replacement.StockInstance=source.StockInstance;
                        for(auto& item:replacement.Placements)
                        {const auto& identity=originals.at(item.PartID).at(item.Instance);item.Instance=identity.Instance;item.Group=identity.Group;item.GapFiller=identity.GapFiller;}
                        candidate.Layouts[bi]=std::move(replacement);
                        if(DomainRank(p,candidate.Layouts[bi])<DomainRank(p,best.Layouts[bi])&&ValidatePlaneDomainLayout(task,candidate).Valid)
                        {best=std::move(candidate);++accepted;improved=true;}
                    }
                }
            }
        }
        if(!improved)break;
    }
    best.Stats.PlacementAttempts=budget.Attempts;best.Stats.ElapsedSeconds=budget.Seconds();
    if(budget.Cancelled)best.State=Status::Cancelled;
    best.GeometryValidated=ValidatePlaneDomainLayout(task,best).Valid;
    best.Diagnostics.push_back("plane.domain.optimize: accepted="+std::to_string(accepted)+" reconstructions="+std::to_string(reconstructions)+
        "; fixed original/stock instances and provenance; bounded event compaction/reconstruction; every acceptance retains real domain/holes/defects");
    if(budget.CallbackFailed)best.Diagnostics.push_back("Domain optimization callback failed; best legal layout retained.");
    return best;
}
PlaneLabelResult LocatePlaneLabel(const PlaneLabelRequest& request,const Options& options)
{
    PlaneLabelResult result;result.HostID=request.HostID;result.LabelID=request.LabelID;
    std::string error;
    if(request.HostID.empty()||request.LabelID.empty()||request.Host.Radius>0||!Detail::ShapeValid(request.Host,1e-7,error)||!Detail::ShapeValid(request.Label.Geometry,1e-7,error)||
        !std::isfinite(request.Margin)||request.Margin<0||!std::isfinite(request.Step)||request.Step<=0||request.MaxRings>10000||!std::isfinite(request.Target.X)||!std::isfinite(request.Target.Y)||!std::isfinite(options.TimeLimitSeconds)||options.TimeLimitSeconds<=0||!options.MaxPlacementAttempts)
    {result.Diagnostic="Invalid label host, geometry, target or budget.";return result;}
    for(const auto& obstacle:request.Obstacles)if(obstacle.Radius>0||!Detail::ShapeValid(obstacle,1e-7,error)){result.Diagnostic="Invalid label obstacle.";return result;}
    const auto start=Clock::now();std::size_t attempts=0;bool cancelled=false;
    const auto stop=[&]{if(!cancelled&&options.Cancel&&options.Cancel())cancelled=true;return cancelled||Seconds(start)>=options.TimeLimitSeconds||attempts>=options.MaxPlacementAttempts;};
    const auto domain=Detail::PlaneDomain::Allowed(request.Host,request.Obstacles,request.Margin);
    for(std::size_t ring=0;ring<=request.MaxRings&&!stop();++ring)
    {
        std::vector<Point> shifts;if(!ring)shifts.push_back({0,0});else for(int i=-static_cast<int>(ring);i<=static_cast<int>(ring);++i)
        {shifts.push_back({i*request.Step,ring*request.Step});shifts.push_back({i*request.Step,-static_cast<double>(ring)*request.Step});if(std::abs(i)!=static_cast<int>(ring)){shifts.push_back({ring*request.Step,i*request.Step});shifts.push_back({-static_cast<double>(ring)*request.Step,i*request.Step});}}
        std::stable_sort(shifts.begin(),shifts.end(),[](Point a,Point b){return a.X*a.X+a.Y*a.Y<b.X*b.X+b.Y*b.Y;});
        for(auto offset:shifts)for(const auto orientation:Detail::CandidateOrientations(request.Label,1e-7,options.OrientationProposals))
        {
            if(stop())break;++attempts;auto b=Detail::Bounds(request.Label.Geometry,orientation);
            Pose pose{request.Target.X+offset.X-b.X-b.Width/2,request.Target.Y+offset.Y-b.Y-b.Height/2,orientation.AngleDegrees,orientation.Mirrored};
            if(Detail::PlaneDomain::Inside(request.Label.Geometry,pose,domain,1e-7))
            {result.Placed=true;result.Transform=pose;result.Distance=std::hypot(offset.X,offset.Y);result.Diagnostic="First feasible bounded center-expansion position; not a global distance optimum.";return result;}
        }
    }
    result.Diagnostic=cancelled?"Label positioning cancelled.":"No position in bounded center-expansion search.";return result;
}
}
