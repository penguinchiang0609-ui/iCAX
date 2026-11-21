#include "NestingInternal.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
    Part Box(std::string id,int width,int height,std::size_t quantity=1)
    {
        Part p;p.ID=std::move(id);p.Material="m";p.Quantity=quantity;p.FreeRotation=false;p.AllowedAnglesDegrees={0};
        p.Geometry.Outer={{0,0},{double(width),0},{double(width),double(height)},{0,double(height)}};return p;
    }
    Options Budget(std::size_t attempts,double seconds=30)
    {
        Options o;o.MaxPlacementAttempts=attempts;o.TimeLimitSeconds=seconds;
        // These pulse-count controls measure the raw constructor's lifecycle.
        o.EnableBasicCompaction=false;
        return o;
    }
    JobResult Solve(const Problem& p,Options o)
    {return RunJob({"mrv-test","rect.block-library-mrv",p,std::move(o)},DefaultStrategies());}
    void Valid(const Problem& p,const JobResult& r,std::size_t cap)
    {
        for(const auto& error:r.Checked.Errors)std::cerr<<error<<'\n';
        Require(r.Checked.Valid,"MRV result must pass independent original-geometry validation");
        Require(r.Output.Stats.PlacementAttempts<=cap,"MRV compatibility and complement checks must share the caller attempt cap");
        std::size_t demand=0,placed=0,remaining=0;
        for(const auto& part:p.Parts)demand+=part.Quantity;
        for(const auto& item:r.Output.Remaining)remaining+=item.Quantity;
        std::set<std::pair<std::string,std::size_t>> identities;
        for(const auto& layout:r.Output.Layouts)for(const auto& pose:layout.Placements)
        {
            ++placed;Require(identities.emplace(pose.PartID,pose.Instance).second,"MRV cannot duplicate an original identity");
            const auto found=std::find_if(p.Parts.begin(),p.Parts.end(),[&](const auto& part){return part.ID==pose.PartID;});
            Require(found!=p.Parts.end()&&pose.Instance<found->Quantity,"MRV must restore source IDs and instances");
        }
        Require(demand==placed+remaining,"MRV must conserve every original copy");
    }
    void Complete(const Problem& p,const JobResult& r,std::size_t cap)
    {
        Valid(p,r,cap);
        if(!r.Checked.Complete)for(const auto& message:r.Output.Diagnostics)std::cerr<<message<<'\n';
        Require(r.Checked.Complete&&r.Output.State==Status::Complete&&r.Output.Layouts.size()==1,"expected a complete single-sheet MRV result");
    }
    void Untouched(const Problem& p,const JobResult& r,std::size_t cap)
    {Valid(p,r,cap);Require(r.Output.Layouts.empty()&&!r.Checked.Complete,"interruption or unsupported scope must retain all original demand");}
    auto Signature(const Problem& p,const JobResult& r)
    {
        std::vector<std::tuple<double,double,double,double>> signature;
        for(const auto& layout:r.Output.Layouts)for(const auto& pose:layout.Placements)
        {
            const auto part=std::find_if(p.Parts.begin(),p.Parts.end(),[&](const auto& item){return item.ID==pose.PartID;});
            const auto b=Detail::Bounds(part->Geometry,pose.Transform);signature.emplace_back(b.X,b.Y,b.Width,b.Height);
        }
        std::sort(signature.begin(),signature.end());return signature;
    }
    std::string Snapshot(const Problem& p)
    {
        std::ostringstream out;out<<std::setprecision(17)<<p.PartGap<<' '<<p.EdgeMargin<<' '<<p.Tolerance;
        for(const auto& part:p.Parts)
        {
            out<<'|'<<part.ID<<'|'<<part.Quantity<<'|'<<part.Material<<'|'<<part.FreeRotation;
            for(auto angle:part.AllowedAnglesDegrees)out<<'/'<<angle;
            for(auto point:part.Geometry.Outer)out<<'/'<<point.X<<','<<point.Y;
        }
        for(const auto& stock:p.Stocks)out<<'|'<<stock.ID<<'|'<<stock.Width<<','<<stock.Height<<','<<stock.Quantity;
        return out.str();
    }
    Problem C3P2()
    {
        const std::pair<int,int> dimensions[]={
            {1,2},{1,4},{2,4},{2,4},{4,1},{4,11},{5,3},{5,6},{5,7},{5,8},{5,11},{6,3},{7,2},{7,7},{7,10},
            {9,9},{10,4},{10,6},{11,6},{12,2},{12,7},{13,7},{16,9},{18,6},{19,10},{21,5},{23,4},{24,6},{25,7}};
        Problem p;p.Stocks={{"stock",60,30,1,1,"m"}};
        for(auto [w,h]:dimensions)p.Parts.push_back(Box("piece-"+std::to_string(p.Parts.size()),w,h));
        std::rotate(p.Parts.begin(),p.Parts.begin()+9,p.Parts.end());return p;
    }
    Problem C4P2()
    {
        // Only the 49 input dimensions are frozen. The test supplies no layout,
        // decomposition tree, source row order or reference-solver hint.
        const std::pair<int,int> dimensions[]={
            {3,4},{28,5},{14,9},{1,1},{2,4},{25,8},{4,14},{5,2},{3,9},{5,8},{4,4},{4,9},{5,30},{13,10},
            {18,7},{12,19},{9,2},{8,17},{9,14},{7,18},{7,5},{3,12},{1,5},{2,7},{2,4},{10,14},{13,9},
            {10,26},{1,4},{18,4},{24,9},{12,14},{6,5},{3,17},{4,17},{6,6},{11,5},{6,2},{3,13},{25,7},
            {4,1},{3,18},{2,6},{5,3},{4,7},{9,12},{6,5},{4,10},{4,14}};
        static_assert(std::size(dimensions)==49);
        Problem p;p.Stocks={{"stock",60,60,1,1,"m"}};
        for(auto [w,h]:dimensions)p.Parts.push_back(Box("unrelated-"+std::to_string(3571*(p.Parts.size()+1)),w,h));
        std::reverse(p.Parts.begin(),p.Parts.end());return p;
    }
}

