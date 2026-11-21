#include "SheetNestingSeparationRepair.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool value,const char* msg){if(!value)throw std::runtime_error(msg);}
    Shape Box(double w,double h){Shape s;s.Outer={{0,0},{w,0},{w,h},{0,h}};return s;}
    Part PartOf(const char* id,double w){Part p;p.ID=id;p.Geometry=Box(w,4);p.Quantity=1;p.Material="m";p.FreeRotation=false;p.AllowedAnglesDegrees={0};return p;}
    Problem Input(){Problem p;p.Parts={PartOf("a",4),PartOf("b",2),PartOf("c",4)};p.Stocks={{"sheet",10.5,4.5,1,1,"m"}};return p;}
    Result Initial(const Problem& p)
    {
        Result r;r.State=Status::Partial;Layout l{"sheet",0,10.5,4.5,{},24};
        l.Placements={{"a",0,{3,0,0},1,false},{"b",0,{7,0,0},2,false}};
        r.Layouts.push_back(l);r.Remaining={{"c",1}};Require(Validate(p,r).Valid,"invalid input fixture");r.GeometryValidated=true;return r;
    }
    class Fixed final:public IStrategy
    {public:explicit Fixed(Result r):value(std::move(r)){}Result Run(const Problem&,const Options&) override{return value;}private:Result value;};
}
int main()
{
    try
    {
        const auto p=Input();const auto initial=Initial(p);Options o;o.TimeLimitSeconds=5;o.MaxPlacementAttempts=500000;
        const auto stage=MakeTriangleSeparationRepairStage();auto processor=stage.Factory();const auto result=processor->Run(p,initial,o);
        Require(Validate(p,result).Valid&&Validate(p,result).Complete&&result.State==Status::Complete,"separation repair did not complete");
        Require(initial.Layouts[0].Placements.size()==2&&initial.Remaining[0].Quantity==1,"original input mutated");
        TriangleSeparationRepairSettings eventSettings;eventSettings.UseNfpSeeds=true;
        const auto eventResult=MakeTriangleSeparationRepairStage(eventSettings).Factory()->Run(p,initial,o);
        Require(Validate(p,eventResult).Complete,"original-material NFP seed repair did not complete");
        // These two diamonds fit in the 18-square only at diagonal contact.
        // An artificial 4*tolerance clearance makes the projection rows
        // inconsistent: the old stage exhausts this 1M budget still partial.
        Problem diamonds;diamonds.Tolerance=1e-7;Part diamond;diamond.ID="diamond";diamond.Material="m";diamond.FreeRotation=false;
        diamond.AllowedAnglesDegrees={0};diamond.Quantity=2;diamond.Geometry.Outer={{0,6},{6,0},{12,6},{6,12}};
        diamonds.Parts={diamond};diamonds.Stocks={{"sheet",18,18,1,1,"m"}};
        Result diamondInitial;diamondInitial.State=Status::Partial;diamondInitial.Layouts={{"sheet",0,18,18,{{"diamond",0,{3,3,0},0,false}},72}};
        diamondInitial.Remaining={{"diamond",1}};Require(Validate(diamonds,diamondInitial).Valid,"invalid exact-contact diamond fixture");
        auto diamondBudget=o;diamondBudget.MaxPlacementAttempts=1000000;
        const auto diamondResult=stage.Factory()->Run(diamonds,diamondInitial,diamondBudget);const auto diamondCheck=Validate(diamonds,diamondResult);
        Require(diamondCheck.Valid&&diamondCheck.Complete&&diamondCheck.PlacedCount==2,"zero-gap diagonal contact exhausted projection budget");
        Require(diamondResult.Stats.PlacementAttempts<=diamondBudget.MaxPlacementAttempts,"exact-contact repair exceeded caller budget");
        Require(diamondInitial.Layouts[0].Placements.size()==1&&diamondInitial.Layouts[0].Placements[0].Transform.X==3&&
            diamondInitial.Remaining[0].Quantity==1,"exact-contact repair mutated original inventory or pose");
        auto tinyProbe=eventSettings;tinyProbe.MaxProbeAttempts=1;
        const auto tinyProbeResult=MakeTriangleSeparationRepairStage(tinyProbe).Factory()->Run(p,initial,o);
        Require(tinyProbeResult.State!=Status::Cancelled&&Validate(p,tinyProbeResult).Valid,
            "local seed preparation limit was misreported as user cancellation");
        auto cancel=o;cancel.Cancel=[]{return true;};const auto stopped=stage.Factory()->Run(p,initial,cancel);
        Require(stopped.State==Status::Cancelled&&Validate(p,stopped).Valid&&stopped.Remaining[0].Quantity==1,"cancel lost original demand");
        std::size_t polls=0;auto mid=o;mid.Cancel=[&]{return ++polls==40;};const auto stoppedMid=stage.Factory()->Run(p,initial,mid);
        Require(stoppedMid.State==Status::Cancelled&&Validate(p,stoppedMid).Valid,"mid-search cancellation invalidated originals");
        auto limited=o;limited.MaxPlacementAttempts=1;const auto cut=stage.Factory()->Run(p,initial,limited);
        Require(Validate(p,cut).Valid&&cut.Remaining[0].Quantity==1&&cut.Stats.PlacementAttempts<=1,"attempt limit lost originals");
        // Every height-4 piece overlaps in Y on this height-4.5 board, so their
        // total width 11.5 cannot fit. A difficult first seed must not consume
        // the entire search budget or leak its overlapping scratch placement.
        auto crowded=p;crowded.Parts[2].Geometry=Box(5.5,4);
        const auto crowdedInitial=Initial(crowded);auto fair=o;fair.MaxPlacementAttempts=20000;
        const auto explored=stage.Factory()->Run(crowded,crowdedInitial,fair);
        Require(explored.State==Status::Partial&&Validate(crowded,explored).Valid&&
            !Validate(crowded,explored).Complete&&explored.Layouts[0].Placements.size()==2&&
            explored.Remaining.size()==1&&explored.Remaining[0].PartID=="c"&&explored.Remaining[0].Quantity==1,
            "seed exhaustion must preserve the valid original partial layout");
        Require(explored.Stats.PlacementAttempts<=fair.MaxPlacementAttempts,
            "per-seed budgets exceeded the shared attempt limit");
        std::size_t triedSeeds=0;
        for(const auto& note:explored.Diagnostics)
        {
            const auto field=note.find("; seeds=");
            if(field!=std::string::npos)triedSeeds=std::stoull(note.substr(field+8));
        }
        Require(triedSeeds>1,"a difficult first seed monopolized the search budget");
        auto no=p;no.PartGap=1;const auto skipped=stage.Factory()->Run(no,initial,o);
        Require(skipped.Layouts[0].Placements.size()==2&&skipped.Remaining[0].Quantity==1,"unsupported gap changed input");
        auto invalid=initial;invalid.Layouts[0].Placements[1].Transform.X=4;
        const auto rejected=stage.Factory()->Run(p,invalid,o);Require(rejected.Layouts[0].Placements[1].Transform.X==4,"invalid incumbent was modified");
        auto zeroRemaining=initial;zeroRemaining.Remaining.insert(zeroRemaining.Remaining.begin(),{"a",0});
        Require(Validate(p,zeroRemaining).Valid,"zero remaining fixture invalid");
        const auto zeroResult=stage.Factory()->Run(p,zeroRemaining,o);
        Require(Validate(p,zeroResult).Valid&&Validate(p,zeroResult).Complete,"zero remaining row hid actual demand");
        auto clockwise=p;for(auto& q:clockwise.Parts)std::reverse(q.Geometry.Outer.begin(),q.Geometry.Outer.end());
        const auto clockwiseInitial=Initial(clockwise);const auto clockwiseResult=stage.Factory()->Run(clockwise,clockwiseInitial,o);
        Require(Validate(clockwise,clockwiseResult).Valid&&Validate(clockwise,clockwiseResult).Complete,"reverse polygon winding failed");
        auto angle=p;angle.Parts[2].AllowedAnglesDegrees={90};const auto angleResult=stage.Factory()->Run(angle,initial,o);
        Require(Validate(angle,angleResult).Valid&&Validate(angle,angleResult).Complete,"allowed rotated missing part failed");
        auto material=p;material.Parts[2].Material="other";const auto other=stage.Factory()->Run(material,initial,o);
        Require(Validate(material,other).Valid&&!Validate(material,other).Complete&&other.Remaining[0].Quantity==1,
            "different-material missing part must remain unplaced");
        StrategyPipeline pipeline;pipeline.Construct=[initial]{return std::make_unique<Fixed>(initial);};pipeline.PostProcessors.push_back(stage);
        StrategyRegistry registry;registry.Register("test",MakePipelineFactory(std::move(pipeline)));
        const auto integrated=RunJob({"separation","test",p,o},registry);
        Require(integrated.Checked.Valid&&integrated.Checked.Complete,"partial-input pipeline failed to accept repair");
        const auto defaults=DefaultStrategies().IDs();Require(std::find(defaults.begin(),defaults.end(),stage.ID)==defaults.end(),"stage must remain opt-in");
        std::cout<<"TriangleSeparationRepairTests passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
