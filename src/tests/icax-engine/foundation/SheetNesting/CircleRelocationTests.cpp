#include "SheetNestingPipeline.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
    Shape Box(double w,double h){Shape s;s.Outer={{0,0},{w,0},{w,h},{0,h}};return s;}
    Part MakePart(const char* id,Shape s,std::size_t count)
    {Part p;p.ID=id;p.Geometry=std::move(s);p.Quantity=count;p.Material="m";p.FreeRotation=false;p.AllowedAnglesDegrees={0};return p;}
    double OccupiedTop(const Layout& layout)
    {
        double top=0;
        for(const auto& p:layout.Placements)
        {
            const double height=p.PartID=="circle"?8:p.PartID=="body"?150:20;
            top=std::max(top,p.Transform.Y+height);
        }
        return top;
    }
    double LargestTop(const Result& r)
    {double area=0;for(const auto& l:r.Layouts)area=std::max(area,98*std::max(0.,199-OccupiedTop(l)-2));return area;}
    Problem Input()
    {
        Problem p;p.EdgeMargin=1;p.PartGap=2;
        Shape c;c.Radius=4;c.Center={4,4};
        p.Parts={MakePart("circle",c,4),MakePart("body",Box(70,150),1),MakePart("donor",Box(60,20),2)};
        p.Stocks={{"sheet",100,200,2,1,"m"}};return p;
    }
    Result Initial(const Problem& p)
    {
        Result r;r.State=Status::Complete;r.Layouts.resize(2);
        for(std::size_t i=0;i<2;++i){r.Layouts[i].StockID="sheet";r.Layouts[i].StockInstance=i;r.Layouts[i].Width=100;r.Layouts[i].Height=200;}
        for(std::size_t i=0;i<4;++i)r.Layouts[0].Placements.push_back({"circle",i,{1+10.*i,1,0},1,false});
        r.Layouts[0].Placements.push_back({"body",0,{1,11,0},2,false});
        r.Layouts[1].Placements={{"donor",0,{1,1,0},3,false},{"donor",1,{1,23,0},4,false}};
        r.Layouts[0].MaterialArea=Classify(p.Parts[1].Geometry,p.Tolerance).Area+4*std::acos(-1.)*16;r.Layouts[1].MaterialArea=2400;
        const auto check=Validate(p,r);Require(check.Valid&&check.Complete,"fixture invalid");r.GeometryValidated=true;return r;
    }
    class FixedStrategy final:public IStrategy
    {
    public:
        explicit FixedStrategy(Result r):Value(std::move(r)){}
        Result Run(const Problem&,const Options&) override{return Value;}
    private:Result Value;
    };
}
int main()
{
    try
    {
        const auto p=Input();const auto initial=Initial(p);Options o;o.TimeLimitSeconds=5;o.MaxPlacementAttempts=20000;
        auto stage=MakeCircleRelocationStage();auto processor=stage.Factory();const auto changed=processor->Run(p,initial,o);
        const auto check=Validate(p,changed);Require(check.Valid&&check.Complete,"relocation corrupted originals");
        Require(changed.Layouts.size()==2&&LargestTop(changed)>LargestTop(initial)+100,"expected safe improvement");
        Require(initial.Layouts[0].Placements.size()==5&&initial.Layouts[1].Placements.size()==2,"input mutated");
        auto limited=o;limited.MaxPlacementAttempts=1;auto partial=stage.Factory()->Run(p,initial,limited);
        Require(Validate(p,partial).Valid&&LargestTop(partial)==LargestTop(initial),"attempt cut changed incumbent");
        auto cancelled=o;cancelled.Cancel=[]{return true;};auto stopped=stage.Factory()->Run(p,initial,cancelled);
        Require(stopped.State==Status::Cancelled&&Validate(p,stopped).Valid&&LargestTop(stopped)==LargestTop(initial),"cancel changed incumbent");
        auto zero=p;zero.PartGap=0;auto same=stage.Factory()->Run(zero,initial,o);
        Require(LargestTop(same)==LargestTop(initial),"unsupported zero gap changed incumbent");
        auto broken=initial;broken.Layouts[0].Placements.erase(broken.Layouts[0].Placements.begin());
        auto rejected=stage.Factory()->Run(p,broken,o);Require(rejected.Layouts[0].Placements.size()==4,"invalid incumbent was changed");
        auto singles=initial;for(auto& l:singles.Layouts)for(auto& q:l.Placements)q.Group=0;
        const auto movedSingles=stage.Factory()->Run(p,singles,o);
        Require(Validate(p,movedSingles).Valid&&Validate(p,movedSingles).Complete&&LargestTop(movedSingles)>LargestTop(singles)+100,
            "ungrouped placements must remain independently movable");
        auto holed=p;holed.Parts[1].Geometry.Holes.push_back({{20,30},{20,50},{40,50},{40,30}});
        const auto holeInitial=Initial(holed);const auto holeMoved=stage.Factory()->Run(holed,holeInitial,o);
        Require(Validate(holed,holeMoved).Valid&&Validate(holed,holeMoved).Complete&&LargestTop(holeMoved)>LargestTop(holeInitial)+100,
            "hole-bearing originals must remain valid after relocation");
        auto mixed=p;mixed.Parts[2].Material="other";mixed.Stocks.push_back({"other-sheet",100,200,1,1,"other"});
        auto mixedInitial=initial;mixedInitial.Layouts[1].StockID="other-sheet";mixedInitial.Layouts[1].StockInstance=0;
        Require(Validate(mixed,mixedInitial).Valid,"different-material fixture invalid");
        const auto mixedOutput=stage.Factory()->Run(mixed,mixedInitial,o);
        Require(Validate(mixed,mixedOutput).Valid&&LargestTop(mixedOutput)==LargestTop(mixedInitial),"different-material layouts must remain untouched");
        StrategyPipeline pipeline;pipeline.Construct=[initial]{return std::make_unique<FixedStrategy>(initial);};pipeline.PostProcessors.push_back(stage);
        StrategyRegistry reg;reg.Register("test.relocate",MakePipelineFactory(std::move(pipeline)));
        const auto pipelineResult=RunJob({"postprocess","test.relocate",p,o},reg);
        Require(pipelineResult.Checked.Valid&&pipelineResult.Checked.Complete&&LargestTop(pipelineResult.Output)>LargestTop(initial)+100,
            "pipeline failed to accept a validated improvement");
        const auto defaults=DefaultStrategies().IDs();
        Require(std::find(defaults.begin(),defaults.end(),stage.ID)==defaults.end(),"stage must be opt-in");
        std::cout<<"CircleRelocationTests passed\n";return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
