#include "SheetNestingDomainSearch.h"
#include "NestingInternal.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace iCAX::SheetNesting;
static void require(bool v,const char* s){if(!v)throw std::runtime_error(s);}
int main() try {
    Problem p;p.Tolerance=1e-7;p.Stocks.push_back({"board",10,14,1,1,"m"});
    Part lower;lower.ID="lower";lower.Material="m";lower.FreeRotation=false;lower.AllowedAnglesDegrees={0};lower.Geometry.Outer={{2,0},{8,0},{8,5},{2,5}};
    Part upper=lower;upper.ID="upper";upper.Geometry.Outer={{2,13.3},{8,13.3},{8,14},{2,14}};
    Part target=lower;target.ID="target";target.Geometry.Outer={{0,8},{10,0},{10,10},{0,9}};
    p.Parts={lower,upper,target};Layout layout{"board",0,10,14,{{"lower",0,{0,0,0},0,false},{"upper",0,{0,0,0},0,false}},34.2};
    Options o;o.TimeLimitSeconds=3;o.MaxPlacementAttempts=100000;
    const auto poses=Detail::ProbeDomainCandidates(p,layout,2,o);bool found=false;
    for(const auto& pose:poses){
        require(Detail::InsideSheet(target.Geometry,pose,10,14,0,p.Tolerance),"candidate outside stock");
        require(!Detail::ShapesConflict(lower.Geometry,{0,0,0},target.Geometry,pose,0,p.Tolerance),"lower collision");
        require(!Detail::ShapesConflict(upper.Geometry,{0,0,0},target.Geometry,pose,0,p.Tolerance),"upper collision");
        if(std::abs(pose.X)<p.Tolerance&&pose.Y>=3.4-p.Tolerance&&pose.Y<=3.5+p.Tolerance)found=true;
    }
    require(found,"missed feasible zero-width translation domain");
    Problem channel;channel.Tolerance=1e-7;channel.Stocks.push_back({"board",25,20,1,1,"m"});
    Part u=lower;u.ID="U";u.Geometry.Outer={{0,0},{2,0},{2,3},{12,3},{12,0},{14,0},{14,5},{0,5}};
    Part fill=lower;fill.ID="fill";fill.Geometry.Outer={{0,0},{10,0},{10,3},{0,3}};channel.Parts={u,fill};
    Layout placed{"board",0,25,20,{{"U",0,{6,5,0},0,false}},40};
    const auto channelPoses=Detail::ProbeDomainCandidates(channel,placed,1,o);bool insideChannel=false;
    for(const auto pose:channelPoses)if(std::abs(pose.X-8)<1e-6&&pose.Y>2.01&&pose.Y<4.99&&
        !Detail::ShapesConflict(u.Geometry,{6,5,0},fill.Geometry,pose,0,channel.Tolerance))insideChannel=true;
    require(insideChannel,"missed touch-only channel inside regularized NFP");
    Problem cut;cut.Tolerance=1e-7;cut.Stocks.push_back({"board",14,8,1,1,"m"});
    Part cu=lower;cu.ID="cu";cu.Geometry.Outer={{0,3},{2,3},{2,6},{12,6},{12,3},{14,3},{14,8},{0,8}};
    Part cb=lower;cb.ID="cb";cb.Geometry.Outer={{9,0},{9,.5},{3,.5},{3,0}};
    Part ct=lower;ct.ID="ct";ct.Geometry.Outer={{11,3.8},{11,4},{4,4},{4,3.8}};
    Part cm=lower;cm.ID="cm";cm.Geometry.Outer={{10,0},{10,3},{0,3},{0,0}};cut.Parts={cu,cb,ct,cm};
    Layout cutLayout{"board",0,14,8,{{"cu",0,{0,0,0},0,false},{"cb",0,{0,0,0},0,false},{"ct",0,{0,0,0},0,false}},0};
    const auto cutPoses=Detail::ProbeDomainCandidates(cut,cutLayout,3,o);bool eventInterval=false;
    for(const auto pose:cutPoses)if(std::abs(pose.X-2)<1e-6&&pose.Y>.5001&&pose.Y<.7999&&
        !Detail::ShapesConflict(cu.Geometry,{0,0,0},cm.Geometry,pose,0,cut.Tolerance)&&
        !Detail::ShapesConflict(cb.Geometry,{0,0,0},cm.Geometry,pose,0,cut.Tolerance)&&
        !Detail::ShapesConflict(ct.Geometry,{0,0,0},cm.Geometry,pose,0,cut.Tolerance))eventInterval=true;
    require(eventInterval,"missed interior of constrained touch-only channel");
    o.Cancel=[] {return true;};require(Detail::ProbeDomainCandidates(p,layout,2,o).empty(),"cancel ignored");
    Problem simple;simple.Tolerance=1e-7;simple.Stocks.push_back({"board",10,10,1,1,"m"});
    Part tile=lower;tile.ID="tile";tile.Quantity=2;tile.Geometry.Outer={{0,0},{5,0},{5,10},{0,10}};simple.Parts.push_back(tile);
    o.Cancel={};auto solver=MakeDomainSearchFactory()();auto result=solver->Run(simple,o);const auto check=Validate(simple,result);
    require(check.Valid&&check.Complete&&check.PlacedCount==2,"simple tiling failed");
    require(result.Stats.PlacementAttempts<=o.MaxPlacementAttempts,"attempt budget exceeded");
    auto cancelled=o;cancelled.Cancel=[] {return true;};result=solver->Run(simple,cancelled);
    require(result.State==Status::Cancelled&&Validate(simple,result).Valid,"job cancellation failed");
    result=solver->Run(simple,o);require(Validate(simple,result).Complete,"cancelled job contaminated reused worker");
    auto invalid=simple;invalid.Parts.push_back(invalid.Parts.front());result=solver->Run(invalid,o);
    require(result.State==Status::InvalidInput,"duplicate ID accepted");
    std::cout<<"domain candidates and controls passed\n";return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
