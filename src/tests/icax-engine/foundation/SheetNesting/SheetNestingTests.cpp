#include "SheetNesting.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
    int Passed = 0, Failed = 0;
    void Require(bool condition, const std::string& message)
    { if (!condition) throw std::runtime_error(message); }
    void Test(const char* name, const std::function<void()>& body)
    {
        try { body(); ++Passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++Failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    }
    Shape Box(double w, double h)
    { Shape s; s.Outer = {{0,0},{w,0},{w,h},{0,h}}; return s; }
    Shape Triangle(double w, double h)
    { Shape s; s.Outer = {{0,0},{w,0},{0,h}}; return s; }
    Part MakePart(std::string id, Shape geometry, std::size_t quantity = 1)
    { Part p; p.ID = std::move(id); p.Geometry = std::move(geometry); p.Quantity = quantity; return p; }
    Stock Sheet(std::string id, double w, double h, std::size_t quantity = 1)
    { Stock s; s.ID=std::move(id); s.Width=w; s.Height=h; s.Quantity=quantity; return s; }
    Options Generous()
    { Options o; o.TimeLimitSeconds=30; o.MaxPlacementAttempts=1000000; o.MaxPatterns=512; return o; }
    std::size_t Placed(const Result& r)
    { std::size_t n=0; for (const auto& l:r.Layouts) n+=l.Placements.size(); return n; }
    std::size_t Missing(const Result& r)
    { std::size_t n=0; for (const auto& u:r.Remaining) n+=u.Quantity; return n; }
    std::string Errors(const Validation& v)
    { std::string s; for (const auto& e:v.Errors) s+=e+"; "; return s; }
    JobResult Solve(const Problem& p, std::string strategy="rules.composite", Options o=Generous())
    { return RunJob({"test-job", std::move(strategy), p, std::move(o)}, DefaultStrategies()); }
    void Valid(const Problem& p, const Result& r, bool complete=false)
    {
        const auto v=Validate(p,r);
        Require(v.Valid,Errors(v));
        if (complete) Require(v.Complete,"expected a complete validated result");
    }
    Result Manual(const Problem& p, std::vector<Placement> placements)
    {
        Result r; r.State=Status::Complete;
        Layout l; l.StockID=p.Stocks[0].ID; l.Width=p.Stocks[0].Width; l.Height=p.Stocks[0].Height;
        l.Placements=std::move(placements); r.Layouts.push_back(std::move(l)); return r;
    }
    Placement At(std::string id, std::size_t instance, double x, double y, double angle=0)
    { Placement p; p.PartID=std::move(id); p.Instance=instance; p.Transform={x,y,angle}; return p; }
    Problem SingleBox()
    { Problem p; p.Parts={MakePart("part",Box(10,10))}; p.Stocks={Sheet("sheet",30,30)}; return p; }
    std::string Signature(const Result& r)
    {
        std::ostringstream s; s<<static_cast<int>(r.State)<<std::fixed<<std::setprecision(8);
        for (const auto& l:r.Layouts)
        {
            s<<'|'<<l.StockID<<':'<<l.StockInstance;
            for (const auto& p:l.Placements)
                s<<';'<<p.PartID<<':'<<p.Instance<<':'<<p.Transform.X<<':'<<p.Transform.Y<<':'<<p.Transform.AngleDegrees<<':'<<p.Group<<':'<<p.GapFiller;
        }
        for (const auto& u:r.Remaining) s<<" missing:"<<u.PartID<<':'<<u.Quantity;
        return s.str();
    }
    class CustomStrategy final : public IStrategy
    {
    public:
        enum Mode { Good, InvalidGeometry, Throw, FailedState, Stateful, CancelledState };
        explicit CustomStrategy(Mode mode):ModeValue(mode) {}
        Result Run(const Problem& p,const Options&) override
        {
            ++Runs;
            if (ModeValue==Throw) throw std::runtime_error("intentional strategy exception");
            auto r=Manual(p,{At(p.Parts[0].ID,0,0,0)});
            if (ModeValue==InvalidGeometry) { r.Layouts[0].Placements[0].Transform.X=-20; r.GeometryValidated=true; }
            if (ModeValue==FailedState) r.State=Status::Failed;
            if (ModeValue==CancelledState)
            {
                r.State=Status::Cancelled;
                if(p.Parts[0].Quantity>1)r.Remaining.push_back({p.Parts[0].ID,p.Parts[0].Quantity-1});
            }
            if (ModeValue==Stateful && Runs!=1) throw std::runtime_error("strategy instance reused across jobs");
            r.Diagnostics.push_back("local-run="+std::to_string(Runs));
            return r;
        }
    private: Mode ModeValue; int Runs=0;
    };
}

