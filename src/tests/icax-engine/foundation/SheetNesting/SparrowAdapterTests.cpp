// Transport/safety controls linked to NestingSparrow.cpp with a local mock FFI.
// This executable must not link the real Rust bridge; SparrowKernelTests does.
#include "SheetNestingSparrow.h"
#ifndef BOOST_ALL_NO_LIB
#define BOOST_ALL_NO_LIB
#endif
#include <boost/json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

using namespace iCAX::SheetNesting;
namespace json = boost::json;
namespace
{
    enum class Mode { Good, Missing, Duplicate, Foreign, Overlap, ForbiddenAngle,
        Outside, Underreported, OverCap, BadJson, Null, Error, Protocol, Revision,
        ConcurrentPoll, Deadline, CancelledNoLayout, WarmSame, WarmBinding, WarmRepaired, WarmBadRepair };
    thread_local Mode CurrentMode = Mode::Good;
    thread_local json::object LastRequest;
    std::atomic<unsigned> SolveCalls{0}, OutstandingResponses{0};
    constexpr const char* Revision = "ab2aaef602755f1ed51b15406a2f0ef0950e18ce";

    void Require(bool value, const char* message)
    { if (!value) throw std::runtime_error(message); }
    Part Box(std::string id, double w, double h, std::size_t quantity = 1)
    {
        Part p; p.ID=std::move(id); p.Material="m"; p.Quantity=quantity;
        p.FreeRotation=false; p.AllowedAnglesDegrees={0};
        p.Geometry.Outer={{0,0},{w,0},{w,h},{0,h}}; return p;
    }
    Problem Input()
    {
        Problem p; p.Tolerance=1e-7; p.Parts={Box("original",4,2,2)};
        p.Stocks.push_back({"stock",10,10,1,1,"m"}); return p;
    }
    std::size_t Remaining(const Result& result)
    { std::size_t n=0; for(const auto& item:result.Remaining)n+=item.Quantity; return n; }
    bool Has(const Result& r,const char* text)
    { for(const auto& d:r.Diagnostics)if(d.find(text)!=std::string::npos)return true; return false; }
    Result Run(const Problem& p, Mode mode=Mode::Good, const SparrowSettings& settings={})
    { CurrentMode=mode; Options o; o.TimeLimitSeconds=3; return MakeSparrowFactory(settings)()->Run(p,o); }
    void Complete(const Problem& p,const Result& result)
    {
        const auto checked=Validate(p,result);
        Require(result.State==Status::Complete&&result.GeometryValidated&&checked.Valid&&checked.Complete,
            "complete original native result was not validated");
        Require(result.Remaining.empty()&&result.Layouts.size()==1&&result.Layouts[0].StockInstance==0,
            "complete result changed stock inventory or demand");
    }
    void FailedCompleteDemand(const Problem& p,const Result& result,Status status=Status::Failed)
    {
        Require(result.State==status&&!result.GeometryValidated&&result.Layouts.empty(),"failed proposal leaked a layout");
        std::size_t required=0;for(const auto& part:p.Parts)required+=part.Quantity;
        Require(Remaining(result)==required,"failed proposal did not retain complete original demand");
        Require(OutstandingResponses.load()==0,"FFI response leaked on failure");
    }
    char* Copy(std::string text)
    {
        auto* result=new char[text.size()+1];std::memcpy(result,text.c_str(),text.size()+1);
        ++OutstandingResponses;return result;
    }
    bool SameLayout(const Result& a,const Result& b)
    {
        if(a.State!=b.State||a.GeometryValidated!=b.GeometryValidated||a.Layouts.size()!=b.Layouts.size()||a.Remaining.size()!=b.Remaining.size())return false;
        for(std::size_t i=0;i<a.Layouts.size();++i)
        {
            const auto& x=a.Layouts[i];const auto& y=b.Layouts[i];
            if(x.StockID!=y.StockID||x.StockInstance!=y.StockInstance||x.Width!=y.Width||x.Height!=y.Height||x.MaterialArea!=y.MaterialArea||x.Placements.size()!=y.Placements.size())return false;
            for(std::size_t j=0;j<x.Placements.size();++j)
            {
                const auto& p=x.Placements[j];const auto& q=y.Placements[j];
                if(p.PartID!=q.PartID||p.Instance!=q.Instance||p.Group!=q.Group||p.GapFiller!=q.GapFiller||
                    p.Transform.X!=q.Transform.X||p.Transform.Y!=q.Transform.Y||p.Transform.AngleDegrees!=q.Transform.AngleDegrees)return false;
            }
        }
        for(std::size_t i=0;i<a.Remaining.size();++i)if(a.Remaining[i].PartID!=b.Remaining[i].PartID||a.Remaining[i].Quantity!=b.Remaining[i].Quantity)return false;
        return true;
    }
}

