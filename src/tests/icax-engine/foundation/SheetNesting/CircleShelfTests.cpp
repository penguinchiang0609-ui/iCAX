#include "NestingInternal.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace iCAX::SheetNesting::Detail {Result RunCircleShelves(const Problem&,const Options&);}
namespace
{
    void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
    Problem Circles(std::size_t copies)
    {
        Problem p;p.EdgeMargin=2;p.PartGap=1;
        for(std::size_t i=0;i<3;++i)
        {
            Part circle;circle.ID="circle-"+std::to_string(i);circle.Quantity=(i+1)*copies;
            circle.Geometry.Radius=10-3*static_cast<double>(i);circle.Geometry.Center={13-7*static_cast<double>(i),-11+3*static_cast<double>(i)};
            circle.FreeRotation=false;circle.AllowedAnglesDegrees={i==0?30.0:i==1?90.0:270.0};
            circle.Material="steel";p.Parts.push_back(circle);
        }
        p.Stocks={{"stock",250,250,20,1,"steel"}};return p;
    }
    Options Budget(){Options o;o.TimeLimitSeconds=5;o.MaxPlacementAttempts=100000;return o;}
    Result Solve(const Problem& p,const Options& o){return Detail::RunCircleShelves(p,o);}
    void Check(const Problem& p,const Result& r)
    {
        const auto checked=Validate(p,r);Require(checked.Valid,"mapped circles must pass original geometry, material, angle, inventory and demand validation");
        for(const auto& layout:r.Layouts)
        {
            double area=0;
            for(const auto& placed:layout.Placements)
                for(const auto& part:p.Parts)if(part.ID==placed.PartID)area+=Detail::Area(part.Geometry);
            Require(std::abs(area-layout.MaterialArea)<=std::max(1e-7,area*1e-12),"cell areas must be replaced with actual analytic circle areas");
        }
    }
}
int main()
{
    try
    {
        auto p=Circles(16);const auto r=Solve(p,Budget());Check(p,r);
        Require(Validate(p,r).Complete&&r.Layouts.size()==1,"mixed-radius row cells must complete a feasible single-sheet job");
        auto big=Circles(256);big.Stocks[0].Width=big.Stocks[0].Height=1000;
        const auto many=Solve(big,Budget());Check(big,many);Require(Validate(big,many).Complete,"large repeated quantities must retain every original instance");
        for(double scale:{1.0,.9,1.5,.8})
        {
            Problem three;const double radii[]={12,20,5};const std::size_t quantities[]={768,512,1536};
            for(std::size_t i=0;i<3;++i)
            {
                Part circle;circle.ID="three-"+std::to_string(i);circle.Quantity=quantities[i];circle.Geometry.Radius=radii[i]*scale;
                circle.Geometry.Center={7-3*static_cast<double>(i),-13+7*static_cast<double>(i)};circle.FreeRotation=false;circle.AllowedAnglesDegrees={90};three.Parts.push_back(circle);
            }
            three.Stocks={{"stock",1600*scale,1312*scale,1,1,""}};
            const auto packed=Solve(three,Budget());Check(three,packed);
            Require(Validate(three,packed).Complete&&packed.Layouts.size()==1,"2816 mixed-radius circles must reuse exact bounded row plans at multiple decimal scales");
        }
        std::cout<<"PASS shifted local centres, 30/90/270-degree permissions, positive clearance and large mixed radii\n";
        auto mixed=p;Part rectangle;rectangle.ID="unsupported";rectangle.Quantity=2;rectangle.Material="steel";
        rectangle.Geometry.Outer={{0,0},{4,0},{4,5},{0,5}};mixed.Parts.push_back(rectangle);
        const auto partial=Solve(mixed,Budget());Check(mixed,partial);Require(partial.State==Status::Partial,"non-circles must remain explicit demand");
        auto unsupported=mixed;unsupported.Parts={rectangle};const auto none=Solve(unsupported,Budget());Check(unsupported,none);
        Require(none.Layouts.empty()&&none.State==Status::Partial,"an all-non-circle problem must retain all original demand without a fabricated subproblem");
        auto mismatch=p;mismatch.Parts[0].Material="aluminium";
        const auto material=Solve(mismatch,Budget());Check(mismatch,material);Require(!Validate(mismatch,material).Complete,"a material mismatch must not consume foreign stock");
        auto finite=p;finite.Stocks[0].Width=finite.Stocks[0].Height=25;finite.Stocks[0].Quantity=2;
        const auto limited=Solve(finite,Budget());Check(finite,limited);Require(limited.Layouts.size()<=2&&!Validate(finite,limited).Complete,"finite stock instances must not be fabricated");
        std::cout<<"PASS unsupported parts, material compatibility and finite stock\n";
        std::size_t cuts=0,retained=0;
        for(std::size_t attempts:{0,1,2,5,20,100})
        {
            auto o=Budget();o.MaxPlacementAttempts=attempts;const auto stopped=Solve(p,o);Check(p,stopped);
            Require(stopped.Stats.PlacementAttempts<=attempts,"wrapper must respect the shared attempt limit");++cuts;
        }
        for(std::size_t cut=1;cut<=800;cut+=37)
        {
            std::size_t calls=0;auto o=Budget();o.Cancel=[&]{return ++calls==cut;};const auto stopped=Solve(p,o);Check(p,stopped);
            if(calls>=cut)Require(stopped.State==Status::Cancelled&&calls==cut,"one-shot cancellation must remain latched across the row subsolver");
            if(!stopped.Layouts.empty())++retained;++cuts;
        }
        auto zero=Budget();zero.TimeLimitSeconds=0;const auto timed=Solve(p,zero);Check(p,timed);
        Require(timed.Layouts.empty()&&timed.State==Status::Partial,"expired preparation must preserve all original demand");
        std::cout<<"PASS "<<cuts<<" budget/cancel cuts; "<<retained<<" retained completed placements\n";return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
