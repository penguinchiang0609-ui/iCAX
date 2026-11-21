#include "SheetNestingRotationCompaction.h"
#include "SheetNestingAxisCompaction.h"
#include "NestingExecution.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>

using namespace iCAX::SheetNesting;

namespace
{
    int Passed=0,Failed=0;
    void Require(bool value,const std::string& message)
    {if(!value)throw std::runtime_error(message);}
    void Test(const char* name,const std::function<void()>& body)
    {
        try{body();++Passed;std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& error){++Failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
    }
    Part Box(std::string id,double width,double height,bool free=false,Point origin={})
    {
        Part part;part.ID=std::move(id);part.Material="metal";part.FreeRotation=free;
        part.AllowedAnglesDegrees=free?std::vector<double>{}:std::vector<double>{0};
        part.Geometry.Outer={{origin.X,origin.Y},{origin.X+width,origin.Y},
            {origin.X+width,origin.Y+height},{origin.X,origin.Y+height}};
        return part;
    }
    Problem Sheet()
    {
        Problem problem;problem.Tolerance=1e-7;problem.EdgeMargin=2.5;
        problem.Stocks={{"sheet",200,160,2,1,"metal"}};return problem;
    }
    Result At(const Problem& problem,std::vector<Placement> placements)
    {
        Result result;result.State=Status::Complete;
        result.Layouts={{"sheet",0,problem.Stocks[0].Width,problem.Stocks[0].Height,std::move(placements),0}};
        return result;
    }
    Options Budget()
    {
        Options options;options.TimeLimitSeconds=20;options.MaxPlacementAttempts=500000;
        options.Ranking=RankingPolicy::LowThenLeft;return options;
    }
    std::string Signature(const Result& result)
    {
        std::ostringstream text;text.precision(17);
        for(const auto& layout:result.Layouts)
        {
            text<<layout.StockID<<'/'<<layout.StockInstance<<'/'<<layout.Width<<'/'<<layout.Height<<';';
            for(const auto& placement:layout.Placements)
                text<<placement.PartID<<'/'<<placement.Instance<<'/'<<placement.Transform.X<<'/'<<placement.Transform.Y
                    <<'/'<<placement.Transform.AngleDegrees<<'/'<<placement.Transform.Mirrored<<'/'<<placement.Group<<'/'<<placement.GapFiller<<';';
        }
        for(const auto& item:result.Remaining)text<<item.PartID<<'/'<<item.Quantity<<';';
        return text.str();
    }
    void Valid(const Problem& problem,const Result& result)
    {
        const auto checked=Validate(problem,result);std::string errors;
        for(const auto& error:checked.Errors)errors+=error+"; ";
        Require(checked.Valid&&checked.Complete,"original material validation failed: "+errors);
    }
    using Identity=std::pair<std::string,std::size_t>;
    using BoundPose=std::tuple<std::string,std::size_t,Placement>;
    std::map<Identity,BoundPose> Poses(const Result& result)
    {
        std::map<Identity,BoundPose> poses;
        for(const auto& layout:result.Layouts)for(const auto& placement:layout.Placements)
            Require(poses.emplace(Identity{placement.PartID,placement.Instance},
                BoundPose{layout.StockID,layout.StockInstance,placement}).second,"duplicate original identity");
        return poses;
    }
    bool Allowed(const Part& part,double angle)
    {
        if(!std::isfinite(angle))return false;
        if(part.FreeRotation)return true;
        const auto angles=part.AllowedAnglesDegrees.empty()?std::vector<double>{0}:part.AllowedAnglesDegrees;
        return std::any_of(angles.begin(),angles.end(),[&](double allowed)
        {return std::abs(std::remainder(angle-allowed,360.))<=1e-8;});
    }
    void Conserved(const Problem& problem,const Result& before,const Result& after)
    {
        Valid(problem,before);Valid(problem,after);
        Require(before.Layouts.size()==after.Layouts.size(),"rotation changed the consumed physical sheet count");
        const auto old=Poses(before),now=Poses(after);Require(old.size()==now.size(),"rotation changed the original inventory");
        for(const auto& [key,value]:old)
        {
            const auto found=now.find(key);Require(found!=now.end(),"original instance disappeared");
            const auto& next=found->second;const auto& a=std::get<2>(value);const auto& b=std::get<2>(next);
            Require(std::get<0>(value)==std::get<0>(next)&&std::get<1>(value)==std::get<1>(next),
                "rotation changed an original's physical stock instance");
            Require(a.Group==b.Group&&a.GapFiller==b.GapFiller,"rotation changed construction provenance");
            Require(a.Transform.Mirrored==b.Transform.Mirrored,"rotation changed original reflection parity");
            const auto part=std::find_if(problem.Parts.begin(),problem.Parts.end(),[&](const Part& p){return p.ID==key.first;});
            Require(part!=problem.Parts.end()&&Allowed(*part,b.Transform.AngleDegrees),"rotation violated the original finite angle domain");
        }
    }
    Result Axis(const Problem& problem,const Result& before,Options options=Budget())
    {return MakeAxisCompactionStage().Factory()->Run(problem,before,options);}
    Result Rotate(const Problem& problem,const Result& before,Options options=Budget())
    {return MakeRotationCompactionStage().Factory()->Run(problem,before,options);}
    double Envelope(const Problem& problem,const Result& result)
    {const auto checked=Validate(problem,result);Require(checked.Valid,"cannot measure an invalid original layout");return checked.UsedEnvelopeArea;}
    double Top(const Problem& problem,const Result& result)
    {
        double top=0;
        for(const auto& layout:result.Layouts)for(const auto& placement:layout.Placements)
        {
            const auto part=std::find_if(problem.Parts.begin(),problem.Parts.end(),[&](const Part& p){return p.ID==placement.PartID;});
            const auto box=Detail::Bounds(part->Geometry,placement.Transform);top=std::max(top,box.Y+box.Height);
        }
        return top;
    }
    bool HasRotationReport(const Result& result)
    {return std::any_of(result.Diagnostics.begin(),result.Diagnostics.end(),[](const std::string& text){return text.rfind("compact.rotation.v1",0)==0;});}
    JobResult Assessed(const Problem& problem,const Result& result)
    {JobResult job;job.Output=result;Detail::AssessCandidate(problem,job);Require(Detail::Eligible(job),"assessment rejected original geometry");return job;}
    struct BarFixture{Problem Input;Result Floating,Settled;};
    BarFixture FreeBar()
    {
        BarFixture fixture;fixture.Input=Sheet();fixture.Input.Parts={Box("bar",30,4,true,{-3.25,5.5})};
        fixture.Floating=At(fixture.Input,{{"bar",0,{50.125,60.375,37},19,true}});
        fixture.Settled=Axis(fixture.Input,fixture.Floating);Conserved(fixture.Input,fixture.Floating,fixture.Settled);
        return fixture;
    }
    class CapturedConstructor final:public IStrategy
    {
        Result Source;
    public:
        explicit CapturedConstructor(Result source):Source(std::move(source)){}
        Result Run(const Problem&,const Options&)override{return Source;}
    };
    JobResult ThroughPipeline(const Problem& problem,const Result& before,Options options=Budget())
    {
        StrategyPipeline pipeline;pipeline.Construct=[before]{return std::make_unique<CapturedConstructor>(before);};
        pipeline.PostProcessors.push_back(MakeRotationCompactionStage());
        StrategyRegistry registry;registry.Register("rotation-test",MakePipelineFactory(std::move(pipeline)));
        return RunJob({"rotation-job","rotation-test",problem,options},registry);
    }
}

int main()
{
    Test("left-pivot rotation and settling improve a free bar after fixed-angle axes stop",[]
    {
        const auto fixture=FreeBar();const auto saved=Signature(fixture.Settled);
        const auto stopped=Axis(fixture.Input,fixture.Settled);Conserved(fixture.Input,fixture.Settled,stopped);
        Require(std::abs(Envelope(fixture.Input,stopped)-Envelope(fixture.Input,fixture.Settled))<1e-6,
            "axis-only control still changed the material envelope");
        auto pivotProblem=fixture.Input;
        pivotProblem.Parts.push_back(Box("corner-block",.5,.5));
        auto pivotSource=fixture.Settled;
        pivotSource.Layouts[0].Placements.push_back({"corner-block",0,
            {pivotProblem.EdgeMargin,pivotProblem.EdgeMargin,0},0,false});
        Valid(pivotProblem,pivotSource);
        const auto oldPose=std::get<2>(Poses(pivotSource).at({"bar",0})).Transform;
        const auto oldBox=Detail::Bounds(pivotProblem.Parts[0].Geometry,oldPose);
        const auto flatBox=Detail::Bounds(pivotProblem.Parts[0].Geometry,{0,0,0});
        for(const bool center:{false,true})
        {
            auto canonical=pivotSource;
            canonical.Layouts[0].Placements[0].Transform={oldBox.X-flatBox.X,
                oldBox.Y-flatBox.Y,0};
            if(center)
            {
                canonical.Layouts[0].Placements[0].Transform.X+=(oldBox.Width-flatBox.Width)*.5;
                canonical.Layouts[0].Placements[0].Transform.Y+=(oldBox.Height-flatBox.Height)*.5;
            }
            Require(!Validate(pivotProblem,canonical).Valid,
                "canonical bbox registration was not blocked in the material-pivot isolation control");
        }
        RotationCompactionSettings pivotSettings;pivotSettings.MaxPasses=1;pivotSettings.SettleWithAxis=false;
        const auto pivoted=MakeRotationCompactionStage(pivotSettings).Factory()->Run(pivotProblem,pivotSource,Budget());
        Conserved(pivotProblem,pivotSource,pivoted);
        const auto newPose=std::get<2>(Poses(pivoted).at({"bar",0})).Transform;
        bool retainedPivot=false;
        for(const auto vertex:fixture.Input.Parts[0].Geometry.Outer)
        {
            const auto before=Detail::Transform(vertex,oldPose),after=Detail::Transform(vertex,newPose);
            retainedPivot=retainedPivot||std::hypot(before.X-after.X,before.Y-after.Y)<1e-7;
        }
        Require(retainedPivot&&Top(pivotProblem,pivoted)<Top(pivotProblem,pivotSource)-5,
            "single-pass rotation did not lower the part around an original material vertex");
        const auto rotated=Rotate(fixture.Input,fixture.Settled);Conserved(fixture.Input,fixture.Settled,rotated);
        Require(Envelope(fixture.Input,rotated)<Envelope(fixture.Input,stopped)*.5&&Top(fixture.Input,rotated)<Top(fixture.Input,stopped)-5,
            "rotation plus axis settling did not reduce the stopped free bar's envelope and height");
        Require(HasRotationReport(rotated),"rotation stage did not report its executed work");
        Require(Signature(fixture.Settled)==saved,"rotation mutated its input layout");
    });
    Test("finite original angles remain hard constraints while permitted turns can improve height",[]
    {
        auto fixture=FreeBar();fixture.Input.Parts[0].FreeRotation=false;fixture.Input.Parts[0].AllowedAnglesDegrees={37};
        const auto locked=Rotate(fixture.Input,fixture.Settled);Conserved(fixture.Input,fixture.Settled,locked);
        Require(locked.Layouts[0].Placements[0].Transform.AngleDegrees==37,"a singleton finite domain was treated as free rotation");
        auto problem=Sheet();auto bar=Box("bar",30,4,false,{-3.25,5.5});bar.AllowedAnglesDegrees={0,90};problem.Parts={bar};
        const auto before=Axis(problem,At(problem,{{"bar",0,{50,60,90},7,false}}));
        const auto after=Rotate(problem,before);Conserved(problem,before,after);
        Require(Top(problem,after)<Top(problem,before)-10,"permitted 90-to-0 turn did not lower the original bar");
    });
    Test("permitted turns and numerical retreats retain original reflection",[]
    {
        auto problem=Sheet();auto bar=Box("mirrored-bar",30,4,false,{-3.25,5.5});
        bar.AllowMirror=true;bar.AllowedAnglesDegrees={0,90};problem.Parts={bar};
        const auto before=Axis(problem,At(problem,{{"mirrored-bar",0,{50,60,90,true},7,true}}));
        const auto signature=Signature(before);
        const auto after=Rotate(problem,before);Conserved(problem,before,after);
        Require(after.Layouts[0].Placements[0].Transform.Mirrored&&Top(problem,after)<Top(problem,before)-10,
            "registered mirrored rotations lost reflection or could not preserve it while lowering the original bar");
        Require(Signature(before)==signature,"mirrored rotation mutated its source pose");
    });
    Test("in-place half-turn registration releases a fit that every material vertex pivot rejects",[]
    {
        auto problem=Sheet();problem.EdgeMargin=0;problem.Stocks[0].Width=40;problem.Stocks[0].Height=10;
        auto elbow=Box("elbow",1,1);elbow.Quantity=2;elbow.AllowedAnglesDegrees={0,180};
        elbow.Geometry.Outer={{0,0},{10,0},{10,2},{2,2},{2,10},{0,10}};problem.Parts={elbow};
        const auto initial=At(problem,{{"elbow",0,{0,0,0},13,false},{"elbow",1,{10,0,0},13,true}});
        const auto before=Axis(problem,initial);Conserved(problem,initial,before);
        Require(std::abs(Envelope(problem,before)-200)<1e-7,"axis-only half-turn fixture did not retain its blocked envelope");
        const auto signature=Signature(before);
        std::size_t feasibleVertexTurns=0;
        for(std::size_t i=0;i<before.Layouts[0].Placements.size();++i)
            for(const auto vertex:elbow.Geometry.Outer)
            {
                auto trial=before;const auto old=trial.Layouts[0].Placements[i].Transform;
                const auto pivot=Detail::Transform(vertex,old);
                const auto turnedVertex=Detail::Transform(vertex,{0,0,180});
                trial.Layouts[0].Placements[i].Transform={pivot.X-turnedVertex.X,pivot.Y-turnedVertex.Y,180};
                if(Validate(problem,trial).Valid)++feasibleVertexTurns;
            }
        Require(feasibleVertexTurns==0,"material-vertex half turns were feasible in the registration isolation control");
        const auto after=Rotate(problem,before);Conserved(problem,before,after);
        Require(Envelope(problem,after)<=120+1e-6,
            "in-place bbox registration followed by settling did not release the blocked half-turn packing");
        Require(std::any_of(after.Layouts[0].Placements.begin(),after.Layouts[0].Placements.end(),[](const Placement& placement)
            {return std::abs(std::remainder(placement.Transform.AngleDegrees-180.,360.))<1e-8;}),
            "the original finite domain was not used for an actual half turn");
        Require(Signature(before)==signature,"half-turn registration mutated its source placements");
    });
    Test("free oblique polygons can use a non-quarter-turn edge alignment",[]
    {
        auto problem=Sheet();auto part=Box("oblique",1,1,true);
        part.Geometry.Outer={{.375,-.625},{24.375,9.375},{22.375,13.375},{-1.625,3.375}};problem.Parts={part};
        const auto before=Axis(problem,At(problem,{{part.ID,0,{40.25,50.75,0},3,false}}));
        const auto after=Rotate(problem,before);Conserved(problem,before,after);
        const auto angle=after.Layouts[0].Placements[0].Transform.AngleDegrees;
        Require(std::abs(std::remainder(angle,90.))>.1,"free polygon refinement only considered quarter turns");
        Require(Top(problem,after)<Top(problem,before)-2&&Envelope(problem,after)<Envelope(problem,before)*.8,
            "original oblique edge alignment did not improve its actual material envelope");
    });
    Test("positive gap margin nonzero source origins and circle angles survive refinement",[]
    {
        auto problem=Sheet();problem.PartGap=1.25;problem.EdgeMargin=2.75;
        auto moving=Box("moving",30,4,true,{-3.25,5.5});auto circle=Box("circle",1,1,true);
        circle.Geometry.Outer.clear();circle.Geometry.Radius=5;circle.Geometry.Center={1.125,-2.375};
        problem.Parts={Box("fixed",12,12,false,{.375,-.625}),moving,circle};
        const auto before=At(problem,{{"fixed",0,{2.375,3.375,0},0,false},
            {"moving",0,{50.125,60.375,37},4,true},{"circle",0,{100.25,90.5,17.25},0,false}});
        const auto after=Rotate(problem,before);Conserved(problem,before,after);
        const auto poses=Poses(after);
        Require(std::get<2>(poses.at({"circle",0})).Transform.AngleDegrees==17.25,"circle's authoritative angle changed");
        Require(after.Stats.PlacementAttempts<=Budget().MaxPlacementAttempts,"rotation and axis settling exceeded their shared attempt cap");
    });
    Test("concave originals and holed material retain geometry and inventory",[]
    {
        auto problem=Sheet();auto concave=Box("concave",1,1,true);
        concave.Geometry.Outer={{0,0},{3,0},{3,10},{12,10},{12,13},{0,13}};problem.Parts={concave};
        const auto before=Axis(problem,At(problem,{{"concave",0,{35.125,40.375,23.7},8,true}}));
        const auto after=Rotate(problem,before);Conserved(problem,before,after);
        auto frame=Box("frame",30,30);frame.Geometry.Holes={{{8,8},{22,8},{22,22},{8,22}}};
        problem.Parts={frame,Box("insert",8,2,true)};
        const auto inside=At(problem,{{"frame",0,{20,20,0},0,false},{"insert",0,{31,31,37},9,true}});
        const auto refined=Rotate(problem,inside);Conserved(problem,inside,refined);
    });
    Test("cancelled or exhausted rotation work retains source geometry and truthful work limits",[]
    {
        const auto fixture=FreeBar();const auto signature=Signature(fixture.Settled);
        for(const int stop:{0,1,2})
        {
            auto options=Budget();
            if(stop==0)options.Cancel=[]{return true;};
            if(stop==1)options.TimeLimitSeconds=0;
            if(stop==2)options.MaxPlacementAttempts=0;
            const auto after=Rotate(fixture.Input,fixture.Settled,options);Conserved(fixture.Input,fixture.Settled,after);
            Require(Signature(after)==signature,"unstarted rotation work changed an original pose");
            Require(after.Stats.PlacementAttempts==0,"unstarted rotation work reported candidate attempts");
        }
        auto options=Budget();options.MaxPlacementAttempts=1;
        const auto limited=Rotate(fixture.Input,fixture.Settled,options);Conserved(fixture.Input,fixture.Settled,limited);
        Require(limited.Stats.PlacementAttempts<=1,"rotation or follow-up axis work escaped the attempt budget");
    });
    Test("normal RunJob pipeline retains a genuine shared-quality improvement",[]
    {
        const auto fixture=FreeBar();auto options=Budget();options.Ranking=RankingPolicy::RegularRemnant;
        const auto before=Assessed(fixture.Input,fixture.Settled);
        const auto job=ThroughPipeline(fixture.Input,fixture.Settled,options);
        Require(job.Checked.Valid&&job.Checked.Complete,"normal pipeline did not return a complete original layout");
        Conserved(fixture.Input,fixture.Settled,job.Output);
        Require(Envelope(fixture.Input,job.Output)<Envelope(fixture.Input,fixture.Settled)*.5,
            "normal pipeline did not publish the rotation improvement");
        Require(Detail::Quality(job,fixture.Input,options.Ranking)<Detail::Quality(before,fixture.Input,options.Ranking),
            "normal pipeline published a result without improving the common final quality");
    });
    Test("normal pipeline also preserves singleton finite domains and original group identities",[]
    {
        auto fixture=FreeBar();fixture.Input.Parts[0].FreeRotation=false;fixture.Input.Parts[0].AllowedAnglesDegrees={37};
        const auto job=ThroughPipeline(fixture.Input,fixture.Settled);Conserved(fixture.Input,fixture.Settled,job.Output);
        Require(job.Output.Layouts[0].Placements[0].Transform.AngleDegrees==37,"normal entry widened a finite original angle domain");
    });
    std::cout<<Passed<<" passed, "<<Failed<<" failed\n";return Failed?1:0;
}