extern "C" char* sparrow_solve_json(const char* request, bool(*cancel)(void*), void* context)
{
    ++SolveCalls; LastRequest=json::parse(request).as_object();
    if(CurrentMode==Mode::Null)return nullptr;
    if(CurrentMode==Mode::BadJson)return Copy("{bad-json");
    if(CurrentMode==Mode::Error)return Copy(R"({"protocol":1,"ok":false,"cancelled":false,"error":"mock error"})");
    if(CurrentMode==Mode::CancelledNoLayout)
    { cancel(context);return Copy(R"({"protocol":1,"ok":false,"cancelled":true,"error":"cancelled"})"); }
    bool stopped=false;
    if(CurrentMode==Mode::ConcurrentPoll)
    {
        std::atomic<bool> any{false};std::vector<std::jthread> workers;
        for(int i=0;i<8;++i)workers.emplace_back([&]{for(int k=0;k<3;++k)if(cancel(context))any.store(true);});
        for(auto& thread:workers)thread.join();stopped=any.load();
    }
    if(CurrentMode==Mode::Deadline)
    {std::this_thread::sleep_for(std::chrono::milliseconds(25));stopped=cancel(context);}
    json::array placements;
    double cursor=0;
    for(const auto& value:LastRequest.at("instance").as_object().at("items").as_array())
    {
        const auto& part=value.as_object();const auto id=part.at("id").to_number<std::size_t>();
        const auto quantity=part.at("demand").to_number<std::size_t>();
        const auto* domain=part.if_contains("allowed_orientations");
        const double degrees=domain?domain->as_array().front().to_number<double>():17.123456789;
        const double a=degrees*3.14159265358979323846/180.;
        const auto rotation=[&](double x,double y){
            if(degrees==0)return Point{x,y};if(degrees==90)return Point{-y,x};
            return Point{x*std::cos(a)-y*std::sin(a),x*std::sin(a)+y*std::cos(a)};};
        double minX=1e100,minY=1e100,maxX=-1e100;
        for(const auto& row:part.at("shape").as_object().at("data").as_array())
        {
            const auto& xy=row.as_array();auto point=rotation(xy[0].to_number<double>(),xy[1].to_number<double>());
            minX=std::min(minX,point.X);maxX=std::max(maxX,point.X);minY=std::min(minY,point.Y);
        }
        for(std::size_t k=0;k<quantity;++k)
        {
            placements.push_back(json::object{{"part_index",id},{"instance",k},{"x",cursor-minX},{"y",-minY},{"angle_degrees",degrees}});
            cursor+=maxX-minX;
        }
    }
    if(CurrentMode==Mode::Missing)placements.pop_back();
    if(CurrentMode==Mode::Duplicate)placements.back()=placements.front();
    if(CurrentMode==Mode::Foreign)placements[0].as_object()["part_index"]=999;
    if(CurrentMode==Mode::Overlap)placements[1].as_object()["x"]=placements[0].as_object()["x"];
    if(CurrentMode==Mode::ForbiddenAngle)placements[0].as_object()["angle_degrees"]=90;
    if(CurrentMode==Mode::Outside)placements[0].as_object()["y"]=1000;
    if(CurrentMode==Mode::Underreported)cursor-=1;
    if(CurrentMode==Mode::OverCap){cursor=1000;placements.back().as_object()["x"]=996;}
    if(CurrentMode==Mode::WarmSame)
    { placements=LastRequest.at("initial_layout").as_array();cursor=LastRequest.at("initial_occupied_width").to_number<double>(); }
    json::object response{{"protocol",CurrentMode==Mode::Protocol?2:1},{"ok",true},{"cancelled",stopped},
        {"placements",std::move(placements)},{"occupied_width",cursor},{"elapsed_seconds",.001},
        {"upstream_revision",CurrentMode==Mode::Revision?"wrong-revision":Revision}};
    if(LastRequest.if_contains("mode"))
    {
        response.emplace("mode","warm_compress");response.emplace("initial_solution_origin",
            CurrentMode==Mode::WarmRepaired||CurrentMode==Mode::WarmBadRepair?"provided-incumbent-after-internal-feasibility-repair":"provided-incumbent");
        response.emplace("initial_occupied_width",LastRequest.at("initial_occupied_width"));
        response.emplace("stock_width_cap",CurrentMode==Mode::WarmBinding?json::value(999):LastRequest.at("stock_width_cap"));
        if(CurrentMode==Mode::WarmRepaired||CurrentMode==Mode::WarmBadRepair)
            response.emplace("warm_feasibility_repair",json::object{{"accepted",true},{"imported_seed_reported_feasible",false},
                {"original_stock_cap_unchanged",true},{"budget_seconds",.01},{"elapsed_seconds",CurrentMode==Mode::WarmBadRepair?.02:.001}});
    }
    return Copy(json::serialize(response));
}

