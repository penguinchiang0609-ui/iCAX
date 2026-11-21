#include "SheetNestingPlaneDomains.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Part Tile(std::size_t count,double side=2)
{
    Part part;part.ID="production-tile";part.Quantity=count;part.Material="steel";
    part.FreeRotation=false;part.AllowedAnglesDegrees={0};part.Geometry.Outer={{0,0},{side,0},{side,side},{0,side}};return part;
}
Options Budget(std::size_t attempts=1000000)
{
    Options value;value.TimeLimitSeconds=10;value.MaxPlacementAttempts=attempts;
    value.EnableBasicCompaction=false;value.EnableCombinations=false;return value;
}
std::size_t DiagnosticCount(const Result& result,const std::string& marker)
{
    for(const auto& line:result.Diagnostics)
    {
        const auto position=line.find(marker);
        if(position!=std::string::npos)return static_cast<std::size_t>(std::stoull(line.substr(position+marker.size())));
    }
    return 0;
}
void Valid(const PlaneDomainTask& task,const Result& result)
{
    const auto checked=ValidatePlaneDomainLayout(task,result);
    if(!checked.Valid)for(const auto& error:checked.Errors)std::cerr<<error<<'\n';
    Require(checked.Valid,"domain result violated real material, original demand or stock identity");
    std::set<std::pair<std::string,std::size_t>> originals,stocks;
    for(const auto& board:result.Layouts)
    {
        Require(stocks.emplace(board.StockID,board.StockInstance).second,"physical stock was reused twice");
        for(const auto& placed:board.Placements)
            Require(originals.emplace(placed.PartID,placed.Instance).second,"original instance was used twice");
        // These fixtures use identical unrotated rectangles. Audit actual
        // stored coordinates independently of the validator's tolerance.
        const auto side=task.Input.Parts.front().Geometry.Outer[1].X;
        for(std::size_t i=0;i<board.Placements.size();++i)for(std::size_t j=i+1;j<board.Placements.size();++j)
        {
            const auto a=board.Placements[i].Transform,b=board.Placements[j].Transform;
            const auto dx=std::max({0.,a.X-b.X-side,b.X-a.X-side});
            const auto dy=std::max({0.,a.Y-b.Y-side,b.Y-a.Y-side});
            Require(std::hypot(dx,dy)+1e-12>=task.Input.PartGap,"domain clearance fell below the nominal original gap");
        }
    }
}
PlaneDomainTask Narrow(std::size_t count=9,std::size_t sheets=3)
{
    PlaneDomainTask task;task.Input.Tolerance=1e-7;task.Input.PartGap=.2;task.Input.EdgeMargin=.1;
    task.Input.Parts={Tile(count)};task.Input.Stocks={{"offset-material",10,3,sheets,1,"steel"}};
    PlaneStockDomain domain;domain.StockID="offset-material";
    // Three originals fit with the nominal pair gap. Domain barriers must not
    // receive an additional pair gap beyond the declared edge margin.
    domain.Boundary.Outer={{0,0},{6.60002,0},{6.60002,2.20002},{0,2.20002}};
    domain.InnerRectangles={{.1,.1,6.40002,2.00002}};task.Domains={domain};return task;
}
PlaneDomainTask Elbow()
{
    PlaneDomainTask task;task.Input.Tolerance=1e-7;task.Input.Parts={Tile(3,4)};
    task.Input.Stocks={{"two-arms",10,10,1,1,"steel"}};task.UseInnerRectangle=true;task.EnablePeripheralFill=false;
    PlaneStockDomain domain;domain.StockID="two-arms";
    domain.Boundary.Outer={{0,0},{10,0},{10,4},{4,4},{4,10},{0,10}};
    domain.InnerRectangles={{0,0,10,4},{0,0,4,10}};task.Domains={domain};return task;
}
std::set<std::pair<std::string,std::size_t>> Identities(const Result& result)
{
    std::set<std::pair<std::string,std::size_t>> values;
    for(const auto& board:result.Layouts)for(const auto& item:board.Placements)values.emplace(item.PartID,item.Instance);
    return values;
}
}

