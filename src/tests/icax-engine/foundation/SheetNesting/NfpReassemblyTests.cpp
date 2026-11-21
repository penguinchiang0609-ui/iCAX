#include "SheetNestingReassembly.h"
#include "NestingInternal.h"
#include <iostream>
#include <stdexcept>
using namespace iCAX::SheetNesting;
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static Part rectangle(const char* id,double width,double height,std::size_t quantity=1)
{
    Part part;part.ID=id;part.Material="m";part.FreeRotation=false;part.AllowedAnglesDegrees={0};part.Quantity=quantity;
    part.Geometry.Outer={{0,0},{width,0},{width,height},{0,height}};return part;
}
int main()try
{
    Problem problem;problem.Tolerance=1e-7;problem.Stocks.push_back({"stock",10,10,1,1,"m"});
    problem.Parts={rectangle("fixed",2,10),rectangle("small",4,4,2),rectangle("missing",8,6)};
    Result before;before.State=Status::Partial;before.Layouts.push_back({"stock",0,10,10,
        {{"fixed",0,{0,0,0},91,false},{"small",0,{2,3,0},92,false},{"small",1,{6,3,0},92,false}},52});
    before.Remaining.push_back({"missing",1});require(Validate(problem,before).Valid,"invalid test incumbent");
    NfpReassemblySettings settings;settings.MaxWindows=3;settings.MaxReleased=2;settings.BeamWidth=12;settings.MaxMissing=1;
    Options options;options.TimeLimitSeconds=4;options.MaxPlacementAttempts=50000;
    const auto stage=MakeNfpReassemblyStage(settings);auto result=stage.Factory()->Run(problem,before,options);const auto checked=Validate(problem,result);
    require(checked.Valid&&checked.Complete&&checked.PlacedCount==4,"local reconstruction did not complete original demand");
    std::size_t fixedCount=0,small0=0,small1=0;
    for(const auto& placed:result.Layouts.front().Placements)
    {
        if(placed.PartID=="fixed")
        {++fixedCount;require(placed.Instance==0&&placed.Transform.X==0&&placed.Transform.Y==0&&placed.Transform.AngleDegrees==0&&placed.Group==91,"fixed original changed");}
        if(placed.PartID=="small"&&placed.Instance==0)++small0;
        if(placed.PartID=="small"&&placed.Instance==1)++small1;
    }
    require(fixedCount==1&&small0==1&&small1==1,"source identity lost or duplicated");
    require(before.Layouts.front().Placements[1].Transform.Y==3&&before.Remaining.size()==1,"input incumbent mutated");
    require(result.Stats.PlacementAttempts<=options.MaxPlacementAttempts,"shared attempt quota exceeded");
    auto bounded=options;bounded.MaxPlacementAttempts=1;result=stage.Factory()->Run(problem,before,bounded);
    require(Validate(problem,result).Valid&&result.Layouts.front().Placements.size()==3&&result.Stats.PlacementAttempts<=1,"bounded failure changed incumbent");
    auto cancelled=options;cancelled.Cancel=[]{return true;};result=stage.Factory()->Run(problem,before,cancelled);
    require(result.State==Status::Cancelled&&Validate(problem,result).Valid&&result.Layouts.front().Placements.size()==3,"cancellation did not preserve incumbent");
    auto unsupported=problem;unsupported.PartGap=.1;result=stage.Factory()->Run(unsupported,before,options);
    require(result.Layouts.front().Placements[1].Transform.Y==3&&result.Stats.PlacementAttempts==0,"unsupported scope modified layout");
    // Filling the left slot first displaces tile 0; it in turn must displace
    // tile 1 before both can be restored into the two right slots.
    Problem chainProblem;chainProblem.Tolerance=1e-7;chainProblem.Stocks.push_back({"chain",12,4,1,1,"m"});
    chainProblem.Parts={rectangle("tile",4,4,3)};Result chainBefore;chainBefore.State=Status::Partial;
    chainBefore.Layouts.push_back({"chain",0,12,4,{{"tile",0,{1,0,0},0,false},{"tile",1,{6,0,0},0,false}},32});
    chainBefore.Remaining={{"tile",1}};auto chainSettings=settings;chainSettings.MaxWindows=1;chainSettings.MaxReleased=2;
    const auto chainResult=MakeNfpReassemblyStage(chainSettings).Factory()->Run(chainProblem,chainBefore,options);
    require(Validate(chainProblem,chainResult).Valid&&Validate(chainProblem,chainResult).Complete,"bounded displacement chain failed");
    bool usedChain=false;for(const auto& diagnostic:chainResult.Diagnostics)if(diagnostic.find("chain accepted=1")!=std::string::npos)usedChain=true;
    require(usedChain,"test did not exercise dynamic neighbour displacement");
    chainProblem.Parts[0].Quantity=4;chainBefore.Remaining[0].Quantity=2;
    const auto oneMissing=MakeNfpReassemblyStage(chainSettings).Factory()->Run(chainProblem,chainBefore,options);
    require(Validate(chainProblem,oneMissing).Valid&&Validate(chainProblem,oneMissing).PlacedCount==3,"MaxMissing one-window bound exceeded");
    // With only one original allowed in an individual chain, completion needs
    // a quantity-neutral hole change followed by a second bounded repair.
    chainProblem.Parts[0].Quantity=3;chainBefore.Remaining[0].Quantity=1;auto neutralSettings=chainSettings;neutralSettings.MaxReleased=1;
    const auto neutralResult=MakeNfpReassemblyStage(neutralSettings).Factory()->Run(chainProblem,chainBefore,options);
    require(Validate(chainProblem,neutralResult).Valid&&Validate(chainProblem,neutralResult).Complete,"neutral hole frontier did not continue repair");
    bool neutralTried=false;for(const auto& diagnostic:neutralResult.Diagnostics)
        if(diagnostic.find("neutral tried=")!=std::string::npos&&diagnostic.find("neutral tried=0")==std::string::npos)neutralTried=true;
    require(neutralTried,"test did not exercise bounded neutral frontier");
    Problem four;four.Tolerance=1e-7;four.Stocks.push_back({"four",10,10,1,1,"m"});
    four.Parts={rectangle("wall",2,10),rectangle("bar",2,4,4),rectangle("large",8,6)};
    Result fourBefore;fourBefore.State=Status::Partial;fourBefore.Layouts.push_back({"four",0,10,10,{{"wall",0,{},71,false}},52});
    for(std::size_t i=0;i<4;++i)fourBefore.Layouts[0].Placements.push_back({"bar",i,{2.+2*i,3,0},0,false});
    fourBefore.Remaining={{"large",1}};auto fourSettings=settings;fourSettings.MaxReleased=4;fourSettings.MaxWindows=4;
    const auto fourResult=MakeNfpReassemblyStage(fourSettings).Factory()->Run(four,fourBefore,options);
    require(Validate(four,fourResult).Valid&&Validate(four,fourResult).Complete,"four-blocker reconstruction failed");
    bool usedScarce=false;for(const auto& diagnostic:fourResult.Diagnostics)
        if(diagnostic.find("scarce accepted=")!=std::string::npos&&diagnostic.find("scarce accepted=0")==std::string::npos)usedScarce=true;
    require(usedScarce,"test did not exercise scarce-domain backtracking");
    auto symmetric=chainProblem;symmetric.Parts[0].AllowedAnglesDegrees={0,90,180,270};auto symmetricBefore=chainBefore;
    symmetricBefore.Layouts[0].Placements[0].Transform={5,0,90};symmetricBefore.Layouts[0].Placements[1].Transform={10,4,180};
    require(Validate(symmetric,symmetricBefore).Valid,"symmetric-angle incumbent invalid");
    const auto symmetricResult=MakeNfpReassemblyStage(neutralSettings).Factory()->Run(symmetric,symmetricBefore,options);
    require(Validate(symmetric,symmetricResult).Valid&&Validate(symmetric,symmetricResult).Complete,"symmetry normalization lost original physical positions");
    std::cout<<"NFP local reconstruction with fixed originals, demand, safety and budgets passed\n";
}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
