#include "SheetNesting.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
    int Passed=0,Failed=0;
    void Require(bool condition,const std::string& text) { if(!condition)throw std::runtime_error(text); }
    void Test(const char* name,const std::function<void()>& body)
    {
        try { body(); ++Passed; std::cout<<"PASS "<<name<<'\n'; }
        catch(const std::exception& e) { ++Failed; std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n'; }
    }
    Shape Rectangle(double w,double h) { Shape s; s.Outer={{0,0},{w,0},{w,h},{0,h}}; return s; }
    Shape Circle(double radius) { Shape s; s.Radius=radius; return s; }
    Part PartOf(std::string id,Shape shape,std::size_t quantity)
    { Part p; p.ID=std::move(id); p.Geometry=std::move(shape); p.Quantity=quantity; p.FreeRotation=false; p.AllowedAnglesDegrees={0}; return p; }
    Stock StockOf(std::string id,double width,double height,std::size_t quantity=1)
    { Stock s; s.ID=std::move(id); s.Width=width; s.Height=height; s.Quantity=quantity; return s; }
    Options Basic()
    {
        Options o; o.TimeLimitSeconds=30; o.MaxPlacementAttempts=1000000; o.MaxPatterns=512;
        o.EnableCombinations=false; o.EnableGapFill=false; return o;
    }
    JobResult Solve(const Problem& p,const std::string& strategy,Options o=Basic())
    { return RunJob({"mixed-test",strategy,p,std::move(o)},DefaultStrategies()); }
    std::size_t Count(const Result& r,const std::string& id={})
    {
        std::size_t n=0; for(const auto& l:r.Layouts)for(const auto& q:l.Placements)if(id.empty()||q.PartID==id)++n; return n;
    }
    std::size_t Remaining(const Result& r,const std::string& id)
    { for(const auto& u:r.Remaining)if(u.PartID==id)return u.Quantity; return 0; }
    void Valid(const Problem& p,const JobResult& r,bool complete)
    {
        const auto v=Validate(p,r.Output); std::string errors;
        for(const auto& e:v.Errors)errors+=e+"; ";
        Require(v.Valid,errors);
        std::string missing;for(const auto& u:r.Output.Remaining)missing+=u.PartID+"="+std::to_string(u.Quantity)+" ";
        Require(v.Complete==complete,"unexpected completeness in "+r.StrategyID+"; placed="+std::to_string(v.PlacedCount)+"; remaining "+missing);
        Require(r.Output.GeometryValidated,"job must carry validation outcome");
    }
    Problem MixedRectangles()
    {
        Problem p; p.Parts={PartOf("R60x40",Rectangle(60,40),2),PartOf("R40x40",Rectangle(40,40),2)};
        p.Stocks={StockOf("100x80",100,80)}; return p;
    }
    Problem MixedParallelograms()
    {
        Shape a; a.Outer={{0,0},{80,0},{100,40},{20,40}};
        Shape b; b.Outer={{0,0},{60,0},{80,40},{20,40}};
        Problem p; p.Parts={PartOf("P80",a,2),PartOf("P60",b,2)}; p.Stocks={StockOf("180x80",180,80)}; return p;
    }
    Problem MixedCircles()
    {
        Problem p; p.Parts={PartOf("large-R20",Circle(20),4),PartOf("small-R8",Circle(8),1)};
        p.Stocks={StockOf("80x80",80,80)}; return p;
    }
    std::string Canonical(const Result& r)
    {
        std::vector<std::string> placements;
        for(const auto& l:r.Layouts)for(const auto& p:l.Placements)
        {
            std::ostringstream s; s<<l.StockID<<':'<<l.StockInstance<<':'<<p.PartID<<':'<<p.Instance<<std::fixed<<std::setprecision(7)
                <<':'<<p.Transform.X<<':'<<p.Transform.Y<<':'<<p.Transform.AngleDegrees;
            placements.push_back(s.str());
        }
        std::sort(placements.begin(),placements.end());
        std::string s; for(const auto& row:placements)s+=row+'\n'; return s;
    }
}