int main()
{
    Test("classify analytic and polygon rules",[] {
        Require(Classify(Box(80,40)).Kind==ShapeKind::Rectangle,"rectangle");
        Require(Classify(Triangle(80,40)).Kind==ShapeKind::Triangle,"triangle");
        Shape p; p.Outer={{0,0},{80,0},{100,40},{20,40}};
        Require(Classify(p).Kind==ShapeKind::Parallelogram,"parallelogram");
        p.Outer={{0,0},{100,0},{80,40},{20,40}};
        Require(Classify(p).Kind==ShapeKind::Trapezoid,"trapezoid");
        p.Outer={{0,0},{100,0},{100,49},{99,50},{0,50}};
        Require(Classify(p).Kind==ShapeKind::NearRectangle,"near rectangle");
        p={}; p.Radius=10; p.Center={15,-8};
        auto c=Classify(p); Require(c.Kind==ShapeKind::Circle,"circle");
        Require(std::abs(c.Area-100*3.14159265358979323846)<1e-7,"circle true area");
        p=Box(80,40); const double a=37*3.14159265358979323846/180;
        for (auto& q:p.Outer) { const double x=q.X,y=q.Y; q={x*std::cos(a)-y*std::sin(a),x*std::sin(a)+y*std::cos(a)}; }
        Require(Classify(p).Kind==ShapeKind::Rectangle,"rotated rectangle recognition");
    });
    Test("six triangles form three rectangles",[] {
        Problem p; p.Parts={MakePart("triangle",Triangle(100,100),6)}; p.Stocks={Sheet("sheet",300,100)};
        auto plain=Generous(); plain.EnableCombinations=false; plain.EnableGapFill=false;
        const auto baseline=Solve(p,"rect.short-side",plain);
        Valid(p,baseline.Output); Require(Placed(baseline.Output)==3,"uncombined envelopes should place three triangles");
        const auto combined=Solve(p);
        Valid(p,combined.Output,true); Require(Placed(combined.Output)==6,"combination must fit all six");
        Require(combined.Output.Stats.CombinationGroups>0,"combination evidence");
        std::map<std::size_t,int> identities;
        for(const auto& v:combined.Output.Layouts[0].Placements) ++identities[v.Instance];
        Require(identities.size()==6,"six original instance identities");
    });
    Test("overlapping envelopes do not imply material collision",[] {
        Problem p; p.Parts={MakePart("triangle",Triangle(100,100),2)}; p.Stocks={Sheet("sheet",100,100)};
        Valid(p,Manual(p,{At("triangle",0,0,0),At("triangle",1,100,100,180)}),true);
    });
    Test("oblique separation survives fixed precision clipping",[] {
        Shape a; a.Outer={{-2,-2},{2,-2},{-2,2}};
        Shape b; b.Outer={{2.5,-2.5},{2.5,2.5},{-2.5,2.5}};
        Problem p; p.Tolerance=1e-7; p.Parts={MakePart("a",a),MakePart("b",b)};
        p.Stocks={Sheet("sheet",100,100)};
        const double ax=std::nextafter(26./3.,std::numeric_limits<double>::infinity());
        const double ay=74./3., bx=8.5, by=149./6.;
        auto exact=Manual(p,{At("a",0,ax,ay,180),At("b",0,bx,by,180)});
        Valid(p,exact,true);
        std::reverse(exact.Layouts[0].Placements.begin(),exact.Layouts[0].Placements.end());
        Valid(p,exact,true);
        // An actual intrusion and a required positive clearance must still fail;
        // the separation check does not expand the geometric tolerance.
        Require(!Validate(p,Manual(p,{At("a",0,ax-1e-5,ay,180),At("b",0,bx,by,180)})).Valid,
            "real oblique material overlap escaped validation");
        p.PartGap=1e-4;
        Require(!Validate(p,Manual(p,{At("a",0,ax,ay,180),At("b",0,bx,by,180)})).Valid,
            "oblique contact escaped positive clearance");
        p.PartGap=0;
        Valid(p,Manual(p,{At("a",0,ax+1e-4,ay,180),At("b",0,bx,by,180)}),true);
    });
    Test("parallelogram rule tiles four parts at eighty millimetre pitch",[] {
        Shape shape; shape.Outer={{0,0},{80,0},{100,40},{20,40}};
        Problem p; p.Parts={MakePart("parallelogram",shape,4)}; p.Stocks={Sheet("sheet",340,40)};
        auto plain=Generous(); plain.EnableCombinations=false; plain.EnableGapFill=false;
        const auto baseline=Solve(p,"rect.short-side",plain);
        Valid(p,baseline.Output); Require(Placed(baseline.Output)==3,"three separate hundred millimetre envelopes");
        auto combined=Generous(); combined.EnableGapFill=false;
        const auto result=Solve(p,"rules.composite",combined);
        Valid(p,result.Output,true); Require(Placed(result.Output)==4,"four actual parallelograms fit a 340 millimetre strip");
        Require(result.Output.Stats.CombinationGroups>0,"rule combination evidence");
    });
    Test("circle hexagonal rule beats square grid",[] {
        Shape c; c.Radius=10;
        Problem p; p.Parts={MakePart("circle",c,23)}; p.Stocks={Sheet("sheet",100,90)};
        const auto result=Solve(p,"rules.circles");
        Valid(p,result.Output,true); Require(Placed(result.Output)==23,"five hexagonal rows fit 23 circles");
        Require(result.Output.Stats.CircleLatticeGroups>0,"circle lattice evidence");
    });
    Test("real holes remain available to small fillers",[] {
        auto frame=Box(200,200); frame.Holes={{{60,60},{60,140},{140,140},{140,60}}};
        Problem p; p.Parts={MakePart("frame",frame),MakePart("small",Box(40,40),4)}; p.Stocks={Sheet("sheet",200,200)};
        const auto result=Solve(p);
        Valid(p,result.Output,true); Require(Placed(result.Output)==5,"four small parts must fill the frame hole");
        Require(result.Output.Stats.GapFilledParts==4,"gap-filler accounting");
        for(const auto& q:result.Output.Layouts[0].Placements)
            if(q.PartID=="small") Require(q.GapFiller,"small placement must be identified as a filler");
    });
    Test("hole clearance checks material not outer envelope",[] {
        auto frame=Box(100,100); frame.Holes={{{20,20},{20,80},{80,80},{80,20}}};
        Problem p; p.Parts={MakePart("frame",frame),MakePart("inner",Box(30,30))}; p.Stocks={Sheet("sheet",100,100)}; p.PartGap=10;
        Valid(p,Manual(p,{At("frame",0,0,0),At("inner",0,35,35)}),true);
        Require(!Validate(p,Manual(p,{At("frame",0,0,0),At("inner",0,25,25)})).Valid,"insufficient distance from hole wall");
    });
    Test("positive part gap and edge margin",[] {
        Problem p; p.Parts={MakePart("square",Box(50,50),4)}; p.Stocks={Sheet("sheet",120,120)};
        p.PartGap=5; p.EdgeMargin=2;
        auto r=Solve(p,"rect.short-side"); Valid(p,r.Output,true);
        p=SingleBox(); p.Parts[0].Quantity=2; p.Stocks[0].Width=30; p.Stocks[0].Height=10; p.PartGap=2;
        Valid(p,Manual(p,{At("part",0,0,0),At("part",1,12,0)}),true);
        Require(!Validate(p,Manual(p,{At("part",0,0,0),At("part",1,10,0)})).Valid,"touching parts violate a positive gap");
    });
    Test("trapezoid and near rectangle jobs produce legal geometry",[] {
        Shape t; t.Outer={{0,0},{100,0},{80,40},{20,40}};
        Shape n; n.Outer={{0,0},{100,0},{100,49},{99,50},{0,50}};
        Problem p; p.Parts={MakePart("trapezoid",t,4),MakePart("near",n,3)}; p.Stocks={Sheet("sheet",400,200,2)}; p.PartGap=1;
        const auto r=Solve(p); Valid(p,r.Output,true);
    });
    Test("finite stock yields explicit remaining quantities",[] {
        Problem p; p.Parts={MakePart("square",Box(60,60),3)}; p.Stocks={Sheet("sheet",100,100,2)};
        const auto r=Solve(p); Valid(p,r.Output);
        Require(r.Output.State==Status::Partial,"finite-stock partial status");
        Require(r.Output.Layouts.size()==2 && Placed(r.Output)==2 && Missing(r.Output)==1,"finite-stock conservation");
    });
    Test("materials and fixed angle constraints",[] {
        auto p=SingleBox(); p.Parts[0].Material="steel"; p.Stocks[0].Material="aluminium";
        auto r=Solve(p); Require(Placed(r.Output)==0 && Missing(r.Output)==1,"incompatible material cannot be packed"); Valid(p,r.Output);
        p=SingleBox(); p.Parts[0].Geometry=Box(20,10); p.Parts[0].FreeRotation=false; p.Parts[0].AllowedAnglesDegrees={0};
        Require(!Validate(p,Manual(p,{At("part",0,10,0,90)})).Valid,"forbidden rotation rejected");
        p.Parts[0].AllowedAnglesDegrees={37};
        r=Solve(p); Valid(p,r.Output,true);
        Require(std::abs(r.Output.Layouts[0].Placements[0].Transform.AngleDegrees-37)<1e-5,"non-cardinal explicit angle honored");
    });
    Test("validator rejects duplicate identities and stock overuse",[] {
        auto p=SingleBox(); p.Parts[0].Quantity=2;
        Require(!Validate(p,Manual(p,{At("part",0,0,0),At("part",0,15,0)})).Valid,"duplicate part instance");
        auto r=Manual(p,{At("part",0,0,0)}); auto second=r.Layouts[0]; second.Placements={At("part",1,0,0)}; second.StockInstance=1; r.Layouts.push_back(second);
        Require(!Validate(p,r).Valid,"stock instance exceeds inventory");
        second.StockInstance=0; r.Layouts[1]=second;
        Require(!Validate(p,r).Valid,"same physical sheet reused");
    });
    Test("invalid input: self intersection and invalid holes",[] {
        auto p=SingleBox(); p.Parts[0].Geometry.Outer={{0,0},{10,10},{0,10},{10,0}};
        Require(Solve(p).Output.State==Status::InvalidInput,"self intersection");
        p=SingleBox(); p.Parts[0].Geometry.Holes={{{15,15},{15,17},{17,17},{17,15}}};
        Require(Solve(p).Output.State==Status::InvalidInput,"hole outside outer ring");
        p=SingleBox(); p.Parts[0].Geometry.Holes={{{1,1},{1,7},{7,7},{7,1}},{{5,5},{5,9},{9,9},{9,5}}};
        Require(Solve(p).Output.State==Status::InvalidInput,"overlapping holes");
    });
    Test("invalid input: duplicate IDs and non-finite numbers",[] {
        auto p=SingleBox(); p.Parts.push_back(p.Parts[0]);
        Require(Solve(p).Output.State==Status::InvalidInput,"duplicate part ID");
        p=SingleBox(); p.Stocks.push_back(p.Stocks[0]); Require(Solve(p).Output.State==Status::InvalidInput,"duplicate stock ID");
        p=SingleBox(); p.PartGap=std::numeric_limits<double>::quiet_NaN(); Require(Solve(p).Output.State==Status::InvalidInput,"NaN gap");
        p=SingleBox(); p.Stocks[0].Width=std::numeric_limits<double>::infinity(); Require(Solve(p).Output.State==Status::InvalidInput,"infinite stock width");
        p=SingleBox(); p.Parts[0].Geometry.Outer[0].X=std::numeric_limits<double>::quiet_NaN(); Require(Solve(p).Output.State==Status::InvalidInput,"NaN point");
        p=SingleBox(); p.Parts[0].AllowedAnglesDegrees={std::numeric_limits<double>::infinity()}; Require(Solve(p).Output.State==Status::InvalidInput,"infinite angle");
        p=SingleBox(); p.PartGap=-1; Require(Solve(p).Output.State==Status::InvalidInput,"negative gap");
    });
    Test("strategy objects and results are job-local",[] {
        std::atomic<int> creations{0}; StrategyRegistry registry;
        registry.Register("fresh",[&] { ++creations; return std::make_unique<CustomStrategy>(CustomStrategy::Stateful); });
        auto p=SingleBox(); const auto a=RunJob({"a","fresh",p,Generous()},registry); const auto b=RunJob({"b","fresh",p,Generous()},registry);
        Require(creations==2,"factory must run once per job"); Require(a.Checked.Valid && b.Checked.Valid,"job results valid");
        Require(std::count(a.Output.Diagnostics.begin(),a.Output.Diagnostics.end(),"local-run=1")==1 &&
            std::count(b.Output.Diagnostics.begin(),b.Output.Diagnostics.end(),"local-run=1")==1,"state was not shared");
        Require(a.ID=="a" && b.ID=="b","job identities retained");
    });
    Test("invalid failed and throwing strategies cannot win",[] {
        StrategyRegistry registry;
        registry.Register("bad",[] { return std::make_unique<CustomStrategy>(CustomStrategy::InvalidGeometry); });
        registry.Register("throw",[] { return std::make_unique<CustomStrategy>(CustomStrategy::Throw); });
        registry.Register("failed",[] { return std::make_unique<CustomStrategy>(CustomStrategy::FailedState); });
        registry.Register("good",[] { return std::make_unique<CustomStrategy>(CustomStrategy::Good); });
        PortfolioOptions o; o.PerJob=Generous(); o.Workers=3;
        const auto r=RunPortfolio(SingleBox(),o,registry);
        Require(r.Jobs.size()==4 && r.BestJob<r.Jobs.size(),"every job must have an explicit result");
        Require(r.Jobs[r.BestJob].StrategyID=="good","invalid/failed strategy must not win");
        for(const auto& j:r.Jobs) if(j.StrategyID=="throw") Require(j.Output.State==Status::Failed && !j.Output.Diagnostics.empty(),"exception diagnostic");
        registry=StrategyRegistry{}; registry.Register("bad",[] { return std::make_unique<CustomStrategy>(CustomStrategy::InvalidGeometry); });
        const auto none=RunPortfolio(SingleBox(),o,registry); Require(none.BestJob==static_cast<std::size_t>(-1),"no invalid fallback winner");
    });
    Test("validated partial cancellation remains available without changing its status",[] {
        StrategyRegistry registry; registry.Register("cancelled",[] { return std::make_unique<CustomStrategy>(CustomStrategy::CancelledState); });
        auto p=SingleBox(); p.Parts[0].Quantity=2; PortfolioOptions options; options.PerJob=Generous();
        const auto result=RunPortfolio(p,options,registry);
        Require(result.BestJob==0,"retain the last legal partial result after cancellation");
        const auto& best=result.Jobs[result.BestJob];
        Require(best.Output.State==Status::Cancelled,"do not relabel cancelled as complete or partial");
        Require(best.Checked.Valid && !best.Checked.Complete && Placed(best.Output)==1 && Missing(best.Output)==1,"cancelled output conservation");
    });
    Test("registry duplicate unknown null and throwing factories are explicit errors",[] {
        StrategyRegistry registry; registry.Register("good",[] { return std::make_unique<CustomStrategy>(CustomStrategy::Good); });
        bool duplicate=false,empty=false,nullFactory=false;
        try { registry.Register("good",[] { return std::make_unique<CustomStrategy>(CustomStrategy::Good); }); } catch(const std::invalid_argument&) { duplicate=true; }
        try { registry.Register("",[] { return std::make_unique<CustomStrategy>(CustomStrategy::Good); }); } catch(const std::invalid_argument&) { empty=true; }
        try { registry.Register("missing",{}); } catch(const std::invalid_argument&) { nullFactory=true; }
        Require(duplicate && empty && nullFactory,"registry validation");
        registry.Register("null",[] { return std::unique_ptr<IStrategy>{}; });
        registry.Register("factory-throws",[]()->std::unique_ptr<IStrategy> { throw std::runtime_error("intentional factory error"); });
        for(const std::string id:{"unknown","null","factory-throws"})
        {
            const auto r=RunJob({id,id,SingleBox(),Generous()},registry);
            Require(r.Output.State==Status::Failed && !r.Output.Diagnostics.empty(),"factory failure diagnostic: "+id);
            Require(Missing(r.Output)==1 && Placed(r.Output)==0,"factory failure demand accounting");
        }
        const auto none=RunPortfolio(SingleBox(),{},StrategyRegistry{});
        Require(none.Jobs.empty() && none.BestJob==static_cast<std::size_t>(-1),"empty registry has no winner");
    });
    Test("serial and parallel portfolio choose the same validated result",[] {
        Problem p; p.Parts={MakePart("rect",Box(65,35),7),MakePart("triangle",Triangle(40,40),6)}; p.Stocks={Sheet("sheet",200,150,3)}; p.PartGap=1;
        PortfolioOptions o; o.PerJob=Generous(); o.Workers=1; const auto serial=RunPortfolio(p,o);
        o.Workers=4; const auto parallel=RunPortfolio(p,o);
        const auto expectedJobs=DefaultStrategies().IDs().size();
        Require(serial.Jobs.size()==expectedJobs && parallel.Jobs.size()==expectedJobs && expectedJobs>=5,"independent default jobs");
        Require(serial.BestJob<serial.Jobs.size() && parallel.BestJob<parallel.Jobs.size(),"valid winner");
        Require(serial.Jobs[serial.BestJob].StrategyID==parallel.Jobs[parallel.BestJob].StrategyID,"winner differs with parallel execution");
        for(std::size_t i=0;i<serial.Jobs.size();++i)
        {
            Require(serial.Jobs[i].StrategyID==parallel.Jobs[i].StrategyID,"stable job ordering");
            Require(Signature(serial.Jobs[i].Output)==Signature(parallel.Jobs[i].Output),"job result changed under parallel execution");
            Valid(p,serial.Jobs[i].Output);
        }
    });
    Test("cancellation and attempt budget keep explicit accounting",[] {
        auto p=SingleBox(); p.Parts[0].Quantity=100; p.Stocks[0].Quantity=100;
        auto options=Generous(); options.Cancel=[] { return true; };
        auto r=Solve(p,"rect.short-side",options);
        Require(r.Output.State==Status::Cancelled,"cancelled status");
        Require(Placed(r.Output)+Missing(r.Output)==100,"cancelled quantities conserved");
        options=Generous(); options.MaxPlacementAttempts=1;
        r=Solve(p,"rect.short-side",options); Valid(p,r.Output);
        Require(Placed(r.Output)+Missing(r.Output)==100 && Missing(r.Output)>0,"budget remainder");
        Require(!r.Output.Diagnostics.empty(),"budget stop diagnostic");
        options=Generous(); options.TimeLimitSeconds=1e-12;
        r=Solve(p,"rect.short-side",options);
        Require(Placed(r.Output)+Missing(r.Output)==100,"time budget quantities conserved");
        Require(!r.Output.Diagnostics.empty(),"time stop diagnostic");
    });
    std::cout<<"\n"<<Passed<<" passed, "<<Failed<<" failed\n";
    return Failed==0?0:1;
}
