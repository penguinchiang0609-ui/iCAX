#include "SheetNestingPlaneSheetMix.h"
#include "NestingInternal.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Part Tile(std::string id,std::size_t quantity)
{
    Part p;p.ID=std::move(id);p.Quantity=quantity;p.Material="mix-material";
    p.Geometry.Outer={{0,0},{1,0},{1,1},{0,1}};p.FreeRotation=false;p.AllowedAnglesDegrees={0};return p;
}
Problem ProblemFor(std::size_t a=8,std::size_t b=8)
{
    Problem p;p.PartGap=.2;p.EdgeMargin=.1;p.Tolerance=1e-7;
    p.Parts={Tile("broad-order-original",a),Tile("narrow-order-original",b)};
    p.Stocks={{"mix-stock",5,1.4,8,1,"mix-material"}};return p;
}
Result LooseMix(const Problem& p,bool uneven=false,bool twoStocks=false)
{
    const std::vector<std::pair<std::size_t,std::size_t>> rows={{2,1},{2,1},{1,2},{1,2},{1,1},uneven?std::pair<std::size_t,std::size_t>{2,0}:std::pair<std::size_t,std::size_t>{1,1}};
    Result result;result.State=Status::Complete;result.GeometryValidated=true;
    std::vector<std::size_t> used(2),stockUsed(p.Stocks.size());
    for(std::size_t bi=0;bi<rows.size();++bi)
    {
        const auto si=twoStocks&&bi>=3?std::size_t{1}:std::size_t{0};
        Layout board{p.Stocks[si].ID,stockUsed[si]++,p.Stocks[si].Width,p.Stocks[si].Height,{},0};
        double x=p.EdgeMargin;
        const std::size_t counts[]={rows[bi].first,rows[bi].second};
        for(std::size_t pi=0;pi<2;++pi)for(std::size_t i=0;i<counts[pi];++i)
        {
            board.Placements.push_back({p.Parts[pi].ID,used[pi]++,{x,p.EdgeMargin,0},17+bi,pi==1});
            x+=1+p.PartGap;board.MaterialArea+=1;
        }
        result.Layouts.push_back(std::move(board));
    }
    Require(Validate(p,result).Complete&&Validate(p,result).Valid,"loose generic recipe fixture is not a complete valid order");return result;
}
// A small generic one-row constructor lets the test control construction
// order while the stage still must discover and jointly choose NEW recipes.
// It has no test identities or hardcoded recipe counts.
class Rows final:public IStrategy
{
public:
    Result Run(const Problem& p,const Options& o)override
    {
        Result result;std::vector<std::size_t> used(p.Parts.size());bool cancelled=false;
        const auto stop=[&]{if(o.Cancel&&o.Cancel())cancelled=true;return cancelled||result.Stats.PlacementAttempts>=o.MaxPlacementAttempts;};
        for(const auto& stock:p.Stocks)for(std::size_t si=0;si<stock.Quantity&&!stop();++si)
        {
            Layout board{stock.ID,si,stock.Width,stock.Height,{},0};double x=p.EdgeMargin;
            for(std::size_t pi=0;pi<p.Parts.size()&&!stop();++pi)
            {
                if(p.Parts[pi].Material!=stock.Material)continue;
                const auto box=Detail::Bounds(p.Parts[pi].Geometry);
                if(box.Height+2*p.EdgeMargin>stock.Height+p.Tolerance)continue;
                while(used[pi]<p.Parts[pi].Quantity&&x+box.Width<=stock.Width-p.EdgeMargin+p.Tolerance&&!stop())
                {
                    ++result.Stats.PlacementAttempts;
                    board.Placements.push_back({p.Parts[pi].ID,used[pi]++,{x-box.X,p.EdgeMargin-box.Y,0},1,false});
                    x+=box.Width+p.PartGap;board.MaterialArea+=Detail::Area(p.Parts[pi].Geometry);
                }
            }
            if(board.Placements.empty())break;result.Layouts.push_back(std::move(board));
        }
        for(std::size_t pi=0;pi<p.Parts.size();++pi)if(used[pi]<p.Parts[pi].Quantity)
            result.Remaining.push_back({p.Parts[pi].ID,p.Parts[pi].Quantity-used[pi]});
        result.State=cancelled?Status::Cancelled:result.Remaining.empty()?Status::Complete:Status::Partial;
        result.GeometryValidated=Validate(p,result).Valid;return result;
    }
};
StrategyFactory Construct(){return []{return std::make_unique<Rows>();};}
Options Budget()
{Options o;o.TimeLimitSeconds=10;o.MaxPlacementAttempts=100000;o.EnableBasicCompaction=false;return o;}
bool Has(const Result& r,const std::string& word)
{return std::any_of(r.Diagnostics.begin(),r.Diagnostics.end(),[&](const auto& line){return line.find(word)!=std::string::npos;});}
void Inventory(const Problem& p,const Result& result)
{
    const auto valid=Validate(p,result);Require(valid.Valid&&valid.Complete&&result.Remaining.empty(),"sheet-mix failed whole-original conservation or physical stock");
    std::map<std::string,std::set<std::size_t>> partInstances,stockInstances;
    std::set<std::size_t> previousGroups;
    for(const auto& board:result.Layouts)
    {
        stockInstances[board.StockID].insert(board.StockInstance);std::set<std::size_t> groups;
        for(const auto& member:board.Placements)
        {
            Require(partInstances[member.PartID].insert(member.Instance).second,"joint recipes repeated an original instance");
            if(member.Group)groups.insert(member.Group);
        }
        for(auto group:groups)Require(previousGroups.insert(group).second,"different physical recipe copies shared a positive group identity");
    }
    for(const auto& part:p.Parts)
    {
        Require(partInstances[part.ID].size()==part.Quantity,"joint copies under/overproduced an original specification");
        for(std::size_t i=0;i<part.Quantity;++i)Require(partInstances[part.ID].contains(i),"joint copies left an original identity hole");
    }
}
}

