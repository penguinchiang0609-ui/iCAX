#include "NestingInternal.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <tuple>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Part Box(std::string id,double width,double height,std::size_t quantity=1)
{
    Part part;part.ID=std::move(id);part.Material="steel";part.Quantity=quantity;
    part.FreeRotation=false;part.AllowedAnglesDegrees={0};
    part.Geometry.Outer={{0,0},{width,0},{width,height},{0,height}};return part;
}
JobResult Solve(const Problem& problem,const std::string& strategy,std::size_t attempts=200000,double seconds=10)
{
    Options options;options.MaxPlacementAttempts=attempts;options.TimeLimitSeconds=seconds;
    auto result=RunJob({"exact-axis-grid",strategy,problem,options},DefaultStrategies());
    if(!result.Checked.Valid)
    {
        std::cerr<<"invalid job "<<strategy<<", rows="<<problem.Parts.size()<<", cap="<<attempts<<'\n';
        for(const auto& message:result.Output.Diagnostics)std::cerr<<message<<'\n';
    }
    return result;
}
void Valid(const Problem& problem,const JobResult& result,std::size_t cap,bool complete)
{
    for(const auto& error:result.Checked.Errors)std::cerr<<error<<'\n';
    Require(result.Checked.Valid,"true original geometry and original inventory must validate");
    Require(result.Checked.Complete==complete,"unexpected complete/incomplete result");
    Require(result.Output.Stats.PlacementAttempts<=cap,"normalization must share the caller budget");
    std::size_t required=0,remaining=0,placed=0;
    for(const auto& part:problem.Parts)required+=part.Quantity;
    for(const auto& part:result.Output.Remaining)remaining+=part.Quantity;
    std::set<std::pair<std::string,std::size_t>> identities;
    for(const auto& layout:result.Output.Layouts)for(const auto& pose:layout.Placements)
    {++placed;Require(identities.emplace(pose.PartID,pose.Instance).second,"source instance repeated");}
    Require(required==placed+remaining,"all original instances must be accounted for");
    if(complete)Require(result.Output.Layouts.size()==1&&placed==required,"all parts must fill one true-size sheet");
    else Require(result.Output.Layouts.empty()&&remaining==required,"unsupported/bounded failure retains all demand");
}
Problem Wheel()
{
    Problem problem;problem.Stocks={{"2240x6000",2240,6000,1,1,"steel"}};
    problem.Parts={Box("A",1344,1500),Box("B",896,3600),Box("C",1680,2400),Box("D",560,4500),Box("E",784,2100)};
    return problem;
}
auto Signature(const Problem& p,const JobResult& result)
{
    std::vector<std::tuple<double,double,double,double>> signature;
    for(const auto& layout:result.Output.Layouts)for(const auto& pose:layout.Placements)
    {
        const auto source=std::find_if(p.Parts.begin(),p.Parts.end(),[&](const auto& part){return part.ID==pose.PartID;});
        const auto bounds=Detail::Bounds(source->Geometry,pose.Transform);
        signature.emplace_back(bounds.X,bounds.Y,bounds.Width,bounds.Height);
    }
    std::sort(signature.begin(),signature.end());return signature;
}
}
int main()
{
    try
    {
        for(const auto* id:{"rect.block-rollout","rect.block-rollout-diverse","rect.block-residual","rect.block-repair"})
        {
            const auto p=Wheel();const auto result=Solve(p,id);Valid(p,result,200000,true);
            Require(result.Checked.MaterialArea==13440000&&result.Checked.SheetArea==13440000,"true 2240x6000 material area, not lattice area");
            auto shifted=p;
            for(std::size_t i=0;i<shifted.Parts.size();++i)for(auto& point:shifted.Parts[i].Geometry.Outer)
            {point.X+=17+31*i;point.Y-=29+13*i;}
            std::reverse(shifted.Parts.begin(),shifted.Parts.end());
            const auto translated=Solve(shifted,id);Valid(shifted,translated,200000,true);
            Require(Signature(p,result)==Signature(shifted,translated),"translation and input order must not affect normalized geometry");
            auto turned=p;
            for(auto& part:turned.Parts)
            {
                for(auto& point:part.Geometry.Outer)std::swap(point.X,point.Y);
                part.AllowedAnglesDegrees={90};
            }
            const auto rotated=Solve(turned,id);Valid(turned,rotated,200000,true);
            Require(Signature(p,result)==Signature(turned,rotated),"restoration must preserve permitted direction and exact bounding box");
            for(std::size_t i=0;i<turned.Parts.size();++i)for(auto& point:turned.Parts[i].Geometry.Outer)
            {point.X-=47+23*i;point.Y+=61+17*i;}
            const auto offsetTurn=Solve(turned,id);Valid(turned,offsetTurn,200000,true);
            Require(Signature(p,result)==Signature(turned,offsetTurn),"rotated, shifted originals must restore in physical coordinates");
            auto margin=p;margin.EdgeMargin=7.5;margin.Stocks[0].Width+=15;margin.Stocks[0].Height+=15;
            Valid(margin,Solve(margin,id),200000,true);
            auto gap=p;gap.PartGap=1;Valid(gap,Solve(gap,id),200000,false);
            auto notch=p;notch.Parts[0].Geometry.Outer[2].X-=1e-7;
            Valid(notch,Solve(notch,id),200000,false);
            auto fractional=p;fractional.Parts[0].Geometry.Outer[1].X+=.1;
            Valid(fractional,Solve(fractional,id),200000,false);
            auto noStock=p;noStock.Stocks[0].Quantity=0;Valid(noStock,Solve(noStock,id),200000,false);
            Problem exact;exact.Parts={Box("single",2240,6000)};exact.Stocks=p.Stocks;
            Valid(exact,Solve(exact,id),200000,true);
            Problem copies;copies.Parts={Box("copy",320,120,350)};copies.Stocks=p.Stocks;
            const auto many=Solve(copies,id,200000000,15);Valid(copies,many,200000000,true);
            Require(many.Checked.PlacedCount==350,"all 350 repeated originals required");
            Problem tooMany;tooMany.Parts={Box("copy",2240,10,600)};tooMany.Stocks=p.Stocks;
            Valid(tooMany,Solve(tooMany,id),200000,false);
            Problem irreducible;irreducible.Parts={Box("wide",2239,6000),Box("strip",1,5999),Box("unit",1,1)};irreducible.Stocks=p.Stocks;
            Valid(irreducible,Solve(irreducible,id),200000,false);
            for(std::size_t cap:{1u,3u,8u})Valid(p,Solve(p,id,cap),cap,false);
            Options cancel;cancel.MaxPlacementAttempts=200000;cancel.TimeLimitSeconds=30;
            std::size_t polls=0;cancel.Cancel=[&]{return ++polls==2;};
            const auto cancelled=RunJob({"cancel",id,p,cancel},DefaultStrategies());Valid(p,cancelled,200000,false);
            Require(cancelled.Output.State==Status::Cancelled&&polls==2,"cancellation must stay latched");
            std::cout<<id<<" real-sheet and inventory controls passed\n";
        }
        std::cout<<"RectangleExactGridTests passed\n";return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
