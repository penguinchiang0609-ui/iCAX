#include "SheetNestingPipeline.h"
#include "NestingInternal.h"
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
Shape Box(double w,double h){Shape s;s.Outer={{0,0},{w,0},{w,h},{0,h}};return s;}
Part Rectangle(std::string id,double w,double h,std::size_t quantity=1,std::vector<double> angles={0})
{Part p;p.ID=std::move(id);p.Geometry=Box(w,h);p.Quantity=quantity;p.Material="m";p.FreeRotation=false;p.AllowedAnglesDegrees=std::move(angles);return p;}
Layout Sheet(std::size_t instance,std::vector<Placement> placements,double side=100)
{Layout l;l.StockID="sheet";l.StockInstance=instance;l.Width=l.Height=side;l.Placements=std::move(placements);return l;}
struct Case{Problem Input;Result Initial;};
class RecordedInitial final:public IStrategy
{
public:
    explicit RecordedInitial(Result initial):Initial(std::move(initial)){}
    Result Run(const Problem&,const Options&) override{return Initial;}
private:Result Initial;
};
void Prepare(Case& c)
{
    c.Initial.State=Status::Complete;
    for(auto& sheet:c.Initial.Layouts){sheet.MaterialArea=0;for(const auto& p:sheet.Placements)for(const auto& part:c.Input.Parts)if(part.ID==p.PartID){sheet.MaterialArea+=Detail::Area(part.Geometry);break;}}
    const auto v=Validate(c.Input,c.Initial);Require(v.Valid&&v.Complete,"invalid test incumbent");c.Initial.GeometryValidated=true;
}
Case Easy(double gap=0,double margin=0)
{
    Case c;c.Input.PartGap=gap;c.Input.EdgeMargin=margin;const double side=100+2*margin;
    c.Input.Stocks={{"sheet",side,side,3,1,"m"}};
    c.Input.Parts={Rectangle("A",40,100),Rectangle("B",60-gap,60-gap),Rectangle("C",60-gap,40)};
    c.Initial.Layouts={Sheet(0,{{"A",0,{margin,margin,0}}},side),Sheet(2,{{"B",0,{margin,margin,0}},{"C",0,{margin,margin+60,0}}},side)};
    Prepare(c);return c;
}
Result Run(const Case& c,Options options={})
{
    options.TimeLimitSeconds=1;auto stage=MakeRectangleRefillStage();Require(stage.ID=="rect.refill"&&stage.Factory,"stage factory contract");
    auto result=stage.Factory()->Run(c.Input,c.Initial,options);const auto checked=Validate(c.Input,result);
    Require(checked.Valid&&checked.Complete,"stage lost complete valid incumbent");
    Require(result.Stats.PlacementAttempts<=options.MaxPlacementAttempts,"stage exceeded caller attempt budget");
    Require(result.Stats.PlacementAttempts<=20000,"stage exceeded internal attempt cap");
    Require(result.Stats.CandidatePatterns<=256,"stage exceeded trial cap");
    Require(result.Stats.EvaluationSeconds==0,"stage measured remnants internally");
    std::set<std::pair<std::string,std::size_t>> beforeParts,afterParts;
    for(const auto& l:c.Initial.Layouts)for(const auto& p:l.Placements)beforeParts.emplace(p.PartID,p.Instance);
    for(const auto& l:result.Layouts)
    {
        const auto old=std::find_if(c.Initial.Layouts.begin(),c.Initial.Layouts.end(),[&](const auto& a){return a.StockID==l.StockID&&a.StockInstance==l.StockInstance;});
        Require(old!=c.Initial.Layouts.end(),"stage created or renumbered stock");
        for(const auto& fixed:old->Placements)
        {
            const auto p=std::find_if(l.Placements.begin(),l.Placements.end(),[&](const auto& a){return a.PartID==fixed.PartID&&a.Instance==fixed.Instance;});
            Require(p!=l.Placements.end(),"fixed target part identity changed");
            Require(p->Transform.X==fixed.Transform.X&&p->Transform.Y==fixed.Transform.Y&&p->Transform.AngleDegrees==fixed.Transform.AngleDegrees&&p->Group==fixed.Group&&p->GapFiller==fixed.GapFiller,"fixed target pose or metadata changed");
        }
        for(const auto& p:l.Placements)afterParts.emplace(p.PartID,p.Instance);
    }
    Require(beforeParts==afterParts,"part IDs or instances changed");return result;
}
}
int main()
{
    try
    {
        std::size_t tests=0;
        auto c=Easy();auto r=Run(c);Require(r.Layouts.size()==1&&r.Layouts.front().StockInstance==2,"easy delete or stock instance hole");++tests;
        c=Easy(1,1);r=Run(c);Require(r.Layouts.size()==1,"exact positive gap/margin placement rejected");++tests;
        c=Easy();c.Input.Parts={Rectangle("squares",40,40,2),Rectangle("tall",50,100)};
        c.Initial.Layouts={Sheet(0,{{"squares",0,{10,10,0}},{"squares",1,{50,50,0}}}),Sheet(2,{{"tall",0,{25,0,0}}})};Prepare(c);r=Run(c);Require(r.Layouts.size()==2,"moved fixed fragmented targets");++tests;
        for(bool rotate:{false,true})
        {
            c=Easy();c.Input.Parts={Rectangle("moving",40,80,1,rotate?std::vector<double>{0,90}:std::vector<double>{0}),Rectangle("fixed",100,60)};
            c.Initial.Layouts={Sheet(0,{{"moving",0,{0,0,0}}}),Sheet(2,{{"fixed",0,{0,0,0}}})};Prepare(c);r=Run(c);Require(r.Layouts.size()==(rotate?1:2),"allowed rotation contract");++tests;
        }
        c=Easy();c.Input.Stocks.push_back({"other",100,100,1,1,"other-material"});
        c.Input.Parts[0].Material="other-material";c.Initial.Layouts[0].StockID="other";c.Initial.Layouts[0].StockInstance=0;Prepare(c);r=Run(c);Require(r.Layouts.size()==2,"crossed material groups");++tests;
        c=Easy();c.Input.Parts={Rectangle("moving",80,80),{}};c.Input.Parts[1].ID="circle";c.Input.Parts[1].Geometry.Radius=5;c.Input.Parts[1].Material="m";c.Input.Parts[1].FreeRotation=false;c.Input.Parts[1].AllowedAnglesDegrees={0};
        c.Initial.Layouts={Sheet(0,{{"moving",0,{0,0,0}}}),Sheet(2,{{"circle",0,{5,5,0}}})};Prepare(c);r=Run(c);Require(r.Layouts.size()==1,"did not skip circle donor and use rectangle donor");++tests;
        c=Easy();for(auto& p:c.Input.Parts[0].Geometry.Outer){p.X+=1000000;p.Y-=2000000;}c.Initial.Layouts[0].Placements[0].Transform={-1000000,2000000,0};Prepare(c);r=Run(c);Require(r.Layouts.size()==1,"lost rectangle local-coordinate offset");++tests;
        c=Easy();c.Input.Parts={Rectangle("small",20,20,3),Rectangle("anchor",60,100)};
        c.Initial.Layouts={Sheet(0,{{"small",0,{0,0,0}},{"small",1,{20,0,0}}}),Sheet(2,{{"small",2,{0,0,0}},{"anchor",0,{40,0,0}}})};Prepare(c);r=Run(c);Require(r.Layouts.size()==1&&r.Layouts[0].StockInstance==2,"same-Part original instance identity");++tests;
        c=Easy();Options cancelled;cancelled.Cancel=[]{return true;};r=Run(c,cancelled);Require(r.State==Status::Cancelled&&r.Layouts.size()==2&&r.Stats.PlacementAttempts==0,"pre-cancel changed incumbent or did placement work");++tests;
        std::size_t polls=0;cancelled.Cancel=[&]{return ++polls==5;};r=Run(c,cancelled);Require(r.State==Status::Cancelled&&r.Layouts.size()==2,"single-pulse cancellation not latched");++tests;
        c=Easy();c.Input.Parts={Rectangle("small",10,10,2),Rectangle("fixed",20,100)};
        c.Initial.Layouts={Sheet(0,{{"small",0,{0,0,0}},{"small",1,{10,0,0}}}),Sheet(2,{{"fixed",0,{0,0,0}}})};Prepare(c);Options one;one.MaxPlacementAttempts=1;r=Run(c,one);Require(r.Layouts.size()==2&&r.Stats.PlacementAttempts==1,"partial donor move did not roll back at attempt limit");Require(r.Diagnostics.back().find("attempt-limit")!=std::string::npos,"attempt truncation labelled exhausted candidates");++tests;
        c=Easy();c.Initial.Stats.PlacementAttempts=123456;c.Initial.Stats.CombinationGroups=1;c.Initial.Stats.GapFilledParts=3;
        for(auto& sheet:c.Initial.Layouts)for(auto& placed:sheet.Placements){placed.GapFiller=true;if(sheet.StockInstance==2)placed.Group=7;}
        r=Run(c);Require(r.Stats.PlacementAttempts<123456&&r.Stats.CombinationGroups==1&&r.Stats.CircleLatticeGroups==0&&r.Stats.GapFilledParts==3,"new-work versus selected-layout statistics contract");++tests;
        c=Easy();c.Input.Parts={Rectangle("small",20,20,3),Rectangle("anchor",60,100)};
        c.Initial.Layouts={Sheet(0,{{"small",0,{0,0,0},42},{"small",1,{20,0,0},42}}),Sheet(2,{{"small",2,{0,0,0},81},{"anchor",0,{40,0,0},81}})};Prepare(c);c.Initial.Stats.CombinationGroups=2;
        r=Run(c);Require(r.Layouts.size()==1&&r.Stats.CombinationGroups==2,"source construction-origin count was rewritten after detaching moved members");
        for(const auto& placed:r.Layouts[0].Placements)if(placed.PartID=="small"&&placed.Instance<2)Require(placed.Group==0,"moved source retained stale rigid/group tag");++tests;
        c=Easy();auto circle=Rectangle("circles",1,1,2);circle.Geometry={};circle.Geometry.Radius=5;c.Input.Parts={Rectangle("moving",20,20),circle,Rectangle("anchor",60,100)};
        c.Initial.Layouts={Sheet(0,{{"moving",0,{0,0,0}}}),Sheet(2,{{"circles",0,{5,5,0},9},{"circles",1,{15,5,0},9},{"anchor",0,{40,0,0}}})};Prepare(c);c.Initial.Stats.CircleLatticeGroups=1;
        r=Run(c);Require(r.Layouts.size()==1&&r.Stats.CircleLatticeGroups==1&&r.Stats.CombinationGroups==0,"fixed circle group miscounted during rectangle refill");++tests;
        c=Easy();while(c.Input.Parts.size()<2049){auto p=Rectangle("zero-"+std::to_string(c.Input.Parts.size()),1,1,0);c.Input.Parts.push_back(p);}Prepare(c);r=Run(c);Require(r.Layouts.size()==2&&r.Stats.PlacementAttempts==0,"out-of-scope input performed placements");++tests;
        c=Easy();c.Initial.GeometryValidated=false;r=MakeRectangleRefillStage().Factory()->Run(c.Input,c.Initial,{});Require(r.Layouts.size()==2&&r.Stats.PlacementAttempts==0,"unvalidated incumbent was processed");++tests;
        c=Easy();Options immediateTime;immediateTime.TimeLimitSeconds=1e-300;r=MakeRectangleRefillStage().Factory()->Run(c.Input,c.Initial,immediateTime);Require(r.Layouts.size()==2&&r.Stats.PlacementAttempts==0&&r.Diagnostics.back().find("time-limit")!=std::string::npos,"time truncation diagnostic");++tests;
        for(auto ranking:{RankingPolicy::RegularRemnant,RankingPolicy::LowThenLeft,RankingPolicy::CompactArea}){c=Easy();Options o;o.Ranking=ranking;r=Run(c,o);Require(r.Layouts.size()==1,"strict board prefix not retained for shared ranking");++tests;}
        for(auto ranking:{RankingPolicy::RegularRemnant,RankingPolicy::LowThenLeft,RankingPolicy::CompactArea})
        {
            c=Easy();c.Initial.Stats.PlacementAttempts=7;
            StrategyPipeline pipeline;
            pipeline.Construct=[initial=c.Initial]{return std::make_unique<RecordedInitial>(initial);};
            pipeline.PostProcessors.push_back(MakeRectangleRefillStage());
            StrategyRegistry registry;registry.Register("pipeline.refill",MakePipelineFactory(std::move(pipeline)));
            Options options;options.TimeLimitSeconds=1;options.MaxPlacementAttempts=100;options.Ranking=ranking;
            const auto result=RunJob({"refill-pipeline","pipeline.refill",c.Input,options},registry);
            Require(result.Checked.Valid&&result.Checked.Complete&&result.Output.Layouts.size()==1,
                "real pipeline did not accept a complete one-fewer-sheet result");
            Require(result.Output.Layouts[0].StockInstance==2&&c.Initial.Layouts.size()==2,
                "pipeline changed original stock identity or caller input");
            Require(result.Output.Stats.PlacementAttempts>7&&result.Output.Stats.PlacementAttempts<=100,
                "pipeline did not aggregate only the stage's new placement work");
            ++tests;
        }
        const auto defaults=DefaultStrategies().IDs();
        Require(std::find(defaults.begin(),defaults.end(),"rect.refill")==defaults.end(),
            "optional postprocessor was incorrectly registered as a default constructor");
        std::cout<<"RectangleRefillTests: "<<tests<<" checks passed\n";return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
