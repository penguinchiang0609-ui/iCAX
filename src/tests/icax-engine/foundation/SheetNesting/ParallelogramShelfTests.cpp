#include "NestingInternal.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace iCAX::SheetNesting;
using namespace iCAX::SheetNesting::Detail;

namespace
{
    void Require(bool condition, const std::string& message)
    { if (!condition) throw std::runtime_error(message); }

    Part Parallelogram(const std::string& id, double width, double height, double shear,
        double x = 0, double y = 0, const std::string& material = "steel")
    {
        Part p;
        p.ID = id; p.Material = material; p.FreeRotation = false;
        p.Geometry.Outer = {{x,y},{x+width,y},{x+width+shear,y+height},{x+shear,y+height}};
        return p;
    }
    Validation Check(const Problem& input, const Result& result)
    {
        const auto checked = Validate(input,result);
        std::string errors;
        for (const auto& message : checked.Errors) errors += message+"; ";
        Require(checked.Valid,"Physical parallelogram layout invalid: "+errors);
        std::size_t remaining = 0, demanded = 0;
        for (const auto& value : result.Remaining) remaining += value.Quantity;
        for (const auto& p : input.Parts) demanded += p.Quantity;
        Require(remaining+checked.PlacedCount==demanded,"Physical demand was lost or duplicated during virtual row planning.");
        return checked;
    }
    Problem Regression361()
    {
        Problem p;
        p.Parts = {Parallelogram("h35-w43",43,35,19,13,-17),
            Parallelogram("h35-w51",51,35,19,-7,4),Parallelogram("h35-w37",37,35,19),
            Parallelogram("h20-s19-w55",55,20,19),Parallelogram("h20-s19-w36",36,20,19),
            Parallelogram("h20-s19-w42",42,20,19),Parallelogram("h20-s11-w42",42,20,11),
            Parallelogram("h20-s11-w40",40,20,11),Parallelogram("h20-s11-w47",47,20,11)};
        p.Stocks = {{"sheet",152,75,1,1,"steel"}};
        return p;
    }
    void DifferentWidthRows()
    {
        const auto p = Regression361();
        Options options; options.TimeLimitSeconds = 2;
        const auto result = RunParallelogramShelves(p,options);
        const auto checked = Check(p,result);
        Require(checked.Complete && checked.PlacedCount==9 && result.Layouts.size()==1,
            "Three same-height/shear mixed-width rows must complete on the known 152x75 sheet.");
        Require(result.Stats.CombinationGroups>=3,"The three physical row patterns were not retained.");
        auto tiled = p;
        for (auto& part : tiled.Parts) part.Quantity = 16;
        tiled.Stocks.front().Width *= 4;
        tiled.Stocks.front().Height *= 4;
        const auto large = RunParallelogramShelves(tiled,options);
        const auto largeChecked = Check(tiled,large);
        Require(largeChecked.Complete && largeChecked.PlacedCount==144 && large.Layouts.size()==1,
            "Bounded row patterns must conserve and place the 4x4 repeated demand.");
    }
    void SignedShearAndGap()
    {
        for (double shear : {-11.,11.})
        {
            Problem p;
            p.PartGap = 1; p.EdgeMargin = 2;
            p.Parts = {Parallelogram("A",42,20,shear,13,-17),
                Parallelogram("B",40,20,shear,-3,9),Parallelogram("C",47,20,shear,2,7)};
            const double horizontalGap = std::sqrt(1+(shear/20)*(shear/20));
            p.Stocks = {{"sheet",42+40+47+std::abs(shear)+2*horizontalGap+4,24,1,1,"steel"}};
            Options options; options.TimeLimitSeconds = 2;
            const auto result = RunParallelogramShelves(p,options);
            Require(Check(p,result).Complete,"A signed-shear row with one Euclidean gap per neighbour did not fit.");

            // Analytic independent check: parallel side lines are separated by
            // horizontalOffset/sqrt(1+slope^2), not by horizontalOffset itself.
            struct Interval { double X, Width; };
            std::vector<Interval> intervals;
            for (const auto& placed : result.Layouts.front().Placements)
            {
                const auto& part = *std::find_if(p.Parts.begin(),p.Parts.end(),[&](const auto& item){return item.ID==placed.PartID;});
                const auto bottom = Transform(part.Geometry.Outer.front(),placed.Transform);
                intervals.push_back({bottom.X,part.Geometry.Outer[1].X-part.Geometry.Outer[0].X});
            }
            std::sort(intervals.begin(),intervals.end(),[](auto a,auto b){return a.X<b.X;});
            for (std::size_t i=1;i<intervals.size();++i)
            {
                const double normalGap = (intervals[i].X-intervals[i-1].X-intervals[i-1].Width)/horizontalGap;
                Require(normalGap+p.Tolerance>=p.PartGap,"The sheared neighbours violate the actual normal gap.");
            }
        }
    }
    void AllowedDirectionsAndUnsupportedGeometry()
    {
        auto rotated = Regression361();
        for (auto& part : rotated.Parts)
        {
            for (auto& point : part.Geometry.Outer) point = Transform(point,{0,0,-30});
            part.AllowedAnglesDegrees = {30};
        }
        Options options; options.TimeLimitSeconds = 2;
        const auto result = RunParallelogramShelves(rotated,options);
        Require(Check(rotated,result).Complete,"An explicitly allowed angle restoring horizontal bases was ignored.");
        for (const auto& layout : result.Layouts)
            for (const auto& placed : layout.Placements)
                Require(std::abs(placed.Transform.AngleDegrees-30)<rotated.Tolerance,"A forbidden direction was substituted.");
        for (auto& part : rotated.Parts) part.AllowedAnglesDegrees = {0};
        const auto unsupported = RunParallelogramShelves(rotated,options);
        Require(Check(rotated,unsupported).PlacedCount==0,"This horizontal-base rule silently rotated against the allowed domain.");

        Problem mixed;
        mixed.EdgeMargin = 2;
        mixed.Parts = {Parallelogram("too-large",1000,1000,11),Parallelogram("A",42,20,-11,13,-17),
            Parallelogram("B",40,20,-11),Parallelogram("C",47,20,-11)};
        for (const auto& id : {"al-A","al-B","al-C"}) mixed.Parts.push_back(Parallelogram(id,40,20,-11,0,0,"aluminium"));
        Part circle; circle.ID="circle";circle.Geometry.Radius=2;circle.Quantity=2;circle.Material="steel";
        mixed.Parts.push_back(circle);
        Part triangle;triangle.ID="triangle";triangle.Geometry.Outer={{0,0},{4,0},{0,4}};triangle.Material="steel";
        mixed.Parts.push_back(triangle);
        mixed.Stocks={{"steel-only",144,24,1,1,"steel"}};
        const auto partial=RunParallelogramShelves(mixed,options);
        const auto checked=Check(mixed,partial);
        Require(checked.PlacedCount==3 && !checked.Complete,"Oversized, unmatched-material or nonapplicable geometry was consumed or blocked supported demand.");
    }
    void BudgetAndCancellation()
    {
        auto p=Regression361();
        for(auto& part:p.Parts)part.Quantity=4;
        p.Stocks.front().Width*=2;p.Stocks.front().Height*=2;
        std::size_t cuts=0,partial=0;
        for(int nth=1;nth<=250;++nth)
        {
            int calls=0;Options options;options.TimeLimitSeconds=2;
            options.Cancel=[&calls,nth]{return ++calls>=nth;};
            const auto result=RunParallelogramShelves(p,options);
            const auto checked=Check(p,result);
            if(result.State==Status::Cancelled && checked.PlacedCount>0)++partial;
            ++cuts;
        }
        for(std::size_t limit=1;limit<=100;++limit)
        {
            Options options;options.TimeLimitSeconds=2;options.MaxPlacementAttempts=limit;
            const auto result=RunParallelogramShelves(p,options);Check(p,result);
            Require(result.Stats.PlacementAttempts<=limit,"Nested row planning exceeded the outer attempt budget.");
            ++cuts;
        }
        Require(partial>0,"Cancellation tests never covered publication of already completed physical placements.");
        std::cout<<"Passed "<<cuts<<" parallelogram budget/cancellation cuts; partial layouts retained="<<partial<<'\n';
    }
}

int main()
{
    try
    {
        DifferentWidthRows();SignedShearAndGap();AllowedDirectionsAndUnsupportedGeometry();BudgetAndCancellation();
        std::cout<<"Parallelogram shelf rules: all regression groups passed.\n";
        return 0;
    }
    catch(const std::exception& error)
    { std::cerr<<"Parallelogram shelf regression failed: "<<error.what()<<'\n';return 1; }
}
