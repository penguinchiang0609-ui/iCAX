#include "SheetNesting.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
    Part Box(std::string id,double w,double h,std::size_t quantity=1,std::string material={})
    {Part p;p.ID=std::move(id);p.Geometry.Outer={{0,0},{w,0},{w,h},{0,h}};p.Quantity=quantity;p.FreeRotation=false;p.Material=std::move(material);return p;}
    Result LayoutOf(const Problem& p,std::vector<Placement> placements)
    {Result r;r.State=Status::Complete;r.Layouts={{p.Stocks[0].ID,0,p.Stocks[0].Width,p.Stocks[0].Height,std::move(placements),0}};return r;}
    Placement At(std::string id,std::size_t instance,double x,double y)
    {return {std::move(id),instance,{x,y,0},0,false};}
    void Check(const Problem& p,const Result& r) {Require(Validate(p,r).Valid,"test layout itself is invalid");}
    class Supplied final:public IStrategy
    {Result Value;public:explicit Supplied(Result r):Value(std::move(r)){} Result Run(const Problem&,const Options&)override{return Value;}};
}
int main()
{
    try
    {
        Problem p;p.Parts={Box("block",80,20,2)};p.Stocks={{"stock",200,100,1,1,""}};
        auto compact=LayoutOf(p,{At("block",0,0,0),At("block",1,0,20)});
        auto low=LayoutOf(p,{At("block",0,0,0),At("block",1,90,0)});Check(p,compact);Check(p,low);
        const auto a=EvaluateRemnants(p,compact),b=EvaluateRemnants(p,low);
        Require(a.Measured&&b.Measured,"remnant metric unavailable");
        Require(std::abs(a.LastSheetRegularArea-12000)<.1&&std::abs(b.LastSheetRegularArea-16000)<.1,"largest regular rectangles differ from occupied area");
        StrategyRegistry registry;
        registry.Register("compact",[compact]{return std::make_unique<Supplied>(compact);});
        registry.Register("regular-remnant",[low]{return std::make_unique<Supplied>(low);});
        auto best=RunPortfolio(p,{},registry);
        Require(best.Jobs[best.BestJob].StrategyID=="regular-remnant","default objective must value the larger regular remnant");
        std::cout<<"PASS a larger regular remnant can beat a smaller occupied envelope\n";
        p.Parts={Box("block",40,96)};p.Stocks={{"stock",200,100,1,1,""}};p.PartGap=3;p.EdgeMargin=2;
        auto gap=LayoutOf(p,{At("block",0,2,2)});Check(p,gap);auto measure=EvaluateRemnants(p,gap);
        Require(measure.Measured&&std::abs(measure.LastSheetRegularArea-153*96)<.2,"gap and board margin must be accounted once");
        Require(measure.PerLayout[0].Verified&&measure.PerLayout[0].Bounds.X>=45-2*p.Tolerance,"retained rectangle intrudes into material clearance");
        std::cout<<"PASS remnant rectangles obey true gap and edge constraints\n";
        auto frame=Box("frame",200,200);frame.Geometry.Holes={{{60,60},{60,140},{140,140},{140,60}}};
        p={};p.Parts={frame};p.Stocks={{"stock",200,200,1,1,""}};
        auto hole=LayoutOf(p,{At("frame",0,0,0)});Check(p,hole);measure=EvaluateRemnants(p,hole);
        Require(measure.Measured&&std::abs(measure.LastSheetRegularArea-6400)<.2,"a real rectangular internal remnant is usable");
        std::cout<<"PASS holes use real material rather than the part AABB\n";
        Part circle;circle.ID="circle";circle.Geometry.Radius=20;
        p={};p.Parts={circle};p.Stocks={{"stock",100,100,1,1,""}};
        auto disc=LayoutOf(p,{At("circle",0,50,50)});Check(p,disc);measure=EvaluateRemnants(p,disc);
        Require(measure.Measured&&std::abs(measure.LastSheetRegularArea-3000)<.2,"analytic circular obstacles must bound the rectangle safely");
        std::cout<<"PASS circular material receives a verified rectangular residual\n";
        // On this large square lattice, R64 cells straddle the small empty voids.
        // The fixed grid has no safe seed even though each four-circle cell has
        // a reusable square. Local circle probes must recover a real certificate.
        Part latticeCircle;latticeCircle.ID="lattice";latticeCircle.Quantity=16*16;
        latticeCircle.Geometry.Radius=20;latticeCircle.Geometry.Center={7,-13};latticeCircle.FreeRotation=false;
        p={};p.PartGap=1;p.EdgeMargin=2;p.Parts={latticeCircle};p.Stocks={{"stock",659,659,1,1,""}};
        std::vector<Placement> latticePlacements;
        for(std::size_t row=0;row<16;++row)for(std::size_t col=0;col<16;++col)
            latticePlacements.push_back(At("lattice",row*16+col,22+41*static_cast<double>(col)-7,22+41*static_cast<double>(row)+13));
        auto lattice=LayoutOf(p,std::move(latticePlacements));Check(p,lattice);
        const auto latticeRemnant=FindRegularRemnant(p,lattice.Layouts[0],64);
        Require(latticeRemnant.Verified&&std::isfinite(latticeRemnant.Area)&&latticeRemnant.Area>120
            &&std::isfinite(latticeRemnant.AreaUpperBound)&&latticeRemnant.AreaUpperBound>=latticeRemnant.Area,
            "pure-circle fallback must recover a positive safe R64 remnant and a valid interval");
        const auto& retained=latticeRemnant.Bounds;
        Require(retained.X>=p.EdgeMargin&&retained.Y>=p.EdgeMargin&&retained.X+retained.Width<=659-p.EdgeMargin
            &&retained.Y+retained.Height<=659-p.EdgeMargin,"circle fallback certificate is outside the usable board");
        Require(std::abs(latticeRemnant.Area-retained.Width*retained.Height)<1e-8,"circle fallback area does not match its rectangle");
        for(const auto& placed:lattice.Layouts[0].Placements)
        {
            const double cx=placed.Transform.X+latticeCircle.Geometry.Center.X,cy=placed.Transform.Y+latticeCircle.Geometry.Center.Y;
            const double nearestX=std::clamp(cx,retained.X,retained.X+retained.Width);
            const double nearestY=std::clamp(cy,retained.Y,retained.Y+retained.Height);
            Require(std::hypot(cx-nearestX,cy-nearestY)>=latticeCircle.Geometry.Radius+p.PartGap,
                "circle fallback rectangle violates exact analytic material clearance");
        }
        std::cout<<"PASS a circle lattice with no R64 grid seed retains an analytically checked square\n";
        Part pa;pa.ID="P80";pa.Quantity=2;pa.Geometry.Outer={{0,0},{80,0},{100,40},{20,40}};
        Part pb;pb.ID="P60";pb.Quantity=2;pb.Geometry.Outer={{0,0},{60,0},{80,40},{20,40}};
        p={};p.Parts={pa,pb};p.Stocks={{"stock",180,80,1,1,""}};
        auto fragmented=LayoutOf(p,{At("P80",0,0,0),At("P60",0,100,0),At("P80",1,0,40),At("P60",1,100,40)});
        auto joined=LayoutOf(p,{At("P80",0,0,0),At("P60",0,80,0),At("P80",1,0,40),At("P60",1,80,40)});
        Check(p,fragmented);Check(p,joined);const auto f=EvaluateRemnants(p,fragmented),j=EvaluateRemnants(p,joined);
        Require(f.Measured&&j.Measured&&f.LastSheetRegularArea>100&&f.LastSheetRegularArea<220,"fragmented diagonal gaps must not be summed into a fictitious rectangle");
        Require(std::abs(j.LastSheetRegularArea-1600)<.2&&j.LastSheetRegularArea>f.LastSheetRegularArea*6,"human compacting improvement should leave the larger regular block");
        for(const auto& r:j.PerLayout)Require(r.AreaUpperBound+.2>=r.Area&&r.Verified,"conservative remnant bounds");
        std::cout<<"PASS the human counterexample changes scattered gaps into a usable straight remnant\n";
        p={};auto border=Box("border",1,1);
        border.Geometry.Outer={{0,0},{192.5,0},{192.5,44},{66,44},{66,110},{0,110}};
        p.Parts={border};p.Stocks={{"stock",220*1.1,100*1.1,1,1,""}};
        auto decimal=LayoutOf(p,{At("border",0,0,0)});Check(p,decimal);measure=EvaluateRemnants(p,decimal);
        const auto& decimalBounds=measure.PerLayout[0].Bounds;
        Require(measure.Measured&&measure.LastSheetRegularArea>11615.99,"decimal board endpoints must not discard the larger safe seed");
        Require(decimalBounds.X>=0&&decimalBounds.Y>=0&&decimalBounds.X+decimalBounds.Width<=decimal.Layouts[0].Width
            &&decimalBounds.Y+decimalBounds.Height<=decimal.Layouts[0].Height,"ULP correction must retain strict board containment");
        std::cout<<"PASS decimal board endpoints preserve the larger safe remnant without relaxing containment\n";
        p={};p.Parts={Box("steel",80,20,3,"steel"),Box("aluminium",80,20,1,"aluminium")};
        p.Stocks={{"S",200,100,2,1,"steel"},{"A",200,100,1,1,"aluminium"}};
        Result multi;multi.State=Status::Complete;
        multi.Layouts={{"S",0,200,100,{At("steel",2,0,0)},0},
            {"S",1,200,100,{At("steel",0,0,0),At("steel",1,0,20)},0},
            {"A",0,200,100,{At("aluminium",0,0,0)},0}};
        Check(p,multi);measure=EvaluateRemnants(p,multi);
        Require(measure.Measured&&measure.FinalLayoutIndices.size()==2&&std::abs(measure.LastSheetRegularArea-32000)<.2,"select one actual final sheet per compatible material group");
        Require(std::find(measure.FinalLayoutIndices.begin(),measure.FinalLayoutIndices.end(),0)!=measure.FinalLayoutIndices.end(),"last sheet is a logical selection, not the array tail");
        auto unused=multi;unused.Layouts.push_back({"S",0,200,100,{},0});
        Require(!Validate(p,unused).Valid,"an empty unused board must not generate remnant credit");
        std::cout<<"PASS final-sheet selection respects compatible materials and physical stock\n";
        return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
