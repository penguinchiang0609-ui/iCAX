#include "SheetNestingRuleStrategies.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
    constexpr double Pi=3.14159265358979323846;
    int Passed=0,Failed=0;
    void Require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
    void Test(const char* name,const std::function<void()>& body)
    {
        try{body();++Passed;std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& error){++Failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
    }
    Shape Box(double w,double h,Point origin={})
    {Shape s;s.Outer={{origin.X,origin.Y},{origin.X+w,origin.Y},{origin.X+w,origin.Y+h},{origin.X,origin.Y+h}};return s;}
    Part Of(std::string id,Shape shape,std::size_t quantity,std::vector<double> angles={0},std::string material="m")
    {Part p;p.ID=std::move(id);p.Geometry=std::move(shape);p.Quantity=quantity;p.FreeRotation=false;p.AllowedAnglesDegrees=std::move(angles);p.Material=std::move(material);return p;}
    Stock Sheet(std::string id,double w,double h,std::size_t quantity=1,std::string material="m")
    {return {std::move(id),w,h,quantity,1,std::move(material)};}
    Options Budget(bool combinations=false,bool filling=false)
    {
        Options o;o.TimeLimitSeconds=5;o.MaxPlacementAttempts=50000;o.MaxPatterns=64;o.MaxPatternMembers=2;
        o.EnableCombinations=combinations;o.EnableGapFill=filling;o.EnableCircleLattice=false;o.Seed=17;return o;
    }
    StrategyRegistry Registry()
    {
        StrategyRegistry r;
        r.Register("test.batch.on-stall",MakeBatchRuleFactory(BatchFillTiming::OnStall));
        r.Register("test.batch.after",MakeBatchRuleFactory(BatchFillTiming::AfterBatch));return r;
    }
    const char* ID(BatchFillTiming timing)
    {return timing==BatchFillTiming::OnStall?"test.batch.on-stall":"test.batch.after";}
    JobResult Solve(const Problem& p,BatchFillTiming timing,Options o=Budget())
    {return RunJob({"batch-behavior",ID(timing),p,std::move(o)},Registry());}
    std::size_t Count(const Result& result,const std::string& id={})
    {std::size_t n=0;for(const auto& l:result.Layouts)for(const auto& p:l.Placements)if(id.empty()||p.PartID==id)++n;return n;}
    Point World(Point p,Pose pose)
    {const double a=pose.AngleDegrees*Pi/180,c=std::cos(a),s=std::sin(a);return {c*p.X-s*p.Y+pose.X,s*p.X+c*p.Y+pose.Y};}
    struct Bounds{double X0=0,Y0=0,X1=0,Y1=0;};
    Bounds WorldBounds(const std::vector<Point>& ring,Pose pose)
    {
        Require(!ring.empty(),"expected polygon ring");const auto first=World(ring[0],pose);Bounds b{first.X,first.Y,first.X,first.Y};
        for(auto p:ring){p=World(p,pose);b.X0=std::min(b.X0,p.X);b.X1=std::max(b.X1,p.X);b.Y0=std::min(b.Y0,p.Y);b.Y1=std::max(b.Y1,p.Y);}return b;
    }
    std::string Canonical(const Result& r)
    {
        std::vector<std::string> rows;
        for(const auto& l:r.Layouts)for(const auto& p:l.Placements)
        {std::ostringstream s;s<<l.StockID<<':'<<l.StockInstance<<':'<<p.PartID<<':'<<p.Instance<<std::setprecision(12)<<':'<<p.Transform.X<<':'<<p.Transform.Y<<':'<<p.Transform.AngleDegrees;rows.push_back(s.str());}
        std::sort(rows.begin(),rows.end());std::string value;for(const auto& row:rows)value+=row+'\n';return value;
    }
    void Check(const Problem& original,const JobResult& job,const Options& options)
    {
        // Revalidate the caller's original snapshot, not any problem a constructor might have altered.
        const auto checked=Validate(original,job.Output);std::string error;
        for(const auto& e:checked.Errors)error+=e+"; ";
        Require(job.Checked.Valid&&checked.Valid,error.empty()?"original result must be valid":error);
        Require(job.Output.GeometryValidated,"RunJob must publish the original validation result");
        Require(job.Output.Stats.PlacementAttempts<=options.MaxPlacementAttempts,"shared placement budget exceeded");
        std::map<std::string,const Part*> parts;for(const auto& p:original.Parts)parts[p.ID]=&p;
        std::map<std::string,const Stock*> stocks;for(const auto& s:original.Stocks)stocks[s.ID]=&s;
        std::map<std::string,std::set<std::size_t>> instances;std::map<std::string,std::size_t> remaining;
        std::set<std::pair<std::string,std::size_t>> sheets;
        for(const auto& u:job.Output.Remaining){Require(parts.count(u.PartID)!=0,"unknown remaining part");remaining[u.PartID]+=u.Quantity;}
        for(const auto& l:job.Output.Layouts)
        {
            Require(stocks.count(l.StockID)!=0,"unknown stock");const auto& stock=*stocks.at(l.StockID);
            Require(sheets.emplace(l.StockID,l.StockInstance).second&&l.StockInstance<stock.Quantity,"stock instance fabricated or reused");
            double area=0;std::vector<Bounds> rectangles;bool onlyRectangles=true;
            for(const auto& p:l.Placements)
            {
                Require(parts.count(p.PartID)!=0,"synthetic part escaped output");const auto& part=*parts.at(p.PartID);
                Require(p.Instance<part.Quantity&&instances[p.PartID].insert(p.Instance).second,"part instance fabricated or duplicated");
                Require(part.Material==stock.Material,"material groups mixed");
                const auto allowed=part.AllowedAnglesDegrees.empty()?std::vector<double>{0}:part.AllowedAnglesDegrees;
                Require(part.FreeRotation||std::any_of(allowed.begin(),allowed.end(),[&](double a){return std::abs(std::remainder(p.Transform.AngleDegrees-a,360.))<=original.Tolerance;}),"unpermitted angle");
                const auto classification=Classify(part.Geometry,original.Tolerance);area+=classification.Area;
                if(classification.Kind==ShapeKind::Rectangle&&part.Geometry.Holes.empty())rectangles.push_back(WorldBounds(part.Geometry.Outer,p.Transform));else onlyRectangles=false;
            }
            Require(std::abs(l.MaterialArea-area)<=std::max(1e-6,original.Tolerance*(stock.Width+stock.Height)*(l.Placements.size()+1)),"batch material-area cache lost a member");
            // These fixtures use axial rectangles: an independent analytic clearance check supplements Validate.
            if(onlyRectangles)
            {
                for(const auto& b:rectangles)Require(b.X0>=original.EdgeMargin-original.Tolerance&&b.Y0>=original.EdgeMargin-original.Tolerance&&b.X1<=stock.Width-original.EdgeMargin+original.Tolerance&&b.Y1<=stock.Height-original.EdgeMargin+original.Tolerance,"axial rectangle outside original stock margin");
                for(std::size_t i=0;i<rectangles.size();++i)for(std::size_t j=i+1;j<rectangles.size();++j)
                {
                    const auto a=rectangles[i],b=rectangles[j];
                    const double overlapX=std::min(a.X1,b.X1)-std::max(a.X0,b.X0),overlapY=std::min(a.Y1,b.Y1)-std::max(a.Y0,b.Y0);
                    Require(overlapX<=original.Tolerance||overlapY<=original.Tolerance,"axial rectangle interiors overlap");
                    const double dx=std::max({0.,a.X0-b.X1,b.X0-a.X1}),dy=std::max({0.,a.Y0-b.Y1,b.Y0-a.Y1});
                    Require(std::hypot(dx,dy)>=original.PartGap-original.Tolerance,"analytic rectangle clearance is below PartGap");
                }
            }
        }
        for(const auto& part:original.Parts)Require(instances[part.ID].size()+remaining[part.ID]==part.Quantity,"placed plus remaining demand does not equal original Quantity");
    }
    void Complete(const Problem& p,const JobResult& r,const Options& options,std::size_t sheets)
    {Check(p,r,options);Require(r.Checked.Complete&&r.Output.State==Status::Complete,"expected complete demand on the simple certified fixture");Require(r.Output.Layouts.size()==sheets,"unexpected sheet count on a simple capacity fixture");}
    Problem MixedRectangles()
    {Problem p;p.Parts={Of("R20",Box(20,10,{31,-17}),23),Of("R30",Box(30,10,{-90,73}),17),Of("R10",Box(10,10,{125,40}),13)};p.Stocks={Sheet("sheet",200,100)};return p;}
    Problem RepeatedTriangle(std::size_t first=5,std::size_t second=0)
    {
        Shape a;a.Outer={{137,-61},{167,-61},{137,-41}};Problem p;p.Parts={Of("A",a,first,{0,180})};
        if(second){Shape b;b.Outer={{-300,42},{-270,42},{-300,62}};p.Parts.push_back(Of("B",b,second,{0,180}));}
        p.Stocks={Sheet("sheet",second?120:90,20)};return p;
    }
    Problem HoleAndMain()
    {
        Shape frame=Box(60,60,{137,-61});frame.Holes={{{157,-41},{157,-21},{177,-21},{177,-41}}};
        Problem p;p.Parts={Of("insert",Box(10,10,{-29,71}),4),Of("main",Box(40,60,{500,300}),1),Of("frame",frame,1)};p.Stocks={Sheet("sheet",100,60)};return p;
    }
    const BatchFillTiming Timings[]={BatchFillTiming::OnStall,BatchFillTiming::AfterBatch};
}

