#include "NestingSheetTemplateReuse.h"
#include "SheetNestingConstructionTrace.h"
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Part Rectangle(const char* id,std::size_t quantity)
{
    Part part;part.ID=id;part.Quantity=quantity;part.Material="steel";part.FreeRotation=false;part.AllowedAnglesDegrees={0};
    part.Geometry.Outer={{0,0},{1.5,0},{1.5,3},{0,3}};return part;
}
struct Fixture
{
    Problem Input;Result Output;std::vector<std::size_t> Used;std::size_t Stock=1,Group=78;
};
void Reconcile(Fixture& value)
{
    value.Output.Remaining.clear();
    for(std::size_t i=0;i<value.Input.Parts.size();++i)
        if(value.Used[i]<value.Input.Parts[i].Quantity)value.Output.Remaining.push_back({value.Input.Parts[i].ID,value.Input.Parts[i].Quantity-value.Used[i]});
    value.Output.State=value.Output.Remaining.empty()?Status::Complete:Status::Partial;
}
Fixture MixedRows(std::size_t stocks=20)
{
    Fixture value;value.Input.PartGap=.125;value.Input.EdgeMargin=.0625;value.Input.Tolerance=1e-7;
    value.Input.Parts={Rectangle("A",11),Rectangle("B",7),Rectangle("C",6)};
    value.Input.Stocks={{"sheet",6.5,3.125,stocks,1,"steel"}};
    Layout board{"sheet",0,6.5,3.125,{},1e20};
    board.Placements={{"A",0,{.0625,.0625,0},42,false},{"A",1,{1.6875,.0625,0},42,false},
        {"B",0,{3.3125,.0625,0},0,false},{"C",0,{4.9375,.0625,0},77,true}};
    value.Output.Layouts.reserve(1);value.Output.Layouts.push_back(std::move(board));
    value.Output.Stats.CombinationGroups=1;value.Output.Stats.GapFilledParts=1;value.Used={2,1,1};Reconcile(value);return value;
}
Options Budget(std::size_t attempts=1000)
{Options options;options.TimeLimitSeconds=5;options.MaxPlacementAttempts=attempts;return options;}
Detail::SheetTemplateReuseReport Repeat(Fixture& value,Options options=Budget(),double utilization=.85)
{
    // The reference intentionally aliases the vector which expansion grows.
    return Detail::AppendSheetTemplateCopies(value.Input,value.Output.Layouts.back(),value.Input.Stocks.front(),
        value.Stock,value.Used,value.Group,value.Output,options,utilization);
}
void Valid(const Fixture& value)
{
    const auto checked=Validate(value.Input,value.Output);
    if(!checked.Valid)for(const auto& error:checked.Errors)std::cerr<<error<<'\n';
    Require(checked.Valid,"repeated boards must conserve valid original demand");
    std::vector<std::size_t> counts(value.Input.Parts.size());
    for(const auto& board:value.Output.Layouts)for(const auto& item:board.Placements)
        for(std::size_t i=0;i<value.Input.Parts.size();++i)if(value.Input.Parts[i].ID==item.PartID)++counts[i];
    Require(counts==value.Used,"committed identity counts must match used inventory");
}
void OriginalPoses(const Fixture& value)
{
    const auto source=value.Output.Layouts.front();
    std::set<std::size_t> groupIDs{42,77};
    for(std::size_t i=1;i<value.Output.Layouts.size();++i)
    {
        const auto& board=value.Output.Layouts[i];
        Require(board.StockInstance==i&&board.Placements.size()==source.Placements.size(),"each copy needs its own physical stock identity");
        Require(board.MaterialArea==18,"copy material area must come from original contours, not source MaterialArea metadata");
        std::map<std::size_t,std::size_t> groups;
        for(std::size_t j=0;j<board.Placements.size();++j)
        {
            const auto& old=source.Placements[j];const auto& item=board.Placements[j];
            Require(item.PartID==old.PartID&&item.Transform.X==old.Transform.X&&item.Transform.Y==old.Transform.Y&&
                item.Transform.AngleDegrees==old.Transform.AngleDegrees&&item.Transform.Mirrored==old.Transform.Mirrored&&item.GapFiller==old.GapFiller,
                "whole-board copies must preserve original geometry, allowed poses and filler provenance");
            if(!old.Group)Require(item.Group==0,"zero planning tags must remain zero");
            else
            {
                const auto [it,inserted]=groups.emplace(old.Group,item.Group);
                Require(it->second==item.Group&&item.Group>0,"one source group must map consistently inside its copy");
                if(inserted)Require(groupIDs.insert(item.Group).second,"positive group tags must be fresh on each physical board");
            }
        }
    }
}
bool SameLayout(const Layout& a,const Layout& b)
{
    if(a.StockID!=b.StockID||a.StockInstance!=b.StockInstance||a.Width!=b.Width||a.Height!=b.Height||
        a.MaterialArea!=b.MaterialArea||a.Placements.size()!=b.Placements.size())return false;
    for(std::size_t i=0;i<a.Placements.size();++i)
    {
        const auto& x=a.Placements[i];const auto& y=b.Placements[i];
        if(x.PartID!=y.PartID||x.Instance!=y.Instance||x.Group!=y.Group||x.GapFiller!=y.GapFiller||
            x.Transform.X!=y.Transform.X||x.Transform.Y!=y.Transform.Y||
            x.Transform.AngleDegrees!=y.Transform.AngleDegrees||x.Transform.Mirrored!=y.Transform.Mirrored)return false;
    }
    return true;
}
void CopyObservationPreservesAtomicOriginalMapping()
{
    auto observed=MixedRows();auto plain=MixedRows();const auto source=observed.Output.Layouts.front();
    std::vector<ConstructionTraceEvent> events;auto options=Budget(4);
    options.ConstructionTraceRoute="actual-copy-route";options.ConstructionTraceScope="actual-copy-scope";
    options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
    options.ConstructionTrace=[&](const ConstructionTraceEvent& event){events.push_back(event);};
    const auto report=Repeat(observed,options),control=Repeat(plain,Budget(4));
    Require(report.Copies==1&&report.Parts==4&&report.Attempts==4&&report.Truncated&&
        report.Copies==control.Copies&&report.Parts==control.Parts&&report.Attempts==control.Attempts&&
        report.Cancelled==control.Cancelled&&report.Truncated==control.Truncated,
        "copy observation changed the real atomic inventory or attempt outcome");
    Require(events.size()==1&&!options.ConstructionObservation->Failed.load(),
        "a complete copy must have exactly one observed operation and no incomplete-draft event");
    const auto& event=events.front();
    Require(event.Source=="sheet-template.copy"&&event.Stage==ConstructionTraceStage::Commit&&
        event.Route==options.ConstructionTraceRoute&&event.Scope==options.ConstructionTraceScope&&
        event.Accepted&&event.Members.empty()&&event.Layouts.size()==2&&event.StockIndex==0&&
        event.StockInstance==1&&event.BoardIndex==1&&event.Attempts==4,
        "copy observation lost the real route, physical target or whole-board operation boundary");
    const auto measure=[&](const char* name)
    {for(const auto& value:event.Measures)if(value.first==name)return value.second;return -1.;};
    Require(measure("sourceLayoutIndex")==0&&measure("targetLayoutIndex")==1&&
        SameLayout(event.Layouts[0],source)&&SameLayout(event.Layouts[1],observed.Output.Layouts.back()),
        "copy snapshots do not retain the exact ordered source and committed identity/group mapping");
    Require(observed.Used==plain.Used&&observed.Stock==plain.Stock&&observed.Group==plain.Group&&
        observed.Output.Layouts.size()==plain.Output.Layouts.size(),
        "copy observation changed the committed original or physical stock counters");
    for(std::size_t i=0;i<observed.Output.Layouts.size();++i)
        Require(SameLayout(observed.Output.Layouts[i],plain.Output.Layouts[i]),
            "copy observation changed actual placement coordinates or original identities");
    Valid(observed);OriginalPoses(observed);

    auto partial=MixedRows();events.clear();options.MaxPlacementAttempts=1;
    const auto unfinished=Repeat(partial,options);
    Require(unfinished.Copies==0&&events.empty()&&partial.Output.Layouts.size()==1,
        "an uncommitted partial copy draft was observed as a real board operation");

    auto failed=MixedRows();auto failing=Budget();std::size_t calls=0;
    failing.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
    failing.ConstructionTrace=[&](const ConstructionTraceEvent&)
        {++calls;throw std::runtime_error("intentional observation failure");};
    const auto unaffected=Repeat(failed,failing);
    Require(calls==1&&failing.ConstructionObservation->Failed.load()&&unaffected.Copies==4&&
        unaffected.Attempts==16&&!unaffected.Cancelled&&!unaffected.Truncated,
        "an observation callback failure changed copy construction or budget semantics");
    Valid(failed);OriginalPoses(failed);
}
}