int main()try
{
    for(const bool inner:{false,true})
    {
        auto task=Narrow();task.UseInnerRectangle=inner;
        const auto result=RunPlaneDomainTask(task,Budget());Valid(task,result);
        Require(ValidatePlaneDomainLayout(task,result).Complete&&result.Layouts.size()==3&&DiagnosticCount(result,"copies=")==2,
            "positive-gap real-domain representative must fit three originals and repeat twice");
        for(const auto& board:result.Layouts)
        {
            Require(board.Placements.size()==3,"boundary neutral obstacles incorrectly added inter-part gap at the material edge");
            for(const auto& item:board.Placements)
            {Require(item.Transform.X>=.1&&item.Transform.Y>=.1,"original domain margin was reduced");Require(item.Transform.X+2<=6.50002+1e-10&&item.Transform.Y+2<=2.10002+1e-10,"part escaped the narrow real material");}
        }
        auto tail=Narrow(7,3);tail.UseInnerRectangle=inner;
        const auto remainder=RunPlaneDomainTask(tail,Budget());Valid(tail,remainder);
        Require(ValidatePlaneDomainLayout(tail,remainder).Complete&&remainder.Layouts.size()==3&&remainder.Layouts.back().Placements.size()==1,
            "positive-gap domain tail must be reconstructed without overproducing a repeated recipe");
        auto shortage=Narrow(10,2);shortage.UseInnerRectangle=inner;
        const auto partial=RunPlaneDomainTask(shortage,Budget());Valid(shortage,partial);
        Require(partial.State==Status::Partial&&partial.Layouts.size()==2&&partial.Remaining.size()==1&&partial.Remaining[0].Quantity==4,
            "positive-gap stock limit lost exact remaining demand");
    }

    auto independent=Elbow();independent.CombineInnerRectangles=false;independent.EnableInnerPrefill=false;
    const auto single=RunPlaneDomainTask(independent,Budget());Valid(independent,single);
    Require(single.State==Status::Partial&&single.Layouts.size()==1&&single.Layouts.front().Placements.size()==2,
        "independent rectangle candidates must retain their best legal two-part baseline");
    auto joint=Elbow();const auto combined=RunPlaneDomainTask(joint,Budget());Valid(joint,combined);
    Require(ValidatePlaneDomainLayout(joint,combined).Complete&&combined.Layouts.size()==1&&combined.Layouts.front().Placements.size()==3&&DiagnosticCount(combined,"joint-plans=")>0,
        "joint certified rectangles must use both material arms with one shared original inventory");
    auto prefill=Elbow();prefill.CombineInnerRectangles=false;
    const auto prefilled=RunPlaneDomainTask(prefill,Budget());Valid(prefill,prefilled);
    Require(ValidatePlaneDomainLayout(prefill,prefilled).Complete&&DiagnosticCount(prefilled,"prefill-plans=")>0,
        "smaller real material regions must prefill before the independently counted main rectangle");

    PlaneDomainTask sloped;sloped.Input.Tolerance=1e-7;sloped.Input.Parts={Tile(4,1)};
    sloped.Input.Stocks={{"sloped-material",10,10,1,1,"steel"}};sloped.UseInnerRectangle=true;sloped.EnablePeripheralFill=false;
    PlaneStockDomain triangle;triangle.StockID="sloped-material";triangle.Boundary.Outer={{0,0},{10,0},{10,10}};
    triangle.InnerRectangles={{4,1,5,2}};sloped.Domains={triangle};
    auto stationary=sloped;stationary.RigidShiftSteps=0;
    const auto unshifted=RunPlaneDomainTask(stationary,Budget());Valid(stationary,unshifted);
    const auto aligned=RunPlaneDomainTask(sloped,Budget());Valid(sloped,aligned);
    Require(ValidatePlaneDomainLayout(sloped,aligned).Complete&&DiagnosticCount(aligned,"rigid-shifts=")>0,
        "automatic finite rigid alignment must use true sloped material instead of an arbitrary fixed translation");
    Require(aligned.Layouts.front().Placements.size()==unshifted.Layouts.front().Placements.size(),"rigid main alignment changed its original member set");
    const auto dx=aligned.Layouts.front().Placements.front().Transform.X-unshifted.Layouts.front().Placements.front().Transform.X;
    const auto dy=aligned.Layouts.front().Placements.front().Transform.Y-unshifted.Layouts.front().Placements.front().Transform.Y;
    Require(dx<0&&dy<0,"main group did not align down and left inside the real sloped boundary");
    for(std::size_t i=0;i<aligned.Layouts.front().Placements.size();++i)
    {
        const auto a=aligned.Layouts.front().Placements[i].Transform,b=unshifted.Layouts.front().Placements[i].Transform;
        Require(std::abs((a.X-b.X)-dx)<1e-10&&std::abs((a.Y-b.Y)-dy)<1e-10,"rigid alignment changed internal relative positions");
    }

    auto different=Narrow(12,2);different.Input.Stocks[0].ID="left-domain";different.Domains[0].StockID="left-domain";
    different.Input.Stocks.push_back({"right-domain",10,3,2,1,"steel"});
    auto right=different.Domains[0];right.StockID="right-domain";
    right.Boundary.Outer={{3.39998,0},{10,0},{10,2.20002},{3.39998,2.20002}};
    right.InnerRectangles={{3.49998,.1,6.40002,2.00002}};different.Domains.push_back(right);
    const auto domains=RunPlaneDomainTask(different,Budget());Valid(different,domains);
    Require(ValidatePlaneDomainLayout(different,domains).Complete&&domains.Layouts.size()==4&&DiagnosticCount(domains,"copies=")==2,
        "same rectangular envelope cannot authorize a positive-gap recipe on a different real domain");
    for(const auto& board:domains.Layouts)if(board.StockID=="right-domain")
        for(const auto& item:board.Placements)Require(item.Transform.X>=3.49998-1e-10,"right domain inherited the left domain's coordinates");

    PlaneDomainTask perforated;perforated.Input.Tolerance=1e-7;perforated.Input.PartGap=.2;
    perforated.Input.Parts={Tile(4,1.8)};perforated.Input.Stocks={{"holed-defective",6,6,1,1,"steel"}};
    PlaneStockDomain material;material.StockID="holed-defective";material.Boundary.Outer={{0,0},{6,0},{6,6},{0,6}};
    material.Boundary.Holes={{{2,2},{4,2},{4,4},{2,4}}};Shape defect;defect.Outer={{4,0},{6,0},{6,2},{4,2}};
    material.Obstacles={defect};material.InnerRectangles={{0,0,2,6},{0,4,6,2}};perforated.Domains={material};
    for(const bool inner:{false,true})
    {
        perforated.UseInnerRectangle=inner;const auto result=RunPlaneDomainTask(perforated,Budget());Valid(perforated,result);
        Require(ValidatePlaneDomainLayout(perforated,result).Complete,"positive-gap perimeter fill must preserve real stock holes and fixed defects");
        auto inHole=result;inHole.Layouts[0].Placements[0].Transform={2,2,0};
        Require(!ValidatePlaneDomainLayout(perforated,inHole).Valid,"typed verification accepted forbidden stock-hole material");
        auto inDefect=result;inDefect.Layouts[0].Placements[0].Transform={4,0,0};
        Require(!ValidatePlaneDomainLayout(perforated,inDefect).Valid,"typed verification accepted fixed defect material");
    }

    PlaneDomainTask optimize;optimize.Input.Tolerance=1e-7;optimize.Input.PartGap=.2;
    optimize.Input.Parts={Tile(3)};optimize.Input.Stocks={{"fixed-domain",8,6,2,1,"steel"}};
    PlaneStockDomain fixed;fixed.StockID="fixed-domain";fixed.Boundary.Outer={{1,1},{7,1},{7,5},{1,5}};
    optimize.Domains={fixed};Result seed;seed.State=Status::Partial;
    Layout board{"fixed-domain",1,8,6,{},8};
    board.Placements={{"production-tile",0,{1.4,1.4,0},17,true},{"production-tile",2,{4.2,2.7,0},23,false}};
    seed.Layouts={board};seed.Remaining={{"production-tile",1}};Valid(optimize,seed);
    PlaneDomainOptimizationSettings setting;setting.Reconstruct=false;
    const auto tightened=OptimizePlaneDomainLayout(optimize,seed,Budget(),setting);Valid(optimize,tightened);
    Require(Identities(tightened)==Identities(seed)&&tightened.Layouts.size()==1&&tightened.Layouts[0].StockInstance==1,
        "typed optimization changed fixed original or stock identities");
    Require(DiagnosticCount(tightened,"accepted=")>0,"typed real-domain event compaction did not improve a movable layout");
    for(const auto& item:tightened.Layouts[0].Placements)
    {const auto old=std::find_if(seed.Layouts[0].Placements.begin(),seed.Layouts[0].Placements.end(),[&](const Placement& value){return value.Instance==item.Instance;});Require(old!=seed.Layouts[0].Placements.end()&&old->GapFiller==item.GapFiller&&old->Group==item.Group,"typed compaction changed provenance or planning tags");}
    setting.Reconstruct=true;const auto rebuilt=OptimizePlaneDomainLayout(optimize,seed,Budget(),setting);Valid(optimize,rebuilt);
    Require(Identities(rebuilt)==Identities(seed)&&rebuilt.Layouts[0].StockInstance==1,"typed reconstruction did not restore noncontiguous original and physical-board identities");

    for(const bool inner:{false,true})
    {
        auto task=Narrow();task.UseInnerRectangle=inner;
        auto cancelled=Budget();cancelled.Cancel=[]{return true;};const auto stopped=RunPlaneDomainTask(task,cancelled);Valid(task,stopped);
        Require(stopped.State==Status::Cancelled&&stopped.Layouts.empty()&&stopped.Remaining.size()==1&&stopped.Remaining[0].Quantity==9,"domain cancellation lost committed inventory semantics");
        for(const std::size_t cap:{1u,30u,200u})
        {const auto bounded=RunPlaneDomainTask(task,Budget(cap));Valid(task,bounded);Require(bounded.Stats.PlacementAttempts<=cap&&bounded.State!=Status::Cancelled,"domain candidate/shift/copy work escaped the shared attempt budget");}
    }
    auto cancelled=Budget();cancelled.Cancel=[]{return true;};
    const auto stoppedOptimization=OptimizePlaneDomainLayout(optimize,seed,cancelled);Valid(optimize,stoppedOptimization);
    Require(stoppedOptimization.State==Status::Cancelled&&Identities(stoppedOptimization)==Identities(seed),"cancelled typed optimization failed to retain its fixed valid baseline");
    for(const auto invalid:{-1.,std::numeric_limits<double>::quiet_NaN()})
    {auto task=Narrow();task.MaxRigidShift=invalid;Require(RunPlaneDomainTask(task,Budget()).State==Status::InvalidInput,"invalid domain shift policy was accepted");}
    std::cout<<"PlaneDomainAdvancedTests passed: positive nominal gap and distinct edge margin, multi-rectangle candidates, holes/defects, repeated/tail stock, typed refinement and shared budgets\n";
    return 0;
}
catch(const std::exception& error){std::cerr<<"PlaneDomainAdvancedTests failed: "<<error.what()<<'\n';return 1;}
