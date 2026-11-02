#include "SheetNestingRepeatCell.h"
#include "NestingInternal.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>

using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    Part Box(std::string id, double w, double h, std::size_t q = 1)
    {
        Part p; p.ID = std::move(id); p.Quantity = q; p.Material = "steel";
        p.FreeRotation = false; p.AllowedAnglesDegrees = {0};
        p.Geometry.Outer = {{0,0},{w,0},{w,h},{0,h}}; return p;
    }
    Problem Four(std::size_t quantity = 4)
    { Problem p; p.Parts = {Box("original",2,3,quantity)}; p.Stocks = {{"stock",double(quantity),6,3,2,"steel"}}; return p; }
    Options Budget()
    { Options o; o.TimeLimitSeconds = 10; o.MaxPlacementAttempts = 1000000; return o; }
    using Function = std::function<Result(const Problem&,const Options&)>;
    class FunctionStrategy final : public IStrategy
    {
    public:
        explicit FunctionStrategy(Function fn) : Fn(std::move(fn)) {}
        Result Run(const Problem& p, const Options& o) override
        { Require(!Used,"inner constructor instance must be fresh for each Run"); Used=true; return Fn(p,o); }
    private: Function Fn; bool Used = false;
    };
    StrategyFactory Mock(Function fn)
    { return [fn=std::move(fn)] { return std::make_unique<FunctionStrategy>(fn); }; }
    Result Line(const Problem& p, const Options&)
    {
        Result r; r.State = Status::Complete; Layout l;
        l.StockID=p.Stocks[0].ID; l.Width=p.Stocks[0].Width; l.Height=p.Stocks[0].Height;
        double x=0;
        for (const auto& part:p.Parts)
            for (std::size_t i=0;i<part.Quantity;++i)
            {
                const double angle=part.AllowedAnglesDegrees.empty()?0:part.AllowedAnglesDegrees[0];
                const auto box=Detail::Bounds(part.Geometry,{0,0,angle});
                l.Placements.push_back({part.ID,i,{x-box.X,-box.Y,angle},77,true}); x+=box.Width;
            }
        l.MaterialArea=999999; // Deliberately wrong: the wrapper must measure true geometry.
        r.Layouts.push_back(std::move(l)); r.Stats.PlacementAttempts=7;
        r.Stats.CandidatePatterns=3; r.Stats.CombinationGroups=1; return r;
    }
    Result Solve(const Problem& p, Function fn=Line, Options o=Budget(), std::size_t columns=2, std::size_t rows=2)
    { return MakeRectangleRepeatCellFactory(Mock(std::move(fn)),columns,rows)()->Run(p,o); }
    void Complete(const Problem& p, const Result& r, std::size_t count)
    {
        const auto v=Validate(p,r);
        for(const auto& e:v.Errors)std::cerr<<e<<'\n';
        for(const auto& e:r.Diagnostics)if(r.State!=Status::Complete)std::cerr<<e<<'\n';
        Require(r.State==Status::Complete&&v.Valid&&v.Complete&&r.GeometryValidated,"repeat-cell must validate the complete original input");
        Require(v.PlacedCount==count&&r.Layouts.size()==1,"one original sheet and every original copy must be returned");
        Require(std::abs(r.Layouts[0].MaterialArea-v.MaterialArea)<1e-7*std::max(1.0,v.MaterialArea),"material area must use measured originals, not inner metadata");
        std::set<std::pair<std::string,std::size_t>> ids;
        for(const auto& x:r.Layouts[0].Placements)Require(ids.emplace(x.PartID,x.Instance).second,"expanded original identity must be unique");
    }
    void Empty(const Problem& p,const Result& r,std::size_t limit)
    {
        Require(r.Layouts.empty()&&!r.GeometryValidated,"unsupported/partial/invalid/budget/cancel result must expose no cell layouts");
        Require(r.Remaining.size()==std::count_if(p.Parts.begin(),p.Parts.end(),[](const auto& x){return x.Quantity>0;}),"all original raw demand rows must remain");
        for(const auto& part:p.Parts)if(part.Quantity)
            Require(std::any_of(r.Remaining.begin(),r.Remaining.end(),[&](const auto& x){return x.PartID==part.ID&&x.Quantity==part.Quantity;}),"remaining quantity must be original quantity");
        Require(r.Stats.PlacementAttempts<=limit,"shared caller attempt limit cannot be reset or exceeded");
    }
    StrategyFactory Named(const std::string& id)
    { return [id] { return DefaultStrategies().Create(id); }; }
    Problem Hard25()
    {
        // Dimensions only from the public stress fixture; no placement witness.
        static constexpr std::pair<int,int> d[]={{140,200},{392,450},{112,100},{140,50},{504,200},
            {532,100},{28,850},{56,700},{476,150},{224,700},{364,1150},{56,650},{448,300},{28,250},
            {84,1500},{280,750},{28,550},{336,800},{952,350},{84,1650},{588,250},{28,950},{168,950},{308,450},{280,1900}};
        Problem p; p.Stocks={{"stock",2240,6000,1,1,"steel"}};
        for(auto[w,h]:d)p.Parts.push_back(Box("hard-"+std::to_string(p.Parts.size()),w,h,4));
        return p;
    }
}