int main()try
{
    CopyObservationPreservesAtomicOriginalMapping();
    auto mixed=MixedRows();Valid(mixed);
    const auto result=Repeat(mixed);
    Require(result.Copies==4&&result.Parts==16&&result.Attempts==16&&!result.Cancelled&&!result.Truncated,
        "2A+1B+1C usage vector must count additional copies after the already committed first board");
    Require(mixed.Output.Layouts.size()==5&&mixed.Stock==5&&mixed.Used==std::vector<std::size_t>({10,5,5}),
        "mixed repetitions exceeded their limiting specification");
    Require(mixed.Output.Remaining.size()==3&&mixed.Output.Remaining[0].Quantity==1&&mixed.Output.Remaining[1].Quantity==2&&
        mixed.Output.Remaining[2].Quantity==1,"nonintegral residual demand must remain available for tail construction");
    Require(mixed.Output.Stats.CombinationGroups==5&&mixed.Output.Stats.GapFilledParts==5,
        "repetition statistics must count each actual repeated group and filler");
    Valid(mixed);OriginalPoses(mixed);

    auto reflected=MixedRows();reflected.Input.Parts[0].AllowMirror=true;
    for(auto& item:reflected.Output.Layouts[0].Placements)if(item.PartID=="A")
    {item.Transform.Mirrored=true;item.Transform.Y+=3;}
    Valid(reflected);const auto mirrorCopies=Repeat(reflected);
    Require(mirrorCopies.Copies==4,"legal mirrored source board must remain reusable");
    Valid(reflected);OriginalPoses(reflected);

    auto parityMismatch=MixedRows();parityMismatch.Input.Parts[0].AllowMirror=true;
    auto differentParity=parityMismatch.Output.Layouts[0];differentParity.Placements[0].Transform.Mirrored=true;
    const auto mismatchedRecipe=Detail::AppendSheetTemplateCopies(parityMismatch.Input,differentParity,parityMismatch.Input.Stocks[0],
        parityMismatch.Stock,parityMismatch.Used,parityMismatch.Group,parityMismatch.Output,Budget(),.85);
    Require(mismatchedRecipe.Copies==0&&parityMismatch.Output.Layouts.size()==1,
        "same XY and angle with different mirror parity must not match the committed source board");

    auto scarce=MixedRows();scarce.Input.Parts[0].Quantity=3;Reconcile(scarce);
    const auto noCopy=Repeat(scarce);
    Require(noCopy.Copies==0&&noCopy.Attempts==0&&scarce.Output.Layouts.size()==1&&scarce.Used[0]==2,
        "a scarce member cannot be rounded up, omitted or overproduced to repeat a board");Valid(scarce);

    auto inventory=MixedRows(2);const auto limitedStock=Repeat(inventory);
    Require(limitedStock.Copies==1&&inventory.Stock==2&&inventory.Output.Layouts.size()==2,
        "the same-stock physical inventory must cap additional copies");Valid(inventory);

    auto different=MixedRows();different.Input.Stocks.push_back({"other",6.5,3.125,100,1,"steel"});
    auto otherStock=0u;std::size_t otherNext=otherStock;
    const auto incompatible=Detail::AppendSheetTemplateCopies(different.Input,different.Output.Layouts.front(),different.Input.Stocks.back(),
        otherNext,different.Used,different.Group,different.Output,Budget());
    Require(incompatible.Copies==0&&incompatible.Attempts==0&&different.Output.Layouts.size()==1&&otherNext==0,
        "equal dimensions and material must not silently change the source stock specification");Valid(different);
    auto wrongDimensions=different.Input.Stocks.front();wrongDimensions.Width+=1;
    const auto dimensions=Detail::AppendSheetTemplateCopies(different.Input,different.Output.Layouts.front(),wrongDimensions,
        different.Stock,different.Used,different.Group,different.Output,Budget());
    Require(dimensions.Copies==0&&different.Output.Layouts.size()==1,"a modified same-ID stock must not reuse a source board");

    auto low=MixedRows();low.Input.Stocks[0].Width=low.Output.Layouts[0].Width=100;
    low.Input.Stocks[0].Height=low.Output.Layouts[0].Height=100;
    const auto gated=Repeat(low);
    Require(gated.Copies==0&&gated.Attempts==0&&low.Output.Layouts.size()==1,
        "forged source MaterialArea cannot bypass the true-material utilization gate");Valid(low);
    auto noStock=MixedRows(1);const auto emptyCapacity=Repeat(noStock);
    Require(emptyCapacity.Copies==0&&emptyCapacity.Attempts==0,"zero repetition capacity must not spend copy attempts");Valid(noStock);

    auto invalid=MixedRows();invalid.Output.Layouts[0].Placements[2].Transform.X=1;
    const auto rejected=Repeat(invalid);
    Require(rejected.Copies==0&&rejected.Attempts==0&&invalid.Output.Layouts.size()==1,
        "an overlapping source board must fail original-material validation before expansion");

    for(const std::size_t cap:{1u,4u,16u})
    {
        auto bounded=MixedRows();const auto work=Repeat(bounded,Budget(cap));
        const auto expected=cap/4;
        Require(work.Copies==expected&&work.Parts==4*expected&&work.Attempts<=cap&&bounded.Output.Layouts.size()==1+expected,
            "attempt exhaustion must publish only complete physical board copies");
        Require(work.Cancelled==false&&work.Truncated==(cap<16),"attempt exhaustion is truncation, not user cancellation");
        Require(bounded.Used==std::vector<std::size_t>({2+2*expected,1+expected,1+expected}),
            "a discarded copy draft consumed original inventory");Valid(bounded);
    }
    auto stopped=MixedRows();auto cancel=Budget();cancel.Cancel=[&]{return stopped.Output.Layouts.size()>=2;};
    const auto cancelled=Repeat(stopped,cancel);
    Require(cancelled.Cancelled&&!cancelled.Truncated&&cancelled.Copies==1&&stopped.Output.State==Status::Cancelled&&stopped.Output.Layouts.size()==2,
        "cancellation must retain committed whole copies and identify only a true callback cancellation");Valid(stopped);
    auto timed=MixedRows();auto expired=Budget();expired.TimeLimitSeconds=0;
    const auto timeout=Repeat(timed,expired);
    Require(timeout.Truncated&&!timeout.Cancelled&&timeout.Copies==0&&timed.Output.Layouts.size()==1,
        "time exhaustion must retain the source board without masquerading as cancellation");Valid(timed);

    Fixture polygons;polygons.Input.PartGap=.25;polygons.Input.EdgeMargin=.125;polygons.Input.Tolerance=1e-7;
    Part left;left.ID="sloped-left";left.Quantity=3;left.Material="steel";left.FreeRotation=false;left.AllowedAnglesDegrees={0};
    left.Geometry.Outer={{0,0},{6,0},{0,6}};
    auto right=left;right.ID="sloped-right";right.AllowedAnglesDegrees={180};
    polygons.Input.Parts={left,right};polygons.Input.Stocks={{"sheet",6.5,6.5,5,1,"steel"}};
    polygons.Output.Layouts={{"sheet",0,6.5,6.5,{{left.ID,0,{.125,.125,0},3,false},{right.ID,0,{6.375,6.375,180},3,true}},0}};
    polygons.Used={1,1};polygons.Group=4;Reconcile(polygons);Valid(polygons);
    const auto irregular=Repeat(polygons);
    Require(irregular.Copies==2&&polygons.Output.Layouts.size()==3&&Validate(polygons.Input,polygons.Output).Complete,
        "real complementary sloped originals at positive clearance must repeat without a rectangle-only restriction");
    for(std::size_t i=1;i<polygons.Output.Layouts.size();++i)
        Require(polygons.Output.Layouts[i].MaterialArea==36,"sloped copies must preserve true original material area");Valid(polygons);

    std::cout<<"SheetTemplateReuseTests passed: mixed sheet recipes, residual demand, physical stock, original identity, positive-gap polygons and atomic budgets\n";
    return 0;
}
catch(const std::exception& error){std::cerr<<"SheetTemplateReuseTests failed: "<<error.what()<<'\n';return 1;}
