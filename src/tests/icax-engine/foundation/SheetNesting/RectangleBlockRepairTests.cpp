#include "NestingInternal.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
    Part Box(std::string id,double w,double h,std::size_t quantity=1)
    {
        Part p;p.ID=std::move(id);p.Material="m";p.Quantity=quantity;
        p.FreeRotation=false;p.AllowedAnglesDegrees={0};p.Geometry.Outer={{0,0},{w,0},{w,h},{0,h}};return p;
    }
    Options Budget(std::size_t attempts=200000,double seconds=5)
    {Options o;o.MaxPlacementAttempts=attempts;o.TimeLimitSeconds=seconds;return o;}
    JobResult Solve(const Problem& p,Options o=Budget())
    {return RunJob({"rollout-test","rect.block-repair",p,std::move(o)},DefaultStrategies());}
    std::string Snapshot(const Problem& p)
    {
        std::ostringstream out;out<<std::setprecision(17)<<p.PartGap<<' '<<p.EdgeMargin<<' '<<p.Tolerance;
        for(const auto& part:p.Parts)
        {
            out<<'|'<<part.ID<<'|'<<part.Material<<'|'<<part.Quantity<<'|'<<part.FreeRotation;
            for(double a:part.AllowedAnglesDegrees)out<<':'<<a;
            out<<'|'<<part.Geometry.Radius<<' '<<part.Geometry.Center.X<<' '<<part.Geometry.Center.Y;
            for(auto point:part.Geometry.Outer)out<<'/'<<point.X<<' '<<point.Y;
            for(const auto& hole:part.Geometry.Holes){out<<'H';for(auto point:hole)out<<'/'<<point.X<<' '<<point.Y;}
        }
        for(const auto& s:p.Stocks)out<<'|'<<s.ID<<'|'<<s.Material<<'|'<<s.Width<<' '<<s.Height<<' '<<s.Quantity<<' '<<s.Cost;
        return out.str();
    }
    void Valid(const Problem& p,const JobResult& r,std::size_t cap)
    {
        for(const auto& error:r.Checked.Errors)std::cerr<<error<<'\n';
        if(r.Output.State==Status::Failed)for(const auto& message:r.Output.Diagnostics)std::cerr<<message<<'\n';
        Require(r.Checked.Valid,"shared validation must verify true geometry, stock, and inventory");
        Require(r.Output.Stats.PlacementAttempts<=cap,"every inventory join must fit the single caller budget");
        std::size_t total=0,remaining=0,placed=0;
        std::set<std::pair<std::string,std::size_t>> identities;
        for(const auto& part:p.Parts)total+=part.Quantity;
        for(const auto& part:r.Output.Remaining)remaining+=part.Quantity;
        for(const auto& layout:r.Output.Layouts)for(const auto& pose:layout.Placements)
        {
            ++placed;Require(identities.emplace(pose.PartID,pose.Instance).second,"source identity cannot be repeated");
            const auto source=std::find_if(p.Parts.begin(),p.Parts.end(),[&](const auto& part){return part.ID==pose.PartID;});
            Require(source!=p.Parts.end()&&pose.Instance<source->Quantity,"only original identities and copy indices may be published");
        }
        Require(placed+remaining==total,"source quantity must be exactly conserved");
    }
    void Complete(const Problem& p,const JobResult& r,std::size_t cap)
    {
        Valid(p,r,cap);
        if(!r.Checked.Complete)for(const auto& d:r.Output.Diagnostics)std::cerr<<d<<'\n';
        Require(r.Output.State==Status::Complete&&r.Checked.Complete&&r.Output.Layouts.size()==1,"expected one complete verified sheet");
    }
    void Untouched(const Problem& p,const JobResult& r,std::size_t cap)
    {Valid(p,r,cap);Require(r.Output.Layouts.empty()&&!r.Checked.Complete,"unsupported or interrupted scope must retain all demand");}
    auto Signature(const Problem& p,const JobResult& r)
    {
        std::vector<std::tuple<double,double,double,double>> signature;
        for(const auto& layout:r.Output.Layouts)for(const auto& placement:layout.Placements)
        {
            const auto source=std::find_if(p.Parts.begin(),p.Parts.end(),[&](const auto& part){return part.ID==placement.PartID;});
            const auto b=Detail::Bounds(source->Geometry,placement.Transform);
            signature.emplace_back(b.X,b.Y,b.Width,b.Height);
        }
        std::sort(signature.begin(),signature.end());return signature;
    }
    Problem Wheel()
    {
        Problem p;p.Stocks={{"stock",223,197,1,1,"m"}};
        p.Parts={Box("A",132,52),Box("B",91,110),Box("C",161,87),Box("D",62,145),Box("E",70,58)};return p;
    }
    Problem C6P3()
    {
        // Original input dimensions only. No coordinates or decomposition hints.
        const std::pair<int,int> dimensions[]={
            {6,35},{1,6},{6,6},{34,13},{10,7},{23,7},{1,7},{6,7},{10,62},{10,33},{13,33},{7,22},
            {4,15},{6,8},{24,8},{6,7},{24,7},{4,7},{30,7},{6,34},{8,17},{10,17},{8,16},{15,16},
            {5,6},{5,6},{9,21},{4,21},{5,23},{5,23},{8,6},{15,6},{8,5},{10,5},{5,4},{7,2},
            {6,2},{6,4},{17,4},{7,2},{6,2},{5,8},{4,4},{9,4},{6,8},{17,8},{4,6},{5,6},
            {4,8},{4,4},{9,4},{4,2},{5,2},{4,25},{6,25},{6,6},{2,6},{18,10},{11,24},{9,4},
            {17,4},{7,9},{9,5},{17,5},{6,4},{2,4},{6,8},{20,8},{7,42},{8,14},{18,14},{6,34},
            {5,9},{8,7},{7,7},{26,20},{5,7},{3,7},{3,15},{8,2},{7,2},{4,19},{6,19},{5,25},
            {8,13},{7,13},{5,8},{3,8},{8,5},{3,5},{6,8},{2,8},{7,12},{10,7},{37,7},{6,4},
            {2,4}};
        static_assert(std::size(dimensions)==97);
        Problem p;p.Stocks={{"sheet",80,120,1,1,"m"}};
        for(auto [w,h]:dimensions)p.Parts.push_back(Box("part-"+std::to_string(3571*(p.Parts.size()+1)),w,h));
        return p;
    }
}