int main()
{
    Test("all rectangle scorers mix dimensions on one sheet without optional filling",[] {
        const auto p=MixedRectangles();
        for(const std::string strategy:{"rect.short-side","rect.area","rect.long-side"})
        {
            const auto r=Solve(p,strategy); Valid(p,r,true);
            Require(r.Output.Layouts.size()==1 && Count(r.Output,"R60x40")==2 && Count(r.Output,"R40x40")==2,"mixed rectangle demand");
            Require(std::abs(r.Checked.MaterialArea-8000)<1e-6 && std::abs(r.Checked.SheetArea-8000)<1e-6,"true full-sheet area");
            Require(r.Output.Stats.GapFilledParts==0,"basic mixed nesting must not depend on optional filler");
        }
    });
    Test("different parallelogram side lengths share one sheet",[] {
        const auto p=MixedParallelograms(); const auto r=Solve(p,"rules.parallelograms"); Valid(p,r,true);
        Require(r.Output.Layouts.size()==1 && Count(r.Output,"P80")==2 && Count(r.Output,"P60")==2,"mixed parallelogram demand");
        Require(r.Output.Stats.GapFilledParts==0,"specialized basic rule must not use optional gap filling");
    });
    Test("parallelogram contact rows coexist with different rectangle dimensions",[] {
        Shape a; a.Outer={{0,0},{100,0},{130,60},{30,60}};
        Problem p; p.Parts={PartOf("parallelogram",a,2),PartOf("rectangle",Rectangle(80,40),2)};p.Stocks={StockOf("230x100",230,100)};
        const auto r=Solve(p,"rules.parallelograms");Valid(p,r,true);
        Require(r.Output.Layouts.size()==1 && Count(r.Output,"parallelogram")==2 && Count(r.Output,"rectangle")==2,"rectangle and oblique parts share true material space");
        Require(r.Output.Stats.GapFilledParts==0,"cross-shape basic rule is not the optional filler pass");
    });
    Test("large circles expose a real central space for a different radius",[] {
        const auto p=MixedCircles();
        Result witness;witness.State=Status::Complete;Layout sheet;sheet.StockID="80x80";sheet.Width=80;sheet.Height=80;
        sheet.Placements={{"large-R20",0,{20,20,0}},{"large-R20",1,{60,20,0}},{"large-R20",2,{20,60,0}},{"large-R20",3,{60,60,0}},{"small-R8",0,{40,40,0}}};
        witness.Layouts.push_back(sheet);const auto certificate=Validate(p,witness);
        Require(certificate.Valid&&certificate.Complete,"analytic four-corner-plus-centre witness must be geometrically valid");
        const auto r=Solve(p,"circles.mixed-large"); Valid(p,r,true);
        Require(r.Output.Layouts.size()==1 && Count(r.Output,"large-R20")==4 && Count(r.Output,"small-R8")==1,"four large plus central small circle");
        Require(r.Output.Stats.GapFilledParts==0,"mixed-radius basic solver is independent of optional gap filling");
        Require(std::abs(r.Checked.MaterialArea-1664*3.14159265358979323846)<1e-6,"circle analytic material area");
        for(const auto& q:r.Output.Layouts[0].Placements)if(q.PartID=="small-R8")
            Require(q.Transform.X>35 && q.Transform.X<45 && q.Transform.Y>35 && q.Transform.Y<45,"small circle occupies the central real gap");
    });
    Test("mixed-radius circles respect positive spacing and edge margin",[] {
        auto p=MixedCircles(); p.Parts[1]=PartOf("small-R6_5",Circle(6.5),1);
        p.Stocks={StockOf("84x84",84,84)}; p.PartGap=2; p.EdgeMargin=1;
        const auto r=Solve(p,"circles.mixed-large"); Valid(p,r,true);
        Require(Count(r.Output)==5 && r.Output.Stats.GapFilledParts==0,"positive gap mixed-radius result");
    });
    Test("three circle radii mix on one sheet without optional filling",[] {
        Problem p;p.Parts={PartOf("R20",Circle(20),2),PartOf("R12",Circle(12),3),PartOf("R5",Circle(5),6)};p.Stocks={StockOf("100x80",100,80)};
        const auto r=Solve(p,"circles.mixed-large");Valid(p,r,true);
        Require(r.Output.Layouts.size()==1&&Count(r.Output,"R20")==2&&Count(r.Output,"R12")==3&&Count(r.Output,"R5")==6,"three-radius quantities on a single physical sheet");
        Require(r.Output.Stats.GapFilledParts==0,"three-radius basic solver must remain independent of filler option");
    });
    Test("mixed-radius materials and finite inventories remain separate",[] {
        auto p=MixedCircles(); for(auto& part:p.Parts)part.Material="steel"; p.Stocks[0].Material="steel";
        auto aluminium=PartOf("aluminium-R5",Circle(5),1); aluminium.Material="aluminium"; p.Parts.push_back(aluminium);
        auto r=Solve(p,"circles.mixed-large"); Valid(p,r,false);
        Require(Count(r.Output)==5 && Remaining(r.Output,"aluminium-R5")==1,"wrong material must stay unplaced");
        auto extra=StockOf("aluminium-stock",10,10); extra.Material="aluminium"; p.Stocks.push_back(extra);
        r=Solve(p,"circles.mixed-large"); Valid(p,r,true); Require(r.Output.Layouts.size()==2 && Count(r.Output)==6,"distinct material stock");
        p=MixedCircles(); p.Parts[0].Quantity=5;
        r=Solve(p,"circles.mixed-large"); Valid(p,r,false);
        Require(r.Output.Layouts.size()==1 && Count(r.Output,"large-R20")==4 && Count(r.Output,"small-R8")==1 && Remaining(r.Output,"large-R20")==1,"finite circular stock quantity");
    });
    Test("mixed circle part identities survive multiple physical sheets",[] {
        auto p=MixedCircles(); p.Parts[0].Quantity=8; p.Parts[1].Quantity=2; p.Stocks[0].Quantity=2;
        const auto r=Solve(p,"circles.mixed-large"); Valid(p,r,true);
        Require(r.Output.Layouts.size()==2 && Count(r.Output)==10,"two physical stock instances");
        std::map<std::string,std::vector<std::size_t>> ids;
        for(const auto& l:r.Output.Layouts)for(const auto& q:l.Placements)ids[q.PartID].push_back(q.Instance);
        for(auto& [id,instances]:ids) { std::sort(instances.begin(),instances.end()); for(std::size_t i=0;i<instances.size();++i)Require(instances[i]==i,"original instance sequence: "+id); }
    });
    Test("equal-radius seed preserves different IDs local centres and fixed angles",[] {
        Shape a=Circle(20);a.Center={7,-3};Shape b=Circle(20);b.Center={-11,4};Shape small=Circle(8);small.Center={5,6};
        Problem p;p.Parts={PartOf("large-A",a,2),PartOf("large-B",b,2),PartOf("small",small,1)};p.Stocks={StockOf("80x80",80,80)};
        p.Parts[1].AllowedAnglesDegrees={33};p.Parts[2].AllowedAnglesDegrees={17};
        const auto r=Solve(p,"circles.mixed-large");Valid(p,r,true);
        Require(Count(r.Output,"large-A")==2&&Count(r.Output,"large-B")==2&&Count(r.Output,"small")==1,"same radius must not collapse original demands");
        for(const auto& layout:r.Output.Layouts)for(const auto& q:layout.Placements)
        {
            const auto found=std::find_if(p.Parts.begin(),p.Parts.end(),[&](const Part& part){return part.ID==q.PartID;});
            Require(found!=p.Parts.end(),"original part identity");
            const double expected=q.PartID=="large-B"?33:q.PartID=="small"?17:0;
            Require(std::abs(q.Transform.AngleDegrees-expected)<1e-6,"fixed angle retained on circle instance");
            const double radians=q.Transform.AngleDegrees*3.14159265358979323846/180;
            const double x=q.Transform.X+found->Geometry.Center.X*std::cos(radians)-found->Geometry.Center.Y*std::sin(radians);
            const double y=q.Transform.Y+found->Geometry.Center.X*std::sin(radians)+found->Geometry.Center.Y*std::cos(radians);
            Require(std::isfinite(x)&&std::isfinite(y)&&x>=8&&y>=8&&x<=72&&y<=72,"nonzero local centre transforms to a valid physical centre");
        }
    });
    Test("changing input specification order does not break demand or large-first choice",[] {
        for(const auto& item:std::vector<std::pair<Problem,std::string>>{{MixedRectangles(),"rect.short-side"},{MixedParallelograms(),"rules.parallelograms"},{MixedCircles(),"circles.mixed-large"}})
        {
            auto p=item.first; const auto a=Solve(p,item.second); Valid(p,a,true);
            std::reverse(p.Parts.begin(),p.Parts.end()); const auto b=Solve(p,item.second); Valid(p,b,true);
            Require(a.Output.Layouts.size()==b.Output.Layouts.size() && Count(a.Output)==Count(b.Output),"input order must not lose a specification");
            if(item.second=="circles.mixed-large")Require(Canonical(a.Output)==Canonical(b.Output),"large-first order should use radius, not input index");
        }
    });
    Test("pattern budget cannot discard a later specification or necessary angle",[] {
        Problem p; p.Parts={PartOf("too-big",Rectangle(20,20),1),PartOf("fits",Rectangle(10,10),1)}; p.Stocks={StockOf("small-stock",10,10)};
        auto options=Basic(); options.MaxPatterns=1;
        auto r=Solve(p,"rect.short-side",options); Valid(p,r,false);
        Require(Count(r.Output,"fits")==1 && Remaining(r.Output,"too-big")==1,"later specification is a mandatory base candidate");
        p.Parts[1]=PartOf("needs-90",Rectangle(20,10),1); p.Parts[1].AllowedAnglesDegrees={0,90}; p.Stocks={StockOf("10x20",10,20)};
        r=Solve(p,"rect.short-side",options); Valid(p,r,false);
        Require(Count(r.Output,"needs-90")==1 && Remaining(r.Output,"too-big")==1,"base orientations must not be cut by combination budget");
        for(const auto& l:r.Output.Layouts)for(const auto& q:l.Placements)Require(std::abs(q.Transform.AngleDegrees-90)<1e-6,"required ninety-degree orientation");
    });
    Test("mixed specification portfolio is stable between serial and parallel jobs",[] {
        const auto p=MixedCircles(); PortfolioOptions o; o.PerJob=Basic(); o.Workers=1;
        const auto serial=RunPortfolio(p,o); o.Workers=4; const auto parallel=RunPortfolio(p,o);
        Require(serial.Jobs.size()==DefaultStrategies().IDs().size() && serial.Jobs.size()==parallel.Jobs.size(),"registry determines independent job count");
        Require(serial.BestJob<serial.Jobs.size() && parallel.BestJob<parallel.Jobs.size(),"best valid result");
        Require(serial.Jobs[serial.BestJob].StrategyID==parallel.Jobs[parallel.BestJob].StrategyID,"serial/parallel best strategy");
        Require(serial.Jobs[serial.BestJob].Checked.Complete,"portfolio must select a complete mixed-radius result");
        for(std::size_t i=0;i<serial.Jobs.size();++i)
        {
            Require(serial.Jobs[i].StrategyID==parallel.Jobs[i].StrategyID,"stable job order");
            const auto v=Validate(p,serial.Jobs[i].Output); Require(v.Valid,"every job has conserved, valid output");
            Require(Canonical(serial.Jobs[i].Output)==Canonical(parallel.Jobs[i].Output),"shared mutable state changed placements");
        }
    });
    Test("mixed-circle cancellation and attempt cutoffs conserve every specification",[] {
        const auto p=MixedCircles();
        const auto audit=[&](const JobResult& r) {
            const auto checked=Validate(p,r.Output);Require(checked.Valid,"truncated mixed-circle geometry and accounting");
            for(const auto& part:p.Parts)Require(Count(r.Output,part.ID)+Remaining(r.Output,part.ID)==part.Quantity,"truncated demand conservation: "+part.ID);
        };
        for(const std::size_t limit:{1,3,8})
        {
            auto options=Basic();options.MaxPlacementAttempts=limit;
            const auto r=Solve(p,"circles.mixed-large",options);audit(r);
            Require(r.Output.State==Status::Partial || r.Output.State==Status::Complete,"attempt cutoff status");
            if(!r.Checked.Complete)Require(!r.Output.Diagnostics.empty(),"attempt cutoff diagnostic");
        }
        for(const int trigger:{1,4,12})
        {
            int calls=0;auto options=Basic();options.Cancel=[&] { return ++calls>=trigger; };
            const auto r=Solve(p,"circles.mixed-large",options);audit(r);
            Require(r.Output.State==Status::Cancelled,"mixed-circle cancellation status");
        }
    });
    std::cout<<'\n'<<Passed<<" passed, "<<Failed<<" failed\n";
    return Failed==0?0:1;
}
