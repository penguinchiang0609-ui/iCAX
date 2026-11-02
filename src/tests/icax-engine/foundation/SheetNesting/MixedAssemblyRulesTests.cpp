#include "NestingInternal.h"
#include <chrono>
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace iCAX::SheetNesting;
namespace iCAX::SheetNesting::Detail {Result RunMixedAssemblyRules(const Problem&,const Options&);}
namespace
{
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Part Poly(std::string id,std::size_t n,std::vector<Point> ring){Part p;p.ID=std::move(id);p.Quantity=n;p.Material="m";p.FreeRotation=true;p.Geometry.Outer=std::move(ring);return p;}
Problem Pair(std::size_t a=2,std::size_t b=2){Problem p;p.Parts={Poly("A",a,{{0,0},{30,0},{30,10},{40,20},{30,30},{30,40},{0,40}}),Poly("B",b,{{30,0},{60,0},{60,40},{30,40},{30,30},{40,20},{30,10}})};p.Stocks={{"stock",180,120,2,1,"m"}};return p;}
Options Budget(){Options o;o.TimeLimitSeconds=.5;o.MaxPlacementAttempts=60000;return o;}
int Units(const Result& r){for(const auto& d:r.Diagnostics){const std::string prefix="Seam components converted ";if(d.starts_with(prefix))return std::stoi(d.substr(prefix.size()));}return 0;}
void Complete(const Problem& p,const Result& r){const auto v=Validate(p,r);Require(v.Valid&&v.Complete,"complete original mixed demand required");}
}
int main()
{
 try
 {
    {const auto p=Pair();const auto registered=RunJob({"mixed-entry","rules.composite-units",p,Budget()},DefaultStrategies());Require(registered.Checked.Valid&&registered.Checked.Complete,"registered mixed assembly entry must validate original demand");}
    {Problem p;p.Parts={Poly("tall",11,{{0,0},{1,0},{1,2},{0,2}}),Poly("small",11,{{0,0},{1,0},{1,1},{0,1}})};for(auto& part:p.Parts){part.FreeRotation=false;part.AllowedAnglesDegrees={0};}p.Stocks={{"stock",2,17,2,1,"m"}};const auto r=Detail::RunMixedAssemblyRules(p,Budget());Complete(p,r);Require(r.Layouts.size()==1,"a complete two-board row plan must not block a one-board column plan");}
    {
        Problem q;
        q.Parts={Poly("A",8,{{0,0},{8,0},{8,16},{0,16}}),Poly("B",8,{{0,0},{15,0},{15,16},{0,16}})};
        Part circle;circle.ID="circle";circle.Quantity=8;circle.Material="m";circle.Geometry.Radius=5;q.Parts.push_back(circle);
        for(auto& part:q.Parts){part.FreeRotation=false;part.AllowedAnglesDegrees={0};}
        q.Stocks={{"stock",180,140,1,1,"m"}};
        auto o=Budget();o.EnableCombinations=false;o.MaxPatterns=32;
        auto cells=q;cells.Parts.back().Geometry={};cells.Parts.back().Geometry.Outer={{-5,-5},{5,-5},{5,5},{-5,5}};
        const auto row=Detail::RunRectanglePatternShelves(cells,o,false);
        const auto column=Detail::RunRectanglePatternShelves(cells,o,true);
        Complete(q,row);Complete(q,column);
        const auto rowArea=EvaluateRemnants(q,row).LastSheetRegularArea;
        const auto columnArea=EvaluateRemnants(q,column).LastSheetRegularArea;
        Require(columnArea>rowArea+2900,"control column must preserve a larger real remnant than the first complete row");
        const auto regular=RunJob({"regular-mixed","rules.composite-units",q,o},DefaultStrategies());Complete(q,regular.Output);
        Require(regular.Output.Layouts.size()==1&&regular.Remnants.LastSheetRegularArea>=columnArea-.001,
            "the first complete single-sheet row must not block a larger safe column remnant");
        o.Ranking=RankingPolicy::LowThenLeft;
        const auto low=RunJob({"low-mixed","rules.composite-units",q,o},DefaultStrategies());Complete(q,low.Output);
        const auto top=[&](const Result& r){double value=0;for(const auto& l:r.Layouts)for(const auto& a:l.Placements){const auto part=std::find_if(q.Parts.begin(),q.Parts.end(),[&](const auto& b){return b.ID==a.PartID;});const auto bounds=Detail::Bounds(part->Geometry,a.Transform);value=std::max(value,bounds.Y+bounds.Height);}return value;};
        Require(low.Output.Layouts.size()==1&&top(low.Output)<=top(row)+q.Tolerance,
            "the same internal candidates must honor the selected low-then-left policy");
        Require(regular.Output.Stats.EvaluationSeconds>0&&regular.Output.Stats.VerificationSeconds>0,
            "internal selection must report its real verification and measurement cost");
        o.Ranking=RankingPolicy::RegularRemnant;o.MaxPlacementAttempts=90;
        const auto limited=Detail::RunMixedAssemblyRules(q,o);Complete(q,limited);
        Require(limited.Layouts.size()==1&&limited.Stats.PlacementAttempts<=30,
            "after 24-piece completion, all later backbones must share the six remaining refinement attempts");
    }
    auto p=Pair(3,2);p.Parts.push_back(Poly("seam-unit-0",3,{{0,0},{20,0},{20,10},{0,10}}));Part c;c.ID="circle";c.Quantity=4;c.Material="m";c.Geometry.Radius=5;c.Geometry.Center={100,-50};p.Parts.push_back(c);
    auto r=Detail::RunMixedAssemblyRules(p,Budget());Complete(p,r);Require(Units(r)==1,"unequal inventory and original-ID collision must retain one actual unit");
    p=Pair();for(auto& part:p.Parts){part.FreeRotation=false;part.AllowedAnglesDegrees={90};}p.EdgeMargin=2;r=Detail::RunMixedAssemblyRules(p,Budget());Complete(p,r);
    auto o=Budget();o.EnableCombinations=false;r=Detail::RunMixedAssemblyRules(p,o);Complete(p,r);Require(Units(r)==0,"combination switch");
    o=Budget();o.MaxAssemblyMembers=1;r=Detail::RunMixedAssemblyRules(p,o);Complete(p,r);Require(Units(r)==0,"assembly cap one disables seam units");
    o.MaxAssemblyMembers=2;r=Detail::RunMixedAssemblyRules(p,o);Complete(p,r);Require(Units(r)==1,"member boundary two");
    p=Pair();p.Parts[0].Material="m1";p.Parts[1].Material="m2";p.Stocks={{"s1",120,120,1,1,"m1"},{"s2",120,120,1,1,"m2"}};r=Detail::RunMixedAssemblyRules(p,Budget());Complete(p,r);Require(Units(r)==0,"material separation");
    for(auto attempts:{1u,2u,8u,64u}){o=Budget();o.MaxPlacementAttempts=attempts;const auto q=Pair();r=Detail::RunMixedAssemblyRules(q,o);Require(Validate(q,r).Valid&&r.Stats.PlacementAttempts<=attempts,"mixed attempt-bound conservation");}
    for(int pulse=1;pulse<=128;++pulse){int calls=0;bool seen=false;o=Budget();o.Cancel=[&]{if(++calls==pulse){seen=true;return true;}return false;};const auto q=Pair();r=Detail::RunMixedAssemblyRules(q,o);Require(Validate(q,r).Valid,"mixed cancellation conservation");Require(!seen||r.State==Status::Cancelled,"mixed cancellation latch");}
    auto large=Pair();large.Parts.resize(1);auto& ring=large.Parts[0].Geometry.Outer;ring.clear();constexpr int n=8750;
    for(int side=0;side<4;++side)for(int i=0;i<n;++i){double f=static_cast<double>(i)/n;ring.push_back(side==0?Point{100*f,0}:side==1?Point{100,50*f}:side==2?Point{100*(1-f),50}:Point{0,50*(1-f)});}
    for(int pulse:{1,7,600}){int calls=0;o=Budget();o.Cancel=[&]{return ++calls==pulse;};const auto start=std::chrono::steady_clock::now();r=Detail::RunMixedAssemblyRules(large,o);const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();Require(r.State==Status::Cancelled&&r.Layouts.empty()&&r.Remaining.size()==1&&r.Remaining[0].Quantity==2,"large mixed cancellation accounting");std::cout<<"35k mixed pulse="<<pulse<<" milliseconds="<<ms<<'\n';}
    std::cout<<"MixedAssemblyRulesTests passed\n";return 0;
 }
 catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