int main()
{
    try
    {
        const auto ids=DefaultStrategies().IDs();
        for(const auto* id:{"rect.block-library","rect.block-library-mrv","rect.block-rollout","rect.block-rollout-diverse","rect.block-residual","rect.block-repair"})
            Require(std::find(ids.begin(),ids.end(),id)!=ids.end(),"all six strategies remain independent");
        const auto wheel=Wheel();Complete(wheel,Solve(wheel),200000);
        auto shifted=wheel;
        for(std::size_t i=0;i<shifted.Parts.size();++i)for(auto& point:shifted.Parts[i].Geometry.Outer)
        {point.X+=17+i;point.Y-=29+i;}
        Complete(shifted,Solve(shifted),200000);
        Problem copies;copies.Parts={Box("copy",3,2,6)};copies.Stocks={{"sheet",9,4,1,1,"m"}};
        Complete(copies,Solve(copies),200000);
        auto margin=copies;margin.EdgeMargin=1.5;margin.Stocks[0].Width+=3;margin.Stocks[0].Height+=3;
        Complete(margin,Solve(margin),200000);
        auto gap=wheel;gap.PartGap=1;Untouched(gap,Solve(gap),200000);
        auto hole=wheel;hole.Parts[0].Geometry.Holes={{{1,1},{2,1},{2,2},{1,2}}};Untouched(hole,Solve(hole),200000);
        auto near=wheel;near.Parts[0].Geometry.Outer[2].X-=near.Tolerance*.1;Untouched(near,Solve(near),200000);
        auto free=wheel;free.Parts[0].FreeRotation=true;Untouched(free,Solve(free),200000);
        auto distinct=wheel;distinct.Parts[0].AllowedAnglesDegrees={0,90};Untouched(distinct,Solve(distinct),200000);
        auto noStock=wheel;noStock.Stocks[0].Quantity=0;Untouched(noStock,Solve(noStock),200000);
        auto wrong=wheel;wrong.Stocks[0].Material="other";Untouched(wrong,Solve(wrong),200000);
        auto material=wheel;material.Parts[0].Material="other";Untouched(material,Solve(material),200000);
        Problem turned;turned.Parts={Box("turn",3,2)};turned.Parts[0].AllowedAnglesDegrees={90};turned.Stocks={{"sheet",2,3,1,1,"m"}};
        Complete(turned,Solve(turned),200000);turned.Parts[0].AllowedAnglesDegrees={0};Untouched(turned,Solve(turned),200000);
        Problem largeExtent;largeExtent.Parts={Box("large",513,1)};largeExtent.Stocks={{"sheet",513,1,1,1,"m"}};
        Complete(largeExtent,Solve(largeExtent),200000);
        Problem largeArea;largeArea.Parts={Box("large",300,300)};largeArea.Stocks={{"sheet",300,300,1,1,"m"}};
        Complete(largeArea,Solve(largeArea),200000);
        Problem irreducible;irreducible.Parts={Box("wide",511,1),Box("tail",2,1)};irreducible.Stocks={{"sheet",513,1,1,1,"m"}};
        Untouched(irreducible,Solve(irreducible),200000);
        Problem denseGrid;denseGrid.Parts={Box("body",299,300),Box("edge",1,299),Box("unit",1,1)};denseGrid.Stocks={{"sheet",300,300,1,1,"m"}};
        Untouched(denseGrid,Solve(denseGrid),200000);
        const auto original=C6P3();const auto before=Snapshot(original);
        for(std::size_t cap:{1u,32u,4096u,200000u})Untouched(original,Solve(original,Budget(cap,.5)),cap);
        Untouched(original,Solve(original,Budget(200000,1e-9)),200000);
        for(std::size_t pulse:{1u,512u,50000u})
        {
            std::size_t calls=0;auto options=Budget(200000000,30);
            options.Cancel=[&]{return ++calls==pulse;};
            const auto cancelled=Solve(original,options);Untouched(original,cancelled,options.MaxPlacementAttempts);
            Require(cancelled.Output.State==Status::Cancelled&&calls==pulse,"early cancellation latches and preserves all originals");
        }
        constexpr std::size_t high=150000000000ull;
        std::size_t callbacks=0;auto options=Budget(high,1800);options.Cancel=[&]{++callbacks;return false;};
        // The late-cancellation pulse below belongs to constructor recovery.
        // Keep automatic refinement callbacks outside that measured sequence.
        options.EnableBasicCompaction=false;
        std::cout<<"C6-p3 repair explicit budget="<<high<<" time=1800 seconds; all 97 originals required\n"<<std::flush;
        const auto solved=Solve(original,options);
        std::cout<<"C6-p3 repair checks="<<solved.Output.Stats.PlacementAttempts<<" seconds="<<solved.Output.Stats.ElapsedSeconds
                 <<" candidates="<<solved.Output.Stats.CandidatePatterns<<" callbacks="<<callbacks<<'\n';
        for(const auto& message:solved.Output.Diagnostics)std::cout<<message<<'\n';
        std::cout<<std::flush;
        Complete(original,solved,high);
        Require(solved.Checked.PlacedCount==97&&solved.Checked.MaterialArea==9600&&solved.Checked.SheetArea==9600,"all 97 true originals must fill 80x120");
        Require(Snapshot(original)==before,"formal pipeline must not mutate original geometry");
        Require(solved.Output.Stats.PlacementAttempts>200000,"full generation and filtering cost must be reported");
        Problem anonymous;anonymous.Stocks=original.Stocks;
        std::map<std::pair<int,int>,std::size_t> counts;
        for(const auto& p:original.Parts)++counts[{int(p.Geometry.Outer[1].X),int(p.Geometry.Outer[2].Y)}];
        for(const auto& [dim,count]:counts)anonymous.Parts.push_back(Box("opaque-"+std::to_string(104729*(anonymous.Parts.size()+1)),dim.first,dim.second,count));
        std::reverse(anonymous.Parts.begin(),anonymous.Parts.end());
        auto anonymousOptions=Budget(high,1800);anonymousOptions.EnableBasicCompaction=false;
        const auto anonymized=Solve(anonymous,anonymousOptions);Complete(anonymous,anonymized,high);
        Require(Signature(original,solved)==Signature(anonymous,anonymized),"opaque IDs, shuffled inputs and merged copies must keep the same geometry");
        Require(solved.Output.Stats.PlacementAttempts==anonymized.Output.Stats.PlacementAttempts,"same dimension inventory must consume the same exact checks");
        std::cout<<"C6-p3 opaque checks="<<anonymized.Output.Stats.PlacementAttempts<<" seconds="<<anonymized.Output.Stats.ElapsedSeconds<<'\n'<<std::flush;
        Require(callbacks>100,"late cancellation target exists");
        std::size_t lateCalls=0;auto late=Budget(high,1800);late.Cancel=[&]{return ++lateCalls==callbacks-100;};
        late.EnableBasicCompaction=false;
        const auto cancelled=Solve(original,late);Untouched(original,cancelled,high);
        Require(cancelled.Output.State==Status::Cancelled&&lateCalls==callbacks-100,"rollout or final reconstruction cancellation remains latched");
        std::cout<<"RectangleBlockRepairTests passed\n";return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
