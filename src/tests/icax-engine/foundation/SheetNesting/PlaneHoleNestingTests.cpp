#include "NestingPlaneHoles.h"
#include "NestingInternal.h"
#include "NestingAxisSafety.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>

using namespace iCAX::SheetNesting;
using namespace iCAX::SheetNesting::Detail;
namespace
{
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Shape Rectangle(double width,double height)
{Shape result;result.Outer={{0,0},{width,0},{width,height},{0,height}};return result;}
Part Polygon(std::string id,Shape geometry,std::size_t quantity=1)
{
    Part result;result.ID=std::move(id);result.Geometry=std::move(geometry);result.Quantity=quantity;
    result.Material="hole-alloy";result.FreeRotation=false;result.AllowedAnglesDegrees={0,90,180,270};return result;
}
Problem Task(const std::vector<Point>& hole,Shape child=Rectangle(3,3))
{
    Problem problem;problem.PartGap=.2;problem.EdgeMargin=.5;problem.Tolerance=1e-7;
    auto frame=Rectangle(20,20);frame.Holes={hole};
    problem.Parts={Polygon("host-frame",std::move(frame)),Polygon("nested-original",std::move(child))};
    problem.Stocks={{"hole-sheet",24,24,1,1,"hole-alloy"}};return problem;
}
Layout Board(const Problem& problem,Pose host={1,1,0})
{return {problem.Stocks[0].ID,0,24,24,{{problem.Parts[0].ID,0,host,7,false}},Area(problem.Parts[0].Geometry)};}
NfpPlacementLimits Limits()
{NfpPlacementLimits limits;limits.MaxAttempts=50000;limits.MaxCandidates=32;limits.MaxRawCandidates=2048;return limits;}
void Safe(const Problem& problem,const Layout& fixed,const NfpPlacementQuery& query,std::size_t index=1)
{
    Require(query.Supported,"valid hole query was not supported");
    std::map<std::string,const Part*> parts;for(const auto& part:problem.Parts)parts.emplace(part.ID,&part);
    for(const auto pose:query.Fits)
    {
        const auto nominal=AxisSafety::Make(problem.Parts[index].Geometry,pose);
        Require(AxisSafety::Containment(nominal,fixed.Width,fixed.Height,problem.EdgeMargin)==AxisSafety::Verdict::Safe,
            "hole query spent original sheet margin");
        for(const auto& old:fixed.Placements)
            Require(AxisSafety::Pair(nominal,AxisSafety::Make(parts.at(old.PartID)->Geometry,old.Transform),
                problem.PartGap,problem.AllowCommonEdges,problem.Tolerance)==AxisSafety::Verdict::Safe,
                "hole query spent nominal original-material clearance");
        Result result;result.State=Status::Complete;result.Layouts={fixed};
        result.Layouts[0].Placements.push_back({problem.Parts[index].ID,0,pose,13,true});
        result.Layouts[0].MaterialArea+=Area(problem.Parts[index].Geometry);
        const auto checked=Validate(problem,result);
        Require(checked.Valid,"original geometry validator rejected returned hole pose");
    }
}
}

