#include "SheetNestingLinearRepair.h"
#include <iostream>
#include <stdexcept>
using namespace iCAX::SheetNesting;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Part rect(const char* id,double width,double height)
{Part p;p.ID=id;p.Material="m";p.FreeRotation=false;p.AllowedAnglesDegrees={0};p.Geometry.Outer={{0,0},{width,0},{width,height},{0,height}};return p;}
int main()try
{
    Problem p;p.Tolerance=1e-7;p.Stocks={{"s",10.5,4.5,1,1,"m"}};p.Parts={rect("a",4,4),rect("b",2,4),rect("c",4,4)};
    Result before;before.State=Status::Partial;before.Layouts={{"s",0,10.5,4.5,{{"a",0,{3,0,0},0,false},{"b",0,{7,0,0},0,false}},24}};before.Remaining={{"c",1}};
    require(Validate(p,before).Valid,"invalid anchored fixture");Options o;o.TimeLimitSeconds=3;o.MaxPlacementAttempts=200000;
    auto stage=MakeConvexLinearRepairStage();auto out=stage.Factory()->Run(p,before,o);
    require(Validate(p,out).Complete,"joint linear translation did not free room for original missing part");
    require(before.Layouts[0].Placements.size()==2&&before.Layouts[0].Placements[0].Transform.X==3,"input changed");
    require(out.Stats.PlacementAttempts<=o.MaxPlacementAttempts,"shared work limit exceeded");
    Problem d;d.Tolerance=1e-7;d.Stocks={{"s",18,18,1,1,"m"}};
    auto diamond=rect("diamond",1,1);diamond.Geometry.Outer={{0,6},{6,0},{12,6},{6,12}};diamond.Quantity=2;d.Parts={diamond};
    Result one;one.State=Status::Partial;one.Layouts={{"s",0,18,18,{{"diamond",0,{3,3,0},0,false}},72}};one.Remaining={{"diamond",1}};
    out=stage.Factory()->Run(d,one,o);require(Validate(d,out).Complete,"oblique separation alternatives failed");
    require(out.Layouts[0].Placements[0].Instance!=out.Layouts[0].Placements[1].Instance,"original instance duplicated");
    Problem turn;turn.Tolerance=1e-7;turn.Stocks={{"s",8,6,1,1,"m"}};turn.Parts={rect("wide",6,4),rect("tall",4,6)};
    turn.Parts[0].AllowedAnglesDegrees={0,90};
    Result turnBefore;turnBefore.State=Status::Partial;turnBefore.Layouts={{"s",0,8,6,{{"wide",0,{1,0,0},0,false}},24}};turnBefore.Remaining={{"tall",1}};
    require(Validate(turn,turnBefore).Valid,"invalid orientation fixture");
    out=stage.Factory()->Run(turn,turnBefore,o);require(Validate(turn,out).Complete,"permitted neighbour rotation was not explored");
    for(const auto& x:out.Layouts[0].Placements)if(x.PartID=="wide")require(x.Transform.AngleDegrees==90,"orientation fixture did not require a real allowed rotation");
    auto forbidden=turn;forbidden.Parts[0].AllowedAnglesDegrees={0};auto brief=o;brief.TimeLimitSeconds=.15;brief.MaxPlacementAttempts=20000;
    out=stage.Factory()->Run(forbidden,turnBefore,brief);require(Validate(forbidden,out).Valid&&!Validate(forbidden,out).Complete,"invented a prohibited neighbour orientation");
    auto tiny=o;tiny.MaxPlacementAttempts=1;out=stage.Factory()->Run(p,before,tiny);
    require(Validate(p,out).Valid&&out.Layouts[0].Placements.size()==2&&out.Stats.PlacementAttempts<=1,"limited search leaked scratch geometry");
    auto cancel=o;cancel.Cancel=[]{return true;};out=stage.Factory()->Run(p,before,cancel);
    require(out.State==Status::Cancelled&&Validate(p,out).Valid&&out.Remaining[0].Quantity==1,"cancel lost original demand");
    auto impossible=p;impossible.Parts[2].Geometry=rect("c",5.5,4).Geometry;auto shortBudget=o;shortBudget.TimeLimitSeconds=.15;shortBudget.MaxPlacementAttempts=20000;
    out=stage.Factory()->Run(impossible,before,shortBudget);
    require(out.State==Status::Partial&&Validate(impossible,out).Valid&&out.Layouts[0].Placements.size()==2,"infeasible exploration changed originals");
    std::cout<<"Convex linear repair joint positions, diagonal contacts, original inventory and resource limits passed\n";
}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
