#include "NestingInternal.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

using namespace iCAX::SheetNesting;
namespace iCAX::SheetNesting::Detail { Result RunRectanglePinwheel(const Problem&, const Options&); }
namespace
{
    void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    Part Box(std::string id, double w, double h, std::size_t quantity = 1)
    {
        Part p; p.ID = std::move(id); p.Quantity = quantity; p.Material = "m";
        p.FreeRotation = false; p.AllowedAnglesDegrees = {0};
        p.Geometry.Outer = {{0,0},{w,0},{w,h},{0,h}};
        return p;
    }
    Options Budget(std::size_t attempts = 200000)
    { Options o; o.TimeLimitSeconds = 5; o.MaxPlacementAttempts = attempts; return o; }
    JobResult Solve(const Problem& p, Options o = Budget())
    { return RunJob({"pinwheel-test","rect.pinwheel-merge",p,std::move(o)},DefaultStrategies()); }
    void Valid(const Problem& p, const JobResult& r)
    {
        for (const auto& error : r.Checked.Errors) std::cerr << error << '\n';
        if (r.Output.State == Status::Failed)
            for (const auto& error : r.Output.Diagnostics) std::cerr << error << '\n';
        Require(r.Checked.Valid, "pinwheel result must validate original geometry and inventory");
        std::size_t demand = 0, remaining = 0, placed = 0;
        std::set<std::pair<std::string,std::size_t>> identities;
        for (const auto& part : p.Parts) demand += part.Quantity;
        for (const auto& item : r.Output.Remaining) remaining += item.Quantity;
        for (const auto& layout : r.Output.Layouts)
            for (const auto& item : layout.Placements)
            {
                ++placed;
                Require(identities.emplace(item.PartID,item.Instance).second, "expanded original identity cannot repeat");
                Require(std::any_of(p.Parts.begin(),p.Parts.end(),[&](const auto& part){return part.ID==item.PartID;}),
                        "virtual planning IDs cannot leak to original placements");
            }
        Require(demand == placed+remaining, "original quantity must be conserved through composition and expansion");
    }
    void Complete(const Problem& p, const JobResult& r, std::size_t sheets = 1)
    {
        Valid(p,r);
        if (!r.Checked.Complete)
        {
            std::size_t demand=0;for(const auto& part:p.Parts)demand+=part.Quantity;
            std::cerr<<"Pinwheel incomplete: specs="<<p.Parts.size()<<" demand="<<demand<<" gap="<<p.PartGap
                     <<" expectedSheets="<<sheets<<" placed="<<r.Checked.PlacedCount<<'\n';
            for (const auto& message : r.Output.Diagnostics) std::cerr << message << '\n';
        }
        Require(r.Output.State == Status::Complete && r.Checked.Complete, "expected complete pinwheel composition");
        Require(r.Output.Layouts.size() == sheets, "expected finite physical sheet count");
    }
    Problem MixedFive(double gap = 0, std::size_t repetitions = 1)
    {
        Problem p; p.PartGap=gap;
        p.Parts={Box("A",132-gap,52-gap,repetitions),Box("B",91-gap,110-gap,repetitions),
                 Box("C",161-gap,87-gap,repetitions),Box("D",62-gap,145-gap,repetitions),Box("E",70-gap,58-gap,repetitions)};
        p.Stocks={{"stock",223,197,repetitions,1,"m"}};
        return p;
    }
    Problem Nested125(std::size_t quantity = 1, std::size_t sheets = 1, double gap = 0)
    {
        // Frozen diagnostic input dimensions only. No layout or combination
        // tree is supplied to the constructor. All 125 specifications differ.
        static constexpr std::pair<int,int> dimensions[] = {
            {574,135}, {238,292}, {527,139}, {285,296}, {289,157}, {262,234}, {200,494}, {339,430},
            {123,690}, {139,260}, {524,160}, {356,336}, {633,262}, {247,438}, {277,176}, {245,309},
            {149,647}, {278,444}, {116,782}, {129,338}, {272,147}, {146,306}, {289,187}, {129,346},
            {143,159}, {435,300}, {207,623}, {435,335}, {207,658}, {228,323}, {206,518}, {118,1237},
            {215,806}, {109,1525}, {97,719}, {436,314}, {218,742}, {424,427}, {230,855}, {206,428},
            {185,639}, {127,1273}, {229,981}, {83,1615}, {102,634}, {205,323}, {125,643}, {238,442},
            {92,762}, {113,320}, {741,273}, {397,543}, {802,330}, {336,600}, {405,270}, {306,470},
            {197,1070}, {348,784}, {155,1384}, {151,600}, {694,250}, {393,565}, {735,369}, {352,684},
            {342,315}, {369,543}, {185,1217}, {367,698}, {187,1372}, {182,674}, {405,292}, {179,651},
            {379,330}, {205,689}, {200,359}, {242,356}, {148,676}, {286,460}, {104,780}, {138,320},
            {120,704}, {89,1524}, {151,1251}, {58,2071}, {62,820}, {237,457}, {152,984}, {274,719},
            {115,1246}, {122,527}, {134,947}, {76,2029}, {145,1313}, {65,2395}, {69,1082}, {119,489},
            {61,1043}, {122,596}, {58,1150}, {61,554}, {271,142}, {121,301}, {260,153}, {132,312},
            {139,159}, {184,235}, {99,575}, {184,355}, {99,695}, {85,340}, {287,204}, {208,418},
            {363,342}, {132,556}, {155,214}, {109,350}, {71,715}, {130,521}, {50,886}, {59,365},
            {134,142}, {78,289}, {150,187}, {62,334}, {72,147}
        };
        static_assert(sizeof(dimensions)/sizeof(dimensions[0]) == 125);
        Problem p; p.PartGap=gap; p.Stocks={{"stock",2240,6000,sheets,1,"m"}};
        for (const auto& [w,h] : dimensions)
            p.Parts.push_back(Box("original-"+std::to_string(p.Parts.size()+1),w-gap,h-gap,quantity));
        return p;
    }
    Problem Mixed60()
    {
        // Twelve differently sized filled composites must also share one sheet.
        static constexpr std::pair<int,int> dimensions[] = {
            {300,228}, {230,481}, {389,419}, {141,672}, {159,253}, {315,349}, {215,733}, {381,567},
            {149,951}, {166,384}, {330,453}, {200,948}, {374,652}, {156,1147}, {174,495}, {345,656},
            {185,1368}, {366,832}, {164,1544}, {181,712}, {482,282}, {228,586}, {480,314}, {230,618},
            {252,304}, {453,329}, {257,786}, {470,514}, {240,971}, {213,457}, {473,429}, {237,1014},
            {460,586}, {250,1171}, {223,585}, {423,623}, {287,1242}, {520,958}, {190,1577}, {233,619},
            {623,268}, {377,534}, {719,366}, {281,632}, {342,266}, {651,407}, {349,809}, {705,491},
            {295,893}, {356,402}, {610,405}, {390,923}, {691,677}, {309,1195}, {301,518}, {638,591},
            {362,1334}, {677,866}, {323,1609}, {315,743}
        };
        static_assert(sizeof(dimensions)/sizeof(dimensions[0])==60);
        Problem p;p.Stocks={{"stock",2240,6000,1,1,"m"}};
        for(const auto& [w,h]:dimensions)
            p.Parts.push_back(Box("mixed-"+std::to_string(p.Parts.size()+1),w,h));
        return p;
    }
    Problem Repeated350(double gap)
    {
        Problem p; p.PartGap=gap;
        p.Parts={Box("A",208-gap,179-gap,70),Box("B",112-gap,373-gap,70),Box("C",221-gap,227-gap,70),
                 Box("D",99-gap,421-gap,70),Box("E",109-gap,194-gap,70)};
        p.Stocks={{"stock",2240,6000,1,1,"m"}};return p;
    }
}
int main()
{
    try
    {
        for (double gap : {0.,3.})
        {
            auto p=MixedFive(gap);
            const auto r=Solve(p); Complete(p,r);
            Require(r.Output.Stats.CombinationGroups==1,"five original rectangles form one placed filled composite");
            if(gap)Require(r.Checked.MaterialArea<r.Checked.SheetArea,"expanded footprints cannot inflate physical material utilization");
            for(std::size_t i=0;i<p.Parts.size();++i)
                for(auto& point:p.Parts[i].Geometry.Outer){point.X-=17+double(i);point.Y+=29-double(i);}
            Complete(p,Solve(p));
        }
        Problem same;
        same.Parts={Box("wide",4,2,2),Box("tall",2,4,2),Box("middle",2,2)};
        same.Stocks={{"stock",6,6,1,1,"m"}};
        Complete(same,Solve(same));

        for(double gap:{0.,3.})
        {
            const auto nested=Nested125(1,1,gap);const auto r=Solve(nested);Complete(nested,r);
            Require(r.Checked.PlacedCount==125 && r.Output.Stats.CombinationGroups==1,
                    "three composition levels restore all 125 distinct original specifications");
            Require(r.Output.Stats.PlacementAttempts<=200000,"125 case uses the stated shared 200k budget");
            std::cout<<"Pinwheel 125 gap="<<gap<<" attempts="<<r.Output.Stats.PlacementAttempts<<'\n';
            const auto repeated=Repeated350(gap);const auto batch=Solve(repeated);Complete(repeated,batch);
            Require(batch.Checked.PlacedCount==350 && batch.Output.Stats.CombinationGroups==70,
                    "seventy five-piece blocks retain all repeated source instances");
            std::cout<<"Pinwheel 350 gap="<<gap<<" attempts="<<batch.Output.Stats.PlacementAttempts<<'\n';
        }

        const auto mixedCells=Mixed60();const auto mixedResult=Solve(mixedCells);Complete(mixedCells,mixedResult);
        Require(mixedResult.Checked.PlacedCount==60&&mixedResult.Output.Stats.CombinationGroups==12,
                "twelve different filled cells must retain all sixty source rectangles on one sheet");
        std::cout<<"Pinwheel mixed60 attempts="<<mixedResult.Output.Stats.PlacementAttempts<<'\n';

        // More specifications AND more copies per specification: four physical
        // sheets, 125 original IDs, four distinct instances of every original.
        auto many=Nested125(4,4);
        const auto four=Solve(many);Complete(many,four,4);
        Require(four.Checked.PlacedCount==500 && four.Output.Stats.PlacementAttempts<=200000,
                "500 originals must be restored without exceeding the common attempt budget");
        std::cout<<"Pinwheel 500 sheets=4 attempts="<<four.Output.Stats.PlacementAttempts<<'\n';
        for(const auto& part:many.Parts)
        {
            std::set<std::size_t> copies;
            for(const auto& layout:four.Output.Layouts)for(const auto& placement:layout.Placements)
                if(placement.PartID==part.ID)copies.insert(placement.Instance);
            Require(copies==std::set<std::size_t>{0,1,2,3},"each original retains its four distinct source instance numbers");
        }
        std::reverse(many.Parts.begin(),many.Parts.end());Complete(many,Solve(many),4);
        many.Stocks[0].Quantity=3;
        const auto scarce=Solve(many);Valid(many,scarce);
        Require(!scarce.Checked.Complete&&scarce.Checked.PlacedCount==375&&scarce.Output.Layouts.size()==3,
                "composition cannot create a fourth unavailable sheet");
        for(const auto& missing:scarce.Output.Remaining)Require(missing.Quantity==1,"scarce stock leaves one copy of each source");

        auto limitedInput=Nested125();
        for(std::size_t cap:{1u,8u,32u,128u,2000u,20000u})
        {
            const auto r=Solve(limitedInput,Budget(cap));Valid(limitedInput,r);
            Require(r.Output.Stats.PlacementAttempts<=cap,"discovery and final cell planning share the attempt cap");
        }
        auto disabled=Budget();disabled.EnableCombinations=false;
        const auto plain=Solve(MixedFive(),disabled);Valid(MixedFive(),plain);
        Require(plain.Output.Stats.CombinationGroups==0,"disabled combinations must not materialize a pinwheel");

        auto hole=MixedFive();hole.Parts[0].Geometry.Holes={{{1,1},{2,1},{2,2},{1,2}}};
        const auto notFilled=Solve(hole);Valid(hole,notFilled);
        Require(notFilled.Output.Layouts.empty()&&!notFilled.Checked.Complete,"real source holes cannot become filled composite cells");
        Problem near;near.Parts={Box("near",3,2)};near.Parts[0].Geometry.Outer[2].X-=near.Tolerance*.1;
        near.Stocks={{"stock",3,2,1,1,"m"}};
        const auto approximate=Solve(near);Valid(near,approximate);
        Require(!approximate.Checked.Complete&&approximate.Output.Layouts.empty(),"near-rectangle area loss is not a full-cell proof");
        auto material=MixedFive();material.Parts[0].Material="other";
        const auto incompatible=Solve(material);Valid(material,incompatible);
        Require(incompatible.Output.Layouts.empty(),"different materials cannot share a composite");

        Problem angle;angle.Parts={Box("turned-offset",3,2)};
        for(auto& point:angle.Parts[0].Geometry.Outer){point.X-=11;point.Y+=4;}
        angle.Parts[0].AllowedAnglesDegrees={90};angle.Stocks={{"stock",2,3,1,1,"m"}};
        Complete(angle,Solve(angle));
        angle.Parts[0].AllowedAnglesDegrees={0};const auto forbidden=Solve(angle);Valid(angle,forbidden);
        Require(!forbidden.Checked.Complete,"cell planner cannot add forbidden rotations");

        Problem scope;scope.Parts={Box("too-many",1,1,513)};scope.Stocks={{"stock",1000,1000,1,1,"m"}};
        const auto outside=Solve(scope);Valid(scope,outside);
        Require(outside.Output.Layouts.empty()&&outside.Output.Remaining[0].Quantity==513,"scope limit retains all demand");

        for(std::size_t pulse:{1u,8u,32u,128u,1024u,20000u})
        {
            std::size_t calls=0;bool seen=false;auto o=Budget();
            o.Cancel=[&]{if(++calls==pulse){seen=true;return true;}return false;};
            const auto r=Solve(limitedInput,o);Valid(limitedInput,r);
            Require(!seen||(calls==pulse&&r.Output.State==Status::Cancelled),"single cancellation pulse must stay latched");
        }
        auto tiny=Budget();tiny.TimeLimitSeconds=1e-9;
        const auto timed=Solve(limitedInput,tiny);Valid(limitedInput,timed);
        Require(timed.Output.State==Status::Partial&&timed.Output.Layouts.empty(),"expired budget retains original source demand");
        std::cout<<"RectanglePinwheelTests passed\n";return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