int main()
{
    Test("custom factories are opt-in and create fresh job instances",[]{
        const auto before=DefaultStrategies().IDs();auto registry=Registry();auto a=registry.Create(ID(BatchFillTiming::OnStall));auto b=registry.Create(ID(BatchFillTiming::OnStall));
        Require(a&&b&&a.get()!=b.get(),"factory must create independent strategy objects");Require(DefaultStrategies().IDs()==before,"custom batch factory changed the default registry");
        StrategyRegistry defaultFactory;defaultFactory.Register("test.batch.default",MakeBatchRuleFactory());const auto p=MixedRectangles();auto o=Budget();o.MaxPatterns=1;
        const auto r=RunJob({"default-timing","test.batch.default",p,o},defaultFactory);Complete(p,r,o,1);
        const auto explicitDefault=Solve(p,BatchFillTiming::OnStall,o);Complete(p,explicitDefault,o,1);Require(Canonical(r.Output)==Canonical(explicitDefault.Output),"default timing must be OnStall");
    });
    Test("default portfolio exposes the appended on-stall factory through its public entry point",[]{
        const auto ids=DefaultStrategies().IDs();
        Require(ids.size()>=35&&ids[34]=="rules.batch-on-stall","default on-stall factory must retain its registered position");
        const std::vector<std::string> originalIDs={
            "rect.short-side","rect.area","rect.long-side","rules.composite","rules.composite-quick","rules.circles",
            "circles.mixed-large","circles.mixed-demand","circles.mixed-interleave","rules.parallelograms",
            "rules.contact-large","rules.contact-interleave","rules.contact-low-left","rules.contact-scarce",
            "rect.shelves-row","rect.shelves-column","rect.pattern-row","rect.pattern-column",
            "rules.contact-remnant","rules.contact-balanced-remnant","rules.strip-width","rules.strip-height",
            "parallelograms.pattern-row","circles.square-voids","rules.hole-rectangles","rules.complement-chains",
            "circles.cell-rows","rules.composite-anchor-fit","rules.composite-fit","rules.composite-closure",
            "rules.seam-assembly","rect.merge-tree","rect.lookahead","rules.composite-units"};
        Require(std::equal(originalIDs.begin(),originalIDs.end(),ids.begin()),"original 34 strategy IDs or prefix order changed");
        Require(std::count(ids.begin(),ids.end(),"rules.batch-on-stall")==1,"default on-stall strategy must appear exactly once");
        Require(std::find(ids.begin(),ids.end(),"rules.batch-after-batch")==ids.end(),"after-batch factory must remain optional");
        const auto p=MixedRectangles();PortfolioOptions o;o.PerJob=Budget();o.PerJob.MaxPatterns=1;
        o.StrategyIDs={"rules.batch-on-stall"};o.Workers=1;
        const auto portfolio=RunPortfolio(p,o);
        Require(portfolio.Jobs.size()==1&&portfolio.BestJob==0,"default public portfolio did not select its requested on-stall strategy");
        const auto& registered=portfolio.Jobs[0];Complete(p,registered,o.PerJob,1);
        Require(registered.StrategyID=="rules.batch-on-stall","portfolio returned a different default strategy");
        const auto explicitFactory=Solve(p,BatchFillTiming::OnStall,o.PerJob);Complete(p,explicitFactory,o.PerJob,1);
        Require(Canonical(registered.Output)==Canonical(explicitFactory.Output),"default and explicit on-stall factories changed original identities or layout");
    });
    Test("mixed specifications retain true quantities with a tiny additional-pattern cap",[]{
        auto p=MixedRectangles();auto unused=Of("zero-quantity",Box(5,5),0);p.Parts.insert(p.Parts.begin(),unused);auto o=Budget();o.MaxPatterns=1;
        for(auto timing:Timings){const auto r=Solve(p,timing,o);Complete(p,r,o,1);Require(Count(r.Output)==53&&Count(r.Output,"R20")==23&&Count(r.Output,"R30")==17&&Count(r.Output,"R10")==13&&Count(r.Output,"zero-quantity")==0,"batch changed per-spec demand");}
    });
    Test("whole rows preserve odd final rows and the last sheet remainder",[]{
        auto o=Budget();Problem p;p.Parts={Of("odd",Box(25,10),11)};p.Stocks={Sheet("sheet",100,30)};
        for(auto timing:Timings)Complete(p,Solve(p,timing,o),o,1);
        p.Parts[0].Quantity=9;p.Stocks={Sheet("sheet",100,20,2)};
        for(auto timing:Timings){const auto r=Solve(p,timing,o);Complete(p,r,o,2);Require(Count(r.Output,"odd")==9,"last-sheet singleton was discarded");}
    });
    Test("unfilled last-row cells remain available to another specification",[]{
        for(double gap:{0.,2.})
        {
            Problem p;p.PartGap=gap;p.EdgeMargin=gap?1:0;p.Parts={Of("large",Box(30,20),5),Of("tail",Box(20,20),1)};
            p.Stocks={Sheet("sheet",90+2*gap+2*p.EdgeMargin,40+gap+2*p.EdgeMargin)};const auto o=Budget();
            for(auto timing:Timings){const auto r=Solve(p,timing,o);Complete(p,r,o,1);Require(Count(r.Output,"large")==5&&Count(r.Output,"tail")==1,"batch reserved nonexistent last-row members");}
        }
    });
    Test("repeated members of one combination clip to demand and preserve the odd original",[]{
        const auto p=RepeatedTriangle();const auto o=Budget(true,false);
        for(auto timing:Timings){const auto r=Solve(p,timing,o);Complete(p,r,o,1);Require(Count(r.Output,"A")==5&&r.Output.Stats.CombinationGroups>0,"five triangles must use real pairs plus their unmatched original");}
    });
    Test("overlapping combination alternatives cannot spend a shared specification twice",[]{
        const auto p=RepeatedTriangle(5,2);const auto o=Budget(true,false);
        for(auto timing:Timings){const auto r=Solve(p,timing,o);Complete(p,r,o,1);Require(Count(r.Output,"A")==5&&Count(r.Output,"B")==2&&r.Output.Stats.CombinationGroups>0,"shared pair inventory lost a real original");}
    });
    Test("unequal L and complement quantities retain unpaired members",[]{
        Shape l;l.Outer={{137,-61},{237,-61},{237,-41},{167,-41},{167,19},{137,19}};
        Problem p;p.Parts={Of("L",l,7),Of("notch",Box(70,60,{-300,42}),3)};p.Stocks={Sheet("sheet",400,160)};const auto o=Budget(true,false);
        for(auto timing:Timings)
        {
            const auto r=Solve(p,timing,o);
            if(!r.Checked.Complete||r.Output.State!=Status::Complete)
            {
                std::cerr<<"L_FIXTURE strategy="<<ID(timing)<<" state="<<static_cast<int>(r.Output.State)
                    <<" valid="<<r.Checked.Valid<<" complete="<<r.Checked.Complete<<" L="<<Count(r.Output,"L")
                    <<" notch="<<Count(r.Output,"notch")<<" patterns="<<r.Output.Stats.CandidatePatterns
                    <<" groups="<<r.Output.Stats.CombinationGroups<<" attempts="<<r.Output.Stats.PlacementAttempts
                    <<" elapsed="<<r.Output.Stats.ElapsedSeconds<<'\n';
                for(const auto& u:r.Output.Remaining)std::cerr<<"  remaining "<<u.PartID<<'='<<u.Quantity<<'\n';
                for(const auto& d:r.Output.Diagnostics)std::cerr<<"  diagnostic "<<d<<'\n';
                for(const auto& layout:r.Output.Layouts)for(const auto& q:layout.Placements)
                {
                    const auto source=std::find_if(p.Parts.begin(),p.Parts.end(),[&](const auto& part){return part.ID==q.PartID;});
                    if(source==p.Parts.end()){std::cerr<<"  unknown placed part "<<q.PartID<<'\n';continue;}
                    const auto b=WorldBounds(source->Geometry.Outer,q.Transform);
                    std::cerr<<"  placed "<<q.PartID<<'['<<q.Instance<<"] group="<<q.Group<<" filler="<<q.GapFiller
                        <<" pose="<<q.Transform.X<<','<<q.Transform.Y<<','<<q.Transform.AngleDegrees
                        <<" bbox="<<b.X0<<','<<b.Y0<<','<<b.X1<<','<<b.Y1<<'\n';
                }
            }
            Complete(p,r,o,1);Require(Count(r.Output,"L")==7&&Count(r.Output,"notch")==3&&r.Output.Stats.CombinationGroups>0,"unequal combination quantities were rounded or multiplied incorrectly");
        }
    });
    Test("legal 90-degree direction and shifted local origins survive array expansion",[]{
        Problem p;p.PartGap=2;p.EdgeMargin=1;p.Parts={Of("turned",Box(20,35,{500,-700}),11,{90})};p.Stocks={Sheet("sheet",148,66)};const auto o=Budget();
        for(auto timing:Timings){const auto r=Solve(p,timing,o);Complete(p,r,o,1);for(const auto& l:r.Output.Layouts)for(const auto& q:l.Placements)Require(std::abs(std::remainder(q.Transform.AngleDegrees-90,360.))<=p.Tolerance,"batch used an unlisted direction");}
    });
    Test("material-compatible arrays use only the original finite stock",[]{
        Problem p;p.PartGap=1;p.EdgeMargin=1;p.Parts={Of("steel",Box(20,10),13,{0},"steel"),Of("aluminium",Box(30,10),7,{0},"aluminium")};
        p.Stocks={Sheet("steel-sheet",106,34,1,"steel"),Sheet("aluminium-sheet",125,23,1,"aluminium")};const auto o=Budget();
        for(auto timing:Timings)Complete(p,Solve(p,timing,o),o,2);
        Problem finite;finite.Parts={Of("part",Box(20,10),13)};finite.Stocks={Sheet("one-only",100,20)};
        for(auto timing:Timings){const auto r=Solve(finite,timing,o);Check(finite,r,o);Require(r.Output.State==Status::Partial&&!r.Checked.Complete&&Count(r.Output)<=10,"exhausted stock must remain an explicit safe partial result");}
    });
    Test("both filling timings fill real holes without losing the main rectangle",[]{
        const auto p=HoleAndMain();const auto o=Budget(false,true);
        for(auto timing:Timings)
        {
            const auto r=Solve(p,timing,o);Complete(p,r,o,1);Require(Count(r.Output,"insert")==4&&r.Output.Stats.GapFilledParts==4,"real-hole inserts must be conserved as fillers");
            const auto& placements=r.Output.Layouts[0].Placements;const auto frame=std::find_if(placements.begin(),placements.end(),[](const auto& q){return q.PartID=="frame";});
            Require(frame!=placements.end(),"frame lost");const auto hole=WorldBounds(p.Parts[2].Geometry.Holes[0],frame->Transform);
            const auto main=std::find_if(placements.begin(),placements.end(),[](const auto& q){return q.PartID=="main";});
            Require(main!=placements.end(),"main rectangle lost");
            // This is the direct optional constructor, before any postprocessor:
            // construction order distinguishes early local filling from stall filling.
            for(auto placed=placements.begin();placed!=placements.end();++placed)if(placed->PartID=="insert")
            {
                Require(placed->GapFiller,"hole insert lost its constructor-origin marker");
                Require(timing==BatchFillTiming::AfterBatch?placed<main:main<placed,"fill timing did not control when hole inserts were constructed");
                const auto b=WorldBounds(p.Parts[0].Geometry.Outer,placed->Transform);
                Require(b.X0>=hole.X0-p.Tolerance&&b.Y0>=hole.Y0-p.Tolerance&&b.X1<=hole.X1+p.Tolerance&&b.Y1<=hole.Y1+p.Tolerance,"early fill escaped the physical frame hole and blocked the main rectangle");
            }
        }
    });
    Test("disabling fill makes the two timing policies the same deterministic constructor",[]{
        const auto p=RepeatedTriangle(5,2);const auto o=Budget(true,false);const auto a=Solve(p,BatchFillTiming::OnStall,o),b=Solve(p,BatchFillTiming::AfterBatch,o);
        Complete(p,a,o,1);Complete(p,b,o,1);Require(Canonical(a.Output)==Canonical(b.Output),"fill timing changed the candidate pool when fill is disabled");
    });
    Test("attempt cuts retain only safe originals and account for all outstanding demand",[]{
        const auto p=MixedRectangles();
        for(auto timing:Timings)for(std::size_t limit:{1,2,3,7,17,53}){auto o=Budget();o.MaxPlacementAttempts=limit;const auto r=Solve(p,timing,o);Check(p,r,o);}
    });
    Test("shared gap-fill allowance preserves stock opening for remaining originals",[]{
        auto o=Budget(false,true);o.TimeLimitSeconds=5;o.MaxPlacementAttempts=16;
        std::vector<std::pair<Problem,JobResult>> runs;
        for(bool hollow:{false,true})for(std::size_t quantity:{2,8})
        {
            auto shape=Box(8,8);if(hollow)shape.Holes={{{1,1},{1,7},{7,7},{7,1}}};
            Problem p;p.Parts={Of("one-per-sheet",shape,quantity,{0})};p.Stocks={Sheet("finite",10,10,quantity)};
            for(auto timing:Timings)
            {
                const auto r=Solve(p,timing,o);Check(p,r,o);
                if(!r.Checked.Complete||r.Output.State!=Status::Complete||r.Output.Layouts.size()!=quantity)
                    std::cerr<<"GAP_ALLOWANCE shape="<<(hollow?"frame":"solid")<<" quantity="<<quantity
                        <<" strategy="<<ID(timing)<<" valid="<<r.Checked.Valid<<" complete="<<r.Checked.Complete
                        <<" placed="<<Count(r.Output)<<" boards="<<r.Output.Layouts.size()
                        <<" attempts="<<r.Output.Stats.PlacementAttempts<<" limit="<<o.MaxPlacementAttempts<<'\n';
                runs.emplace_back(p,r);
            }
        }
        for(const auto& [p,r]:runs)Complete(p,r,o,p.Parts[0].Quantity);
    });
    Test("one-shot cancellation is latched without leaking an uncommitted batch",[]{
        Problem p;p.Parts={Of("many",Box(10,10,{500,-700}),2000)};p.Stocks={Sheet("sheet",500,400)};
        for(auto timing:Timings)
        {
            std::size_t observed=0;
            for(std::size_t cut:{1,5,50,200})
            {
                std::size_t calls=0;bool seen=false;auto o=Budget();o.MaxPatterns=1;o.Cancel=[&]{if(++calls==cut){seen=true;return true;}return false;};
                const auto r=Solve(p,timing,o);Check(p,r,o);
                if(seen){++observed;Require(r.Output.State==Status::Cancelled&&calls==cut,"observed cancellation was not latched");}
            }
            Require(observed>=2,"cancellation controls never reached meaningful public polling points");
            const auto good=Budget();Complete(p,Solve(p,timing,good),good,1);
        }
    });
    Test("parallel optional timing jobs keep independent inventories",[]{
        const auto p=MixedRectangles();PortfolioOptions o;o.PerJob=Budget();o.StrategyIDs={ID(BatchFillTiming::OnStall),ID(BatchFillTiming::AfterBatch)};o.Workers=2;
        const auto portfolio=RunPortfolio(p,o,Registry());Require(portfolio.Jobs.size()==2&&portfolio.BestJob<2,"optional batch jobs were not selected independently");
        for(const auto& r:portfolio.Jobs)Complete(p,r,o.PerJob,1);
    });
    Test("area-first batch job retains original demand and stock in parallel",[]{
        auto p=RepeatedTriangle(5,2);p.Stocks[0].Quantity=2;
        PortfolioOptions o;o.PerJob=Budget(true,true);o.StrategyIDs={"rules.batch-on-stall","rules.batch-area-first"};o.Workers=2;
        const auto portfolio=RunPortfolio(p,o);Require(portfolio.Jobs.size()==2&&portfolio.BestJob<2,"batch alternatives were not independently evaluated");
        for(const auto& r:portfolio.Jobs)Complete(p,r,o.PerJob,1);
        auto limited=o.PerJob;limited.MaxPlacementAttempts=3;
        const auto partial=RunJob({"limited","rules.batch-area-first",p,limited},DefaultStrategies());Check(p,partial,limited);
    });
    std::cout<<"BatchRuleTests: "<<Passed<<" passed, "<<Failed<<" failed\n";return Failed?1:0;
}
