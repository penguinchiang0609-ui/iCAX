#include "SheetNestingPlaneStrategies.h"
#include "NestingPlaneConstruction.h"
#include "SheetNestingPlaneEvaluation.h"
#include "NestingInternal.h"
#include "NestingExecution.h"
#include "NestingAxisSafety.h"
#include "NestingPoseBounds.h"
#include "NestingNfpPlacement.h"
#include "NestingClearancePlacement.h"
#include "SheetNestingAxisCompaction.h"
#include "SheetNestingRotationCompaction.h"
#include "SheetNestingPlaneCompaction.h"
#include "NestingNfpCombinations.h"
#include "NestingSheetTemplateReuse.h"
#include "NestingPlaneHoles.h"
#include "NestingTrace.h"
#include "SheetNestingNfp.h"
#include <clipper2/clipper.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace iCAX::SheetNesting
{
namespace
{
using namespace Detail;
using Kind = PlaneStrategyKind;
using namespace Detail::PlaneConstruction;
using Constructor=Kernel;
bool GeometryScore(Kind k){return k==Kind::ArrayGeometryDeferred||k==Kind::ArrayGeometryImmediate;}
struct InitialConstructorDefinition
{
    const char* ID;
    Kind Strategy;
    bool SourceEvaluation=false;
    bool RectanglesOnly=false;
};
// Only mechanisms with a distinct construction contract enter the default
// race. Historical names and order wrappers remain callable below.
const std::array<InitialConstructorDefinition,18>& InitialConstructors()
{
    static const std::array<InitialConstructorDefinition,18> definitions{{
        {"plane.array-search-multi",Kind::ArrayMultiSpecificationSearch},
        {"plane.nfp-initial",Kind::NfpAllSpecifications},
        {"plane.array-layered",Kind::ArrayQuantityDeferred},
        {"plane.array-deferred",Kind::ArrayGeometryDeferred},
        {"plane.array-immediate",Kind::ArrayGeometryImmediate},
        {"plane.array-random",Kind::RandomArrays},
        {"plane.mix",Kind::MixedCompetition},
        {"plane.scanline",Kind::Scanline},
        {"plane.nfp-closest",Kind::NfpClosest},
        {"plane.nfp-directional",Kind::NfpDirectional},
        {"plane.nfp-strip",Kind::NfpStrip},
        {"plane.nfp-array",Kind::NfpArray},
        {"plane.nfp-multi-array",Kind::NfpMultiArray},
        {"plane.rect-block-search",Kind::RectangleBlockSearch,false,true},
        {"plane.rect-contact",Kind::RectangleContact,false,true},
        {"plane.single-product",Kind::SingleProductArray,false,true},
        {"plane.single-column",Kind::SingleColumn,false,true},
        {"plane.four-block",Kind::FourBlockArray,false,true}
    }};
    return definitions;
}
PlaneStrategySettings InitialSettings(const InitialConstructorDefinition& definition,PlaneStrategySettings base)
{
    base.Kind=definition.Strategy;
    base.UseSourceArrayEvaluation=definition.SourceEvaluation;
    base.EnableLayoutRefinement=false;
    base.EnableContourSettling=false;
    base.EnableAllowedAngleSettling=false;
    return base;
}

bool AtOriginalMaterialBoardLowerBound(const Problem& problem,const JobResult& candidate)
{
    if(!candidate.Checked.Valid||!candidate.Checked.Complete||!AxisSafety::ArithmeticSupported())return false;
    // Prove only that fewer boards are impossible. All source polygon areas,
    // margins and stock dimensions use exact binary64 expansion arithmetic.
    // 3*r*r is a conservative lower bound for an analytic disc's material;
    // a weaker bound can only postpone this successful early stop.
    using AxisSafety::Number;
    try
    {
        const auto scaled=[](Number value,std::size_t count)
        {
            Number sum;
            for(;count;count>>=1){if(count&1)sum=sum+value;if(count>1)value=value+value;}
            return sum;
        };
        const auto ringArea2=[](const std::vector<Point>& ring)
        {
            Number area;
            for(std::size_t i=0;i<ring.size();++i)
            {const auto a=ring[i],b=ring[(i+1)%ring.size()];area=area+Number(a.X)*Number(b.Y)-Number(a.Y)*Number(b.X);}
            return area.Sign()<0?Number{}-area:area;
        };
        std::map<std::string,Number> material2,capacity2;
        std::map<std::string,std::size_t> boards;
        std::map<std::string,const Stock*> stocks;
        std::map<std::string,const Part*> parts;
        for(const auto& stock:problem.Stocks)
        {
            stocks.emplace(stock.ID,&stock);
            if(!stock.Quantity)continue;
            const auto width=Number(stock.Width)-Number(2)*Number(problem.EdgeMargin);
            const auto height=Number(stock.Height)-Number(2)*Number(problem.EdgeMargin);
            if(width.Sign()<=0||height.Sign()<=0)continue;
            const auto area2=Number(2)*width*height;
            auto [entry,inserted]=capacity2.emplace(stock.Material,area2);
            if(!inserted&&(area2-entry->second).Sign()>0)entry->second=area2;
        }
        for(const auto& part:problem.Parts)
        {
            parts.emplace(part.ID,&part);
            if(!part.Quantity)continue;
            Number area2;
            if(part.Geometry.Radius>0)area2=Number(6)*Number(part.Geometry.Radius)*Number(part.Geometry.Radius);
            else
            {
                area2=ringArea2(part.Geometry.Outer);
                for(const auto& hole:part.Geometry.Holes)area2=area2-ringArea2(hole);
            }
            if(area2.Sign()<=0)return false;
            material2[part.Material]=material2[part.Material]+scaled(area2,part.Quantity);
        }
        for(const auto& layout:candidate.Output.Layouts)
        {
            const auto stock=stocks.find(layout.StockID);if(stock==stocks.end())return false;
            for(const auto& placement:layout.Placements)
            {
                const auto part=parts.find(placement.PartID);if(part==parts.end())return false;
                const auto angle=NormalizeAngle(placement.Transform.AngleDegrees);
                // Quarter turns preserve exact source area. For other polygon
                // angles, do not base a proof on rounded trig world material.
                if(part->second->Geometry.Radius<=0&&angle!=0&&angle!=90&&angle!=180&&angle!=270)return false;
            }
            ++boards[stock->second->Material];
        }
        if(material2.empty())return candidate.Output.Layouts.empty();
        if(boards.size()!=material2.size())return false;
        for(const auto& [material,area2]:material2)
        {
            const auto count=boards.find(material);
            const auto capacity=capacity2.find(material);
            if(count==boards.end()||!count->second||capacity==capacity2.end()||
                (area2-scaled(capacity->second,count->second-1)).Sign()<=0||
                (area2-scaled(capacity->second,count->second)).Sign()>0)return false;
        }
        return true;
    }
    catch(...){return false;} // Uncertain arithmetic never certifies an early stop.
}

StrategyFactory MakePlaneInitialWithSettings(ParallelConstructionSettings settings,PlaneStrategySettings base);


void ObserveAssessed(const Problem& problem,const Options& options,const JobResult& candidate,
    const std::string& source,bool selected)
{
    ObserveConstruction(options,ConstructionTraceStage::Complete,[&](ConstructionTraceEvent& event)
    {
        event.Source=source;event.Accepted=Eligible(candidate);event.ResultStatus=candidate.Output.State;
        event.Layouts=candidate.Output.Layouts;event.Remaining=candidate.Output.Remaining;
        event.Reason=selected?"new-best":"not-selected";
        event.Measures={{"valid",candidate.Checked.Valid?1.:0.},{"complete",candidate.Checked.Complete?1.:0.},
            {"placed",static_cast<double>(candidate.Checked.PlacedCount)},{"selected",selected?1.:0.}};
        if(Eligible(candidate))
        {
            const auto quality=Quality(candidate,problem,options.Ranking);
            if(options.Ranking==RankingPolicy::Utilization)
            {
                TraceTuple(event,std::tuple(std::get<0>(quality),std::get<2>(quality),std::get<4>(quality),
                    std::get<5>(quality),std::get<7>(quality),std::get<8>(quality),std::get<9>(quality),
                    std::get<10>(quality),std::get<11>(quality),std::get<12>(quality),std::get<13>(quality)),
                    {"incomplete","negativePartialNetMaterial","negativeStockUtilization","negativeUsedSectionUtilization",
                     "usedStockArea","usedSectionArea","occupiedEnvelopeArea","topSum","rightSum","meanY","meanX"});
                event.Measures.emplace_back("stockUtilization",-std::get<4>(quality));
                event.Measures.emplace_back("usedSectionUtilization",-std::get<5>(quality));
                event.Measures.emplace_back("envelopeUtilization",std::get<9>(quality)>0?
                    candidate.Checked.MaterialArea/std::get<9>(quality):0.);
            }
            else TraceTuple(event,quality,{"missing","boards","partialMaterial", "remnantUnavailable",
                "negativeUsableRemnant","consolidation","prefixUtilization","tailMaterial","tailDensity","topSum","rightSum","reserved","meanY","meanX"});
        }
    });
}
void CaptureConstructionScope(Options& options,std::string& actualScope)
{
    if(!options.ConstructionTrace)return;
    const auto observe=options.ConstructionTrace;
    options.ConstructionTrace=[&actualScope,observe](const ConstructionTraceEvent& event)
    {
        // Bind selection to the scope actually emitted by this child. A name
        // inferred from its requested kind cannot identify a recorded branch.
        if(event.Source=="constructor"&&event.Stage==ConstructionTraceStage::Begin)actualScope=event.Scope;
        observe(event);
    };
}
class PlaneStrategy final:public IStrategy
{
    PlaneStrategySettings S;
    std::shared_ptr<NfpCache> GeometryCache;
public:
    explicit PlaneStrategy(PlaneStrategySettings s,std::shared_ptr<NfpCache> geometryCache={})
        :S(s),GeometryCache(std::move(geometryCache)){}
    bool OwnsRefinementLifecycle()const noexcept override
    {return S.EnableLayoutRefinement||S.EnableContourSettling||S.EnableAllowedAngleSettling;}
    Result Run(const Problem& p,const Options& o)override
    {
        Result bad;bad.State=Status::InvalidInput;bad.Diagnostics=CheckProblem(p);
        if(!std::isfinite(o.TimeLimitSeconds)||o.TimeLimitSeconds<=0||!o.MaxPlacementAttempts)bad.Diagnostics.push_back("Invalid solve budget.");
        if(!bad.Diagnostics.empty())return bad;
        Budget budget{o};Result result;
        const bool competition=S.Kind==Kind::ArrayCompetition||S.Kind==Kind::MixedCompetition;
        if(competition)
        {
            std::vector<Kind> kinds{Kind::ArrayGeometryDeferred,Kind::Scanline};
            if(S.Kind==Kind::MixedCompetition)kinds={Kind::ArrayQuantityDeferred,Kind::ArrayGeometryDeferred,Kind::ArrayGeometryImmediate,Kind::NfpAllSpecifications,Kind::NfpArray};
            std::vector<std::size_t> consumed(p.Parts.size());std::size_t group=1,reusedBoards=0,reusedParts=0;
            for(std::size_t stockOrdinal=0;stockOrdinal<p.Stocks.size();++stockOrdinal)
            {
            const auto& stock=p.Stocks[stockOrdinal];
            for(std::size_t si=0;si<stock.Quantity&&!budget.Stop();++si)
            {
                Problem local=p;local.Stocks={stock};local.Stocks[0].Quantity=1;
                for(std::size_t pi=0;pi<p.Parts.size();++pi)local.Parts[pi].Quantity-=consumed[pi];
                std::optional<QualityKey> key;Result best;std::string winningChildScope;
                for(std::size_t i=0;i<kinds.size()&&!budget.Stop();++i)
                {
                    auto q=budget.Remaining();q.TimeLimitSeconds/=kinds.size()-i;q.MaxPlacementAttempts/=kinds.size()-i;if(!q.MaxPlacementAttempts)break;
                    q.ConstructionTraceScope+="/stock-"+std::to_string(stockOrdinal)+"/board-"+
                        std::to_string(si)+"/competitor-"+std::to_string(i);
                    std::string childScope;CaptureConstructionScope(q,childScope);
                    q.Cancel=[&]{return budget.Stop();};Budget child{q};auto settings=S;settings.Kind=kinds[i];
                    Constructor solver(local,settings,child,0,GeometryCache);JobResult candidate;candidate.Output=solver.Run();budget.Attempts+=child.Attempts;
                    AssessCandidate(local,candidate);
                    ObserveAssessed(local,q,candidate,"competition.result",Eligible(candidate)&&(!key||Quality(candidate,local,o.Ranking)<*key));
                    if(Eligible(candidate))
                    {auto value=Quality(candidate,local,o.Ranking);if(!key||value<*key)
                        {key=value;best=std::move(candidate.Output);winningChildScope=std::move(childScope);}}
                }
                if(!key||best.Layouts.empty())break;
                std::optional<Layout> sourceBoard;
                if(o.ConstructionTrace)try{sourceBoard=best.Layouts.front();}
                catch(...)
                {
                    if(o.ConstructionObservation)
                    {o.ConstructionObservation->Failed.store(true);++o.ConstructionObservation->Omitted;}
                }
                auto board=std::move(best.Layouts.front());board.StockInstance=si;std::map<std::size_t,std::size_t> groups;
                for(auto& placed:board.Placements)
                {
                    auto it=std::find_if(p.Parts.begin(),p.Parts.end(),[&](const Part& part){return part.ID==placed.PartID;});
                    const auto index=static_cast<std::size_t>(it-p.Parts.begin());placed.Instance=consumed[index]++;
                    auto [g,inserted]=groups.emplace(placed.Group,group);if(inserted)++group;placed.Group=g->second;
                }
                result.Stats.CombinationGroups+=best.Stats.CombinationGroups;result.Stats.GapFilledParts+=best.Stats.GapFilledParts;
                result.Layouts.push_back(std::move(board));
                if(sourceBoard&&!winningChildScope.empty())ObserveConstruction(o,ConstructionTraceStage::Selected,[&](ConstructionTraceEvent& event)
                {
                    event.Source="competition.board-selection";event.Scope=winningChildScope;
                    event.StockIndex=stockOrdinal;event.StockInstance=si;event.BoardIndex=result.Layouts.size()-1;
                    event.Layouts={*sourceBoard,result.Layouts.back()};event.ResultStatus=best.State;
                    event.ElapsedSeconds=budget.Seconds();event.Accepted=true;
                    event.Reason="actual-winning-child-board-promoted-with-ordered-original-identity-mapping";
                    event.Measures={{"sourceLayoutIndex",0.},{"targetLayoutIndex",1.}};
                });
                auto nextStock=si+1;
                const auto reuse=ReuseLastBoard(p,S,budget,stock,nextStock,consumed,group,result);
                reusedBoards+=reuse.Copies;reusedParts+=reuse.Parts;si=nextStock-1;
            }
            }
            result.Remaining.clear();
            for(std::size_t i=0;i<p.Parts.size();++i)if(consumed[i]<p.Parts[i].Quantity)result.Remaining.push_back({p.Parts[i].ID,p.Parts[i].Quantity-consumed[i]});
            result.State=budget.Cancelled?Status::Cancelled:result.Remaining.empty()?Status::Complete:Status::Partial;
            result.Diagnostics.push_back("plane.competition: independent per-board candidates, one shared budget, winner committed once");
            if(S.EnableSheetTemplateReuse)result.Diagnostics.push_back("plane.sheet-template-reuse: copies="+std::to_string(reusedBoards)+
                " parts="+std::to_string(reusedParts)+"; current-run winning board copied within original demand and stock; residual demand reconstructed");
        }
        else if(S.Kind==Kind::RandomArrays)
        {
            std::set<std::string> layouts;std::optional<QualityKey> key;
            std::string winningChildScope;std::size_t winningRestart=0;
            for(std::size_t restart=0;restart<S.Restarts&&!budget.Stop();++restart)
            {
                auto q=budget.Remaining();q.TimeLimitSeconds/=S.Restarts-restart;q.MaxPlacementAttempts/=S.Restarts-restart;
                if(!q.MaxPlacementAttempts)break;q.Seed=o.Seed+restart;q.ConstructionTraceScope+="/restart-"+std::to_string(restart);q.Cancel=[&]{return budget.Stop();};Budget child{q};
                std::string childScope;CaptureConstructionScope(q,childScope);
                Constructor solver(p,S,child,0,GeometryCache);JobResult candidate;candidate.Output=solver.Run();budget.Attempts+=child.Attempts;
                std::ostringstream signature;signature.precision(17);
                for(const auto& l:candidate.Output.Layouts){signature<<l.StockID<<':'<<l.StockInstance<<';';for(const auto& v:l.Placements)signature<<v.PartID<<':'<<v.Instance<<':'<<v.Transform.X<<':'<<v.Transform.Y<<':'<<v.Transform.AngleDegrees<<':'<<v.Transform.Mirrored<<';';}
                if(!layouts.insert(signature.str()).second)continue;
                AssessCandidate(p,candidate);ObserveAssessed(p,q,candidate,"restart.result",Eligible(candidate)&&(!key||Quality(candidate,p,o.Ranking)<*key));
                if(Eligible(candidate)){auto value=Quality(candidate,p,o.Ranking);if(!key||value<*key)
                    {key=value;result=std::move(candidate.Output);winningChildScope=std::move(childScope);winningRestart=restart;}}
            }
            if(!key){result.State=Status::Partial;for(const auto& part:p.Parts)if(part.Quantity)result.Remaining.push_back({part.ID,part.Quantity});}
            else if(!winningChildScope.empty())ObserveConstruction(o,ConstructionTraceStage::Selected,[&](ConstructionTraceEvent& event)
            {
                event.Source="strategy.construction-selection";event.Scope=winningChildScope;
                event.Layouts=result.Layouts;event.Remaining=result.Remaining;event.ResultStatus=result.State;
                event.ElapsedSeconds=budget.Seconds();event.Accepted=true;
                event.Reason="actual-strict-quality-winner-of-independent-restarts";
                event.Measures={{"restartIndex",double(winningRestart)}};
            });
            result.Diagnostics.push_back("plane.random unique-complete-layout-signatures="+std::to_string(layouts.size()));
        }
        else if(S.Kind==Kind::NfpAllSpecifications)
        {
            std::optional<QualityKey> key;std::size_t completed=0;
            if(S.EnableLayoutRefinement&&!budget.Stop())
            {
                auto q=budget.Remaining();q.TimeLimitSeconds*=.75;
                q.MaxPlacementAttempts=std::max<std::size_t>(1,
                    (q.MaxPlacementAttempts/4)*3+(q.MaxPlacementAttempts%4)*3/4);
                q.EnableBasicCompaction=false;q.Cancel=[&]{return budget.Stop();};
                JobResult candidate;candidate.Output=MakePlaneInitialWithSettings({},S)()->Run(p,q);
                budget.Attempts+=std::min(candidate.Output.Stats.PlacementAttempts,q.MaxPlacementAttempts);
                ++completed;AssessCandidate(p,candidate);
                if(Eligible(candidate)){key=Quality(candidate,p,o.Ranking);result=std::move(candidate.Output);}
            }
            else
            {
                // One complete greedy construction. Axis/order/search changes
                // belong to explicit controllers, never duplicated hidden trials.
                auto q=budget.Remaining();q.Cancel=[&]{return budget.Stop();};Budget child{q};
                Constructor solver(p,S,child,0,GeometryCache);JobResult candidate;candidate.Output=solver.Run();
                budget.Attempts+=child.Attempts;++completed;AssessCandidate(p,candidate);
                ObserveAssessed(p,q,candidate,"nfp-greedy.result",Eligible(candidate));
                if(Eligible(candidate)){key=Quality(candidate,p,o.Ranking);result=std::move(candidate.Output);}
            }
            if(!key){result.State=Status::Partial;for(const auto& part:p.Parts)if(part.Quantity)result.Remaining.push_back({part.ID,part.Quantity});}
            if(S.EnableLayoutRefinement&&key&&result.State==Status::Complete&&!result.Layouts.empty()&&!budget.Stop())
            {
                Result working=result;std::size_t stagesRun=0;
                AxisCompactionSettings horizontal;horizontal.XFirst=true;
                const std::vector<PostProcessStage> stages{MakeNfpRefillStage(),MakeAxisCompactionStage(),
                    MakeNfpRefillStage(),MakeAxisCompactionStage(horizontal),MakeNfpRefillStage(),
                    MakePlaneOptimizationStage({},MakePlaneStrategyFactory())};
                for(std::size_t i=0;i<stages.size()&&!budget.Stop();++i)
                {
                    auto q=budget.Remaining();q.TimeLimitSeconds/=stages.size()-i;q.MaxPlacementAttempts/=stages.size()-i;
                    if(!q.MaxPlacementAttempts)break;q.Cancel=[&]{return budget.Stop();};
                    JobResult candidate;candidate.Output=stages[i].Factory()->Run(p,working,q);
                    budget.Attempts+=std::min(candidate.Output.Stats.PlacementAttempts,o.MaxPlacementAttempts-budget.Attempts);++stagesRun;
                    AssessCandidate(p,candidate);
                    if(Eligible(candidate)&&candidate.Checked.Complete)
                    {
                        working=candidate.Output;const auto value=Quality(candidate,p,o.Ranking);
                        if(value<*key){key=value;result=std::move(candidate.Output);}
                    }
                }
                result.Diagnostics.push_back("plane.nfp-all complete-layout compaction/refill stages="+std::to_string(stagesRun)+"; original identities and best complete layout retained");
            }
            result.Diagnostics.push_back(S.EnableLayoutRefinement?
                "plane.nfp-all parallel initial race completed before complete-layout refinement; shared remaining time/attempt budget, validated best retained":
                "plane.nfp-initial greedy constructions="+std::to_string(completed)+"; all specifications, dynamic allowed poses, no hidden repeated trials");
        }
        else {Constructor solver(p,S,budget,0,GeometryCache);result=solver.Run();}
        if(budget.Cancelled)result.State=Status::Cancelled;
        result.Stats.PlacementAttempts=budget.Attempts;result.Stats.ElapsedSeconds=budget.Seconds();
        const auto checked=Validate(p,result);result.GeometryValidated=checked.Valid;
        if(!checked.Valid){result.State=Status::Failed;result.Diagnostics.insert(result.Diagnostics.end(),checked.Errors.begin(),checked.Errors.end());}
        ObserveConstruction(o,ConstructionTraceStage::Selected,[&](ConstructionTraceEvent& event)
        {event.Source="strategy.result";event.ResultStatus=result.State;event.Layouts=result.Layouts;event.Remaining=result.Remaining;event.Accepted=checked.Valid;event.Reason="selected-native-strategy-result";});
        return result;
    }
};
class NfpFill final:public IPostProcessor
{
    bool Directional;
public:
    explicit NfpFill(bool directional):Directional(directional){}
    Result Run(const Problem& p,const Result& input,const Options& options)override
    {
        if(!Validate(p,input).Valid)return input;
        if(!std::isfinite(options.TimeLimitSeconds)||options.TimeLimitSeconds<=0||!options.MaxPlacementAttempts)
        {auto skipped=input;skipped.Diagnostics.push_back("plane.nfp-fill skipped: invalid budget");return skipped;}
        Budget budget{options};PlaneStrategySettings settings;settings.Kind=Directional?Kind::NfpDirectional:Kind::NfpClosest;
        Constructor solver(p,settings,budget);auto result=solver.Run(&input);
        if(!Validate(p,result).Valid)return input;
        return result;
    }
};
class OrderPortfolio final:public IStrategy
{
    StrategyFactory Construct;std::size_t Maximum;std::string Mechanism;
    bool CanonicalLargeFirst=false;
public:
    OrderPortfolio(StrategyFactory f,std::size_t maximum,std::string mechanism={},bool largeFirst=false)
        :Construct(std::move(f)),Maximum(maximum),Mechanism(std::move(mechanism)),CanonicalLargeFirst(largeFirst){}
    bool OwnsRefinementLifecycle()const noexcept override{return true;}
    bool IsPureConstruction()const noexcept override{return true;}
    Result Run(const Problem& p,const Options& options)override
    {
        Result empty;empty.State=Status::Partial;for(const auto& part:p.Parts)if(part.Quantity)empty.Remaining.push_back({part.ID,part.Quantity});
        auto errors=CheckProblem(p);if(!errors.empty()||!std::isfinite(options.TimeLimitSeconds)||options.TimeLimitSeconds<=0||!options.MaxPlacementAttempts){empty.State=Status::InvalidInput;empty.Diagnostics=std::move(errors);return empty;}
        Budget budget{options};std::vector<std::size_t> identity(p.Parts.size());std::iota(identity.begin(),identity.end(),0);
        std::vector<std::array<double,5>> metrics(p.Parts.size());
        for(std::size_t i=0;i<p.Parts.size();++i)
        {
            const auto box=Bounds(p.Parts[i].Geometry);
            metrics[i]={std::max(box.Width,box.Height),Area(p.Parts[i].Geometry),
                std::min(box.Width,box.Height),box.Width,box.Height};
        }
        struct Order {std::vector<std::size_t> Indices;const char* Rule;};
        std::vector<Order> orders;
        const auto append=[&](std::vector<std::size_t> order,const char* rule)
        {
            // Ordinary main arrays now consume large originals first. Apply
            // that same primary order before de-duplication, rather than run
            // many nominal permutations which the constructor will undo.
            if(CanonicalLargeFirst)std::stable_sort(order.begin(),order.end(),
                [&](auto a,auto b){return metrics[a][1]>metrics[b][1];});
            if(orders.size()<Maximum&&std::none_of(orders.begin(),orders.end(),[&](const auto& old){return old.Indices==order;}))
                orders.push_back({std::move(order),rule});
        };
        const char* rules[]={"long-side","material-area","short-side","width","height"};
        for(std::size_t metric=0;metric<5;++metric)
        {
            auto order=identity;
            std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return metrics[a][metric]>metrics[b][metric];});
            append(std::move(order),rules[metric]);
        }
        append(identity,"source-input");
        const auto base=orders.front().Indices;
        std::vector<std::pair<std::size_t,std::size_t>> swaps;
        for(std::size_t i=0;i<std::min<std::size_t>(10,base.size());++i)for(std::size_t j=i+1;j<std::min<std::size_t>(10,base.size());++j)
            if(p.Parts[base[i]].Material==p.Parts[base[j]].Material&&Classify(p.Parts[base[i]].Geometry,p.Tolerance).Kind==Classify(p.Parts[base[j]].Geometry,p.Tolerance).Kind)swaps.emplace_back(i,j);
        std::mt19937_64 random(options.Seed);std::shuffle(swaps.begin(),swaps.end(),random);
        for(const auto [a,b]:swaps){auto order=base;std::swap(order[a],order[b]);append(std::move(order),"compatible-swap");}
        const auto count=orders.size();std::optional<QualityKey> key;Result best=empty;std::size_t completed=0;
        bool publicationFailed=false;
        std::size_t ordersStarted=0;
        for(std::size_t i=0;i<count&&!budget.Stop();++i)
        {
            ordersStarted=std::max(ordersStarted,i+1);
            auto order=orders[i].Indices;Problem local=p;for(std::size_t j=0;j<order.size();++j)local.Parts[j]=p.Parts[order[j]];
            // Each order must first have a continuous chance to construct the
            // complete original demand. Maximum is a restart ceiling, not a
            // requirement to divide one usable window into partial runs.
            auto q=budget.Remaining();q.Cancel=[&]{return budget.Stop();};if(!q.MaxPlacementAttempts)break;
            q.ConstructionTraceScope+="/order-"+std::to_string(i);
            q.ConstructionTraceScope+="/";q.ConstructionTraceScope+=orders[i].Rule;
            if(!Mechanism.empty())q.ConstructionTraceScope+="/"+Mechanism;
            if(options.ConstructionTrace)q.ConstructionTrace=[&,order](const ConstructionTraceEvent& native)
            {
                auto event=native;
                if(event.PartIndex<order.size())event.PartIndex=order[event.PartIndex];
                for(auto& member:event.Members)if(member.PartIndex<order.size())member.PartIndex=order[member.PartIndex];
                options.ConstructionTrace(event);
            };
            auto solver=Construct();
            if(!solver||!solver->IsPureConstruction())
            {
                best.Diagnostics.push_back("plane.order-portfolio skipped: wrapped strategy is not a pure initial constructor");
                if(!key)best.State=Status::InvalidInput;
                break;
            }
            ObserveConstruction(q,ConstructionTraceStage::Budget,[&](ConstructionTraceEvent& event)
            {
                event.Source="order.permutation";event.Reason=std::string("fresh-original-demand-continuous-remaining-budget; rule=")+orders[i].Rule;
                event.Measures={{"permutation",static_cast<double>(i)},{"constructor",0.},{"timeLimitSeconds",q.TimeLimitSeconds},
                    {"attemptCap",static_cast<double>(q.MaxPlacementAttempts)},
                    {"orientationProposalPolicy",static_cast<double>(q.OrientationProposals)},
                    {"continuousConstructionTime",q.ContinuousConstructionTime?1.:0.}};
                for(std::size_t j=0;j<order.size();++j)event.Measures.emplace_back("sourcePartIndex["+std::to_string(j)+"]",static_cast<double>(order[j]));
            });
            JobResult candidate;candidate.Output=solver->Run(local,q);budget.Attempts+=std::min(candidate.Output.Stats.PlacementAttempts,options.MaxPlacementAttempts-budget.Attempts);++completed;
            AssessCandidate(p,candidate);
            const bool improved=Eligible(candidate)&&(!key||Quality(candidate,p,options.Ranking)<*key);
            // Publish the complete original-demand improvement immediately.
            // This transport is independent of observation and does not end
            // the route's remaining orders, lease or cancellation lifecycle.
            if(improved&&candidate.Checked.Complete&&options.PublishConstructionCandidate&&!publicationFailed)
            {
                try{options.PublishConstructionCandidate(candidate.Output);}
                catch(...){publicationFailed=true;}
            }
            ObserveAssessed(p,q,candidate,"order.result",improved);
            if(improved){key=Quality(candidate,p,options.Ranking);best=std::move(candidate.Output);}
        }
        if(budget.Cancelled)best.State=Status::Cancelled;best.Stats.PlacementAttempts=budget.Attempts;best.Stats.ElapsedSeconds=budget.Seconds();
        if(publicationFailed)best.Diagnostics.push_back("plane.order-portfolio candidate publication failed; validated best retained and later orders continued");
        best.Diagnostics.push_back("plane.order-portfolio independent full permutations="+std::to_string(ordersStarted)+
            " constructors=1 completed-constructions="+std::to_string(completed)+
             "; mechanism="+(Mechanism.empty()?std::string("provided-constructor"):Mechanism)+
             "; orientation-proposals="+(options.OrientationProposals==OrientationProposalPolicy::OriginalOnly?std::string("original-only"):std::string("allowed-reflections"))+
             (CanonicalLargeFirst?"; primary-order=net-material-area-descending; equivalent main orders deduplicated;":";")+
             " deterministic geometric orders before noncumulative compatible swaps of first ten specifications; fresh original demand per construction");return best;
    }
};
StrategyFactory InitialOrderFactory(PlaneStrategySettings base,Kind mechanism,std::shared_ptr<NfpCache> shared={})
{
    // Each mechanism owns a complete independent ordering portfolio. Only
    // immutable geometry is shared with another concurrently scheduled route.
    base.Kind=mechanism;base.UseSourceArrayEvaluation=false;base.EnableLayoutRefinement=false;
    base.EnableContourSettling=false;base.EnableAllowedAngleSettling=false;
    return [base,shared]
    {
        auto cache=shared?shared:std::make_shared<NfpCache>();
        return std::make_unique<OrderPortfolio>([base,cache]{return std::make_unique<PlaneStrategy>(base,cache);},16,
            base.Kind==Kind::NfpArray?"nfp-array":"quantity-array",base.Kind==Kind::ArrayQuantityDeferred);
    };
}
class PlaneInitial final:public IStrategy
{
    ParallelConstructionSettings Settings;
    PlaneStrategySettings Base;
public:
    PlaneInitial(ParallelConstructionSettings settings,PlaneStrategySettings base)
        :Settings(std::move(settings)),Base(base){}
    bool OwnsRefinementLifecycle()const noexcept override{return true;}
    bool IsPureConstruction()const noexcept override{return true;}
    Result Run(const Problem& problem,const Options& options)override
    {
        // A new race owns its cache. Only immutable geometry is shared; each
        // constructor has original independent demand, stock and placement state.
        const auto cache=std::make_shared<NfpCache>();
        StrategyRegistry constructors;std::vector<std::string> ids,notes;
        constructors.Register("plane.envelope-initial",MakePlaneEnvelopeInitialFactory());
        ids.emplace_back("plane.envelope-initial");
        bool rectanglesOnly=true;
        if(CheckProblem(problem).empty())
            for(const auto& part:problem.Parts)if(part.Quantity&&
                (!part.Geometry.Holes.empty()||Classify(part.Geometry,problem.Tolerance).Kind!=ShapeKind::Rectangle))
            {rectanglesOnly=false;break;}
        for(const auto& definition:InitialConstructors())
        {
            if(definition.RectanglesOnly&&!rectanglesOnly)
            {
                notes.push_back(std::string("plane.initial job=")+definition.ID+
                    " skipped: requires all demanded originals to be hole-free rectangles");
                ObserveConstruction(options,ConstructionTraceStage::Filter,[&](ConstructionTraceEvent& event)
                {event.Route=definition.ID;event.Source="scheduler.applicability";event.Accepted=false;event.Reason="requires-hole-free-rectangular-demand";});
                continue;
            }
            const auto settings=InitialSettings(definition,Base);
            constructors.Register(definition.ID,[settings,cache]
                {return std::make_unique<PlaneStrategy>(settings,cache);});
            ids.emplace_back(definition.ID);
        }
        const std::array<std::string,4> firstWave{{"plane.envelope-initial","plane.array-layered",
            "plane.nfp-initial","plane.nfp-array"}};
        const auto priority=[&](const std::string& id)
            {return std::find(firstWave.begin(),firstWave.end(),id)-firstWave.begin();};
        std::stable_sort(ids.begin(),ids.end(),[&](const auto& a,const auto& b)
            {return priority(a)<priority(b);});
        auto settings=Settings;settings.FairScheduling=true;
        settings.ContinuousTimeRoutes={"plane.envelope-initial","plane.array-layered","plane.nfp-initial"};
        auto race=MakeParallelConstructionPortfolioFactory(constructors,ids,std::move(settings))();
        auto execution=options;execution.ContinuousConstructionTime=false;
        auto result=race->Run(problem,execution);
        result.Diagnostics.insert(result.Diagnostics.end(),notes.begin(),notes.end());
        result.Diagnostics.push_back("plane.initial candidates=19 eligible="+std::to_string(ids.size())+
            " skipped="+std::to_string(notes.size())+
            "; canonical construction mechanisms only; aliases and order wrappers excluded; fair worker scheduling; no complete-layout refinement");
        return result;
    }
};
StrategyFactory MakePlaneInitialWithSettings(ParallelConstructionSettings settings,PlaneStrategySettings base)
{
    if(settings.Workers>32)throw std::invalid_argument("Invalid plane initial worker count.");
    // A material-area lower bound certifies board count only. It cannot stop
    // the default competition before other constructors compare layout quality.
    return [settings=std::move(settings),base]
    {
        return std::make_unique<PlaneInitial>(settings,base);
    };
}
}
StrategyFactory MakePlaneStrategyFactory(PlaneStrategySettings settings)
{
    if(!std::isfinite(settings.ScanStep)||settings.ScanStep<=0||!std::isfinite(settings.ContactBand)||settings.ContactBand<=0||
        !std::isfinite(settings.MinimumSheetReuseUtilization)||settings.MinimumSheetReuseUtilization<0||settings.MinimumSheetReuseUtilization>1||
        (settings.UseSourceArrayEvaluation&&!GeometryScore(settings.Kind))||
        !settings.MaxCandidates||settings.MaxCandidates>1024||!settings.BranchWidth||settings.BranchWidth>64||
        !settings.MaxNodes||settings.MaxNodes>1000000||!settings.Restarts||settings.Restarts>1024||!settings.NfpTrials||settings.NfpTrials>6||settings.BranchDecisions>64||!settings.MaxArrayAxis||settings.MaxArrayAxis>512||
        static_cast<int>(settings.Kind)<0||settings.Kind>Kind::MixedCompetition)
        throw std::invalid_argument("Invalid plane strategy limits.");
    return [settings]{return std::make_unique<PlaneStrategy>(settings);};
}
StrategyFactory MakePlaneInitialFactory(ParallelConstructionSettings settings)
{
    return MakePlaneInitialWithSettings(std::move(settings),{});
}
void RegisterPlaneStrategies(StrategyRegistry& registry)
{
    registry.Register("plane.envelope-initial",MakePlaneEnvelopeInitialFactory());
    for(const auto& definition:InitialConstructors())
        registry.Register(definition.ID,MakePlaneStrategyFactory(InitialSettings(definition,{})));
    // Compatibility names do not add work to the default race. Source-score
    // aliases retain provenance only; quantity is not a quality objective.
    const std::array<InitialConstructorDefinition,10> legacy{{
        {"plane.array-1",Kind::ArrayQuantityDeferred},{"plane.array-2",Kind::ArrayGeometryDeferred},
        {"plane.array-4",Kind::ArrayGeometryImmediate},{"plane.array-5",Kind::ArrayGeometryImmediate},
        {"plane.array-2-source",Kind::ArrayGeometryDeferred,true},{"plane.array-5-source",Kind::ArrayGeometryImmediate,true},
        {"plane.array-first",Kind::HistoricalFirstArray},{"plane.array-search-first",Kind::ArrayFirstSpecificationSearch},
        {"plane.array-reduce",Kind::ArrayReduceSearch},{"plane.array-3",Kind::ArrayCompetition}
    }};
    for(const auto& definition:legacy)
        registry.Register(definition.ID,MakePlaneStrategyFactory(InitialSettings(definition,{})));
    PlaneStrategySettings complete;complete.Kind=Kind::NfpAllSpecifications;
    complete.EnableLayoutRefinement=true;complete.EnableContourSettling=true;complete.EnableAllowedAngleSettling=true;
    registry.Register("plane.nfp-all",MakePlaneStrategyFactory(complete));
    const auto orderCache=std::make_shared<NfpCache>();
    registry.Register("plane.order-ultra",InitialOrderFactory({},Kind::NfpArray,orderCache));
    registry.Register("plane.order-array",InitialOrderFactory({},Kind::ArrayQuantityDeferred,orderCache));
    registry.Register("plane.initial",MakePlaneInitialFactory());
}
StrategyFactory MakePlaneOrderPortfolioFactory(StrategyFactory construct,std::size_t maxOrders)
{if(!construct||!maxOrders||maxOrders>46)throw std::invalid_argument("Invalid plane order portfolio configuration.");return [construct=std::move(construct),maxOrders]{return std::make_unique<OrderPortfolio>(construct,maxOrders);};}
PostProcessStage MakePlaneNfpFillStage(bool directional)
{return {"plane.nfp-fill",[directional]{return std::make_unique<NfpFill>(directional);}};}
}
