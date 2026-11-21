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
Part Tile(std::size_t quantity)
{
    Part part;part.ID="tile-local";part.Quantity=quantity;part.Material="metal";
    part.FreeRotation=false;part.AllowedAnglesDegrees={0};part.Geometry.Outer={{0,0},{2,0},{2,2},{0,2}};return part;
}
Options Budget(std::size_t attempts=200000)
{
    Options options;options.TimeLimitSeconds=10;options.MaxPlacementAttempts=attempts;
    options.EnableBasicCompaction=false;options.EnableCombinations=false;return options;
}
PlaneDomainTask LStock(std::size_t quantity=48,std::size_t stock=3)
{
    PlaneDomainTask task;task.Input.Tolerance=1e-7;task.Input.Parts={Tile(quantity)};
    task.Input.Stocks={{"L-left",10,10,stock,1,"metal"}};
    PlaneStockDomain domain;domain.StockID="L-left";
    domain.Boundary.Outer={{0,0},{10,0},{10,4},{4,4},{4,10},{0,10}};
    domain.InnerRectangles={{0,0,10,4}};task.Domains={domain};return task;
}
PlaneDomainTask HoleAndDefect()
{
    PlaneDomainTask task;task.Input.Tolerance=1e-7;task.Input.Parts={Tile(14)};
    task.Input.Stocks={{"perforated",6,6,2,1,"metal"}};
    PlaneStockDomain domain;domain.StockID="perforated";
    domain.Boundary.Outer={{0,0},{6,0},{6,6},{0,6}};
    domain.Boundary.Holes={{{2,2},{4,2},{4,4},{2,4}}};
    Shape defect;defect.Outer={{4,0},{6,0},{6,2},{4,2}};domain.Obstacles={defect};
    domain.InnerRectangles={{0,0,2,6}};task.Domains={domain};return task;
}
void Valid(const PlaneDomainTask& task,const Result& result)
{
    const auto checked=ValidatePlaneDomainLayout(task,result);
    if(!checked.Valid)for(const auto& error:checked.Errors)std::cerr<<error<<'\n';
    Require(checked.Valid,"repeated domain layouts must obey original demand and the actual stock boundary/holes/obstacles");
    std::set<std::pair<std::string,std::size_t>> parts,stocks;
    for(const auto& board:result.Layouts)
    {
        Require(stocks.emplace(board.StockID,board.StockInstance).second,"physical domain stock identity was repeated");
        for(const auto& item:board.Placements)
            Require(parts.emplace(item.PartID,item.Instance).second,"a domain copy duplicated an original instance");
    }
}
std::size_t Copies(const Result& result)
{
    for(const auto& value:result.Diagnostics)
    {
        if(value.rfind("domain.sheet-template-reuse:",0)!=0)continue;
        const auto position=value.find("copies=");
        Require(position!=std::string::npos,"domain reuse diagnostic lost its physical-copy count");
        return static_cast<std::size_t>(std::stoull(value.substr(position+7)));
    }
    throw std::runtime_error("domain sheet-template reuse diagnostic is missing");
}
void SameCopies(const Layout& source,const Layout& other)
{
    Require(source.StockID==other.StockID&&source.Width==other.Width&&source.Height==other.Height&&
        source.StockInstance!=other.StockInstance&&source.Placements.size()==other.Placements.size(),
        "same-domain copies need distinct physical stock with an unchanged recipe");
    std::map<std::size_t,std::size_t> groups;
    for(std::size_t i=0;i<source.Placements.size();++i)
    {
        const auto& a=source.Placements[i];const auto& b=other.Placements[i];
        Require(a.PartID==b.PartID&&a.Instance!=b.Instance&&a.Transform.X==b.Transform.X&&a.Transform.Y==b.Transform.Y&&
            a.Transform.AngleDegrees==b.Transform.AngleDegrees&&a.GapFiller==b.GapFiller,
            "domain repetition must preserve original coordinates, angles and provenance while assigning new instances");
        if(!a.Group)Require(!b.Group,"zero planning tags must stay zero");
        else
        {
            const auto [where,inserted]=groups.emplace(a.Group,b.Group);(void)inserted;
            Require(where->second==b.Group&&a.Group!=b.Group,"copied domain groups must be fresh and internally consistent");
        }
    }
}
}

