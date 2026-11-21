#include "SheetNestingPlaneStrategies.h"
#include <iostream>
#include <set>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value,const char* message)
{if(!value)throw std::runtime_error(message);}
Part Rectangle(const char* id,double width,double height,std::size_t quantity)
{
    Part part;part.ID=id;part.Quantity=quantity;part.FreeRotation=false;
    part.AllowedAnglesDegrees={0};part.Geometry.Outer={{0,0},{width,0},{width,height},{0,height}};return part;
}
Options Limits()
{
    Options options;options.TimeLimitSeconds=5;options.MaxPlacementAttempts=100000;
    options.EnableCombinations=false;options.EnableGapFill=false;options.EnableBasicCompaction=false;return options;
}
Result Run(const Problem& problem,PlaneStrategyKind kind,Options options,std::size_t nodes)
{
    PlaneStrategySettings settings;settings.Kind=kind;settings.MaxNodes=nodes;
    auto output=MakePlaneStrategyFactory(settings)()->Run(problem,options);
    Require(Validate(problem,output).Valid,"array search must conserve valid original demand");
    Require(output.Stats.PlacementAttempts<=options.MaxPlacementAttempts,"search exceeded shared attempt cap");return output;
}
}
int main()
{
    try
    {
        Problem many;many.Parts={Rectangle("part",3,3,6)};many.Stocks={{"sheet",3,3,6}};
        for(auto kind:{PlaneStrategyKind::ArrayFirstSpecificationSearch,PlaneStrategyKind::ArrayMultiSpecificationSearch,
            PlaneStrategyKind::ArrayReduceSearch,PlaneStrategyKind::RectangleBlockSearch})
        {
            auto output=Run(many,kind,Limits(),1);
            Require(output.State==Status::Complete&&output.Layouts.size()==6,"one-node searches must retain greedy boards after the first board");
            std::set<std::size_t> identities;
            for(const auto& board:output.Layouts)
            {Require(board.Placements.size()==1,"one original per physical sheet");identities.insert(board.Placements[0].Instance);}
            Require(identities==std::set<std::size_t>{0,1,2,3,4,5},"per-board searches reused an original instance");
        }

        Problem stock;stock.Parts={Rectangle("tiles",2,2,10)};stock.Stocks={{"sheet",4,4,3}};
        auto bounded=Limits();bounded.MaxPlacementAttempts=1200;
        auto shared=Run(stock,PlaneStrategyKind::ArrayMultiSpecificationSearch,bounded,10000);
        Require(shared.State==Status::Complete&&shared.Layouts.size()==3,"local search budgets must preserve construction opportunity on later sheets");
        bounded.MaxPlacementAttempts=1;
        auto exhausted=Run(stock,PlaneStrategyKind::ArrayMultiSpecificationSearch,bounded,10000);
        Require(exhausted.State==Status::Partial,"one-attempt budget cannot falsely claim complete demand");

        Problem area;area.Parts={Rectangle("small",1,1,2),Rectangle("large",4,4,1)};area.Stocks={{"sheet",4,4,2}};
        auto material=Run(area,PlaneStrategyKind::ArrayMultiSpecificationSearch,Limits(),1000);
        Require(Validate(area,material).Complete&&material.Layouts[0].MaterialArea==16&&material.Layouts[0].Placements.size()==1,
            "search leaves must prefer actual material area over a larger count of small originals");

        Problem cavity;Part bracket;bracket.ID="bracket";bracket.FreeRotation=false;bracket.AllowedAnglesDegrees={0};
        bracket.Geometry.Outer={{0,0},{4,0},{4,2},{2,2},{2,4},{0,4}};
        cavity.Parts={bracket,Rectangle("filler",2,2,1)};cavity.Stocks={{"sheet",4,4,1}};
        auto noFill=Run(cavity,PlaneStrategyKind::ArrayFirstSpecificationSearch,Limits(),1);
        Require(noFill.State==Status::Partial&&noFill.Layouts[0].Placements.size()==1,"first-specification baseline must not bypass disabled gap fill");
        auto fill=Limits();fill.EnableGapFill=true;
        auto filled=Run(cavity,PlaneStrategyKind::ArrayFirstSpecificationSearch,fill,1);
        Require(filled.State==Status::Complete,"selected-leaf gap fill must retain a reserved work share");

        auto cancelled=Limits();cancelled.Cancel=[]{return true;};
        auto stopped=Run(many,PlaneStrategyKind::ArrayMultiSpecificationSearch,cancelled,1);
        Require(stopped.State==Status::Cancelled&&stopped.Layouts.empty()&&stopped.Remaining.size()==1&&stopped.Remaining[0].Quantity==6,
            "cancellation must preserve every original demand");
        std::cout<<"PlaneArraySearchBudgetTests passed: per-board greedy incumbents, local node caps, shared attempts, gap-fill policy and cancellation\n";
        return 0;
    }
    catch(const std::exception& error)
    {std::cerr<<"PlaneArraySearchBudgetTests failed: "<<error.what()<<'\n';return 1;}
}
