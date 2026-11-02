#include "SheetNestingLocalRepair.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool value,const char* msg){if(!value)throw std::runtime_error(msg);}
    Shape Box(double w,double h){Shape s;s.Outer={{0,0},{w,0},{w,h},{0,h}};return s;}
    Part PartOf(const char* id,double w){Part p;p.ID=id;p.Geometry=Box(w,4);p.Quantity=1;p.Material="m";p.FreeRotation=false;p.AllowedAnglesDegrees={0};return p;}
    Problem Input(){Problem p;p.Parts={PartOf("a",4),PartOf("b",2),PartOf("c",4)};p.Stocks={{"sheet",10,4,1,1,"m"}};return p;}
    Result Initial(const Problem& p)
    {
        Result r;r.State=Status::Partial;Layout l{"sheet",0,10,4,{},24};
        l.Placements={{"a",0,{3,0,0},1,false},{"b",0,{7,0,0},2,false}};
        r.Layouts.push_back(l);r.Remaining={{"c",1}};
        Require(Validate(p,r).Valid,"invalid initial test case");r.GeometryValidated=true;return r;
    }
    class Fixed final:public IStrategy
    {public:explicit Fixed(Result r):value(std::move(r)){} Result Run(const Problem&,const Options&) override{return value;}private:Result value;};
}
int main()
{
    try
    {
        const auto p=Input();const auto initial=Initial(p);Options o;o.TimeLimitSeconds=5;o.MaxPlacementAttempts=100000;
        const auto stage=MakeEvictionRepairStage();const auto result=stage.Factory()->Run(p,initial,o);const auto check=Validate(p,result);
        Require(check.Valid&&check.Complete&&result.State==Status::Complete,"two-blocker repair did not complete");
        Require(initial.Layouts[0].Placements.size()==2&&initial.Remaining[0].Quantity==1,"original mutated");
        auto cancel=o;cancel.Cancel=[]{return true;};const auto stopped=stage.Factory()->Run(p,initial,cancel);
        Require(stopped.State==Status::Cancelled&&Validate(p,stopped).Valid&&stopped.Remaining[0].Quantity==1,"cancel lost original demand");
        std::size_t polls=0;auto mid=o;mid.Cancel=[&]{return ++polls==30;};const auto stoppedMid=stage.Factory()->Run(p,initial,mid);
        Require(stoppedMid.State==Status::Cancelled&&Validate(p,stoppedMid).Valid,"mid-search cancellation published invalid geometry");
        auto limited=o;limited.MaxPlacementAttempts=1;const auto cut=stage.Factory()->Run(p,initial,limited);
        Require(Validate(p,cut).Valid&&cut.Remaining[0].Quantity==1,"attempt limit lost original demand");
        auto no=p;no.PartGap=1;const auto skipped=stage.Factory()->Run(no,initial,o);
        Require(skipped.Layouts[0].Placements.size()==2&&skipped.Remaining[0].Quantity==1,"unsupported gap modified input");
        auto invalid=initial;invalid.Layouts[0].Placements[1].Transform.X=4;
        const auto rejected=stage.Factory()->Run(p,invalid,o);Require(rejected.Layouts[0].Placements[1].Transform.X==4,"invalid input was modified");
        auto turned=p;turned.Parts[2].AllowedAnglesDegrees={90};const auto rotated=stage.Factory()->Run(turned,initial,o);
        Require(Validate(turned,rotated).Valid&&Validate(turned,rotated).Complete,"allowed-angle repair failed");
        const auto placed=std::find_if(rotated.Layouts[0].Placements.begin(),rotated.Layouts[0].Placements.end(),[](const auto& x){return x.PartID=="c";});
        Require(placed!=rotated.Layouts[0].Placements.end()&&std::abs(std::remainder(placed->Transform.AngleDegrees-90,360.))<1e-6,"repair used a forbidden angle");
        auto two=p;two.Stocks[0].Width=12;two.Parts[2].Geometry=Box(3,4);two.Parts[2].Quantity=2;
        auto twoInitial=initial;twoInitial.Layouts[0].Width=12;twoInitial.Remaining[0].Quantity=2;
        Require(Validate(two,twoInitial).Valid,"two-missing fixture invalid");
        const auto twoResult=stage.Factory()->Run(two,twoInitial,o);
        Require(Validate(two,twoResult).Valid&&Validate(two,twoResult).Complete,"two missing originals were not both recovered");
        auto zeroRemaining=initial;zeroRemaining.Remaining.insert(zeroRemaining.Remaining.begin(),{"a",0});
        Require(Validate(p,zeroRemaining).Valid,"zero remaining fixture invalid");
        const auto zeroResult=stage.Factory()->Run(p,zeroRemaining,o);
        Require(Validate(p,zeroResult).Valid&&Validate(p,zeroResult).Complete,"zero remaining row hid actual demand");
        StrategyPipeline pipeline;pipeline.Construct=[initial]{return std::make_unique<Fixed>(initial);};pipeline.PostProcessors.push_back(stage);
        StrategyRegistry registry;registry.Register("test",MakePipelineFactory(std::move(pipeline)));
        const auto integrated=RunJob({"repair","test",p,o},registry);
        Require(integrated.Checked.Valid&&integrated.Checked.Complete,"partial-input pipeline failed to accept repair");
        const auto defaults=DefaultStrategies().IDs();Require(std::find(defaults.begin(),defaults.end(),stage.ID)==defaults.end(),"repair must remain opt-in");
        std::cout<<"EvictionRepairTests passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
