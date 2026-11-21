#include "SheetNestingPlaneStrategies.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value,const char* message)
{if(!value)throw std::runtime_error(message);}
Part Rectangle(const char* id,double width,double height,std::size_t quantity)
{
    Part part;part.ID=id;part.Quantity=quantity;part.FreeRotation=false;part.AllowedAnglesDegrees={0};
    part.Geometry.Outer={{0,0},{width,0},{width,height},{0,height}};return part;
}
Options Limits()
{
    Options options;options.TimeLimitSeconds=5;options.MaxPlacementAttempts=100000;
    options.EnableCombinations=false;options.EnableGapFill=false;options.EnableBasicCompaction=false;return options;
}
Result Construct(const Problem& problem,bool reuse,Options options=Limits())
{
    PlaneStrategySettings settings;settings.Kind=PlaneStrategyKind::ArrayMultiSpecificationSearch;
    settings.MaxNodes=8;settings.EnableSheetTemplateReuse=reuse;
    settings.EnableContourSettling=false;settings.EnableAllowedAngleSettling=false;
    auto result=MakePlaneStrategyFactory(settings)()->Run(problem,options);
    Require(Validate(problem,result).Valid,"reuse-aware search must conserve original identities, geometry and demand");
    Require(result.Stats.PlacementAttempts<=options.MaxPlacementAttempts,"reuse-aware search exceeded the shared attempt cap");
    return result;
}
std::size_t Placed(const Result& result)
{
    std::size_t count=0;for(const auto& board:result.Layouts)count+=board.Placements.size();return count;
}
std::string Signature(const Result& result)
{
    std::ostringstream value;value.precision(17);
    for(const auto& board:result.Layouts)
    {
        value<<board.StockID<<':'<<board.StockInstance<<';';
        for(const auto& item:board.Placements)
            value<<item.PartID<<':'<<item.Instance<<':'<<item.Transform.X<<':'<<item.Transform.Y<<':'
                <<item.Transform.AngleDegrees<<':'<<item.Group<<':'<<item.GapFiller<<';';
    }
    return value.str();
}
void Inventory(const Problem& problem,const Result& result)
{
    std::map<std::string,std::set<std::size_t>> identities;
    for(const auto& board:result.Layouts)for(const auto& item:board.Placements)
        Require(identities[item.PartID].insert(item.Instance).second,"a repeated recipe duplicated an original instance");
    for(const auto& part:problem.Parts)
    {
        Require(identities[part.ID].size()==part.Quantity,"complete repeated recipe lost original demand");
        std::size_t expected=0;for(const auto instance:identities[part.ID])
            Require(instance==expected++,"repeated original identities are not a complete inventory prefix");
    }
}
}