int main()
{
    try
    {
        static_assert(sizeof(std::size_t)>=8);
        const auto strategies=DefaultStrategies().IDs();
        Require(std::find(strategies.begin(),strategies.end(),"rect.block-library")!=strategies.end(),"original root strategy must remain registered");
        Require(std::find(strategies.begin(),strategies.end(),"rect.block-library-mrv")!=strategies.end(),"MRV root must be a separate strategy");
        const auto small=C3P2();const auto before=Snapshot(small);std::size_t callbacks=0;
        auto smallBudget=Budget(200000000);smallBudget.Cancel=[&]{++callbacks;return false;};
        const auto solved=Solve(small,smallBudget);Complete(small,solved,smallBudget.MaxPlacementAttempts);
        Require(Snapshot(small)==before,"MRV cannot alter original geometry or direction rules");
        auto anonymous=small;std::reverse(anonymous.Parts.begin(),anonymous.Parts.end());
        // Merge two equal specifications into two copies of one source. This
        // exercises a needed type with quantity >1 without giving search hints.
        std::vector<std::size_t> equal;
        for(std::size_t i=0;i<anonymous.Parts.size();++i)
            if(anonymous.Parts[i].Geometry.Outer[1].X==2&&anonymous.Parts[i].Geometry.Outer[2].Y==4)equal.push_back(i);
        Require(equal.size()==2,"two equal inventory members expected");
        anonymous.Parts[equal[0]].Quantity=2;anonymous.Parts.erase(anonymous.Parts.begin()+static_cast<std::ptrdiff_t>(equal[1]));
        for(std::size_t i=0;i<anonymous.Parts.size();++i)anonymous.Parts[i].ID="anon-"+std::to_string(104729*(i+1));
        const auto changed=Solve(anonymous,Budget(200000000));Complete(anonymous,changed,200000000);
        Require(Signature(small,solved)==Signature(anonymous,changed),"source IDs, row order and equal-specification splitting cannot guide MRV geometry");
        Require(solved.Output.Stats.PlacementAttempts==changed.Output.Stats.PlacementAttempts,"identical dimensions and counts must perform identical compatibility checks");

        for(std::size_t cap:{1u,32u,4096u,200000u})Untouched(small,Solve(small,Budget(cap,1)),cap);
        Require(callbacks>200,"cancellation probes must cover the active search");
        for(std::size_t pulse:{std::size_t(1),std::size_t(512),callbacks-100})
        {
            std::size_t calls=0;bool seen=false;auto cancellation=Budget(200000000);
            cancellation.Cancel=[&]{if(++calls==pulse){seen=true;return true;}return false;};
            const auto result=Solve(small,cancellation);Untouched(small,result,cancellation.MaxPlacementAttempts);
            Require(seen&&calls==pulse&&result.Output.State==Status::Cancelled,"MRV cancellation must latch, including late search/reconstruction cancellation");
        }
        Untouched(small,Solve(small,Budget(200000,1e-9)),200000);
        auto gap=small;gap.PartGap=1;Untouched(gap,Solve(gap,Budget(200000)),200000);
        auto directions=small;directions.Parts[0].AllowedAnglesDegrees={0,90};Untouched(directions,Solve(directions,Budget(200000)),200000);
        auto free=small;free.Parts[0].FreeRotation=true;Untouched(free,Solve(free,Budget(200000)),200000);
        auto wrong=small;wrong.Stocks[0].Material="wrong";Untouched(wrong,Solve(wrong,Budget(200000)),200000);
        auto unavailable=small;unavailable.Stocks[0].Quantity=0;Untouched(unavailable,Solve(unavailable,Budget(200000)),200000);
        Problem turned;turned.Parts={Box("turned",3,2)};turned.Parts[0].AllowedAnglesDegrees={90};turned.Stocks={{"stock",2,3,1,1,"m"}};
        Complete(turned,Solve(turned,Budget(200000)),200000);
        turned.Parts[0].AllowedAnglesDegrees={0};Untouched(turned,Solve(turned,Budget(200000)),200000);

        const auto large=C4P2();const auto largeBefore=Snapshot(large);
        Untouched(large,Solve(large,Budget(200000,.5)),200000);
        constexpr std::size_t highAttempts=15000000000ull;
        std::cout<<"C4-p2 MRV explicit budget="<<highAttempts<<" time=120 seconds; all 49 originals required\n"<<std::flush;
        const auto result=Solve(large,Budget(highAttempts,120));
        std::cout<<"C4-p2 MRV actual checks="<<result.Output.Stats.PlacementAttempts
                 <<" seconds="<<result.Output.Stats.ElapsedSeconds<<" candidates="<<result.Output.Stats.CandidatePatterns<<'\n';
        Complete(large,result,highAttempts);
        Require(result.Checked.PlacedCount==49&&result.Checked.MaterialArea==3600&&result.Checked.SheetArea==3600,"full original 49-piece demand must fill the actual 60x60 sheet");
        Require(result.Output.Stats.PlacementAttempts>200000,"the large-budget regression must report its actual compatibility workload");
        Require(Snapshot(large)==largeBefore,"large-budget MRV must retain exact source geometry");
        for(const auto& diagnostic:result.Output.Diagnostics)std::cout<<diagnostic<<'\n';
        std::cout<<"RectangleBlockLibraryMrvTests passed\n";return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
