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
    {return RunJob({"block-test","rect.block-library",p,std::move(o)},DefaultStrategies());}
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
    Problem C3(bool second)
    {
        // Public benchmark dimensions only, with no coordinates, source order,
        // grouping hints, reference layout or reference solver output.
        const std::vector<std::pair<int,int>> first={
            {3,4},{3,5},{4,3},{4,5},{4,8},{4,11},{5,3},{5,4},{6,2},{6,7},{7,3},{7,4},{7,5},{7,11},
            {12,2},{14,5},{14,5},{14,8},{14,11},{17,3},{17,11},{18,3},{18,4},{18,5},{18,5},{21,3},{21,5},{21,13}};
        const std::vector<std::pair<int,int>> other={
            {1,2},{1,4},{2,4},{2,4},{4,1},{4,11},{5,3},{5,6},{5,7},{5,8},{5,11},{6,3},{7,2},{7,7},{7,10},
            {9,9},{10,4},{10,6},{11,6},{12,2},{12,7},{13,7},{16,9},{18,6},{19,10},{21,5},{23,4},{24,6},{25,7}};
        Problem p;p.Stocks={{"public",60,30,1,1,"m"}};
        for(auto [w,h]:second?other:first)p.Parts.push_back(Box("part-"+std::to_string(p.Parts.size()),w,h));
        std::rotate(p.Parts.begin(),p.Parts.begin()+7,p.Parts.end());return p;
    }
}

