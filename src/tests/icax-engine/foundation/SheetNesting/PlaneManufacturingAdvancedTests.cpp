#include "SheetNestingPlaneManufacturing.h"
#include "NestingInternal.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
Part Rectangle(std::string id,double width,double height,std::size_t quantity=1)
{
    Part p;p.ID=std::move(id);p.Quantity=quantity;p.FreeRotation=false;
    p.AllowedAnglesDegrees={0};p.Geometry.Outer={{0,0},{width,0},{width,height},{0,height}};return p;
}
Part Triangle(std::string id,double side,std::size_t quantity=1,double angle=0)
{
    auto p=Rectangle(std::move(id),side,side,quantity);p.Geometry.Outer.pop_back();
    p.Geometry.Outer={{0,0},{side,0},{0,side}};p.AllowedAnglesDegrees={angle};return p;
}
Options Limits(){Options o;o.TimeLimitSeconds=15;o.MaxPlacementAttempts=200000;o.EnableBasicCompaction=false;return o;}
std::vector<PlaneCut> Cuts(const PlaneManufacturingResult& r,const Layout& b)
{
    std::vector<PlaneCut> cuts;for(const auto& c:r.Cuts)if(c.StockID==b.StockID&&c.StockInstance==b.StockInstance)cuts.push_back(c);return cuts;
}
void Complete(const PlaneManufacturingTask& task,const PlaneManufacturingResult& result,const char* message)
{
    const auto checked=ValidatePlaneManufacturingLayout(task,result);
    if(!checked.Valid||!checked.Complete)
    {for(const auto& e:result.Output.Diagnostics)std::cerr<<e<<'\n';for(const auto& e:checked.Errors)std::cerr<<e<<'\n';}
    Require(checked.Valid&&checked.Complete&&result.Output.State==Status::Complete,message);
}
}
int main()
try
{
    PlaneManufacturingTask adjacent;
    adjacent.Input.Parts={Triangle("left contour",3,3),Triangle("right contour",3,3,180)};
    adjacent.Input.Stocks={{"paired",4,4,3}};adjacent.Input.EdgeMargin=.1;adjacent.Input.PartGap=.2;
    adjacent.Pairs={{"left contour","right contour",.3,false}};
    auto pairResult=RunPlaneManufacturingTask(adjacent,Limits());
    Complete(adjacent,pairResult,"nonrectangular original pairs construct and repeat atomically");
    for(const auto& board:pairResult.Output.Layouts)
    {Require(board.Placements.size()==2,"hard pairs are never split across sheets");Require(board.Placements[0].Instance==board.Placements[1].Instance,"paired original identities remain aligned after copying");}

    auto falseBoxes=adjacent;falseBoxes.Input.Parts={Triangle("left contour",4),Triangle("right contour",4)};
    falseBoxes.Input.Stocks={{"paired",10,10,1}};falseBoxes.Input.EdgeMargin=0;falseBoxes.Input.PartGap=0;
    falseBoxes.Pairs[0].MaximumSeparation=1;
    PlaneManufacturingResult distant;distant.Output.State=Status::Complete;
    distant.Output.Layouts={{"paired",0,10,10,{{"left contour",0,{0,0,0},1,false},{"right contour",0,{4,4,0},1,false}},16}};
    Require(Validate(falseBoxes.Input,distant.Output).Complete,"separated triangles are individually legal");
    Require(!ValidatePlaneManufacturingLayout(falseBoxes,distant).Valid,"overlapping or touching boxes cannot stand in for actual pair distance");

    for(const auto mode:{PlaneManufacturingMode::CommonEdgeArray,PlaneManufacturingMode::CommonEdgeDistance})
    {
        PlaneManufacturingTask common;common.Mode=mode;common.Input.Parts={Triangle("wedge",3,6)};
        common.Input.Parts[0].AllowedAnglesDegrees={0,180};common.Input.Stocks={{"common",3.3,3.3,3}};
        common.Input.EdgeMargin=.1;common.Input.PartGap=.15;common.Input.AllowCommonEdges=true;
        common.GroupGap=.2;common.MinimumSharedLength=3;common.MaxGroupMembers=2;
        const auto result=RunPlaneManufacturingTask(common,Limits());
        Complete(common,result,"nonrectangular common-edge motifs use original contact geometry");
        for(const auto& board:result.Output.Layouts)
        {Require(board.Placements.size()==2,"complementary wedge motif fills each repeated sheet");Require(board.Placements[0].Group==board.Placements[1].Group,"physical common-edge contacts keep their shared group");}
    }

    PlaneManufacturingTask column;column.Mode=PlaneManufacturingMode::Guillotine;
    column.Input.Parts={Rectangle("low",3,2),Rectangle("high",3,4)};column.Input.Stocks={{"column",3,6,1}};
    auto columnResult=RunPlaneManufacturingTask(column,Limits());
    Complete(column,columnResult,"same-width column is a genuine guillotine construction");
    Require(columnResult.Output.Layouts[0].Placements[0].Group==columnResult.Output.Layouts[0].Placements[1].Group,"same-width originals are selected together as one column");
    Require(!columnResult.Cuts.empty(),"column retains a real cutting trace");
    auto badTrace=columnResult;badTrace.Cuts.front().Position+=.5;
    Require(!ValidatePlaneManufacturingLayout(column,badTrace).Valid,"cut edits still invalidate column evidence");

    PlaneManufacturingTask batch;batch.Mode=PlaneManufacturingMode::Guillotine;
    batch.Input.Parts={Rectangle("tile",3,2,19)};batch.Input.Stocks={{"batch",10,6,5}};
    batch.Input.EdgeMargin=.2;batch.Input.PartGap=.1;
    const auto copied=RunPlaneManufacturingTask(batch,Limits());
    Complete(batch,copied,"guillotine sheet reuse handles repeated full sheets and an unequal tail");
    Require(copied.Output.Layouts.size()>=3,"batch exercises physical sheet copies");
    const auto firstCuts=Cuts(copied,copied.Output.Layouts[0]);
    const auto secondCuts=Cuts(copied,copied.Output.Layouts[1]);
    Require(!firstCuts.empty()&&firstCuts.size()==secondCuts.size(),"sheet copying includes its ordered cut evidence");
    for(std::size_t i=0;i<firstCuts.size();++i)
    {
        const auto& a=firstCuts[i];const auto& b=secondCuts[i];
        Require(a.StockInstance!=b.StockInstance&&a.Vertical==b.Vertical&&a.Position==b.Position&&a.Kerf==b.Kerf&&a.Region.X==b.Region.X&&a.Region.Y==b.Region.Y&&a.Region.Width==b.Region.Width&&a.Region.Height==b.Region.Height,"copied cuts address the new sheet with the same real leaf sequence");
    }
    std::set<std::size_t> originals,groups;
    for(const auto& b:copied.Output.Layouts)for(const auto& p:b.Placements)
    {Require(originals.insert(p.Instance).second,"copies issue new original instance identities");groups.insert(p.Group);}
    Require(originals.size()==19&&copied.Output.Layouts.back().Placements.size()<copied.Output.Layouts.front().Placements.size(),"tail consumes only residual quantities");
    Require(copied.Output.Layouts[0].Placements[0].Group!=copied.Output.Layouts[1].Placements[0].Group,"copied groups are independent of their prototype");
    Require(std::any_of(copied.Output.Diagnostics.begin(),copied.Output.Diagnostics.end(),[](const auto& d){return d.find("sheet-template")!=std::string::npos;}),"manufacturing entry actually invokes sheet-template reuse");
    auto tiny=Limits();tiny.MaxPlacementAttempts=23;
    auto bounded=RunPlaneManufacturingTask(batch,tiny);
    Require(bounded.Output.Stats.PlacementAttempts<=tiny.MaxPlacementAttempts&&ValidatePlaneManufacturingLayout(batch,bounded).Valid,"construction and copying share one bounded attempt budget");
    auto cancelled=Limits();cancelled.Cancel=[]{return true;};
    auto stopped=RunPlaneManufacturingTask(batch,cancelled);
    Require(stopped.Output.State==Status::Cancelled&&stopped.Output.Layouts.empty()&&stopped.Cuts.empty()&&ValidatePlaneManufacturingLayout(batch,stopped).Valid,"cancelled construction publishes neither half a board nor orphan cuts");

    PlaneManufacturingTask processes;processes.Mode=PlaneManufacturingMode::SawMill;
    processes.Input.Parts={Rectangle("named mill",3,2),Triangle("named saw",2)};
    processes.Input.Stocks={{"process",10,8,1}};processes.Input.PartGap=.1;processes.SawGap=.2;processes.MillGap=.3;
    processes.ProcessClassification=PlaneSawMillClassification::RectanglesSawPolygonsMill;
    auto classified=RunPlaneManufacturingTask(processes,Limits());
    Complete(processes,classified,"explicit geometric process rule ignores misleading original names");
    Require(!classified.Cuts.empty(),"geometrically classified saw rectangle has actual cut evidence");
    processes.ProcessClassification=PlaneSawMillClassification::Explicit;
    processes.ProcessAssignments={{"named mill",PlanePartProcess::Saw},{"named saw",PlanePartProcess::Mill}};
    Complete(processes,RunPlaneManufacturingTask(processes,Limits()),"complete explicit per-original assignments are honored");
    processes.ProcessAssignments.pop_back();
    Require(RunPlaneManufacturingTask(processes,Limits()).Output.State==Status::InvalidInput,"partial process tables cannot silently default unclassified originals");

    PlaneManufacturingTask refine;refine.Input.Parts={Rectangle("first",2,2),Rectangle("second",2,2)};
    refine.Input.Stocks={{"refine",20,10,1}};refine.Input.PartGap=.2;refine.Pairs={{"first","second",.3,false}};
    PlaneManufacturingResult loose;loose.Output.State=Status::Complete;
    loose.Output.Layouts={{"refine",0,20,10,{{"first",0,{5,5,0},11,false},{"second",0,{7.2,5,0},11,false}},8}};
    Complete(refine,loose,"typed refinement starts from a complete paired incumbent");
    auto refined=OptimizePlaneManufacturingLayout(refine,loose,Limits());
    Complete(refine,refined,"typed optimization rechecks pair constraints before accepting edits");
    auto cancelledRefinement=OptimizePlaneManufacturingLayout(refine,loose,cancelled);
    Require(ValidatePlaneManufacturingLayout(refine,cancelledRefinement).Complete&&cancelledRefinement.Output.Layouts[0].Placements[0].Transform.X==5,"cancelled optimization retains its legal historical best");
    auto cutRefined=OptimizePlaneManufacturingLayout(batch,copied,Limits());
    Complete(batch,cutRefined,"typed optimization retains valid copied guillotine leaves");
    Require(cutRefined.Cuts.size()==copied.Cuts.size(),"optimization keeps the cut evidence associated with accepted stock instances");
    std::cout<<"PlaneManufacturingAdvancedTests passed\n";return 0;
}
catch(const std::exception& e){std::cerr<<"PlaneManufacturingAdvancedTests failed: "<<e.what()<<'\n';return 1;}
