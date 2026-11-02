#include "NestingInternal.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace iCAX::SheetNesting;
namespace iCAX::SheetNesting::Detail {Result RunRectangleLookahead(const Problem&,const Options&);}
namespace
{
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Part Rectangle(std::string id,double w,double h,std::size_t quantity=1)
{Part p;p.ID=std::move(id);p.Quantity=quantity;p.Material="m";p.FreeRotation=false;p.Geometry.Outer={{0,0},{w,0},{w,h},{0,h}};return p;}
Options Budget(){Options o;o.TimeLimitSeconds=.15;o.MaxPlacementAttempts=40000;return o;}
Problem Pinwheel(double gap=0)
{Problem p;p.PartGap=gap;p.Parts={Rectangle("horizontal",4,2,2),Rectangle("vertical",2,4,2),Rectangle("center",2,2)};p.Stocks={{"stock",gap?6.3:6,gap?6.3:6,1,1,"m"}};return p;}
void Complete(const Problem& p,const Result& r){const auto v=Validate(p,r);Require(v.Valid&&v.Complete,"complete original rectangle demand required");Require(r.Layouts.size()==1,"single-stock candidate required");}
}
int main()
{
 try
 {
    {const auto p=Pinwheel();const auto registered=RunJob({"lookahead-entry","rect.lookahead",p,Budget()},DefaultStrategies());Require(registered.Checked.Valid&&registered.Checked.Complete,"registered lookahead entry must validate complete original demand");}
    for(double gap:{0.,.1}){const auto p=Pinwheel(gap);Complete(p,Detail::RunRectangleLookahead(p,Budget()));}
    Problem mixed;mixed.Parts={Rectangle("wide",60,40,2),Rectangle("narrow",40,40,2)};mixed.Stocks={{"stock",100,80,1,1,"m"}};
    Complete(mixed,Detail::RunRectangleLookahead(mixed,Budget()));
    for(std::size_t i=0;i<mixed.Parts.size();++i)for(auto& point:mixed.Parts[i].Geometry.Outer){point.X+=5000000+i*1000;point.Y-=6000000+i*1000;}
    Complete(mixed,Detail::RunRectangleLookahead(mixed,Budget()));
    auto p=Pinwheel();for(auto limit:{1u,2u,3u,10u,64u}){auto o=Budget();o.MaxPlacementAttempts=limit;auto r=Detail::RunRectangleLookahead(p,o);Require(Validate(p,r).Valid,"bounded partial inventory");Require(r.Stats.PlacementAttempts<=limit,"attempt cap");}
    for(int pulse=1;pulse<=64;++pulse){int calls=0;bool seen=false;auto o=Budget();o.Cancel=[&]{if(++calls==pulse){seen=true;return true;}return false;};const auto r=Detail::RunRectangleLookahead(p,o);Require(Validate(p,r).Valid,"cancelled partial inventory");Require(!seen||r.State==Status::Cancelled,"single-pulse cancellation must latch");}
    auto unsupported=Pinwheel();unsupported.Parts[0].Quantity=21;const auto outside=Detail::RunRectangleLookahead(unsupported,Budget());Require(Validate(unsupported,outside).Valid&&!outside.Remaining.empty(),"scope limits retain demand");
    // Exercise cancellation during linear extraction without entering the
    // separate, uncancellable final topology verification of a 35k-edge input.
    Problem large;auto part=Rectangle("subdivided",100,50);part.Geometry.Outer.clear();constexpr int n=8750;
    for(int side=0;side<4;++side)for(int i=0;i<n;++i){const double f=static_cast<double>(i)/n;part.Geometry.Outer.push_back(side==0?Point{100*f,0}:side==1?Point{100,50*f}:side==2?Point{100*(1-f),50}:Point{0,50*(1-f)});}
    large.Parts={part};large.Stocks={{"stock",200,100,1,1,"m"}};
    for(int pulse:{1,7,600,1500,2200})
    {
        int calls=0;auto o=Budget();o.Cancel=[&]{return ++calls==pulse;};
        const auto start=std::chrono::steady_clock::now();const auto r=Detail::RunRectangleLookahead(large,o);
        const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        Require(r.State==Status::Cancelled&&r.Layouts.empty()&&r.Remaining.size()==1&&r.Remaining[0].Quantity==1,"large subdivided cancellation accounting");
        Require(calls==pulse,"large cancellation latch");std::cout<<"35k lookahead pulse="<<pulse<<" milliseconds="<<ms<<'\n';
    }
    std::cout<<"RectangleLookaheadTests passed\n";return 0;
 }
 catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
