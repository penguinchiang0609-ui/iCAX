#include "SheetNesting.h"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace iCAX::SheetNesting;
namespace {
    void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
    Part PolygonPart(const char* id,std::size_t quantity,std::vector<Point> ring) {
        Part p;p.ID=id;p.Quantity=quantity;p.Geometry.Outer=std::move(ring);p.FreeRotation=false;p.AllowedAnglesDegrees={0,90};return p;
    }
    Part CirclePart(const char* id,std::size_t quantity,double radius,Point center) {
        auto p=PolygonPart(id,quantity,{});p.Geometry.Radius=radius;p.Geometry.Center=center;return p;
    }
    Problem MixedParallelograms() {
        Problem p;p.PartGap=2;p.EdgeMargin=1;
        p.Parts.push_back(PolygonPart("spec-0",2,{{12,1},{48,1},{48,40},{12,40}}));
        p.Parts.push_back(PolygonPart("spec-1",5,{{0,0},{62,0},{67,39},{5,39}}));
        p.Parts.push_back(PolygonPart("spec-2",5,{{14,2},{40,2},{46,46},{20,46}}));
        p.Parts.push_back(CirclePart("spec-3",3,18,{0,0}));
        p.Parts.push_back(CirclePart("spec-4",4,7,{0,0}));
        p.Parts.push_back(CirclePart("spec-5",4,24,{0,0}));
        p.Stocks={{"stock",202,114,2,1,""}};return p;
    }
    Problem MixedPolygons() {
        Problem p;p.PartGap=1;p.EdgeMargin=1;
        p.Parts.push_back(PolygonPart("spec-0",5,{{0,0},{51,0},{68,23},{17,23}}));
        p.Parts.push_back(PolygonPart("spec-1",4,{{-12,10},{39,10},{39,45},{-12,45}}));
        p.Parts.push_back(PolygonPart("spec-2",4,{{7,9},{80,9},{80,25},{7,25}}));
        p.Parts.push_back(PolygonPart("spec-3",5,{{0,0},{40,0},{50,35},{10,35}}));
        p.Parts.push_back(PolygonPart("spec-4",3,{{0,0},{37,0},{37,33},{0,33}}));
        p.Parts.push_back(PolygonPart("spec-5",2,{{0,0},{20,0},{20,48},{0,48}}));
        p.Stocks={{"stock",154,140,2,1,""}};return p;
    }
    Problem MixedDiscs() {
        Problem p;p.PartGap=2;p.EdgeMargin=1;
        p.Parts.push_back(PolygonPart("spec-0",3,{{11,0},{27,0},{27,62},{11,62}}));
        p.Parts.push_back(PolygonPart("spec-1",4,{{0,0},{70,0},{70,53},{0,53}}));
        p.Parts.push_back(CirclePart("spec-2",3,16,{12,-10}));
        p.Parts.push_back(PolygonPart("spec-3",2,{{14,-1},{66,-1},{66,61},{14,61}}));
        p.Parts.push_back(PolygonPart("spec-4",4,{{-3,11},{44,11},{44,43},{-3,43}}));
        p.Parts.push_back(CirclePart("spec-5",3,19,{0,0}));
        p.Stocks={{"stock",159,147,2,1,""}};return p;
    }
    Options Budget() { Options o;o.TimeLimitSeconds=1;o.MaxPlacementAttempts=20000;o.MaxPatterns=128;return o; }
}
int main() { try {
    auto registry=DefaultStrategies();
    { auto p=MixedParallelograms();auto o=Budget();auto r=RunJob({"fit","rules.composite-fit",p,o},registry);Require(r.Checked.Valid && r.Checked.Complete,"mixed specifications must fit completely");Require(r.Output.Layouts.size()==2,"two-sheet demand"); }
    { auto p=MixedPolygons();auto o=Budget();auto r=RunJob({"fit","rules.composite-anchor-fit",p,o},registry);Require(r.Checked.Valid && r.Checked.Complete,"mixed specifications must fit completely");Require(r.Output.Layouts.size()==2,"two-sheet demand"); }
    { auto p=MixedDiscs();auto o=Budget();o.MaxPlacementAttempts=40000;auto r=RunJob({"fit","rules.composite-anchor-fit",p,o},registry);Require(r.Checked.Valid && r.Checked.Complete,"mixed specifications must fit completely");Require(r.Output.Layouts.size()==2,"two-sheet demand"); }
    for(const char* strategy:{"rules.composite-fit","rules.composite-anchor-fit"}) {
        const auto p=MixedParallelograms();auto o=Budget();o.MaxPlacementAttempts=1;
        const auto limited=RunJob({"limit",strategy,p,o},registry);
        Require(limited.Checked.Valid,"attempt-limited output must remain valid");
        Require(limited.Output.Stats.PlacementAttempts<=1,"placement budget must be respected");
        bool pulse=true;o=Budget();o.Cancel=[&]{const bool value=pulse;pulse=false;return value;};
        const auto cancelled=RunJob({"cancel",strategy,p,o},registry);
        Require(cancelled.Checked.Valid && cancelled.Output.State==Status::Cancelled,"cancellation remains latched");
        Require(cancelled.Checked.PlacedCount==0,"cancelled before preparation");
    }
    std::cout<<"Composite fit strategies passed\n";return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; } }
