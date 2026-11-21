#include "NestingClearancePlacement.h"
#include "NestingInternal.h"
#include <iostream>
#include <stdexcept>

using namespace iCAX::SheetNesting;
using namespace iCAX::SheetNesting::Detail;
namespace
{
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Part Box(std::string id,double width,double height,std::size_t count=1)
{Part p;p.ID=std::move(id);p.Quantity=count;p.FreeRotation=false;p.AllowedAnglesDegrees={0};p.Geometry.Outer={{0,0},{width,0},{width,height},{0,height}};return p;}
void Safe(const Problem& p,const Layout& board,std::size_t index,const NfpPlacementQuery& query)
{
    Require(query.Supported&&!query.Fits.empty(),"supported conservative contour produced no fit");
    for(const auto pose:query.Fits)
    {
        const auto a=AxisSafety::Make(p.Parts[index].Geometry,pose);
        Require(AxisSafety::Containment(a,board.Width,board.Height,p.EdgeMargin)==AxisSafety::Verdict::Safe,
            "conservative proposal spent real board margin");
        for(const auto& old:board.Placements)
        {
            const auto original=std::find_if(p.Parts.begin(),p.Parts.end(),[&](const Part& part){return part.ID==old.PartID;});
            Require(original!=p.Parts.end()&&AxisSafety::Pair(a,AxisSafety::Make(original->Geometry,old.Transform),
                p.PartGap,p.AllowCommonEdges,p.Tolerance)==AxisSafety::Verdict::Safe,
                "conservative proposal spent nominal original-material clearance");
        }
    }
}
}
int main()try
{
    Problem p;p.PartGap=.2;p.EdgeMargin=.5;p.Parts={Box("polygon",2,2)};
    Part circle;circle.ID="circle";circle.Quantity=2;circle.FreeRotation=false;circle.AllowedAnglesDegrees={0,90};circle.Geometry.Radius=1;circle.Geometry.Center={.3,-.2};
    auto frame=Box("frame",4,4);frame.Geometry.Holes={{{1,1},{3,1},{3,3},{1,3}}};
    p.Parts.push_back(circle);p.Parts.push_back(frame);p.Stocks={{"mixed",14,10,1}};
    ClearanceNfpPlacement mixed(p,p.Stocks[0],{},{});
    Require(mixed.Ready(),"circle or holed original disabled the entire compatible NFP problem");
    Layout board{"mixed",0,14,10,{{"frame",0,{.5,.5,0},1,false},{"circle",0,{6,2,90},2,false}},16-4+Pi};
    Safe(p,board,0,mixed.Find(board,0,0,{},{}));
    Safe(p,board,1,mixed.Find(board,1,0,{},{}));
    Require(p.Parts[1].Geometry.Outer.empty()&&p.Parts[1].Geometry.Radius==1&&p.Parts[2].Geometry.Holes.size()==1,
        "private NFP proposal contours leaked into source parts");

    Problem large;large.PartGap=.1;large.EdgeMargin=.1;
    for(std::size_t i=0;i<70;++i)large.Parts.push_back(Box("specification-"+std::to_string(i),1,1));
    large.Stocks={{"many-specifications",20,20,1}};
    ClearanceNfpPlacement specifications(large,large.Stocks[0],{},{});
    Require(specifications.Ready(),"positive-gap input above 64 distinct specifications was rejected");
    Layout empty{"many-specifications",0,20,20,{},0};
    Safe(large,empty,69,specifications.Find(empty,69,0,{},{}));

    Problem dense;dense.Parts={Box("fixed",.5,.5,300),Box("next",.5,.5)};dense.Stocks={{"dense",40,10,1}};
    Layout occupied{"dense",0,40,10,{},0};
    for(std::size_t i=0;i<300;++i)occupied.Placements.push_back({"fixed",i,{static_cast<double>(i%30),static_cast<double>(i/30),0},0,false});
    ClearanceNfpPlacement manyPlacements(dense,dense.Stocks[0],{},{});
    NfpPlacementLimits limits;limits.MaxCandidates=1;limits.StopAfterFirstFit=true;limits.FastAdjacency=true;
    Safe(dense,occupied,1,manyPlacements.Find(occupied,1,0,limits,{}));
    auto bounded=limits;bounded.MaxFixedPlacements=256;
    Require(!manyPlacements.Find(occupied,1,0,bounded,{}).Supported,"explicit fixed-obstacle memory guard was ignored");

    // The real domain caller already accounts for margin at this immutable
    // exterior wall. Adding the production gap again would reject a legal fit.
    Problem domain;domain.PartGap=.4;domain.Parts={Box("tile",2,2),Box("exterior-wall",1,3,0)};domain.Stocks={{"domain",3,3,1}};
    Layout wall{"domain",0,3,3,{{"exterior-wall",0,{0,0,0},0,false}},0};
    ClearanceNfpPlacement neutral(domain,domain.Stocks[0],{},{},{"exterior-wall"});
    const auto touching=neutral.Find(wall,0,0,{},{});
    Require(touching.Supported&&!touching.Fits.empty(),"domain boundary incorrectly received another part clearance");
    for(const auto pose:touching.Fits)Require(pose.X>=1&&pose.X+2<=3,"neutral obstacle allowed material outside the real domain");
    ClearanceNfpPlacement ordinary(domain,domain.Stocks[0],{},{});
    Require(ordinary.Find(wall,0,0,{},{}).Fits.empty(),"production obstacle lost its nonzero clearance");
    std::cout<<"PlaneClearanceCoverageTests passed: conservative circles/holes, 70 specifications, 300 fixed originals and typed boundary clearance\n";
    return 0;
}
catch(const std::exception& e){std::cerr<<"PlaneClearanceCoverageTests failed: "<<e.what()<<'\n';return 1;}
