#include "SheetNesting.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
    Part Box(const char* id,double w,double h,std::size_t quantity=1)
    {
        Part p;p.ID=id;p.FreeRotation=false;p.AllowedAnglesDegrees={0};p.Quantity=quantity;
        p.Geometry.Outer={{0,0},{w,0},{w,h},{0,h}};return p;
    }
    void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
    JobResult Run(const Problem& p,Options options={})
    {
        options.TimeLimitSeconds=5;
        return RunJob({"cavity","rect.cavity-search",p,options},DefaultStrategies());
    }
    void Complete(const Problem& p)
    {
        const auto r=Run(p);
        Require(r.Checked.Valid&&r.Checked.Complete&&r.Output.State==Status::Complete,"expected valid full one-sheet layout");
        Require(r.Output.Layouts.size()==1,"one finite sheet");
    }
}
int main()
{
    try
    {
        Problem pin;pin.Parts={Box("wide",4,2,2),Box("tall",2,4,2),Box("middle",2,2)};
        pin.Stocks={{"sheet",6,6,1}};Complete(pin);
        std::reverse(pin.Parts.begin(),pin.Parts.end());Complete(pin);
        // Equal current dimensions do not make different legal orientation sets interchangeable.
        Problem directions;directions.Parts={Box("A",2,3),Box("B",2,3),Box("C",1,1),Box("D",5,1)};
        directions.Parts[0].AllowedAnglesDegrees={0,90};directions.Stocks={{"sheet",7,3,1}};Complete(directions);
        // Infer a dense subrectangle with gap expansion; original positions/IDs are never replaced.
        pin.PartGap=1;pin.Stocks={{"sheet",60,60,1}};
        for(auto& part:pin.Parts)for(auto& point:part.Geometry.Outer)
        { point.X=point.X?point.X*10-1:0;point.Y=point.Y?point.Y*10-1:0; }
        Complete(pin);
        Problem quantity;quantity.Parts={Box("wide",4,2,70),Box("tall",2,4,70),Box("middle",2,2,35)};
        quantity.Stocks={{"sheet",30,42,1}};Complete(quantity);
        // Hundreds of placements previously exhausted the Windows call stack.
        // This diagnostic has a complete nonslicing tiling; only dimensions go in.
        Problem large;large.Parts={Box("a",208,179,70),Box("b",112,373,70),Box("c",221,227,70),Box("d",99,421,70),Box("e",109,194,70)};
        large.Stocks={{"sheet",2240,6000,1}};Complete(large);
        large.PartGap=3;
        for(auto& part:large.Parts)for(auto& point:part.Geometry.Outer)
        {if(point.X)point.X-=3;if(point.Y)point.Y-=3;}
        Complete(large);
        // Public 2DPackLib C1-p2/C2-p1, oriented variant. Reverse input order
        // deliberately; transposing planning axes must preserve physical 0 deg.
        const std::vector<std::pair<int,int>> c1={{4,1},{4,5},{9,4},{3,5},{3,9},{1,4},{5,3},{4,1},{5,5},{7,2},{9,3},{3,13},{2,8},{15,4},{5,4},{10,6},{7,2}};
        const std::vector<std::pair<int,int>> c2={{11,3},{13,3},{9,2},{7,2},{9,3},{7,3},{11,2},{13,2},{11,4},{13,4},{3,5},{11,2},{2,2},{11,3},{2,3},{5,4},{6,4},{12,2},{1,2},{3,5},{13,5},{12,4},{1,4},{5,2},{6,2}};
        for(bool second:{false,true})
        {
            Problem q;const auto& dims=second?c2:c1;
            for(std::size_t i=0;i<dims.size();++i)q.Parts.push_back(Box(std::to_string(i).c_str(),dims[i].first,dims[i].second));
            std::reverse(q.Parts.begin(),q.Parts.end());q.Stocks={{"sheet",second?40.:20.,second?15.:20.,1}};
            Options o;o.TimeLimitSeconds=5;o.MaxPlacementAttempts=20000;
            const auto found=RunJob({"axis",second?"rect.cavity-column-height":"rect.cavity-column",q,o},DefaultStrategies());
            Require(found.Checked.Valid&&found.Checked.Complete,"virtual axis exchange solves public oriented order");
            for(const auto& l:found.Output.Layouts)for(const auto& placement:l.Placements)
                Require(placement.Transform.AngleDegrees==0,"planning axes cannot grant physical rotation");
        }
        auto impossible=directions;impossible.Stocks[0].Width=1;
        auto r=Run(impossible);Require(r.Checked.Valid&&!r.Checked.Complete,"retain infeasible order quantities");
        Problem tiny; tiny.Parts={Box("huge",1e7,1e7,2)};tiny.Stocks={{"thin",1,1e-5,1}};
        r=Run(tiny);Require(r.Checked.Valid&&!r.Checked.Complete&&r.Output.Layouts.empty(),"no out-of-range area-factor integer conversion");
        Options budget;budget.MaxPlacementAttempts=1;r=Run(directions,budget);
        Require(r.Checked.Valid&&!r.Checked.Complete&&r.Output.Stats.PlacementAttempts<=1,"strict probe cap and accounting");
        int calls=0;budget.MaxPlacementAttempts=200000;budget.Cancel=[&]{return ++calls==7;};r=Run(pin,budget);
        Require(r.Output.State==Status::Cancelled&&r.Checked.Valid,"latched cancellation and valid partial");
        std::cout<<"Rectangle cavity tests passed\n";return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
