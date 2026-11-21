#include "SheetNestingCompaction.h"
#include "NestingInternal.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace iCAX::SheetNesting;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
double right(const Problem& p,const Result& r){double value=0;for(const auto& x:r.Layouts[0].Placements){
    const auto it=std::find_if(p.Parts.begin(),p.Parts.end(),[&](const Part& a){return a.ID==x.PartID;});
    const auto b=Detail::Bounds(it->Geometry,x.Transform);value=std::max(value,b.X+b.Width);}return value;}
int main()try
{
    Problem p;p.Tolerance=1e-7;p.Stocks.push_back({"s",25,10,1,1,"m"});
    Part u;u.ID="U";u.Material="m";u.FreeRotation=false;u.AllowedAnglesDegrees={0};
    u.Geometry.Outer={{0,0},{2,0},{2,3},{12,3},{12,0},{14,0},{14,5},{0,5}};
    Part fill=u;fill.ID="fill";fill.Geometry.Outer={{0,0},{10,0},{10,3},{0,3}};p.Parts={u,fill};
    Result original;original.State=Status::Complete;original.Layouts.push_back({"s",0,25,10,
        {{"U",0,{5,0,0},0,false},{"fill",0,{7,0,0},0,false}},70});
    require(Validate(p,original).Complete,"invalid interlocking fixture");
    Options options;options.TimeLimitSeconds=3;options.MaxPlacementAttempts=100000;
    NfpCompactionSettings individual;individual.MaxGroupMembers=1;
    auto one=MakeNfpCompactionStage(individual).Factory()->Run(p,original,options);
    require(Validate(p,one).Complete&&std::abs(right(p,one)-19)<1e-7,"individual interlocking fixture moved incorrectly");
    auto together=MakeNfpCompactionStage().Factory()->Run(p,original,options);
    require(Validate(p,together).Complete&&std::abs(right(p,together)-14)<1e-7,"interlocking group did not move together");
    require(original.Layouts[0].Placements[0].Transform.X==5,"input layout was mutated");
    Problem d;d.Tolerance=1e-7;d.Stocks.push_back({"s",12,8,1,1,"m"});
    Part a=u;a.ID="A";a.Geometry.Outer={{0,0},{6,0},{0,6}};
    Part b=u;b.ID="B";b.Geometry.Outer={{0,0},{4,0},{4,4}};d.Parts={a,b};
    Result diagonal;diagonal.State=Status::Complete;diagonal.Layouts.push_back({"s",0,12,8,
        {{"A",0,{0,0,0},0,false},{"B",0,{6,0,0},0,false}},26});
    NfpCompactionSettings horizontal;horizontal.MaxGroupMembers=1;horizontal.MaxDirections=1;
    auto axisOnly=MakeNfpCompactionStage(horizontal).Factory()->Run(d,diagonal,options);
    require(Validate(d,axisOnly).Complete&&std::abs(right(d,axisOnly)-10)<1e-7,"axis-only control changed unexpectedly");
    auto sliding=MakeNfpCompactionStage(individual).Factory()->Run(d,diagonal,options);
    require(Validate(d,sliding).Complete&&right(d,sliding)<6+1e-6,"edge-tangent sliding did not shrink width");
    Problem refill;refill.Tolerance=1e-7;refill.Stocks.push_back({"s",10,10,1,1,"m"});
    Part narrow=u;narrow.ID="narrow";narrow.Geometry.Outer={{0,0},{4,0},{4,10},{0,10}};
    Part wide=narrow;wide.ID="wide";wide.Geometry.Outer={{0,0},{6,0},{6,10},{0,10}};refill.Parts={narrow,wide};
    Result blocked;blocked.State=Status::Partial;blocked.Layouts.push_back({"s",0,10,10,{{"narrow",0,{3,0,0},0,false}},40});
    blocked.Remaining.push_back({"wide",1});NfpCompactionSettings withRefill;withRefill.RefillMissing=true;
    auto filled=MakeNfpCompactionStage(withRefill).Factory()->Run(refill,blocked,options);
    require(Validate(refill,filled).Complete&&filled.Layouts[0].Placements.size()==2,"compaction did not release space for original missing part");
    require(filled.Stats.PlacementAttempts<=options.MaxPlacementAttempts,"refill exceeded the shared attempt budget");
    auto cancelled=options;cancelled.Cancel=[] {return true;};
    auto stopped=MakeNfpCompactionStage().Factory()->Run(p,original,cancelled);
    require(stopped.State==Status::Cancelled&&stopped.Layouts[0].Placements[0].Transform.X==5,"cancel altered original placements");
    auto gap=p;gap.PartGap=.1;auto unsupported=MakeNfpCompactionStage().Factory()->Run(gap,original,options);
    require(unsupported.Layouts[0].Placements[0].Transform.X==5,"unsupported gap modified placements");
    std::cout<<"NFP compaction: interlocking groups, slanted slides, original geometry and cancellation passed\n";
}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