int main()
{
    try
    {
        const auto defaultIDs=DefaultStrategies().IDs();
        auto p=Four(); const auto original=p;
        bool sawReduced=false;
        auto result=Solve(p,[&](const Problem& unit,const Options& inner)
        {
            sawReduced=unit.Parts[0].ID==p.Parts[0].ID&&unit.Parts[0].Quantity==1&&unit.EdgeMargin==0&&
                unit.Stocks[0].Quantity==1&&inner.MaxPlacementAttempts<Budget().MaxPlacementAttempts&&inner.TimeLimitSeconds<Budget().TimeLimitSeconds;
            return Line(unit,inner);
        });
        Complete(p,result,4); Require(sawReduced,"inner sees original ID/reduced quantity and only remaining budget");
        Require(result.Stats.PlacementAttempts>7&&result.Stats.CandidatePatterns==3,"preparation/expansion/audits add to inner work statistics");
        Require(p.Parts[0].Quantity==original.Parts[0].Quantity&&p.Stocks[0].Quantity==original.Stocks[0].Quantity,"original quantities and stock inventory stay immutable");
        auto eight=Four(8); Complete(eight,Solve(eight),8);
        auto shifted=Four(); shifted.EdgeMargin=2; shifted.Stocks[0].Width=10; shifted.Stocks[0].Height=8;
        shifted.Parts[0].AllowedAnglesDegrees={90};
        for(auto& point:shifted.Parts[0].Geometry.Outer){point.X+=17;point.Y-=9;}
        const auto beforePoints=shifted.Parts[0].Geometry.Outer;
        const auto rotated=Solve(shifted); Complete(shifted,rotated,4);
        for(const auto& x:rotated.Layouts[0].Placements)Require(x.Transform.AngleDegrees==90,"inner original angle must survive cell translation");
        for(std::size_t i=0;i<beforePoints.size();++i)
            Require(shifted.Parts[0].Geometry.Outer[i].X==beforePoints[i].X&&shifted.Parts[0].Geometry.Outer[i].Y==beforePoints[i].Y,"source shifted geometry must not be normalized or mutated");
        auto zero=p; zero.Parts.push_back(Box("zero-row",1,1,0)); Complete(zero,Solve(zero),4);
        auto allZero=p; allZero.Parts[0].Quantity=0; allZero.PartGap=2;
        bool emptyCalled=false;
        const auto noDemand=Solve(allZero,[&](const Problem& u,const Options& o){emptyCalled=true;return Line(u,o);});
        const auto emptyChecked=Validate(allZero,noDemand);
        Require(noDemand.State==Status::Complete&&noDemand.GeometryValidated&&noDemand.Layouts.empty()&&
            noDemand.Remaining.empty()&&emptyChecked.Valid&&emptyChecked.Complete&&!emptyCalled,
            "all-zero valid original input completes without consuming a sheet or calling the constructor");
        auto zeroTiny=Budget();zeroTiny.MaxPlacementAttempts=1;
        const auto noBudget=Solve(allZero,Line,zeroTiny);Empty(allZero,noBudget,1);
        Require(noBudget.State!=Status::Complete,"empty demand still pays original validation under caller budget");
        auto zeroCancel=Budget();zeroCancel.Cancel=[] {return true;};
        const auto stoppedEmpty=Solve(allZero,Line,zeroCancel);Empty(allZero,stoppedEmpty,zeroCancel.MaxPlacementAttempts);
        Require(stoppedEmpty.State==Status::Cancelled,"empty demand still observes real caller cancellation");
        auto ambiguous=p; ambiguous.Stocks.push_back(ambiguous.Stocks[0]); ambiguous.Stocks.back().ID="other";
        Empty(ambiguous,Solve(ambiguous),Budget().MaxPlacementAttempts);
        auto gap=p; gap.PartGap=.001; Empty(gap,Solve(gap),Budget().MaxPlacementAttempts);
        auto sparse=p; sparse.Stocks[0].Width+=.000001; sparse.Tolerance=.01;
        bool sparseCalled=false; auto unsupported=Solve(sparse,[&](const Problem& u,const Options& o){sparseCalled=true;return Line(u,o);});
        Empty(sparse,unsupported,Budget().MaxPlacementAttempts); Require(!sparseCalled,"user geometric tolerance must not allow sparse repeated units");
        auto raw=p; raw.Parts={Box("a",2,3,2),Box("b",2,3,2)}; Empty(raw,Solve(raw),Budget().MaxPlacementAttempts);
        auto huge=Four(5000); Empty(huge,Solve(huge),Budget().MaxPlacementAttempts);
        for(std::size_t limit:{std::size_t(0),std::size_t(1),std::size_t(8),std::size_t(16),std::size_t(32)})
        {auto o=Budget();o.MaxPlacementAttempts=limit;const auto r=Solve(p,Line,o);Empty(p,r,limit);Require(r.State!=Status::Cancelled,"attempt exhaustion is not cancellation");}
        auto tiny=Budget();tiny.TimeLimitSeconds=1e-12;
        auto timed=Solve(p,Line,tiny);Empty(p,timed,tiny.MaxPlacementAttempts);Require(timed.State!=Status::Cancelled,"deadline exhaustion is not cancellation");
        auto slow=Budget();slow.TimeLimitSeconds=.001;
        timed=Solve(p,[](const Problem& u,const Options& o){std::this_thread::sleep_for(std::chrono::milliseconds(5));return Line(u,o);},slow);
        Empty(p,timed,slow.MaxPlacementAttempts);Require(timed.State!=Status::Cancelled,"inner deadline overrun cannot be reported as cancellation");
        auto early=Budget();std::size_t cancelCalls=0;early.Cancel=[&]{return ++cancelCalls==1;};
        auto cancelled=Solve(p,Line,early);Empty(p,cancelled,early.MaxPlacementAttempts);Require(cancelled.State==Status::Cancelled,"real cancellation must latch even if callback later returns false");
        bool armed=false;auto late=Budget();late.Cancel=[&]{return armed;};
        cancelled=Solve(p,[&](const Problem& u,const Options& o){auto r=Line(u,o);armed=true;return r;},late);
        Empty(p,cancelled,late.MaxPlacementAttempts);Require(cancelled.State==Status::Cancelled,"late real cancellation must clear expanded and unit layouts");
        Empty(p,Solve(p,[](const Problem& u,const Options& o){auto r=Line(u,o);r.State=Status::Partial;return r;}),Budget().MaxPlacementAttempts);
        Empty(p,Solve(p,[](const Problem& u,const Options& o){auto r=Line(u,o);r.Layouts[0].Placements[0].Transform.X+=.5;return r;}),Budget().MaxPlacementAttempts);
        auto wideTolerance=p;wideTolerance.Tolerance=.01;
        for(double delta:{1e-8,.005})
            Empty(wideTolerance,Solve(wideTolerance,[delta](const Problem& u,const Options& o)
            {auto r=Line(u,o);r.Layouts[0].Placements[0].Transform.X+=delta;return r;}),Budget().MaxPlacementAttempts);
        const auto fakeExtent=Solve(wideTolerance,[](const Problem& u,const Options& o)
        {auto r=Line(u,o);r.Layouts[0].Width+=1e-8;r.Layouts[0].Placements[0].Transform.X+=1e-8;return r;});
        Empty(wideTolerance,fakeExtent,Budget().MaxPlacementAttempts);
        Require(std::any_of(fakeExtent.Diagnostics.begin(),fakeExtent.Diagnostics.end(),[](const auto& note)
            {return note.find("constructor unit failed")!=std::string::npos;}),
            "strict unit boundary must use real stock dimensions rather than inner layout metadata");
        auto overlap=eight;overlap.Tolerance=.01;
        for(double delta:{1e-8,.005})
            Empty(overlap,Solve(overlap,[delta](const Problem& u,const Options& o)
            {auto r=Line(u,o);r.Layouts[0].Placements[1].Transform.X-=delta;return r;}),Budget().MaxPlacementAttempts);
        auto notch=p;notch.Tolerance=.01;
        notch.Parts[0].Geometry.Outer={{0,0},{2,0},{2,3},{1,3},{1,3-1e-8},{0,3}};
        Empty(notch,Solve(notch),Budget().MaxPlacementAttempts);
        auto shiftedLoose=shifted;shiftedLoose.Tolerance=.01;Complete(shiftedLoose,Solve(shiftedLoose),4);
        Empty(eight,Solve(eight,[](const Problem& u,const Options& o){auto r=Line(u,o);r.Layouts[0].Placements[1].Instance=0;return r;}),Budget().MaxPlacementAttempts);
        Empty(p,Solve(p,[](const Problem& u,const Options& o){auto r=Line(u,o);r.Stats.PlacementAttempts=o.MaxPlacementAttempts;return r;}),Budget().MaxPlacementAttempts);
        std::size_t created=0;auto f=MakeRectangleRepeatCellFactory([&]{++created;return std::make_unique<FunctionStrategy>(Line);},2,2);
        auto object=f();Complete(p,object->Run(p,Budget()),4);Complete(p,object->Run(p,Budget()),4);Require(created==2,"each wrapper invocation creates a fresh constructor");
        bool threw=false;try{MakeRectangleRepeatCellFactory(Mock(Line),std::numeric_limits<std::size_t>::max(),2);}catch(const std::invalid_argument&){threw=true;}
        Require(threw,"cell product overflow/private scope must be rejected");
        const auto hard=Hard25();auto native=MakeRectangleRepeatCellFactory(Named("rect.block-library-mrv"),2,2)();
        Complete(hard,native->Run(hard,Budget()),100);
        Problem repeated;repeated.Stocks={{"stock",2240,6000,1,1,"steel"}};
        repeated.Parts={Box("A",208,179,70),Box("B",112,373,70),Box("C",221,227,70),Box("D",99,421,70),Box("E",109,194,70)};
        auto factory=MakeRectangleRepeatCellFactory(Named("rect.pinwheel-merge"),7,10);
        const auto full=factory()->Run(repeated,Budget());Complete(repeated,full,350);
        Require(full.Stats.PlacementAttempts<=Budget().MaxPlacementAttempts,"350 original expansion and verification share one caller budget");
        Require(DefaultStrategies().IDs()==defaultIDs,"optional repeat-cell API must not change default strategy IDs");
        std::cout<<"Rectangle repeat-cell tests passed; hard25 and 350 originals complete.\n";
        return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