int main()try
{
    const std::vector<Point> triangle{{2,2},{17,2},{2,17}};
    auto sloped=Task(triangle);
    const auto board=Board(sloped);
    const auto query=FindPlaneHolePlacements(sloped,board,1,0,Limits());
    Require(!query.Fits.empty(),"positive-gap placement inside a sloped hole was missed");Safe(sloped,board,query);

    // Its envelope fits the hole's envelope, but the material crosses the
    // hypotenuse. A bbox-only implementation would incorrectly accept it.
    auto tooLarge=Task(triangle,Rectangle(9,9));
    const auto rejected=FindPlaneHolePlacements(tooLarge,Board(tooLarge),1,0,Limits());
    Require(rejected.Supported&&rejected.Fits.empty(),"sloped-hole envelope was treated as usable material");

    const std::vector<Point> concave{{2,2},{17,2},{17,6},{6,6},{6,17},{2,17}};
    auto elbow=Task(concave,Rectangle(3,3));
    auto elbowBoard=Board(elbow,{21,1,90});
    const auto concaveQuery=FindPlaneHolePlacements(elbow,elbowBoard,1,90,Limits());
    Require(!concaveQuery.Fits.empty(),"rotated concave hole did not expose a legal arm");Safe(elbow,elbowBoard,concaveQuery);
    auto elbowLarge=Task(concave,Rectangle(5,5));
    Require(FindPlaneHolePlacements(elbowLarge,Board(elbowLarge),1,0,Limits()).Fits.empty(),
        "concave-hole cavity was silently replaced by its hull or envelope");

    auto occupied=Task({{2,2},{18,2},{18,18},{2,18}},Rectangle(4,4));
    occupied.Parts.push_back(Polygon("existing-hole-occupant",Rectangle(8,15.6)));
    auto occupiedBoard=Board(occupied);
    occupiedBoard.Placements.push_back({occupied.Parts.back().ID,0,{3.2,3.2,0},29,false});
    occupiedBoard.MaterialArea+=Area(occupied.Parts.back().Geometry);
    const auto freeSide=FindPlaneHolePlacements(occupied,occupiedBoard,1,0,Limits());
    Require(!freeSide.Fits.empty(),"occupied hole did not retain its free side");Safe(occupied,occupiedBoard,freeSide);
    for(const auto pose:freeSide.Fits)Require(pose.X>=11.4,"hole candidate ignored a fixed original occupant");

    auto childFrame=Rectangle(5,5);childFrame.Holes={{{1,1},{4,1},{4,4},{1,4}}};
    auto framed=Task({{2,2},{18,2},{18,18},{2,18}},childFrame);
    const auto framedQuery=FindPlaneHolePlacements(framed,Board(framed),1,0,Limits());
    Require(!framedQuery.Fits.empty()&&framed.Parts[1].Geometry.Holes.size()==1,
        "holed moving original was rejected or its original geometry was overwritten");Safe(framed,Board(framed),framedQuery);

    Shape circle;circle.Radius=2;circle.Center={1,-2};
    auto disk=Task({{10,2},{18,10},{10,18},{2,10}},circle);
    const auto diskQuery=FindPlaneHolePlacements(disk,Board(disk),1,90,Limits());
    Require(!diskQuery.Fits.empty(),"analytic circle did not enter a sloped hole");Safe(disk,Board(disk),diskQuery);
    Require(disk.Parts[1].Geometry.Outer.empty()&&disk.Parts[1].Geometry.Radius==2,
        "conservative circle proposal leaked into original material");

    // Exact-width zero-gap slots remain useful even when regularized clipping
    // would erase their translation-domain area.
    auto touching=Task({{2,2},{7,2},{7,7},{2,7}},Rectangle(5,5));touching.PartGap=0;
    const auto contact=FindPlaneHolePlacements(touching,Board(touching),1,0,Limits());
    Require(!contact.Fits.empty(),"touch-only hole domain was discarded");Safe(touching,Board(touching),contact);

    auto exactGap=Task({{2,2},{7.4,2},{7.4,7.4},{2,7.4}},Rectangle(5,5));
    const auto exact=FindPlaneHolePlacements(exactGap,Board(exactGap),1,0,Limits());
    Require(!exact.Fits.empty(),"exact-gap hole domain lost its direct original-material candidate");Safe(exactGap,Board(exactGap),exact);

    auto tiny=Limits();tiny.MaxAttempts=1;
    const auto bounded=FindPlaneHolePlacements(sloped,board,1,0,tiny);
    Require(bounded.Attempts<=1,"hole candidate generation exceeded the shared attempt cap");Safe(sloped,board,bounded);
    auto noWork=Limits();noWork.MaxAttempts=0;
    const auto zero=FindPlaneHolePlacements(sloped,board,1,0,noWork);
    Require(zero.Truncated&&zero.Attempts==0&&zero.Fits.empty(),"zero hole budget performed candidate work");
    const auto cancelled=FindPlaneHolePlacements(sloped,board,1,0,Limits(),[]{return true;});
    Require(cancelled.Truncated&&cancelled.Attempts==0&&cancelled.Fits.empty(),"cancelled hole query published a candidate");

    auto completeBoard=board;completeBoard.Placements.push_back({sloped.Parts[1].ID,0,query.Fits.front(),31,true});
    const auto noExtra=FindPlaneHolePlacements(sloped,completeBoard,1,0,Limits());
    Require(noExtra.Fits.empty()&&noExtra.Attempts==0,"hole query proposed locally exhausted originals");
    Require(sloped.Parts[0].Quantity==1&&sloped.Parts[1].Quantity==1&&board.Placements.size()==1,
        "query mutated original demand or fixed placements");

    std::cout<<"PlaneHoleNestingTests passed: sloped/concave/rotated hole domains, occupied holes, holed originals, analytic circles and bounded original-material queries\n";
    return 0;
}
catch(const std::exception& error){std::cerr<<"PlaneHoleNestingTests failed: "<<error.what()<<'\n';return 1;}