int main()try
{
    const auto p=ProblemFor();const auto initial=LooseMix(p);
    PlaneSheetMixSettings settings;settings.MaxSearchNodes=3000;
    const auto mixed=MakePlaneSheetMixStage(Construct(),settings).Factory()->Run(p,initial,Budget());
    Inventory(p,mixed);
    Require(mixed.Layouts.size()==4&&initial.Layouts.size()==6,"joint new-recipe integer selection did not reduce six loose boards to four");
    Require(Has(mixed,"recipes=")&&Has(mixed,"exact-selections="),"joint integer recipe search did not expose its work");
    std::set<std::string> pure;
    for(const auto& board:mixed.Layouts)
    {
        Require(board.Placements.size()==4,"selected dense generic recipe is incomplete");
        std::set<std::string> ids;for(const auto& member:board.Placements)ids.insert(member.PartID);
        Require(ids.size()==1,"joint selection did not combine the two independently constructed pure recipes");pure.insert(*ids.begin());
    }
    Require(pure.size()==2,"only one recipe type was repeated instead of selecting a joint mixture");
    std::cout<<"sheet-mix: originals=16 initial-boards="<<initial.Layouts.size()<<" selected-boards="<<mixed.Layouts.size()
        <<" recipe-types="<<pure.size()<<" attempts="<<mixed.Stats.PlacementAttempts<<'\n';

    auto uneven=ProblemFor(9,7);const auto unevenInitial=LooseMix(uneven,true);
    const auto tail=OptimizePlaneSheetMix(uneven,unevenInitial,Budget(),Construct(),settings);
    Inventory(uneven,tail);Require(tail.Layouts.size()==4,"nonmultiple recipe counts were not completed by residual reconstruction");
    Require(!Has(tail,"tail-trials=0"),"nonmultiple joint selection did not exercise tail reconstruction");

    auto rareProblem=ProblemFor();auto rareInitial=LooseMix(rareProblem);
    auto rare=Tile("rare-width-original",1);rare.Geometry.Outer={{0,0},{4.5,0},{4.5,1},{0,1}};
    rareProblem.Parts.push_back(std::move(rare));
    rareInitial.Layouts.push_back({rareProblem.Stocks[0].ID,6,5,1.4,
        {{rareProblem.Parts.back().ID,0,{.1,.1,0},91,false}},4.5});
    Require(Validate(rareProblem,rareInitial).Complete&&Validate(rareProblem,rareInitial).Valid,
        "rare original fixture is not a complete physical order");
    auto smallLibrary=settings;smallLibrary.MaxRecipes=4;
    const auto rareTail=OptimizePlaneSheetMix(rareProblem,rareInitial,Budget(),Construct(),smallLibrary);
    Inventory(rareProblem,rareTail);
    Require(rareTail.Layouts.size()==5&&Has(rareTail,"recipes=4")&&!Has(rareTail,"tail-trials=0"),
        "an original missing from the bounded recipe library blocked dense-recipe selection and residual reconstruction");

    auto stocked=ProblemFor();stocked.Stocks[0].Quantity=3;
    stocked.Stocks.push_back({"separate-stock-identity",5,1.4,3,1,"mix-material"});
    const auto stockInitial=LooseMix(stocked,false,true);
    const auto constrained=OptimizePlaneSheetMix(stocked,stockInitial,Budget(),Construct(),settings);
    Inventory(stocked,constrained);Require(constrained.Layouts.size()==4,"stock-constrained recipe mixture failed to reduce the complete order");
    std::map<std::string,std::size_t> consumed;for(const auto& board:constrained.Layouts)++consumed[board.StockID];
    Require(consumed[stocked.Stocks[0].ID]<=3&&consumed[stocked.Stocks[1].ID]<=3,"recipe search exceeded one stock identity's inventory");

    auto cancelled=Budget();cancelled.Cancel=[]{return true;};
    const auto stopped=OptimizePlaneSheetMix(p,initial,cancelled,Construct(),settings);
    Inventory(p,stopped);Require(stopped.State==Status::Cancelled&&stopped.Layouts.size()==initial.Layouts.size(),
        "cancelled integer selection discarded the complete original incumbent");
    for(std::size_t attempts:{0u,1u,5u,20u})
    {
        auto bounded=Budget();bounded.MaxPlacementAttempts=attempts;
        const auto retained=OptimizePlaneSheetMix(p,initial,bounded,Construct(),settings);
        Inventory(p,retained);Require(retained.Stats.PlacementAttempts<=attempts,"joint recipe work exceeded its shared attempt cap");
        Require(retained.Layouts.size()<=initial.Layouts.size(),"bounded recipe search published an inferior or partial candidate");
    }

    auto onlyExisting=settings;onlyExisting.MaxConstructionProbes=0;onlyExisting.MaxTailTrials=0;
    const auto unchanged=OptimizePlaneSheetMix(p,initial,Budget(),Construct(),onlyExisting);
    Inventory(p,unchanged);Require(unchanged.Layouts.size()==6,"existing three-piece recipes fabricated a four-piece layout without construction");
    auto partial=initial;const auto missing=partial.Layouts.back().Placements.back();partial.Layouts.pop_back();
    partial.State=Status::Partial;partial.Remaining={{p.Parts[0].ID,1},{p.Parts[1].ID,1}};
    const auto unsupported=OptimizePlaneSheetMix(p,partial,Budget(),Construct(),settings);
    Require(unsupported.Layouts.size()==partial.Layouts.size()&&unsupported.Remaining.size()==partial.Remaining.size(),
        "sheet-mix pretended an incomplete starting order was complete");(void)missing;

    const StrategyFactory failedFactory=[]()->std::unique_ptr<IStrategy>{throw std::runtime_error("test construction factory failure");};
    const auto failedProbe=OptimizePlaneSheetMix(p,initial,Budget(),failedFactory,settings);
    Inventory(p,failedProbe);Require(failedProbe.Layouts.size()<=initial.Layouts.size(),
        "throwing probe factory lost the complete incumbent");
    auto calls=std::make_shared<std::size_t>(0);
    const StrategyFactory failedTail=[calls]()->std::unique_ptr<IStrategy>
    {
        if(++*calls>1)throw std::runtime_error("test residual factory failure");
        return std::make_unique<Rows>();
    };
    auto oneProbe=settings;oneProbe.MaxConstructionProbes=1;
    const auto failedResidual=OptimizePlaneSheetMix(p,initial,Budget(),failedTail,oneProbe);
    Inventory(p,failedResidual);
    Require(*calls>1&&failedResidual.Layouts.size()<=initial.Layouts.size(),
        "throwing residual factory escaped or discarded the best complete joint selection");
    auto noFlag=initial;noFlag.GeometryValidated=false;
    const auto verified=OptimizePlaneSheetMix(p,noFlag,Budget(),failedFactory,settings);
    Require(verified.GeometryValidated&&Validate(p,verified).Complete,
        "original-valid retained incumbent lost its successful geometry-validation flag");
    bool invalid=false;try{auto bad=settings;bad.MaxRecipes=0;MakePlaneSheetMixStage(Construct(),bad);}catch(const std::invalid_argument&){invalid=true;}
    Require(invalid,"invalid joint recipe configuration was silently accepted");
    std::cout<<"PlaneSheetMixTests passed: joint integer recipes, new patterns, residual tails, physical stocks, strict originals and bounded atomic acceptance\n";
    return 0;
}
catch(const std::exception& error){std::cerr<<"PlaneSheetMixTests failed: "<<error.what()<<'\n';return 1;}