int main()try
{
    // Identical one-unit rectangles give an exact material bound: every full
    // physical sheet contains sixteen originals. Reuse may save search work,
    // but must retain the same complete quality for both balanced and uneven
    // orders, including a real partial tail sheet.
    for(const auto quantities:{std::pair<std::size_t,std::size_t>{64,64},{65,67}})
    {
        Problem lot;lot.Parts={Rectangle("left",1,1,quantities.first),Rectangle("right",1,1,quantities.second)};
        lot.Stocks={{"sheet",4,4,20}};
        const auto cold=Construct(lot,false),reused=Construct(lot,true);
        const auto lower=(quantities.first+quantities.second+15)/16;
        Require(cold.State==Status::Complete&&reused.State==Status::Complete&&
            cold.Layouts.size()==lower&&reused.Layouts.size()==lower,
            "representative-board planning must preserve complete batch quality and the nonmultiple tail");
        Inventory(lot,reused);
    }

    // The old per-physical-sheet share is one attempt for this large order.
    // It cannot even validate an initial four-piece column. The bounded
    // startup must instead produce a real full-sheet recipe before granting
    // it a repeat-weighted search share. No wall-clock threshold is asserted.
    Problem large;large.Parts={Rectangle("many-a",1,1,50000),Rectangle("many-b",1,1,50000)};
    large.Stocks={{"sheet",4,4,20000}};
    auto startup=Limits();startup.MaxPlacementAttempts=4096;
    const auto starved=Construct(large,false,startup),bootstrapped=Construct(large,true,startup);
    Require(starved.Layouts.empty(),"control must expose the tiny physical-board allocation");
    Require(!bootstrapped.Layouts.empty()&&bootstrapped.Layouts.front().MaterialArea==16&&
        bootstrapped.Layouts.front().Placements.size()==16&&Placed(bootstrapped)>Placed(starved),
        "large repeated demand must obtain a feasible high-density representative board");
    Require(bootstrapped.State==Status::Partial&&!bootstrapped.Remaining.empty(),
        "a bounded representative run must not claim completion of an unexpanded large order");

    // A first-specification rare board cannot earn a repeated-recipe reward.
    // Its actual original remains present once, then abundant demand receives
    // its own opportunity. Copy capacity is never inferred from the largest
    // quantity or a common divisor of unrelated specifications.
    Problem scarce;scarce.Parts={Rectangle("rare",4,4,1),Rectangle("many",1,1,99999)};
    scarce.Stocks={{"sheet",4,4,10000}};
    auto rareSettings=PlaneStrategySettings{};rareSettings.Kind=PlaneStrategyKind::ArrayFirstSpecificationSearch;
    rareSettings.MaxNodes=8;rareSettings.EnableSheetTemplateReuse=true;
    rareSettings.EnableContourSettling=false;rareSettings.EnableAllowedAngleSettling=false;
    const auto rare=MakePlaneStrategyFactory(rareSettings)()->Run(scarce,startup);
    Require(Validate(scarce,rare).Valid&&rare.Stats.PlacementAttempts<=startup.MaxPlacementAttempts,
        "rare-recipe bootstrap must retain shared limits and demand");
    std::size_t rareCount=0;for(const auto& board:rare.Layouts)for(const auto& item:board.Placements)rareCount+=item.PartID=="rare";
    Require(!rare.Layouts.empty()&&rare.Layouts.front().Placements.size()==1&&
        rare.Layouts.front().Placements.front().PartID=="rare"&&rareCount==1,
        "an abundant unrelated specification must never amplify a scarce recipe");

    // Stock with no second instance and all-quantity-one problems retain the
    // exact original search path, rather than merely a similar final density.
    Problem oneStock;oneStock.Parts={Rectangle("a",1,1,64),Rectangle("b",1,1,64)};
    oneStock.Stocks={{"sheet",4,4,1}};
    Require(Signature(Construct(oneStock,false))==Signature(Construct(oneStock,true)),
        "one-stock search must not receive representative-board bootstrap or amplification");
    Problem unique;unique.Parts={Rectangle("one-a",1,2,1),Rectangle("one-b",2,2,1),Rectangle("one-c",3,2,1)};
    unique.Stocks={{"sheet",8,4,4}};
    Require(Signature(Construct(unique,false))==Signature(Construct(unique,true)),
        "quantity-one search must retain its original candidate path");

    auto cancelled=Limits();cancelled.Cancel=[]{return true;};
    const auto stopped=Construct(large,true,cancelled);
    Require(stopped.State==Status::Cancelled&&stopped.Layouts.empty()&&stopped.Remaining.size()==large.Parts.size(),
        "cancelled representative planning must retain all demand");
    for(std::size_t cap:{1u,30u,200u})
    {
        auto bounded=Limits();bounded.MaxPlacementAttempts=cap;
        const auto output=Construct(large,true,bounded);
        Require(output.Stats.PlacementAttempts<=cap,"bootstrap or amplified search overran a small caller budget");
    }
    std::cout<<"PlaneReuseBudgetTests passed: actual-recipe weighting, bounded startup, scarce stock, tails and shared limits\n";
    return 0;
}
catch(const std::exception& error){std::cerr<<"PlaneReuseBudgetTests failed: "<<error.what()<<'\n';return 1;}
