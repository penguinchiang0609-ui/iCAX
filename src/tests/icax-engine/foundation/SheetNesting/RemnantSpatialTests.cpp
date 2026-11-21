#include "SheetNesting.h"
#include "SheetNestingPipeline.h"
#include "SheetNestingPlaneStrategies.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>

using namespace iCAX::SheetNesting;
namespace iCAX::SheetNesting::Detail
{
    RegularRemnant FindRegularRemnantUnfiltered(const Problem&,const Layout&,std::size_t);
    RemnantEvaluation EvaluateRemnantsUnfiltered(const Problem&,const Result&,std::size_t);
}
namespace
{
void Require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
Part Rectangle(const char* id,double width,double height)
{Part p;p.ID=id;p.Geometry.Outer={{0,0},{width,0},{width,height},{0,height}};p.Material="m";return p;}
Result Place(const Problem& p,Pose pose)
{Result r;r.State=Status::Complete;r.Layouts={{p.Stocks[0].ID,0,p.Stocks[0].Width,p.Stocks[0].Height,{{p.Parts[0].ID,0,pose,0,false}},0}};return r;}
void Same(const RegularRemnant& actual,const RegularRemnant& reference)
{
    Require(actual.Verified==reference.Verified&&actual.Resolution==reference.Resolution,
        "spatial filtering changed remnant verification or resolution");
    Require(actual.Area==reference.Area&&actual.AreaUpperBound==reference.AreaUpperBound&&
        actual.Bounds.X==reference.Bounds.X&&actual.Bounds.Y==reference.Bounds.Y&&
        actual.Bounds.Width==reference.Bounds.Width&&actual.Bounds.Height==reference.Bounds.Height,
        "spatial filtering changed the original grid/refinement certificate");
}
void CertifyRectangle(const Problem& problem,const Result& source,const RegularRemnant& remnant,std::size_t layoutIndex=0)
{
    if(!remnant.Verified||remnant.Area<=0)return;
    auto p=problem;auto output=source;
    auto proof=Rectangle("__independent_remnant_rectangle__",remnant.Bounds.Width,remnant.Bounds.Height);
    const auto stock=std::find_if(p.Stocks.begin(),p.Stocks.end(),[&](const Stock& s){return s.ID==output.Layouts[layoutIndex].StockID;});
    Require(stock!=p.Stocks.end(),"missing physical stock for independent certificate");
    proof.Material=stock->Material;p.Parts.push_back(std::move(proof));
    p.AllowCommonEdges=false; // A reusable rectangle must retain the ordinary gap.
    output.Layouts[layoutIndex].Placements.push_back({"__independent_remnant_rectangle__",0,
        {remnant.Bounds.X,remnant.Bounds.Y,0},0,false});
    const auto checked=Validate(p,output);
    Require(checked.Valid&&checked.Complete,"indexed remnant overlaps material, loses demand or relaxes the actual gap");
}
void Compare(const Problem& p,const Result& output,std::size_t resolution)
{
    Require(Validate(p,output).Valid,"remnant differential fixture itself is invalid");
    const auto reference=Detail::EvaluateRemnantsUnfiltered(p,output,resolution);
    const auto actual=EvaluateRemnants(p,output,resolution);
    Require(actual.Measured==reference.Measured&&actual.FinalLayoutIndices==reference.FinalLayoutIndices&&
        actual.LastSheetFreeArea==reference.LastSheetFreeArea&&actual.LastSheetRegularArea==reference.LastSheetRegularArea&&
        actual.LastSheetAreaUpperBound==reference.LastSheetAreaUpperBound&&actual.PerLayout.size()==reference.PerLayout.size(),
        "spatial filtering changed material-tail aggregate quality");
    for(std::size_t i=0;i<actual.PerLayout.size();++i)
    {Same(actual.PerLayout[i],reference.PerLayout[i]);CertifyRectangle(p,output,actual.PerLayout[i],i);}
}
void DifferentialCases()
{
    Problem p;p.PartGap=.2;p.EdgeMargin=.5;p.Parts={Rectangle("plate",120,100)};p.Stocks={{"sheet",320,260,2,1,"m"}};
    for(std::size_t row=0;row<4;++row)for(std::size_t col=0;col<5;++col)
    {
        const double x=5+22*double(col),y=5+22*double(row);
        std::vector<Point> ring{{x,y},{x+10,y},{x+10,y+12},{x,y+12}};
        if((row+col)%2)std::reverse(ring.begin(),ring.end());
        p.Parts[0].Geometry.Holes.push_back(std::move(ring));
    }
    p.Parts[0].Geometry.CommonEdgeRings.resize(21,false);p.Parts[0].Geometry.CommonEdgeRings[0]=true;
    for(const auto pose:{Pose{10.125,12.25,0},Pose{150.5,10.75,90},Pose{100,70,23.7}})
        for(const auto resolution:{8u,24u,64u})Compare(p,Place(p,pose),resolution);
    // A translated non-axis-aligned contour and several curved, disjoint holes.
    auto curved=Rectangle("curved",1,1);curved.Geometry.Outer.clear();
    constexpr double pi=3.141592653589793238462643383279502884;
    for(std::size_t i=0;i<96;++i)
    {const double a=2*pi*i/96,radius=i%2?38:40;curved.Geometry.Outer.push_back({13+radius*std::cos(a),-7+radius*std::sin(a)});}
    for(int row=-1;row<=1;++row)for(int col=-1;col<=1;++col)
    {
        std::vector<Point> ring;
        for(std::size_t i=0;i<32;++i){const auto a=2*pi*i/32;ring.push_back({13+col*12+2*std::cos(a),-7+row*12+2*std::sin(a)});}
        curved.Geometry.Holes.push_back(std::move(ring));
    }
    curved.Geometry.CommonEdgeRings.resize(10,false);
    p.Parts={curved};for(auto pose:{Pose{70,60,0},Pose{100.125,90.5,37}})Compare(p,Place(p,pose),32);
    // Every sub-grid material island must still exclude the entire intersected cell.
    p.Parts={Rectangle("tiny",.001,.002)};Compare(p,Place(p,{47.1234567,61.7654321,0}),16);
    p.Parts={Rectangle("touching",40,256)};p.Stocks={{"sheet",320,260,1,1,"m"}};p.EdgeMargin=2;p.PartGap=3;
    Compare(p,Place(p,{2,2,0}),64);
    // Analytic circles retain their original path and exact circle safety gate.
    Part disc;disc.ID="disc";disc.Material="m";disc.Geometry.Radius=12;disc.Geometry.Center={7,-13};p.Parts={disc};
    Compare(p,Place(p,{70,80,0}),32);
    // Reusing the same mutable objects cannot retain an earlier pose or source geometry.
    p.Parts={Rectangle("changed",50,30)};auto output=Place(p,{2,2,0});Compare(p,output,24);
    p.Parts[0].Geometry.Holes={{{10,5},{40,5},{40,25},{10,25}}};output.Layouts[0].Placements[0].Transform={120,80,90};Compare(p,output,24);
    std::cout<<"PASS remnant spatial/reference fields, original rings, source edits, holes, circles and tiny islands\n";
}
Problem ReadOriginal(const std::filesystem::path& path)
{
    std::ifstream input(path);input.imbue(std::locale::classic());std::string version;std::size_t parts=0,stocks=0;
    input>>version>>parts>>stocks;Require(version=="ICAX_NESTING_PROBLEM_2"||version=="ICAX_NESTING_PROBLEM_3","unsupported production input version");
    Problem p;input>>p.PartGap>>p.EdgeMargin>>p.Tolerance>>p.AllowCommonEdges;
    const auto ring=[&](std::size_t count){std::vector<Point> points(count);for(auto& point:points)input>>point.X>>point.Y;return points;};
    for(std::size_t i=0;i<parts;++i)
    {
        Part part;std::size_t angles=0,outer=0,holes=0;input>>std::quoted(part.ID)>>part.Quantity>>std::quoted(part.Material)>>part.FreeRotation>>angles;
        part.AllowedAnglesDegrees.resize(angles);for(auto& angle:part.AllowedAnglesDegrees)input>>angle;
        input>>part.Geometry.Radius>>part.Geometry.Center.X>>part.Geometry.Center.Y>>outer>>holes;part.Geometry.Outer=ring(outer);
        for(std::size_t j=0;j<holes;++j){std::size_t count=0;input>>count;part.Geometry.Holes.push_back(ring(count));}
        if(version=="ICAX_NESTING_PROBLEM_3")
        {std::size_t count=0;input>>count;for(std::size_t j=0;j<count;++j){bool value=false;input>>value;part.Geometry.CommonEdgeRings.push_back(value);}}
        p.Parts.push_back(std::move(part));
    }
    for(std::size_t i=0;i<stocks;++i){Stock stock;input>>std::quoted(stock.ID)>>stock.Width>>stock.Height>>stock.Quantity>>stock.Cost>>std::quoted(stock.Material);p.Stocks.push_back(std::move(stock));}
    Require(bool(input),"incomplete original production input");return p;
}
void ProfileEvaluation(const std::filesystem::path& path)
{
    const auto p=ReadOriginal(path);StrategyRegistry registry;RegisterPlaneStrategies(registry);Options options;
    options.EnableBasicCompaction=false;options.TimeLimitSeconds=60;options.MaxPlacementAttempts=8000000;
    const auto output=registry.Create("plane.envelope-initial")->Run(p,options);
    Require(Validate(p,output).Valid&&Validate(p,output).Complete,"real profile constructor did not preserve original complete demand");
    const auto started=std::chrono::steady_clock::now();const auto indexed=EvaluateRemnants(p,output);
    const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    const auto referenceStarted=std::chrono::steady_clock::now();const auto reference=Detail::EvaluateRemnantsUnfiltered(p,output,64);
    const double referenceSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-referenceStarted).count();
    Require(indexed.Measured&&reference.Measured&&indexed.PerLayout.size()==reference.PerLayout.size(),"real remnant evaluation did not finish");
    for(std::size_t i=0;i<indexed.PerLayout.size();++i){Same(indexed.PerLayout[i],reference.PerLayout[i]);CertifyRectangle(p,output,indexed.PerLayout[i],i);}
    std::cout<<"REMNANT-EVALUATION input="<<path.filename().string()<<" indexed-seconds="<<seconds<<" reference-seconds="<<referenceSeconds
        <<" measured="<<indexed.Measured<<" regular-area="<<indexed.LastSheetRegularArea<<std::endl;
}
void ProfilePipelines(const std::vector<std::filesystem::path>& paths)
{
    struct Measurement{std::filesystem::path Path;double First=-1,FirstMeasured=-1,Seconds=0;JobResult Result;bool IndependentValid=true;};
    struct Progress{std::atomic<int> Stage{0};std::mutex Lock;std::string LastMeasuredRoute;};
    std::mutex print;
    std::vector<std::future<Measurement>> jobs;
    std::vector<std::shared_ptr<Progress>> progress;
    for(const auto path:paths)
    {
    auto state=std::make_shared<Progress>();progress.push_back(state);
    jobs.push_back(std::async(std::launch::async,[&,path,state]
    {
        const auto p=ReadOriginal(path);Options options;options.EnableBasicCompaction=false;
        options.TimeLimitSeconds=60;options.MaxPlacementAttempts=8000000;
        std::atomic<double> first{-1},firstMeasured{-1};const auto started=std::chrono::steady_clock::now();
        const auto elapsed=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();};
        ParallelConstructionSettings settings;settings.Workers=4;settings.FairScheduling=true;
        settings.OnImprovement=[&](const JobResult& candidate)
        {
            if(!candidate.Checked.Valid||!candidate.Checked.Complete)return;
            if(candidate.Remnants.Measured){std::lock_guard lock(state->Lock);state->LastMeasuredRoute=candidate.StrategyID;}
            double expected=-1;const double seconds=elapsed();
            first.compare_exchange_strong(expected,seconds);
            expected=-1;
            if(candidate.Remnants.Measured&&firstMeasured.compare_exchange_strong(expected,seconds))
            {std::lock_guard lock(print);std::cout<<"REMNANT-FIRST input="<<path.filename().string()<<" measured-complete-seconds="<<seconds<<std::endl;}
        };
        StrategyRegistry registry;registry.Register("profile.initial",MakePlaneInitialFactory(settings));
        state->Stage.store(1);
        auto result=RunJob({"remnant-profile","profile.initial",p,options},registry);
        state->Stage.store(2);
        const double seconds=elapsed();bool independent=true;
        try{for(std::size_t i=0;i<result.Remnants.PerLayout.size();++i)CertifyRectangle(p,result.Output,result.Remnants.PerLayout[i],i);}
        catch(...){independent=false;}
        state->Stage.store(3);return Measurement{path,first.load(),firstMeasured.load(),seconds,std::move(result),independent};
    }));
    }
    bool valid=true,firstInBudget=true,wholeInBudget=true;
    std::vector<bool> reported(jobs.size(),false);std::size_t complete=0;
    const auto watchStarted=std::chrono::steady_clock::now();auto lastProgress=watchStarted;
    while(complete<jobs.size())
    {
        bool changed=false;
        for(std::size_t i=0;i<jobs.size();++i)
        {
        if(reported[i]||jobs[i].wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)continue;
        reported[i]=true;++complete;changed=true;
        try
        {
        auto m=jobs[i].get();std::size_t placed=m.Result.Checked.PlacedCount;
        std::cout<<"REMNANT-PIPELINE input="<<m.Path.filename().string()<<" jobs="<<paths.size()<<" workers=4 first-complete-seconds="<<m.First
            <<" first-measured-complete-seconds="<<m.FirstMeasured<<" whole-seconds="<<m.Seconds<<" placed="<<placed
            <<" valid="<<m.Result.Checked.Valid<<" complete="<<m.Result.Checked.Complete<<" measured="<<m.Result.Remnants.Measured
            <<" independent-remnant-valid="<<m.IndependentValid<<std::endl;
        valid=valid&&m.Result.Checked.Valid&&m.Result.Checked.Complete&&m.Result.Remnants.Measured&&m.IndependentValid;
        firstInBudget=firstInBudget&&m.FirstMeasured>=0&&m.FirstMeasured<60;
        wholeInBudget=wholeInBudget&&m.Seconds<=65;
        for(const auto& line:m.Result.Output.Diagnostics)if(line.rfind("parallel.construction job=",0)==0)
            std::cout<<"REMNANT-ROUTE input="<<m.Path.filename().string()<<' '<<line<<std::endl;
        }
        catch(const std::exception& error)
        {valid=false;std::cout<<"REMNANT-PIPELINE-ERROR input="<<paths[i].filename().string()<<" error="<<error.what()<<std::endl;}
        }
        const auto now=std::chrono::steady_clock::now();
        if(now-lastProgress>=std::chrono::seconds(10))
        {
            for(std::size_t i=0;i<jobs.size();++i)if(!reported[i])
            {
                const int stage=progress[i]->Stage.load();std::string route;
                {std::lock_guard lock(progress[i]->Lock);route=progress[i]->LastMeasuredRoute;}
                std::cout<<"REMNANT-WAIT input="<<paths[i].filename().string()<<" elapsed="
                    <<std::chrono::duration<double>(now-watchStarted).count()<<" stage="
                    <<(stage==0?"reading-input":stage==1?"RunJob-awaiting-portfolio":stage==2?"independent-remnant-check":"ready")
                    <<" last-measured-incumbent-route="<<route<<std::endl;
            }
            lastProgress=now;
        }
        if(!changed)std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    Require(valid,"real pipeline did not retain a complete assessed original result");
    std::cout<<"REMNANT-TIMING first-all-under-60="<<firstInBudget<<" whole-all-within-65-observation="<<wholeInBudget<<std::endl;
    Require(firstInBudget,"first complete assessed original exceeded the requested first-minute construction window");
}
}
int main(int argc,char** argv)try
{
    if(argc>=3&&std::string(argv[1])=="--pipeline")
    {std::vector<std::filesystem::path> paths;for(int i=2;i<argc;++i)paths.emplace_back(argv[i]);ProfilePipelines(paths);return 0;}
    if(argc==3&&std::string(argv[1])=="--evaluate"){ProfileEvaluation(argv[2]);return 0;}
    DifferentialCases();return 0;
}
catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