int main()try
{
    for(const bool inner:{false,true})
    {
        auto task=LStock();task.UseInnerRectangle=inner;task.MinimumSheetReuseUtilization=.85;
        const auto repeated=RunPlaneDomainTask(task,Budget());Valid(task,repeated);
        Require(ValidatePlaneDomainLayout(task,repeated).Complete&&repeated.Layouts.size()==3&&Copies(repeated)==2,
            "a full 64-square-unit L-domain recipe must repeat for all three available physical sheets");
        for(const auto& board:repeated.Layouts)
            Require(board.Placements.size()==16&&board.MaterialArea==64,"L-domain board did not retain its complete original-material recipe");
        SameCopies(repeated.Layouts[0],repeated.Layouts[1]);SameCopies(repeated.Layouts[0],repeated.Layouts[2]);
        auto independent=task;independent.EnableSheetTemplateReuse=false;
        const auto baseline=RunPlaneDomainTask(independent,Budget());Valid(independent,baseline);
        Require(ValidatePlaneDomainLayout(independent,baseline).Complete&&baseline.Layouts.size()==3&&Copies(baseline)==0,
            "disabling domain repetition must retain independent complete geometric construction");
    }

    auto different=LStock(64,2);
    different.Input.Stocks.push_back({"L-right",10,10,2,1,"metal"});
    PlaneStockDomain right;right.StockID="L-right";
    right.Boundary.Outer={{6,0},{10,0},{10,10},{0,10},{0,6},{6,6}};
    right.InnerRectangles={{0,6,10,4}};different.Domains.push_back(right);
    const auto sides=RunPlaneDomainTask(different,Budget());Valid(different,sides);
    Require(ValidatePlaneDomainLayout(different,sides).Complete&&sides.Layouts.size()==4&&Copies(sides)==2,
        "same-bbox but different-boundary stock IDs each need their own solved recipe");
    std::map<std::string,std::vector<const Layout*>> byStock;
    for(const auto& board:sides.Layouts)byStock[board.StockID].push_back(&board);
    Require(byStock["L-left"].size()==2&&byStock["L-right"].size()==2,"one domain consumed another domain's material count");
    SameCopies(*byStock["L-left"][0],*byStock["L-left"][1]);
    SameCopies(*byStock["L-right"][0],*byStock["L-right"][1]);
    Require(byStock["L-left"][0]->Placements.front().Transform.X!=byStock["L-right"][0]->Placements.front().Transform.X||
        byStock["L-left"][0]->Placements.front().Transform.Y!=byStock["L-right"][0]->Placements.front().Transform.Y,
        "different real domains must not inherit a recipe from equal rectangular dimensions");

    for(const bool inner:{false,true})
    {
        auto task=HoleAndDefect();task.UseInnerRectangle=inner;task.MinimumSheetReuseUtilization=.95;
        const auto repeated=RunPlaneDomainTask(task,Budget());Valid(task,repeated);
        Require(ValidatePlaneDomainLayout(task,repeated).Complete&&repeated.Layouts.size()==2&&Copies(repeated)==1,
            "the utilization gate must use the 28-unit real domain after holes and fixed defects");
        Require(repeated.Layouts[0].Placements.size()==7&&repeated.Layouts[1].Placements.size()==7,
            "hole/defect copies did not retain the complete allowed-material recipe");
        SameCopies(repeated.Layouts[0],repeated.Layouts[1]);
        auto inHole=repeated;inHole.Layouts[1].Placements[0].Transform={2,2,0};
        Require(Validate(task.Input,inHole).Valid&&!ValidatePlaneDomainLayout(task,inHole).Valid,
            "a rectangular-box validator must not certify a repeated part inside a stock hole");
        auto inDefect=repeated;inDefect.Layouts[1].Placements[0].Transform={4,0,0};
        Require(Validate(task.Input,inDefect).Valid&&!ValidatePlaneDomainLayout(task,inDefect).Valid,
            "a rectangular-box validator must not certify a repeated part inside a fixed defect");
    }

    auto stockLimit=LStock(36,2);const auto limited=RunPlaneDomainTask(stockLimit,Budget());Valid(stockLimit,limited);
    Require(limited.State==Status::Partial&&limited.Layouts.size()==2&&Copies(limited)==1&&
        limited.Remaining.size()==1&&limited.Remaining[0].Quantity==4,
        "same-domain physical inventory must leave accurate residual demand without duplicate remaining rows");
    auto tail=LStock(20,3);const auto remainder=RunPlaneDomainTask(tail,Budget());Valid(tail,remainder);
    Require(ValidatePlaneDomainLayout(tail,remainder).Complete&&remainder.Layouts.size()==2&&Copies(remainder)==0&&
        remainder.Layouts[0].Placements.size()==16&&remainder.Layouts[1].Placements.size()==4,
        "nonintegral domain demand must construct its tail instead of overproducing a full recipe");

    for(const double gate:{-.1,1.1,std::numeric_limits<double>::quiet_NaN()})
    {
        auto invalid=LStock();invalid.MinimumSheetReuseUtilization=gate;
        Require(RunPlaneDomainTask(invalid,Budget()).State==Status::InvalidInput,"invalid domain reuse quality policy must be rejected");
    }
    for(const bool changedHole:{false,true})
    {
        auto invalid=HoleAndDefect();auto alternate=invalid.Domains[0];
        if(changedHole)alternate.Boundary.Holes.clear();else alternate.Obstacles.clear();
        invalid.Domains.push_back(alternate);
        Require(RunPlaneDomainTask(invalid,Budget()).State==Status::InvalidInput,
            "one stock identity cannot stand for different holes or defect maps in the same task");
    }
    for(const bool inner:{false,true})
    {
        auto task=LStock();task.UseInnerRectangle=inner;
        auto cancelled=Budget();cancelled.Cancel=[]{return true;};
        const auto stopped=RunPlaneDomainTask(task,cancelled);Valid(task,stopped);
        Require(stopped.State==Status::Cancelled&&stopped.Layouts.empty()&&stopped.Remaining.size()==1&&stopped.Remaining[0].Quantity==48,
            "cancelled domain construction must retain original demand and no fictitious stock copies");
        for(const std::size_t cap:{1u,30u,200u})
        {
            const auto bounded=RunPlaneDomainTask(task,Budget(cap));Valid(task,bounded);
            Require(bounded.Stats.PlacementAttempts<=cap&&bounded.State!=Status::Cancelled,
                "domain copy/search attempt exhaustion is bounded truncation rather than user cancellation");
        }
    }
    std::cout<<"PlaneDomainReuseTests passed: real L/holed/defect domains, same-stock recipes, typed geometry, residual inventory and bounded work\n";
    return 0;
}
catch(const std::exception& error){std::cerr<<"PlaneDomainReuseTests failed: "<<error.what()<<'\n';return 1;}