extern "C" void sparrow_free_json(char* response)
{ if(response){delete[] response;--OutstandingResponses;} }

int main()try
{
    const auto p=Input();
    const auto signature=p.Parts[0].Geometry.Outer[2];
    Complete(p,Run(p));
    Require(p.Parts[0].Geometry.Outer[2].X==signature.X&&p.Parts[0].Geometry.Outer[2].Y==signature.Y,
        "transport changed original source geometry");
    Require(LastRequest.at("protocol").to_number<int>()==1&&
        LastRequest.at("instance").as_object().at("strip_height").to_number<double>()==10,
        "request protocol or original board height changed");
    Require(!LastRequest.at("instance").as_object().if_contains("strip_width"),"hard acceptance cap silently changed strip objective");
    Require(OutstandingResponses.load()==0,"successful FFI response leaked");

    for(const auto mode:{Mode::Missing,Mode::Duplicate,Mode::Foreign,Mode::Overlap,Mode::ForbiddenAngle,
        Mode::Outside,Mode::OverCap,Mode::BadJson,Mode::Null,Mode::Error,Mode::Protocol,Mode::Revision})
        FailedCompleteDemand(p,Run(p,mode));
    const auto measuredFromSource=Run(p,Mode::Underreported);Complete(p,measuredFromSource);
    Require(Has(measuredFromSource,"occupied-width=8; reported-width=7"),"upstream width metadata replaced original source extent");

    auto rejectScope=[&](Problem input){const auto calls=SolveCalls.load();FailedCompleteDemand(input,Run(input),Status::InvalidInput);
        Require(SolveCalls.load()==calls,"unsupported scope entered kernel");};
    auto gap=p;gap.PartGap=.001;rejectScope(gap);
    auto margin=p;margin.EdgeMargin=.001;rejectScope(margin);
    auto multistock=p;multistock.Stocks.push_back({"second",10,10,1,1,"m"});rejectScope(multistock);
    auto material=p;material.Parts[0].Material="other";rejectScope(material);
    auto holes=p;holes.Parts[0].Geometry.Holes={{{1,.5},{2,.5},{2,1.5},{1,1.5}}};rejectScope(holes);
    auto circle=p;circle.Parts[0].Geometry.Outer.clear();circle.Parts[0].Geometry.Radius=1;rejectScope(circle);
    SparrowSettings bad;bad.Threads=0;const auto calls=SolveCalls.load();
    FailedCompleteDemand(p,Run(p,Mode::Good,bad),Status::InvalidInput);Require(SolveCalls.load()==calls,"invalid settings entered kernel");

    auto free=p;free.Parts[0].FreeRotation=true;const auto freeResult=Run(free);Complete(free,freeResult);
    Require(freeResult.Layouts[0].Placements[0].Transform.AngleDegrees==17.123456789,"free output angle was rounded to a discrete direction");
    Require(!LastRequest.at("instance").as_object().at("items").as_array()[0].as_object().if_contains("allowed_orientations"),
        "free rotation was reduced to discrete directions");
    auto zero=p;zero.Parts[0].AllowedAnglesDegrees.clear();Complete(zero,Run(zero));
    const auto& zeroDomain=LastRequest.at("instance").as_object().at("items").as_array()[0].as_object().at("allowed_orientations").as_array();
    Require(zeroDomain.size()==1&&zeroDomain[0].to_number<double>()==0,"host empty finite domain did not remain zero-only");
    auto turned=p;turned.Parts[0].Quantity=1;turned.Parts[0].AllowedAnglesDegrees={90};turned.Stocks[0].Width=2;turned.Stocks[0].Height=4;
    Complete(turned,Run(turned));
    auto fractional=p;fractional.Parts[0].AllowedAnglesDegrees={37.25};
    const auto fractionalResult=Run(fractional);Complete(fractional,fractionalResult);
    Require(fractionalResult.Layouts[0].Placements[0].Transform.AngleDegrees==37.25,"nonquarter finite angle was rounded");

    auto shifted=p;for(auto& point:shifted.Parts[0].Geometry.Outer){point.X+=10;point.Y+=20;}
    auto shiftedResult=Run(shifted);Complete(shifted,shiftedResult);
    Require(shiftedResult.Layouts[0].Placements[0].Transform.X==-10&&shiftedResult.Layouts[0].Placements[0].Transform.Y==-20,
        "source origin or output translation was changed");
    const auto& exported=LastRequest.at("instance").as_object().at("items").as_array()[0].as_object().at("shape").as_object().at("data").as_array();
    Require(exported[0].as_array()[0].to_number<double>()==10&&exported[0].as_array()[1].to_number<double>()==20,
        "input polygon was silently recentered");

    auto zeroQuantity=p;zeroQuantity.Parts.insert(zeroQuantity.Parts.begin(),Box("unused",1,1,0));
    auto mapped=Run(zeroQuantity);Complete(zeroQuantity,mapped);
    Require(mapped.Layouts[0].Placements[0].PartID=="original"&&
        LastRequest.at("instance").as_object().at("items").as_array().size()==1&&
        LastRequest.at("instance").as_object().at("items").as_array()[0].as_object().at("id").to_number<int>()==0,
        "zero-demand filtering broke contiguous external indices or original identity mapping");
    SparrowSettings exact;exact.PolygonSimplificationRatio=-1;exact.NarrowConcavityRatio=-1;
    Complete(p,Run(p,Mode::Good,exact));
    Require(LastRequest.at("polygon_simplification_ratio").is_null()&&LastRequest.at("narrow_concavity_ratio").is_null(),
        "disabled preprocessing did not remain null");

    Options cancelled;cancelled.TimeLimitSeconds=3;cancelled.Cancel=[]{return true;};
    const auto beforeCancel=SolveCalls.load();
    FailedCompleteDemand(p,MakeSparrowFactory()()->Run(p,cancelled),Status::Cancelled);
    Require(SolveCalls.load()==beforeCancel,"pre-cancelled job entered kernel");
    Options throwing;throwing.TimeLimitSeconds=3;throwing.Cancel=[]()->bool{throw std::runtime_error("callback");};
    FailedCompleteDemand(p,MakeSparrowFactory()()->Run(p,throwing));
    int finalPolls=0;throwing.Cancel=[&]()->bool{if(++finalPolls>=2)throw 9;return false;};
    CurrentMode=Mode::Good;FailedCompleteDemand(p,MakeSparrowFactory()()->Run(p,throwing));

    std::atomic<int> active{0},maximum{0};Options polling;polling.TimeLimitSeconds=3;
    polling.Cancel=[&]{const int n=++active;maximum.store(std::max(maximum.load(),n));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));--active;return false;};
    CurrentMode=Mode::ConcurrentPoll;Complete(p,MakeSparrowFactory()()->Run(p,polling));
    Require(maximum.load()==1,"parallel kernel polled a non-threadsafe caller callback concurrently");
    int polls=0;polling.Cancel=[&]()->bool{if(++polls>=2)throw 7;return false;};
    CurrentMode=Mode::ConcurrentPoll;FailedCompleteDemand(p,MakeSparrowFactory()()->Run(p,polling));
    Require(OutstandingResponses.load()==0,"callback exception leaked kernel response");

    Options shortBudget;shortBudget.TimeLimitSeconds=.01;CurrentMode=Mode::Deadline;
    auto expired=MakeSparrowFactory()()->Run(p,shortBudget);Complete(p,expired);
    Require(Has(expired,"deadline-polled=1"),"normal deadline path was not exercised");
    polls=0;Options delayedCancel;delayedCancel.TimeLimitSeconds=3;delayedCancel.Cancel=[&]{return ++polls>=2;};
    CurrentMode=Mode::ConcurrentPoll;auto cancelledValid=MakeSparrowFactory()()->Run(p,delayedCancel);
    Require(cancelledValid.State==Status::Cancelled&&cancelledValid.GeometryValidated&&Validate(p,cancelledValid).Complete,
        "cancelled valid original layout was published as complete or lost validation");

    auto concurrent=[&](std::uint64_t seed){return std::async(std::launch::async,[&,seed]{CurrentMode=Mode::Good;
        Options o;o.TimeLimitSeconds=3;o.Seed=seed;auto r=MakeSparrowFactory()()->Run(p,o);Complete(p,r);
        return LastRequest.at("seed").to_number<std::uint64_t>()==seed;});};
    auto first=concurrent(17),second=concurrent(std::numeric_limits<std::uint64_t>::max());
    Require(first.get()&&second.get(),"job-local seed/state leaked between concurrent solves");
    Require(OutstandingResponses.load()==0,"final FFI response leak");

    // The optional postprocessor receives an immutable original incumbent;
    // rejected, malformed, late and out-of-domain proposals keep every layout field.
    Result seed;seed.State=Status::Complete;seed.GeometryValidated=true;
    seed.Layouts.push_back({"stock",0,10,10,{{"original",1,{5,4,0},9,true},{"original",0,{0,4,0},7,false}},16});
    Complete(p,seed);Options refineOptions;refineOptions.TimeLimitSeconds=3;
    const auto stage=MakeSparrowRefineStage(exact);Require(stage.ID=="sparrow.warm-compress","refine stage is not independent");
    CurrentMode=Mode::Good;const auto improved=stage.Factory()->Run(p,seed,refineOptions);Complete(p,improved);
    Require(Has(improved,"mode=warm_compress")&&improved.Layouts[0].Placements[0].Instance==1&&
        improved.Layouts[0].Placements[0].Group==9&&improved.Layouts[0].Placements[0].GapFiller&&
        improved.Layouts[0].Placements[1].Group==7&&!improved.Layouts[0].Placements[1].GapFiller,
        "warm accepted result lost original ordering or constructor provenance");
    Require(LastRequest.at("mode").as_string()=="warm_compress"&&LastRequest.at("stock_width_cap").to_number<double>()==10&&
        LastRequest.at("initial_occupied_width").to_number<double>()==9&&LastRequest.at("initial_layout").as_array()[0].as_object().at("instance").to_number<int>()==1,
        "warm request did not bind the complete original seed or cap");
    CurrentMode=Mode::WarmRepaired;const auto repaired=stage.Factory()->Run(p,seed,refineOptions);Complete(p,repaired);
    Require(Has(repaired,"provided-incumbent-after-internal-feasibility-repair"),"warm feasibility-repair origin lost its explicit phase binding");
    for(const auto mode:{Mode::WarmSame,Mode::WarmBinding,Mode::WarmBadRepair,Mode::Missing,Mode::Duplicate,Mode::Foreign,
        Mode::Overlap,Mode::ForbiddenAngle,Mode::Outside,Mode::OverCap,Mode::BadJson,Mode::Null,Mode::Error,Mode::Revision})
    {
        CurrentMode=mode;const auto retained=stage.Factory()->Run(p,seed,refineOptions);
        Require(SameLayout(seed,retained)&&Has(retained,"retained immutable incumbent"),"warm failed proposal changed original incumbent fields");
    }
    const auto beforeInvalid=SolveCalls.load();auto invalidSeed=seed;invalidSeed.Layouts[0].Placements[1].Instance=1;
    Require(SameLayout(invalidSeed,stage.Factory()->Run(p,invalidSeed,refineOptions))&&SolveCalls.load()==beforeInvalid,
        "invalid complete seed reached warm kernel or changed fields");
    auto stoppedRefine=refineOptions;stoppedRefine.Cancel=[]{return true;};
    Require(SameLayout(seed,stage.Factory()->Run(p,seed,stoppedRefine)),"cancelled warm stage changed incumbent");
    CurrentMode=Mode::Deadline;auto shortRefine=refineOptions;shortRefine.TimeLimitSeconds=.01;
    Require(SameLayout(seed,stage.Factory()->Run(p,seed,shortRefine)),"late warm proposal replaced incumbent");
    auto multiInstance=p;multiInstance.Stocks[0].Quantity=4;auto instanceSeed=seed;instanceSeed.Layouts[0].StockInstance=3;
    CurrentMode=Mode::Good;const auto keptInstance=stage.Factory()->Run(multiInstance,instanceSeed,refineOptions);
    Require(keptInstance.Layouts[0].StockInstance==3&&Validate(multiInstance,keptInstance).Valid,"warm stage changed physical sheet instance");
    Require(OutstandingResponses.load()==0,"warm FFI response leaked");
    std::cout<<"PASS Sparrow adapter protocol, original geometry/demand/angle/cap controls, cancellation and isolation\n";
    return 0;
}
catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
