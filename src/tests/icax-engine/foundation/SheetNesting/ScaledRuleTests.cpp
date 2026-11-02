#include "SheetNesting.h"
#include <iostream>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
    Part Circle(std::string id,double r,std::size_t quantity,Point center)
    {Part p;p.ID=std::move(id);p.Quantity=quantity;p.Geometry.Radius=r;p.Geometry.Center=center;p.FreeRotation=false;return p;}
    Problem Circles(std::size_t tiles)
    {
        Problem p;p.PartGap=1;p.EdgeMargin=2;
        p.Parts={Circle("large-A",20,2*tiles*tiles,{7,-13}),Circle("large-B",20,2*tiles*tiles,{-10,14}),Circle("small",6,tiles*tiles,{2,3})};
        const double side=tiles*81.0+(tiles-1)+4;p.Stocks={{"stock",side,side,1,1,""}};return p;
    }
    Problem Frames(std::size_t tiles)
    {
        Problem p;p.PartGap=2;p.EdgeMargin=3;
        Part frame;frame.ID="frame";frame.Quantity=tiles*tiles;frame.FreeRotation=false;
        frame.Geometry.Outer={{13,-6},{173,-6},{173,114},{13,114}};
        frame.Geometry.Holes={{{50,22},{50,86},{136,86},{136,22}}};
        Part insert;insert.ID="insert";insert.Quantity=2*tiles*tiles;insert.FreeRotation=false;
        insert.Geometry.Outer={{-8,11},{32,11},{32,71},{-8,71}};
        p.Parts={frame,insert};p.Stocks={{"stock",tiles*160.0+(tiles-1)*2+6,tiles*120.0+(tiles-1)*2+6,1,1,""}};return p;
    }
    Options Budget(){Options o;o.TimeLimitSeconds=10;o.MaxPlacementAttempts=10000;o.EnableCombinations=false;return o;}
    JobResult Solve(const Problem& p,const char* strategy,const Options& o)
    {return RunJob({"scale",strategy,p,o},DefaultStrategies());}
}
int main()
{
    try
    {
        for(std::size_t tiles:{8,16})
        {
            const auto p=Circles(tiles);const auto r=Solve(p,"circles.square-voids",Budget());
            Require(r.Checked.Valid&&r.Checked.Complete&&r.Output.Layouts.size()==1,"mixed circles must retain square holes for smaller radii at scale");
            Require(r.Checked.PlacedCount==5*tiles*tiles,"circle demand identities were lost");
        }
        std::cout<<"PASS 320 and 1280 mixed-radius circles with positive clearance and distinct local centres\n";
        for(std::size_t tiles:{4,8})
        {
            const auto p=Frames(tiles);const auto r=Solve(p,"rules.hole-rectangles",Budget());
            Require(r.Checked.Valid&&r.Checked.Complete&&r.Output.Layouts.size()==1,"rectangular physical holes must accept all inserts at scale");
            Require(r.Output.Stats.GapFilledParts==2*tiles*tiles,"inserts must occupy real holes");
        }
        std::cout<<"PASS 16 and 64 frames with two inserts each, gap, margin and shifted local origins\n";
        Problem decimal;Part f;f.ID="frame";f.FreeRotation=false;f.Geometry.Outer={{0,0},{127,0},{127,120},{0,120}};
        f.Geometry.Holes={{{31.75,30},{31.75,90},{95.25,90},{95.25,30}}};
        Part i;i.ID="insert";i.Quantity=2;i.FreeRotation=false;i.AllowedAnglesDegrees={0,90};i.Geometry.Outer={{0,0},{31.75,0},{31.75,60},{0,60}};
        decimal.Parts={f,i};decimal.Stocks={{"stock",127,120,1,1,""}};
        const auto exact=Solve(decimal,"rules.hole-rectangles",Budget());
        Require(exact.Checked.Valid&&exact.Checked.Complete,"one-ULP rotation area must not defeat the direction that fills the hole");
        f.Geometry.Outer={{0,0},{153,0},{153,119},{0,119}};f.Geometry.Holes={{{38.25,29.75},{38.25,89.25},{114.75,89.25},{114.75,29.75}}};
        i.Geometry.Outer={{0,0},{38.25,0},{38.25,59.5},{0,59.5}};decimal.Parts={i,f};decimal.Stocks={{"stock",153,119,1,1,""}};
        const auto quantized=Solve(decimal,"rules.hole-rectangles",Budget());
        Require(quantized.Checked.Valid&&quantized.Checked.Complete,"rounding bucket boundaries must not reintroduce a false rotation preference");
        std::cout<<"PASS decimal hole dimensions compare residuals without one-ULP rotation bias\n";
        Problem tiny;tiny.Tolerance=1e-7;Part tf;tf.ID="frame";tf.FreeRotation=false;
        const double origin=9000000;
        tf.Geometry.Outer={{origin,origin},{origin+100,origin},{origin+100,origin+100},{origin,origin+100}};
        tf.Geometry.Holes={{{origin+50,origin+1},{origin+50,origin+99},{origin+50.00001,origin+99},{origin+50.00001,origin+1}}};
        Part ti;ti.ID="tiny-insert";ti.FreeRotation=false;ti.Geometry.Outer={{0,0},{.00001,0},{.00001,98},{0,98}};
        tiny.Parts={tf,ti};tiny.Stocks={{"stock",100,100,1,1,""}};
        const auto narrow=Solve(tiny,"rules.hole-rectangles",Budget());
        Require(narrow.Checked.Valid&&narrow.Checked.Complete,"area pruning must preserve a small real hole at a large source origin");
        std::cout<<"PASS full-sheet pruning retains tolerance-scale real material spaces\n";
        std::size_t checked=0;
        for(const auto strategy:{"circles.square-voids","rules.hole-rectangles"})
        {
            const auto p=std::string(strategy)=="circles.square-voids"?Circles(4):Frames(4);
            for(std::size_t limit=1;limit<=40;limit+=3)
            {
                auto o=Budget();o.MaxPlacementAttempts=limit;auto r=Solve(p,strategy,o);
                Require(r.Checked.Valid&&r.Output.Stats.PlacementAttempts<=limit,"attempt cut must preserve geometry and demand");++checked;
            }
            for(std::size_t cut=1;cut<=400;cut+=19)
            {
                std::size_t calls=0;auto o=Budget();o.Cancel=[&]{return ++calls>=cut;};auto r=Solve(p,strategy,o);
                Require(r.Checked.Valid&&r.Output.State==Status::Cancelled,"cancelled cached-rule results must conserve every instance");++checked;
            }
        }
        std::cout<<"PASS "<<checked<<" cached-rule attempt/cancellation cuts\n";
        std::size_t pulses=0;
        for(const auto strategy:{"circles.square-voids","rules.hole-rectangles","rect.shelves-row",
            "rules.contact-large","rules.parallelograms","rules.strip-width","rules.strip-height"})
        {
            const auto p=std::string(strategy)=="circles.square-voids"?Circles(4):Frames(4);
            for(const std::size_t cut:{1,10,30})
            {
                std::size_t stickyCalls=0,pulseCalls=0;
                auto stickyOptions=Budget();stickyOptions.Cancel=[&]{return ++stickyCalls>=cut;};
                const auto sticky=Solve(p,strategy,stickyOptions);
                auto pulseOptions=Budget();pulseOptions.Cancel=[&]{return ++pulseCalls==cut;};
                const auto pulse=Solve(p,strategy,pulseOptions);
                Require(sticky.Checked.Valid&&pulse.Checked.Valid,"one-shot cancellation must preserve geometry and demand");
                Require(sticky.Output.State==Status::Cancelled&&pulse.Output.State==Status::Cancelled,"observed cancellation must remain latched");
                Require(stickyCalls==cut&&pulseCalls==cut,"cancel callback must not be polled again after cancellation");
                Require(sticky.Checked.PlacedCount==pulse.Checked.PlacedCount
                    &&sticky.Output.Stats.PlacementAttempts==pulse.Output.Stats.PlacementAttempts,"a one-shot cancel must retain the same completed prefix as persistent cancellation");
                ++pulses;
            }
        }
        std::cout<<"PASS "<<pulses<<" one-shot cancellation latches across rule jobs and strip subsolves\n";return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