int main()
{
    try
    {
        Require(sizeof(std::size_t)>=8,"block-library evidence requires 64-bit join statistics");
        const auto ids=DefaultStrategies().IDs();
        Require(std::find(ids.begin(),ids.end(),"rect.block-library")!=ids.end(),"strategy must have an independent registry entry");

        auto wheel=Wheel();const auto original=Snapshot(wheel);const auto packed=Solve(wheel);
        Complete(wheel,packed,200000);Require(Snapshot(wheel)==original,"search and publication must not mutate original geometry or rules");
        Require(packed.Output.Stats.CombinationGroups==1,"complete compound retains construction provenance");
        for(std::size_t i=0;i<wheel.Parts.size();++i)
            for(auto& point:wheel.Parts[i].Geometry.Outer){point.X-=17+double(i);point.Y+=29-double(i);}
        Complete(wheel,Solve(wheel),200000);

        Problem copies;copies.Parts={Box("same-source",3,2,6)};copies.Stocks={{"finite",9,4,1,1,"m"}};
        Complete(copies,Solve(copies),200000);

        const auto c1=C3(false);const auto c1Budget=Budget(2000000,20);const auto first=Solve(c1,c1Budget);
        Complete(c1,first,c1Budget.MaxPlacementAttempts);const auto signature=Signature(c1,first);
        auto reordered=c1;std::reverse(reordered.Parts.begin(),reordered.Parts.end());
        const auto reverse=Solve(reordered,c1Budget);Complete(reordered,reverse,c1Budget.MaxPlacementAttempts);
        Require(Signature(reordered,reverse)==signature,"row permutation cannot guide the dimension search");
        std::rotate(reordered.Parts.begin(),reordered.Parts.begin()+3,reordered.Parts.end());
        for(std::size_t i=0;i<reordered.Parts.size();++i)reordered.Parts[i].ID="opaque-"+std::to_string(7919*(i+3));
        const auto anonymous=Solve(reordered,c1Budget);Complete(reordered,anonymous,c1Budget.MaxPlacementAttempts);
        Require(Signature(reordered,anonymous)==signature,"anonymous IDs cannot change geometric layout");
        Require(first.Output.Stats.PlacementAttempts==reverse.Output.Stats.PlacementAttempts&&
                first.Output.Stats.PlacementAttempts==anonymous.Output.Stats.PlacementAttempts,"dimension-equivalent inputs must use identical inventory joins");

        const auto c2=C3(true);const auto snapshot=Snapshot(c2);
        const auto ordinary=Solve(c2,Budget(200000,.5));Untouched(c2,ordinary,200000);
        Require(ordinary.Output.State==Status::Partial,"default budget failure is retained, not reported as solved");
        const auto researchBudget=Budget(100000000,30);const auto solved=Solve(c2,researchBudget);
        Complete(c2,solved,researchBudget.MaxPlacementAttempts);
        Require(solved.Output.Stats.PlacementAttempts>200000,"millions of rejected inventory joins cannot be relabelled as 200k work");
        Require(Snapshot(c2)==snapshot,"large-budget search must preserve exact original geometry");
        std::cout<<"C3-p2 explicit budget="<<researchBudget.MaxPlacementAttempts
                 <<" joins="<<solved.Output.Stats.PlacementAttempts<<" seconds="<<solved.Output.Stats.ElapsedSeconds<<'\n';

        for(std::size_t cap:{1u,8u,32u,256u,4096u})Untouched(c2,Solve(c2,Budget(cap,10)),cap);
        for(std::size_t pulse:{1u,32u,256u,4096u})
        {
            auto settings=Budget(2000000,10);std::size_t calls=0;bool seen=false;
            settings.Cancel=[&]{if(++calls==pulse){seen=true;return true;}return false;};
            const auto r=Solve(c2,settings);Untouched(c2,r,settings.MaxPlacementAttempts);
            Require(seen&&calls==pulse&&r.Output.State==Status::Cancelled,"one-shot cancellation must latch without another callback");
        }
        auto expired=Budget(200000,1e-9);Untouched(c2,Solve(c2,expired),expired.MaxPlacementAttempts);

        auto gap=Wheel();gap.PartGap=1;Untouched(gap,Solve(gap),200000);
        auto disabled=Budget();disabled.EnableCombinations=false;Untouched(Wheel(),Solve(Wheel(),disabled),disabled.MaxPlacementAttempts);
        auto holes=Wheel();holes.Parts[0].Geometry.Holes={{{1,1},{2,1},{2,2},{1,2}}};Untouched(holes,Solve(holes),200000);
        auto near=Wheel();near.Parts[0].Geometry.Outer[2].X-=near.Tolerance*.1;Untouched(near,Solve(near),200000);
        auto mixed=Wheel();mixed.Parts[0].Material="other";Untouched(mixed,Solve(mixed),200000);
        auto wrong=Wheel();wrong.Stocks[0].Material="other";Untouched(wrong,Solve(wrong),200000);
        auto noStock=Wheel();noStock.Stocks[0].Quantity=0;Untouched(noStock,Solve(noStock),200000);
        auto finite=Wheel();finite.Stocks.insert(finite.Stocks.begin(),{"unavailable",223,197,0,1,"m"});
        const auto finiteResult=Solve(finite);Complete(finite,finiteResult,200000);
        Require(finiteResult.Output.Layouts[0].StockID=="stock"&&finiteResult.Output.Layouts[0].StockInstance==0,"cannot invent a stock instance");
        auto twoSheets=Wheel();for(auto& part:twoSheets.Parts)part.Quantity=2;twoSheets.Stocks[0].Quantity=2;
        Untouched(twoSheets,Solve(twoSheets),200000);
        auto underfilled=Wheel();underfilled.Stocks[0].Width+=1;Untouched(underfilled,Solve(underfilled),200000);

        Problem angle;angle.Parts={Box("turned",3,2)};angle.Parts[0].AllowedAnglesDegrees={90};
        for(auto& point:angle.Parts[0].Geometry.Outer){point.X-=11;point.Y+=4;}
        angle.Stocks={{"stock",2,3,1,1,"m"}};Complete(angle,Solve(angle),200000);
        angle.Parts[0].AllowedAnglesDegrees={0};Untouched(angle,Solve(angle),200000);
        angle.Parts[0].AllowedAnglesDegrees={0,90};Untouched(angle,Solve(angle),200000);
        angle.Parts[0].AllowedAnglesDegrees={180,0};angle.Stocks[0].Width=3;angle.Stocks[0].Height=2;
        Complete(angle,Solve(angle),200000);
        angle.Parts[0].FreeRotation=true;Untouched(angle,Solve(angle),200000);
        Problem fractional;fractional.Parts={Box("fraction",2.5,2,2)};fractional.Stocks={{"stock",5,2,1,1,"m"}};
        Untouched(fractional,Solve(fractional),200000);
        Problem margin;margin.EdgeMargin=1.5;margin.Parts={Box("inside",3,2)};margin.Stocks={{"stock",6,5,1,1,"m"}};
        Complete(margin,Solve(margin),200000);
        Problem tooMany;tooMany.Parts={Box("many",1,1,513)};tooMany.Stocks={{"stock",513,1,1,1,"m"}};
        Untouched(tooMany,Solve(tooMany),200000);
        std::cout<<"RectangleBlockLibraryTests passed\n";return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
