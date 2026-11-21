#include "SheetNestingPlaneStrategies.h"
#include "SheetNestingPlaneCompaction.h"
#include "SheetNestingPlaneDomains.h"
#include "SheetNestingPlaneManufacturing.h"
#include "SheetNestingConstructionTrace.h"
#include "NestingInternal.h"
#include "NestingAxisSafety.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include <string>

using namespace iCAX::SheetNesting;
using K=PlaneStrategyKind;
namespace
{
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
Part MakeRect(std::string id,double w,double h,std::size_t n=1)
{Part p;p.ID=std::move(id);p.Geometry.Outer={{0,0},{w,0},{w,h},{0,h}};p.Quantity=n;p.FreeRotation=false;p.AllowedAnglesDegrees={0,90};return p;}
Options Settings(){Options o;o.TimeLimitSeconds=3;o.MaxPlacementAttempts=200000;o.EnableBasicCompaction=false;o.EnableCombinations=false;return o;}
Result Run(const Problem& p,K kind,Options o=Settings())
{PlaneStrategySettings s;s.Kind=kind;s.Restarts=4;auto r=MakePlaneStrategyFactory(s)()->Run(p,o);auto v=Validate(p,r);if(!v.Valid){for(const auto& e:v.Errors)std::cerr<<e<<'\n';}Require(v.Valid,"original-material validation");return r;}
std::string Signature(const Result& r)
{std::string s;for(const auto& l:r.Layouts)for(const auto& p:l.Placements)s+=p.PartID+":"+std::to_string(p.Instance)+":"+std::to_string(p.Transform.X)+":"+std::to_string(p.Transform.Y)+":"+std::to_string(p.Transform.AngleDegrees)+";";return s;}
bool HasDiagnostic(const Result& result,const std::string& text)
{return std::any_of(result.Diagnostics.begin(),result.Diagnostics.end(),[&](const std::string& message){return message.find(text)!=std::string::npos;});}
const std::set<std::string>& DefaultConstructors()
{
    static const std::set<std::string> ids{
        "plane.envelope-initial","plane.array-layered","plane.array-deferred","plane.array-immediate",
        "plane.array-random","plane.scanline","plane.nfp-initial","plane.nfp-closest",
        "plane.nfp-directional","plane.nfp-strip","plane.nfp-array","plane.nfp-multi-array",
        "plane.array-search-multi","plane.mix","plane.rect-block-search","plane.rect-contact",
        "plane.single-product","plane.single-column","plane.four-block"};
    return ids;
}
const std::set<std::string>& HistoricalConstructors()
{
    static const std::set<std::string> ids{"plane.array-1","plane.array-2","plane.array-2-source",
        "plane.array-3","plane.array-4","plane.array-5","plane.array-5-source","plane.array-first",
        "plane.array-search-first","plane.array-reduce","plane.order-ultra","plane.order-array"};
    return ids;
}
void CheckStripConstruction()
{
    Problem p;p.PartGap=.2;p.EdgeMargin=.5;p.Tolerance=1e-7;
    p.Parts={MakeRect("locked-width-original",2,2,3)};
    p.Parts[0].AllowedAnglesDegrees={0};p.Stocks={{"physical-sheet",8,8,1}};
    std::vector<ConstructionTraceEvent> events;
    auto options=Settings();options.EnableGapFill=false;
    options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
    options.ConstructionTrace=[&](const ConstructionTraceEvent& event){events.push_back(event);};
    PlaneStrategySettings settings;settings.Kind=K::NfpStrip;settings.NfpTrials=1;
    settings.EnableLayoutRefinement=false;settings.EnableContourSettling=false;
    settings.EnableAllowedAngleSettling=false;settings.EnableHoleNesting=false;settings.EnableSheetTemplateReuse=false;
    const auto result=MakePlaneStrategyFactory(settings)()->Run(p,options);
    const auto checked=Validate(p,result);
    Require(checked.Valid&&checked.Complete&&checked.PlacedCount==3&&result.Layouts.size()==1&&
        result.Layouts[0].Width==8&&result.Layouts[0].Height==8,
        "NFP strip lost demand or returned its private locked-width region as physical stock");
    std::vector<const ConstructionTraceEvent*> commits;const Layout* raw=nullptr;
    for(const auto& event:events)
    {
        Require(event.Source!="fill.commit","strip construction depended on later fill instead of its locked-width NFP loop");
        if(event.Source=="constructor.commit")commits.push_back(&event);
        if(event.Source=="board.raw-construction"&&event.Layouts.size()==1)raw=&event.Layouts.front();
    }
    Require(commits.size()==3&&raw&&raw->Placements.size()==3&&!options.ConstructionObservation->Failed.load(),
        "strip width was only declared by a label without three raw original commits");
    const auto first=Detail::Bounds(p.Parts[0].Geometry,commits[0]->Members.at(0).Transform);
    std::uint64_t parent=0;double previousTop=0;
    for(std::size_t i=0;i<commits.size();++i)
    {
        const auto& event=*commits[i];
        Require(event.Members.size()==1&&event.ParentStateID==parent&&event.Layouts.size()==1&&
            event.Layouts.front().Placements.size()==i+1&&event.Layouts.front().Width==8&&event.Layouts.front().Height==8,
            "strip controller did not advance by one original in its own parent state");
        const auto utilization=std::find_if(event.Rank.begin(),event.Rank.end(),[](const auto& term)
            {return term.first=="negativeUsedSectionUtilization";});
        const auto top=Detail::Bounds(p.Parts[0].Geometry,event.Members[0].Transform);
        Require(utilization!=event.Rank.end()&&std::abs(utilization->second+
            double(i+1)*4./(8.*(top.Y+top.Height+p.EdgeMargin)))<1e-12,
            "strip ranking used a private domain width instead of the physical stock width");
        const auto box=Detail::Bounds(p.Parts[0].Geometry,event.Members[0].Transform);
        Require(box.X>=first.X-p.Tolerance&&box.X+box.Width<=first.X+first.Width+p.Tolerance&&
            (i==0||box.Y>=previousTop+p.PartGap-p.Tolerance),
            "strip NFP escaped the first original's physical width or reverted to general two-axis placement");
        if(i)Require(std::any_of(events.begin(),events.end(),[&](const auto& proposal)
            {return proposal.Source=="nfp.query"&&proposal.StateID==event.ParentStateID;}),
            "subsequent strip originals did not query the real NFP against the occupied parent state");
        previousTop=box.Y+box.Height;parent=event.StateID;
    }
    for(const auto& placed:result.Layouts.front().Placements)
    {
        const auto source=std::find_if(raw->Placements.begin(),raw->Placements.end(),[&](const auto& value)
            {return value.PartID==placed.PartID&&value.Instance==placed.Instance;});
        Require(source!=raw->Placements.end()&&source->Transform.X==placed.Transform.X&&
            source->Transform.Y==placed.Transform.Y&&source->Transform.AngleDegrees==placed.Transform.AngleDegrees&&
            source->Transform.Mirrored==placed.Transform.Mirrored,
            "a later finishing pass supplied the strip's tested positions");
    }
}
class NonPureConstructor final:public IStrategy
{
    std::size_t& Runs;
public:
    explicit NonPureConstructor(std::size_t& runs):Runs(runs){}
    bool OwnsRefinementLifecycle()const noexcept override{return true;}
    Result Run(const Problem&,const Options&)override
    {++Runs;Result result;result.State=Status::Failed;return result;}
};
class RecordingInitialConstructor final:public IStrategy
{
    std::vector<std::size_t>& Caps;std::vector<double>& Seconds;std::vector<std::vector<std::string>>* Orders;
public:
    RecordingInitialConstructor(std::vector<std::size_t>& caps,std::vector<double>& seconds,
        std::vector<std::vector<std::string>>* orders=nullptr):Caps(caps),Seconds(seconds),Orders(orders){}
    bool OwnsRefinementLifecycle()const noexcept override{return true;}
    bool IsPureConstruction()const noexcept override{return true;}
    Result Run(const Problem& p,const Options& o)override
    {
        Caps.push_back(o.MaxPlacementAttempts);Seconds.push_back(o.TimeLimitSeconds);
        if(Orders){std::vector<std::string> ids;for(const auto& part:p.Parts)ids.push_back(part.ID);Orders->push_back(std::move(ids));}
        return MakePlaneEnvelopeInitialFactory()()->Run(p,o);
    }
};
}
int main()
{
try
{
    StrategyRegistry registry;RegisterPlaneStrategies(registry);
    for(const auto& id:DefaultConstructors())
    {const auto constructor=registry.Create(id);Require(constructor&&constructor->IsPureConstruction(),"a canonical default constructor is unavailable");}
    for(const auto& id:HistoricalConstructors())
    {const auto constructor=registry.Create(id);Require(constructor&&constructor->IsPureConstruction(),"a historical directly callable constructor was removed");}
    {
        // Equal contours with different original identities must not be
        // collapsed into one combination type by preparation or dispatch.
        Problem p;p.PartGap=.2;p.EdgeMargin=.5;
        p.Parts={MakeRect("original-A",2,2,4),MakeRect("original-B",2,2,2)};
        p.Stocks={{"self-only-sheet",20,20,2}};
        bool observedSelfUnit=false;
        for(const auto kind:{K::ArrayQuantityDeferred,K::ArrayGeometryDeferred,K::ArrayQuantityImmediate,
            K::ArrayGeometryImmediate,K::HistoricalFirstArray,K::ArrayFirstSpecificationSearch,
            K::ArrayMultiSpecificationSearch,K::ArrayReduceSearch,K::RandomArrays,K::ArrayCompetition,
            K::MixedCompetition,K::Scanline,K::NfpAllSpecifications,K::NfpClosest,K::NfpDirectional,
            K::NfpStrip,K::NfpArray,K::NfpMultiArray,K::RectangleBlockSearch,K::RectangleContact,
            K::SingleProductArray,K::SingleColumn,K::FourBlockArray})
        {
            auto o=Settings();o.TimeLimitSeconds=std::numeric_limits<double>::max();
            o.EnableCombinations=true;o.EnableGapFill=false;o.MaxPatternMembers=4;o.MaxPatterns=32;
            o.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
            bool crossType=false,crossTypeWork=false;
            o.ConstructionTrace=[&](const ConstructionTraceEvent& event)
            {
                if(!event.Members.empty())for(const auto& member:event.Members)
                    crossType|=member.PartIndex!=event.Members.front().PartIndex;
                if(event.Members.size()>1&&event.Source=="constructor.commit")observedSelfUnit=true;
                if(event.Source=="preparation.combination-summary")for(const auto& [name,value]:event.Measures)
                    if(name=="crossPartRegionRequests"||name=="crossPartBeamChoices")crossTypeWork|=value!=0;
            };
            PlaneStrategySettings s;s.Kind=kind;s.Restarts=1;s.MaxNodes=16;s.BranchWidth=2;s.BranchDecisions=1;
            s.EnableLayoutRefinement=false;s.EnableContourSettling=false;s.EnableAllowedAngleSettling=false;
            s.EnableSheetTemplateReuse=false;s.EnableHoleNesting=false;
            const auto result=MakePlaneStrategyFactory(s)()->Run(p,o);
            const auto checked=Validate(p,result);
            bool groupsAreSelf=true;
            for(const auto& board:result.Layouts)
            {
                std::map<std::size_t,std::string> types;
                for(const auto& placed:board.Placements)if(placed.Group)
                {
                    const auto [item,inserted]=types.emplace(placed.Group,placed.PartID);
                    groupsAreSelf&=inserted||item->second==placed.PartID;
                }
            }
            if(!checked.Valid||!checked.Complete||crossType||crossTypeWork||!groupsAreSelf||o.ConstructionObservation->Failed.load())
                throw std::runtime_error("production initial route violated self-only combinations or original demand; kind="+
                    std::to_string(static_cast<int>(kind)));
        }
        Require(observedSelfUnit,"self-only policy accidentally disabled all repeated-original combinations");
    }
    {
        Problem p;p.Parts={MakeRect("small",2,2),MakeRect("long",8,3),MakeRect("middle",4,3)};
        p.Stocks={{"stock",30,30,1}};
        std::vector<std::size_t> caps;std::vector<double> seconds;std::size_t results=0;
        auto o=Settings();o.TimeLimitSeconds=10;o.MaxPlacementAttempts=3200;
        o.ConstructionTrace=[&](const ConstructionTraceEvent& e){if(e.Source=="order.result")++results;};
        const auto run=MakePlaneOrderPortfolioFactory([&]{return std::make_unique<RecordingInitialConstructor>(caps,seconds);},16)()->Run(p,o);
        Require(!caps.empty()&&caps.front()==o.MaxPlacementAttempts&&seconds.front()>9,
            "order portfolio split its first full construction into restart-count slices");
        Require(Validate(p,run).Complete&&p.Parts.front().ID=="small"&&results==caps.size(),
            "independent order construction changed original demand or omitted per-order results");
    }
    {
        Problem p;p.Parts={MakeRect("long-strip",10,1),MakeRect("dense",4,4),MakeRect("wide",6,2)};
        p.Stocks={{"stock",30,30,1}};
        for(const auto seed:{1u,999u})
        {
            std::vector<std::size_t> caps;std::vector<double> seconds;std::vector<std::vector<std::string>> orders;
            auto o=Settings();o.Seed=seed;
            auto factory=[&]{return std::make_unique<RecordingInitialConstructor>(caps,seconds,&orders);};
            const auto result=MakePlaneOrderPortfolioFactory(factory,2)()->Run(p,o);
            Require(Validate(p,result).Complete&&orders.size()==2&&
                orders[0]==std::vector<std::string>{"long-strip","wide","dense"}&&
                orders[1]==std::vector<std::string>{"dense","wide","long-strip"},
                "strong initial order prefix depended on a lucky random swap instead of geometric order rules");
        }
    }
    Require(registry.Create("plane.nfp-all")->OwnsRefinementLifecycle(),"NFP complete-layout workflow owns its shared refinement budget");
    Require(!registry.Create("plane.nfp-initial")->OwnsRefinementLifecycle(),"NFP initial constructor excludes complete-layout compaction and refill");
    Require(!registry.Create("plane.nfp-all")->IsPureConstruction()&&registry.Create("plane.nfp-initial")->IsPureConstruction(),
        "construction capability distinguishes the NFP initial route from the complete optimization workflow");
    {
        Problem tiny;tiny.Parts={MakeRect("tile",2,2)};tiny.Parts[0].AllowedAnglesDegrees={0};tiny.Stocks={{"sheet",4,4,1}};
        Part unused;unused.ID="unused-triangle";unused.Quantity=0;unused.FreeRotation=false;unused.AllowedAnglesDegrees={0};
        unused.Geometry.Outer={{0,0},{2,0},{0,2}};tiny.Parts.push_back(unused);
        ParallelConstructionSettings exhaustive;exhaustive.Workers=1;exhaustive.FairScheduling=false;
        StrategyRegistry allInitial;allInitial.Register("all-initial",MakePlaneInitialFactory(exhaustive));
        auto limits=Settings();limits.TimeLimitSeconds=5;limits.MaxPlacementAttempts=1000000;limits.EnableBasicCompaction=true;
        std::vector<std::string> queued;
        limits.ConstructionTrace=[&](const ConstructionTraceEvent& e)
            {if(e.Source=="scheduler.queue")queued.push_back(e.Route);};
        const auto initial=RunJob({"all-initial","all-initial",tiny,limits},allInitial);
        Require(initial.Checked.Valid&&initial.Checked.Complete,"all applicable initial methods retain the full original task");
        Require(queued.size()==DefaultConstructors().size()&&
            std::set<std::string>(queued.begin(),queued.end())==DefaultConstructors()&&
            std::vector<std::string>(queued.begin(),queued.begin()+4)==std::vector<std::string>{
                "plane.envelope-initial","plane.array-layered","plane.nfp-initial","plane.nfp-array"},
            "default initial race duplicated historical source/order variants or omitted an independent constructor");
        Require(HasDiagnostic(initial.Output,"plane.initial candidates=19 eligible=19 skipped=0"),
            "all canonical initial constructors must be considered for demanded rectangles, ignoring zero demand");
        for(const auto& id:DefaultConstructors())
        {
            const auto route="parallel.construction job="+id+" ";
            Require(HasDiagnostic(initial.Output,route+"placed="),"an applicable registered initial method did not actually return a candidate");
        }
        for(const auto& id:HistoricalConstructors())
            Require(!HasDiagnostic(initial.Output,"parallel.construction job="+id+" "),
                "a historical alias or order wrapper re-entered the default race");
        for(const auto& id:{"plane.initial","plane.nfp-all"})
            Require(!HasDiagnostic(initial.Output,std::string("parallel.construction job=")+id+" "),
                "initial scheduling must exclude itself and the full optimization workflow");
        Require(HasDiagnostic(initial.Output,"target-stop=0"),"default initial scheduling must not stop merely because minimum board count was reached");
        Require(HasDiagnostic(initial.Output,"fair-scheduling=1"),"the plane initial scheduler must enable fair scheduling even when the generic setting is false");
        Require(initial.Output.Stats.PostProcessingSeconds==0,
            "a pure initial job must own its lifecycle and exclude automatic outer layout refinement");

        auto mixed=tiny;mixed.Parts[1].Quantity=1;mixed.Stocks[0].Width=8;mixed.Stocks[0].Height=6;
        const auto nonrectangular=MakePlaneInitialFactory(exhaustive)()->Run(mixed,limits);
        Require(Validate(mixed,nonrectangular).Complete,"nonrectangular initial candidates retain all demanded originals");
        Require(HasDiagnostic(nonrectangular,"plane.initial candidates=19 eligible=14 skipped=5"),
            "mixed original geometry must retain all 14 applicable canonical initial methods");
        const std::set<std::string> rectangularOnly{"plane.rect-block-search","plane.rect-contact","plane.single-product","plane.single-column","plane.four-block"};
        for(const auto& id:DefaultConstructors())
        {
            if(rectangularOnly.contains(id))
            {
                Require(HasDiagnostic(nonrectangular,"plane.initial job="+id+" skipped: requires all demanded originals to be hole-free rectangles"),
                    "a rectangle-only initial method must report its original-geometry qualification");
                Require(!HasDiagnostic(nonrectangular,"parallel.construction job="+id+" "),
                    "a rectangle-only method must not run on a mixed nonrectangular task");
            }
            else Require(HasDiagnostic(nonrectangular,"parallel.construction job="+id+" placed="),
                "an applicable nonrectangular initial method was silently omitted");
        }

        auto order=MakePlaneOrderPortfolioFactory(MakePlaneStrategyFactory());
        Require(order()->OwnsRefinementLifecycle()&&order()->IsPureConstruction(),
            "order construction portfolios can own their lifecycle while remaining pure initial methods");
        StrategyRegistry orderOnly;orderOnly.Register("order",order);
        const auto accepted=MakeParallelConstructionPortfolioFactory(orderOnly,{"order"},exhaustive)()->Run(tiny,limits);
        Require(Validate(tiny,accepted).Complete&&HasDiagnostic(accepted,"parallel.construction job=order placed="),
            "a pure order construction portfolio must be admitted by the generic initial race");
        std::size_t refinementRuns=0;
        const auto rejected=MakePlaneOrderPortfolioFactory([&]{return std::make_unique<NonPureConstructor>(refinementRuns);})()->Run(tiny,limits);
        Require(refinementRuns==0&&rejected.State==Status::InvalidInput&&
            HasDiagnostic(rejected,"plane.order-portfolio skipped: wrapped strategy is not a pure initial constructor"),
            "an order construction portfolio must reject a refinement lifecycle before invoking its wrapped strategy");
    }
    {
        auto frame=MakeRect("steel-frame",6,6);frame.Material="steel";
        frame.Geometry.Holes={{{2,2},{4,2},{4,4},{2,4}}};
        auto plug=MakeRect("steel-plug",2,2);plug.Material="steel";
        auto aluminium=MakeRect("aluminium",3,3);aluminium.Material="aluminium";
        Problem mixed;mixed.Parts={frame,plug,aluminium};
        mixed.Stocks={{"steel",8,6,2,1,"steel"},{"aluminium",3,3,2,1,"aluminium"}};
        auto limits=Settings();limits.TimeLimitSeconds=3;
        const auto initial=registry.Create("plane.initial")->Run(mixed,limits);
        Require(Validate(mixed,initial).Complete&&initial.Layouts.size()==2,
            "parallel initial construction lost original hole or material separation");
        Require(std::any_of(initial.Diagnostics.begin(),initial.Diagnostics.end(),[](const std::string& d)
            {return d.find("target-stop=0")!=std::string::npos;}),
            "default initial race stopped on a board-count bound without an explicit caller target");
        std::size_t targetCalls=0;
        ParallelConstructionSettings explicitTarget;explicitTarget.Workers=1;
        explicitTarget.StopWhen=[&](const Problem& original,const JobResult& candidate)
        {++targetCalls;Require(Validate(original,candidate.Output).Complete,"explicit target saw unvalidated partial data");return true;};
        const auto stopped=MakePlaneInitialFactory(explicitTarget)()->Run(mixed,limits);
        Require(targetCalls>0&&Validate(mixed,stopped).Complete&&std::any_of(stopped.Diagnostics.begin(),stopped.Diagnostics.end(),[](const std::string& d)
            {return d.find("target-stop=1")!=std::string::npos;}),"explicit caller target no longer stops the initial race");
    }
    {
        auto frame=MakeRect("hollow",10,10,2);frame.Geometry.Holes={{{1,1},{9,1},{9,9},{1,9}}};
        Problem hollow;hollow.Parts={frame};hollow.Stocks={{"sheet",12,12,2}};
        auto limits=Settings();limits.TimeLimitSeconds=3;
        const auto initial=registry.Create("plane.initial")->Run(hollow,limits);
        Require(Validate(hollow,initial).Complete&&initial.Layouts.size()==2,"parallel hollow original construction failed");
        Require(std::any_of(initial.Diagnostics.begin(),initial.Diagnostics.end(),[](const std::string& d)
            {return d.find("target-stop=0")!=std::string::npos;}),
            "default hollow-original initial race unexpectedly installed a target predicate");
    }
    {
        Part circle;circle.ID="analytic-circle";circle.Quantity=2;circle.Geometry.Radius=4;
        circle.FreeRotation=false;circle.AllowedAnglesDegrees={0};
        Problem circles;circles.Parts={circle};circles.Stocks={{"sheet",10,10,2}};
        auto limits=Settings();limits.TimeLimitSeconds=3;
        const auto initial=registry.Create("plane.initial")->Run(circles,limits);
        Require(Validate(circles,initial).Complete&&initial.Layouts.size()==2,"parallel initial route lost analytic discs");
        Require(std::any_of(initial.Diagnostics.begin(),initial.Diagnostics.end(),[](const std::string& d)
            {return d.find("target-stop=0")!=std::string::npos;}),
            "default circle initial race unexpectedly installed a target predicate");
    }
    Problem sourceOrder;sourceOrder.Parts={MakeRect("small-source",2,2),MakeRect("large-source",4,4)};
    for(auto& part:sourceOrder.Parts)part.AllowedAnglesDegrees={0};sourceOrder.Stocks={{"source",6,4,1}};
    auto sourceImmediate=registry.Create("plane.array-5-source")->Run(sourceOrder,Settings());
    Require(Validate(sourceOrder,sourceImmediate).Complete&&sourceImmediate.Layouts[0].Placements[0].PartID=="large-source",
        "source immediate evaluation compares distinct specification-container weights before committing");
    Problem p;p.Parts={MakeRect("a",6,4,6)};p.Stocks={{"sheet",12,12,3}};
    for(const auto& id:registry.IDs())
    {
        auto o=Settings();o.TimeLimitSeconds=1;
        const auto r=RunJob({id,id,p,o},registry);Require(r.Checked.Valid,"registry RunJob original validation");
        Require(r.Checked.PlacedCount>0,"every registered strategy executes a nonempty constructor");
    }
    const auto count=Run(p,K::ArrayQuantityDeferred),geometry=Run(p,K::ArrayGeometryDeferred);
    Require(count.State==Status::Complete&&geometry.State==Status::Complete,"array variants complete");
    // Public variants share the utilization objective. Different coordinates
    // are not a quality requirement when their candidate mechanisms coincide.
    Require(count.Layouts.size()==1&&geometry.Layouts.size()==1,
        "array variants must retain the attainable full-sheet material utilization");
    Problem notch;Part a;a.ID="L";a.FreeRotation=false;a.AllowedAnglesDegrees={0};a.Geometry.Outer={{0,0},{4,0},{4,2},{2,2},{2,4},{0,4}};
    auto b=MakeRect("small",2,2,2);b.AllowedAnglesDegrees={0};notch.Parts={a,b};notch.Stocks={{"s",7,4,1}};
    std::vector<ConstructionTraceEvent> delayedEvents,immediateEvents;
    auto delayedOptions=Settings(),immediateOptions=Settings();
    delayedOptions.ConstructionTrace=[&](const auto& event){delayedEvents.push_back(event);};
    immediateOptions.ConstructionTrace=[&](const auto& event){immediateEvents.push_back(event);};
    const auto delayed=Run(notch,K::ArrayGeometryDeferred,delayedOptions);
    const auto immediate=Run(notch,K::ArrayGeometryImmediate,immediateOptions);
    Require(delayed.State==Status::Complete&&immediate.State==Status::Complete,"notch inventory conserved");
    const auto firstFill=std::find_if(immediateEvents.begin(),immediateEvents.end(),[](const auto& event)
        {return event.Source=="fill.commit";});
    Require(firstFill!=immediateEvents.end()&&firstFill->Members.size()==1&&firstFill->Members[0].PartIndex==1&&
        std::any_of(firstFill,immediateEvents.end(),[](const auto& event){return event.Source=="constructor.commit";})&&
        immediate.Stats.GapFilledParts>0,"immediate filling did not consume a real notch original before a later main batch");
    bool delayedFilled=false;
    for(const auto& event:delayedEvents)
    {
        if(event.Source=="fill.commit")delayedFilled=true;
        if(event.Source=="constructor.commit")Require(!delayedFilled,
            "deferred construction resumed main batches after already committing its delayed gap fill");
    }
    Problem products;products.Parts={MakeRect("A",3,2,2),MakeRect("B",2,2,2)};products.Stocks={{"s",10,10,3}};
    auto one=Run(products,K::SingleProductArray);Require(one.Layouts.size()==2,"single-product policy opens separate sheets");
    for(const auto& l:one.Layouts){std::set<std::string> ids;for(const auto& q:l.Placements)ids.insert(q.PartID);Require(ids.size()==1,"single-product layout is unmixed");}
    Problem column;auto c=MakeRect("c",2,3,2),d=MakeRect("d",2,2,2);c.AllowedAnglesDegrees=d.AllowedAnglesDegrees={0};column.Parts={c,d};column.Stocks={{"s",10,10,1}};
    auto col=Run(column,K::SingleColumn);Require(col.State==Status::Complete,"same-width column completes");
    for(const auto& q:col.Layouts[0].Placements)Require(q.Transform.X==0,"one column has no side fill");
    Problem shifted;auto offset=MakeRect("offset",3,2,5);for(auto& v:offset.Geometry.Outer){v.X+=-7;v.Y+=11;}offset.AllowedAnglesDegrees={90};shifted.Parts={offset};shifted.Stocks={{"s",12,12,2}};shifted.PartGap=.5;shifted.EdgeMargin=.75;
    auto mapped=Run(shifted,K::ArrayQuantityDeferred);Require(mapped.State==Status::Complete,"offset/gap/rotation complete");
    for(const auto& l:mapped.Layouts)for(const auto& q:l.Placements)Require(q.Transform.AngleDegrees==90,"finite allowed angle retained");
    Problem nf=p;nf.PartGap=.1;auto gapArray=Run(nf,K::NfpArray);Require(gapArray.State==Status::Complete,"positive-gap NFP arrays preserve original demand and clearance");
    Problem triangles;Part triangle;triangle.ID="triangle";triangle.Quantity=2;triangle.FreeRotation=false;
    triangle.AllowedAnglesDegrees={0,180};triangle.Geometry.Outer={{0,0},{6,0},{0,6}};
    triangles.Parts={triangle};triangles.Stocks={{"s",7.5,7.5,1}};triangles.PartGap=.2;triangles.EdgeMargin=.5;
    for(auto kind:{K::NfpAllSpecifications,K::NfpClosest,K::NfpDirectional})
    {
        const auto result=Run(triangles,kind);
        if(result.State!=Status::Complete){std::cerr<<"positive-gap kind="<<static_cast<int>(kind)<<" attempts="<<result.Stats.PlacementAttempts<<'\n';for(const auto& message:result.Diagnostics)std::cerr<<message<<'\n';for(const auto& board:result.Layouts)for(const auto& q:board.Placements)std::cerr<<q.PartID<<' '<<q.Instance<<' '<<q.Transform.X<<' '<<q.Transform.Y<<' '<<q.Transform.AngleDegrees<<'\n';}
        Require(result.State==Status::Complete&&result.Layouts.size()==1,"positive-gap NFP nests complementary sloped originals on one board");
        const auto& members=result.Layouts[0].Placements;
        const auto ga=AxisSafety::Make(triangle.Geometry,members[0].Transform),gb=AxisSafety::Make(triangle.Geometry,members[1].Transform);
        Require(AxisSafety::Pair(ga,gb,.2,false,triangles.Tolerance)==AxisSafety::Verdict::Safe,"positive-gap NFP preserves nominal material clearance");
        Require(AxisSafety::Containment(ga,7.5,7.5,.5)==AxisSafety::Verdict::Safe&&AxisSafety::Containment(gb,7.5,7.5,.5)==AxisSafety::Verdict::Safe,"clearance obstacles do not replace original edge-margin checks");
        const auto firstBox=Detail::Bounds(triangle.Geometry,members[0].Transform),secondBox=Detail::Bounds(triangle.Geometry,members[1].Transform);
        Require(std::min(firstBox.X+firstBox.Width,secondBox.X+secondBox.Width)>std::max(firstBox.X,secondBox.X)&&std::min(firstBox.Y+firstBox.Height,secondBox.Y+secondBox.Height)>std::max(firstBox.Y,secondBox.Y),"positive-gap fitting uses true sloped contours rather than disjoint boxes");
    }
    auto edgeCase=triangles;edgeCase.EdgeMargin=0;
    auto onEdge=Run(edgeCase,K::NfpAllSpecifications);Require(onEdge.State==Status::Complete,"positive gap works with zero edge margin");
    bool touchesEdge=false;for(const auto& q:onEdge.Layouts[0].Placements){auto box=Detail::Bounds(triangle.Geometry,q.Transform);touchesEdge|=box.X==0||box.Y==0;}
    Require(touchesEdge,"positive clearance must not introduce an artificial board margin");
    auto reversedSource=triangles;
    for(auto& point:reversedSource.Parts[0].Geometry.Outer){point.X=6-point.X;point.Y=6-point.Y;}
    Require(Run(reversedSource,K::NfpDirectional).State==Status::Complete,"first-piece direction is based on actual material rather than the source angle number");
    auto shiftedTriangles=triangles;shiftedTriangles.Parts[0].ID="__plane_clearance_0";
    shiftedTriangles.Parts[0].AllowedAnglesDegrees={90,270};
    for(auto& point:shiftedTriangles.Parts[0].Geometry.Outer){point.X-=7;point.Y+=11;}
    Require(Run(shiftedTriangles,K::NfpAllSpecifications).State==Status::Complete,"clearance mapping retains negative local origins, finite rotations and colliding private-name prefixes");
    auto mixedTriangles=triangles;mixedTriangles.Parts[0].ID="left";mixedTriangles.Parts[0].Quantity=1;mixedTriangles.Parts[0].AllowedAnglesDegrees={0};
    auto right=mixedTriangles.Parts[0];right.ID="right";right.AllowedAnglesDegrees={180};mixedTriangles.Parts.push_back(right);
    auto motifOptions=Settings();motifOptions.EnableCombinations=true;motifOptions.EnableGapFill=false;
    const auto singleTypeGroups=[](const Result& result)
    {
        for(const auto& board:result.Layouts)
        {
            std::map<std::size_t,std::string> types;
            for(const auto& placed:board.Placements)if(placed.Group)
            {
                const auto [item,inserted]=types.emplace(placed.Group,placed.PartID);
                if(!inserted&&item->second!=placed.PartID)return false;
            }
        }
        return true;
    };
    auto mixedMotif=Run(mixedTriangles,K::ArrayGeometryDeferred,motifOptions);
    Require(Validate(mixedTriangles,mixedMotif).Valid&&singleTypeGroups(mixedMotif)&&mixedMotif.Stats.CombinationGroups==0,
        "different one-off originals must not be converted to a mixed array unit");
    auto nfpMotif=Run(mixedTriangles,K::ArrayMultiSpecificationSearch,motifOptions);
    Require(Validate(mixedTriangles,nfpMotif).Valid&&singleTypeGroups(nfpMotif)&&nfpMotif.Stats.CombinationGroups==0,
        "multi-specification search bypassed the self-only combination policy");
    const auto independentOriginals=Run(mixedTriangles,K::NfpArray,motifOptions);
    Require(Validate(mixedTriangles,independentOriginals).Complete&&independentOriginals.Layouts.size()==1&&
        singleTypeGroups(independentOriginals)&&independentOriginals.Stats.CombinationGroups==0,
        "self-only combinations must not prohibit different originals from independent NFP interlocking on one board");
    Problem mixedCounts;mixedCounts.Parts={MakeRect("small",2,2,2),MakeRect("tall",2,4,1)};
    for(auto& part:mixedCounts.Parts)part.AllowedAnglesDegrees={0};mixedCounts.Stocks={{"s",4,4,1}};
    motifOptions.MaxPatternMembers=3;
    // A source-order greedy may choose the short horizontal self row first.
    // The production ordering portfolio also constructs the tall-first order
    // from an empty board; it must complete without a cross-type 2A+1B unit.
    auto unequalMotif=registry.Create("plane.order-array")->Run(mixedCounts,motifOptions);
    Require(Validate(mixedCounts,unequalMotif).Complete&&singleTypeGroups(unequalMotif),
        "self-only units must retain the complete demand of independently placed original types");
    std::size_t tallCount=0;std::set<std::size_t> motifGroups;
    for(const auto& board:unequalMotif.Layouts)for(const auto& q:board.Placements){tallCount+=q.PartID=="tall";motifGroups.insert(q.Group);}
    Require(tallCount==1&&motifGroups.size()>=2,
        "scarce original must remain independent of the repeated self unit");
    Result fixedTriangle;fixedTriangle.State=Status::Partial;fixedTriangle.Layouts={{"s",0,7.5,7.5,{{"triangle",1,{.5,.5,0},29,false}},18}};fixedTriangle.Remaining={{"triangle",1}};
    auto gapFilled=MakePlaneNfpFillStage().Factory()->Run(triangles,fixedTriangle,Settings());
    Require(Validate(triangles,gapFilled).Complete&&gapFilled.Layouts[0].Placements[0].Instance==1&&gapFilled.Layouts[0].Placements[0].Group==29&&gapFilled.Layouts[0].Placements[0].Transform.X==.5,"positive-gap fill retains fixed originals and sparse identities");
    auto random1=Run(products,K::RandomArrays),random2=Run(products,K::RandomArrays);Require(Signature(random1)==Signature(random2),"fixed seed replays random construction");
    auto mixed=Run(products,K::MixedCompetition);Require(mixed.State==Status::Complete,"per-board competition completes");
    Problem fill;fill.Parts={MakeRect("x",2,2,3)};fill.Stocks={{"s",6,2,1}};fill.Parts[0].AllowedAnglesDegrees={0};
    Result fixed;fixed.State=Status::Partial;fixed.Layouts={{"s",0,6,2,{{"x",2,{0,0,0},47,false}},4}};fixed.Remaining={{"x",2}};
    Require(Validate(fill,fixed).Valid,"partial fixture uses sparse original instance");
    auto filled=MakePlaneNfpFillStage().Factory()->Run(fill,fixed,Settings());Require(Validate(fill,filled).Complete,"fixed-layout fill supplies missing instances");
    Require(filled.Layouts[0].Placements[0].Instance==2&&filled.Layouts[0].Placements[0].Transform.X==0&&filled.Layouts[0].Placements[0].Group==47,"fixed identity and pose unchanged");
    std::set<std::size_t> instances;for(const auto& q:filled.Layouts[0].Placements)instances.insert(q.Instance);Require(instances==std::set<std::size_t>{0,1,2},"fill uses unused original IDs");
    auto cancelled=Settings();cancelled.Cancel=[]{return true;};auto stop=Run(p,K::ArrayMultiSpecificationSearch,cancelled);Require(stop.State==Status::Cancelled&&stop.Layouts.empty(),"cancel preserves all demand");
    auto limited=Settings();limited.MaxPlacementAttempts=1;auto partial=Run(p,K::ArrayQuantityDeferred,limited);Require(partial.Stats.PlacementAttempts<=1,"attempt limit is shared and bounded");
    auto bad=Settings();bad.TimeLimitSeconds=0;auto invalid=MakePlaneStrategyFactory()()->Run(p,bad);Require(invalid.State==Status::InvalidInput,"invalid budget rejected");
    Problem compact;compact.Parts={MakeRect("box",2,2,2)};compact.Parts[0].AllowedAnglesDegrees={0};compact.Stocks={{"s",20,20,1}};
    Result loose;loose.State=Status::Complete;loose.Layouts={{"s",0,20,20,{{"box",0,{0,0,0},1,false},{"box",1,{8,5,0},2,false}},8}};
    PlaneCompactionSettings optimize;optimize.StableIterations=4;optimize.Rotate=false;optimize.Reassemble=false;optimize.ShrinkRegion=false;
    auto improved=OptimizePlaneLayout(compact,loose,Settings(),optimize);
    Require(Validate(compact,improved.Output).Complete&&improved.BestUtilization>improved.InitialUtilization,"fixed-layout controller improves true envelope utilization");
    Require(improved.Stop==PlaneOptimizationStop::Stable&&improved.StableCount==4,"stable stopping counts rejected/no-improvement rounds");
    optimize.TargetUtilization=improved.BestUtilization;auto reached=OptimizePlaneLayout(compact,improved.Output,Settings(),optimize);
    Require(reached.Stop==PlaneOptimizationStop::Target&&reached.Iterations==0,"initial target terminates without an operator");
    auto cancelledOptimization=OptimizePlaneLayout(compact,loose,cancelled,optimize);
    Require(cancelledOptimization.Stop==PlaneOptimizationStop::Cancelled&&Signature(cancelledOptimization.Output)==Signature(loose),"cancel returns intact historical best");
    auto shrink=MakePlaneShrinkStage(MakePlaneStrategyFactory(),4).Factory()->Run(compact,loose,Settings());
    Require(Validate(compact,shrink).Complete&&shrink.Layouts[0].Width==20&&shrink.Layouts[0].StockInstance==0,"shrink restores physical stock and complete original set");
    Problem tailProblem=compact;tailProblem.Parts[0].Quantity=3;tailProblem.Stocks[0].Quantity=3;
    auto tailInitial=loose;tailInitial.Layouts[0].StockInstance=2;tailInitial.Layouts[0].Placements[1].Instance=2;
    tailInitial.Layouts.insert(tailInitial.Layouts.begin(),{"s",0,20,20,{{"box",1,{5,6,0},91,false}},4});
    auto tailShrunk=MakePlaneShrinkStage(MakePlaneStrategyFactory(),4).Factory()->Run(tailProblem,tailInitial,Settings());
    Require(Validate(tailProblem,tailShrunk).Complete&&tailShrunk.Layouts.back().StockInstance==2,"tail shrink preserves physical stock instance and sparse part IDs");
    Result prefixBefore,prefixAfter;prefixBefore.Layouts={tailInitial.Layouts.front()};prefixAfter.Layouts={tailShrunk.Layouts.front()};
    Require(Signature(prefixBefore)==Signature(prefixAfter)&&prefixAfter.Layouts[0].Placements[0].Group==91,"tail shrink leaves earlier layouts untouched");
    PlaneDomainTask domain;domain.Input.Parts={MakeRect("tile",2,2,12)};domain.Input.Parts[0].AllowedAnglesDegrees={0};domain.Input.Stocks={{"L-stock",10,10,1}};
    PlaneStockDomain stockDomain;stockDomain.StockID="L-stock";stockDomain.Boundary.Outer={{0,0},{10,0},{10,4},{4,4},{4,10},{0,10}};stockDomain.InnerRectangles={{0,0,10,4}};domain.Domains={stockDomain};
    auto domainNfp=RunPlaneDomainTask(domain,Settings());Require(ValidatePlaneDomainLayout(domain,domainNfp).Valid&&domainNfp.State==Status::Complete,"L-stock exterior obstacle construction");
    for(const auto& q:domainNfp.Layouts[0].Placements)Require(q.PartID=="tile","private obstacles never enter production output");
    domain.UseInnerRectangle=true;auto hybrid=RunPlaneDomainTask(domain,Settings());Require(ValidatePlaneDomainLayout(domain,hybrid).Complete,"inner rectangle plus peripheral NFP");
    auto corrupted=hybrid;corrupted.Layouts[0].Placements[0].Transform={8,8,0};Require(!ValidatePlaneDomainLayout(domain,corrupted).Valid,"real concave board validator rejects rectangle-only false positive");
    PlaneLabelRequest label;label.HostID="host";label.LabelID="label";label.Host=MakeRect("host",10,10).Geometry;label.Host.Holes={{{4,4},{6,4},{6,6},{4,6}}};label.Label=MakeRect("label",2,2);label.Target={5,5};
    auto located=LocatePlaneLabel(label,Settings());Require(located.Placed&&located.Distance>=2,"label center expansion leaves host hole");
    PlaneManufacturingTask pairTask;pairTask.Input.Parts={MakeRect("doorA",3,2,3),MakeRect("doorB",2,2,1)};pairTask.Input.Stocks={{"s",20,10,1}};pairTask.Pairs={{"doorA","doorB",1,true}};
    auto paired=RunPlaneManufacturingTask(pairTask,Settings());Require(ValidatePlaneManufacturingLayout(pairTask,paired).Valid&&paired.Output.State==Status::Complete,"unequal pair demand does not overproduce the second member");
    auto wrongPair=paired;for(auto& q:wrongPair.Output.Layouts[0].Placements)if(q.PartID=="doorB")q.Transform={15,7,0};Require(!ValidatePlaneManufacturingLayout(pairTask,wrongPair).Valid,"hard adjacency survives independent final verification");
    PlaneManufacturingTask common;common.Mode=PlaneManufacturingMode::CommonEdgeArray;common.Input.Parts={MakeRect("common",5,3,7)};common.Input.Parts[0].AllowedAnglesDegrees={0};common.Input.Stocks={{"s",30,20,1}};common.Input.PartGap=1;common.Input.AllowCommonEdges=true;common.GroupGap=2;common.MaxGroupMembers=3;common.MinimumSharedLength=2;
    auto grouped=RunPlaneManufacturingTask(common,Settings());if(grouped.Output.State!=Status::Complete){for(const auto& e:grouped.Output.Diagnostics)std::cerr<<e<<'\n';for(const auto& l:grouped.Output.Layouts)for(const auto& q:l.Placements)std::cerr<<q.PartID<<' '<<q.Instance<<' '<<q.Group<<' '<<q.Transform.X<<' '<<q.Transform.Y<<'\n';}Require(ValidatePlaneManufacturingLayout(common,grouped).Valid&&grouped.Output.State==Status::Complete,"common-edge array obeys capacity and inter-group gap");
    common.Mode=PlaneManufacturingMode::CommonEdgeDistance;common.Input.Parts[0].Quantity=5;common.MaxGroupMembers=2;
    auto dual=RunPlaneManufacturingTask(common,Settings());Require(ValidatePlaneManufacturingLayout(common,dual).Valid&&dual.Output.State==Status::Complete,"normal and offset NFP domains form distinct safe groups");
    common.Input.PartGap=0;common.GroupGap=0;
    auto zeroGap=RunPlaneManufacturingTask(common,Settings());Require(ValidatePlaneManufacturingLayout(common,zeroGap).Valid&&zeroGap.Output.State==Status::Complete,"zero-offset common-edge domain remains usable");
    auto badMode=common;badMode.Mode=static_cast<PlaneManufacturingMode>(999);
    Require(RunPlaneManufacturingTask(badMode,Settings()).Output.State==Status::InvalidInput,"unknown process mode cannot fall through to another strategy");
    auto cancelledProcess=RunPlaneManufacturingTask(common,cancelled);
    Require(cancelledProcess.Output.State==Status::Cancelled&&cancelledProcess.Output.Layouts.empty()&&ValidatePlaneManufacturingLayout(common,cancelledProcess).Valid,"process cancellation retains original demand");
    PlaneManufacturingTask cutting;cutting.Mode=PlaneManufacturingMode::Guillotine;cutting.Input.Parts={MakeRect("wide",3,2,2),MakeRect("narrow",2,2,2)};cutting.Input.Stocks={{"s",12,6,1}};cutting.Input.PartGap=.5;
    auto cut=RunPlaneManufacturingTask(cutting,Settings());Require(ValidatePlaneManufacturingLayout(cutting,cut).Valid&&cut.Output.State==Status::Complete&&!cut.Cuts.empty(),"same-height row produces a valid guillotine trace");
    auto badCut=cut;badCut.Cuts[0].Region.X+=1;Require(!ValidatePlaneManufacturingLayout(cutting,badCut).Valid,"cut trace is checked against actual remaining leaves");
    badCut=cut;badCut.Cuts.back().Kerf=0;Require(!ValidatePlaneManufacturingLayout(cutting,badCut).Valid,"a smaller kerf cannot bypass the process clearance");
    cutting.Mode=PlaneManufacturingMode::SawMill;cutting.Input.Parts.resize(1);cutting.Input.Parts[0].Quantity=1;Part mill;mill.ID="mill";mill.Geometry.Outer={{0,0},{2,0},{0,2}};mill.FreeRotation=false;mill.AllowedAnglesDegrees={0};cutting.Input.Parts.push_back(mill);cutting.SawPartIDs={"wide"};cutting.SawGap=.5;cutting.MillGap=1;
    auto mixedProcess=RunPlaneManufacturingTask(cutting,Settings());Require(ValidatePlaneManufacturingLayout(cutting,mixedProcess).Valid&&mixedProcess.Output.State==Status::Complete,"saw/mill routing retains separate process gaps and leaf containment");
    auto cancelledDomain=RunPlaneDomainTask(domain,cancelled);
    Require(cancelledDomain.State==Status::Cancelled&&cancelledDomain.Layouts.empty()&&ValidatePlaneDomainLayout(domain,cancelledDomain).Valid,"domain cancellation retains all originals");
    Problem multiFill=fill;multiFill.Parts[0].Quantity=4;multiFill.Stocks[0].Width=4;multiFill.Stocks[0].Quantity=2;
    Result twoFixed;twoFixed.State=Status::Partial;twoFixed.Layouts={{"s",0,4,2,{{"x",3,{0,0,0},11,false}},4},{"s",1,4,2,{{"x",1,{0,0,0},12,false}},4}};twoFixed.Remaining={{"x",2}};
    auto twoFilled=MakePlaneNfpFillStage().Factory()->Run(multiFill,twoFixed,Settings());
    Require(Validate(multiFill,twoFilled).Complete&&twoFilled.Layouts[0].Placements[0].Instance==3&&twoFilled.Layouts[1].Placements[0].Instance==1,"multi-sheet fill keeps sparse fixed identities without duplicate additions");
    Problem scan;scan.Parts={MakeRect("scan",2,2,2)};scan.Parts[0].AllowedAnglesDegrees={0};scan.Stocks={{"s",6,6,1}};
    PlaneStrategySettings scanSettings;scanSettings.Kind=K::Scanline;
    auto yFirst=MakePlaneStrategyFactory(scanSettings)()->Run(scan,Settings());scanSettings.XFirst=true;
    auto xFirst=MakePlaneStrategyFactory(scanSettings)()->Run(scan,Settings());
    Require(Validate(scan,xFirst).Complete&&Validate(scan,yFirst).Complete&&Signature(xFirst)!=Signature(yFirst),"scan direction changes physical traversal");
    CheckStripConstruction();
    bool rejectedKind=false;try{scanSettings.Kind=static_cast<K>(999);MakePlaneStrategyFactory(scanSettings);}catch(const std::invalid_argument&){rejectedKind=true;}
    Require(rejectedKind,"unknown construction kind is rejected");
    // Reproducible varied stock/material/rotation/gap problems exercise actual
    // original validation for all entries, including bounded partial results.
    std::mt19937 randomCases(271828);
    for(int sample=0;sample<6;++sample)
    {
        Problem varied;varied.PartGap=sample%2?.25:0;varied.EdgeMargin=.5;
        for(int i=0;i<3;++i)
        {
            auto part=MakeRect("v"+std::to_string(i),2+randomCases()%4,2+randomCases()%3,1+randomCases()%4);
            part.Material=i==2?"steel":"aluminium";if(i==1)part.AllowedAnglesDegrees={90};
            for(auto& q:part.Geometry.Outer){q.X-=3;q.Y+=7;}varied.Parts.push_back(part);
        }
        varied.Stocks={{"al",13,11,2},{"st",12,10,2}};varied.Stocks[0].Material="aluminium";varied.Stocks[1].Material="steel";
        for(const auto& id:registry.IDs())
        {
            auto limits=Settings();limits.MaxPlacementAttempts=8000;
            auto output=registry.Create(id)->Run(varied,limits);const auto validation=Validate(varied,output);
            if(!validation.Valid)throw std::runtime_error("varied original validation: "+id);
            Require(output.Stats.PlacementAttempts<=limits.MaxPlacementAttempts,"varied case shares its attempt cap");
        }
    }
    std::cout<<"PlaneStrategyTests passed: registrations, semantic variants, geometry, inventory, fill, budgets and cancellation\n";
    return 0;
}
catch(const std::exception& e){std::cerr<<"PlaneStrategyTests failed: "<<e.what()<<'\n';return 1;}
}
