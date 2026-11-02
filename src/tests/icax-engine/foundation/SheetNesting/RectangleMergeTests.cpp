#include "NestingInternal.h"
#include "NestingRectangleDirections.h"
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
using namespace iCAX::SheetNesting;
namespace iCAX::SheetNesting::Detail {Result RunRectangleMerge(const Problem&,const Options&);}
namespace {
void Require(bool v,const char* m){if(!v)throw std::runtime_error(m);}
JobResult Solve(const Problem& p,Options o={})
{return RunJob({"merge-test","rect.merge-tree",p,std::move(o)},DefaultStrategies());}
Part Box(const char* id,double w,double h,std::size_t qty)
{Part p;p.ID=id;p.Quantity=qty;p.Material="m";p.FreeRotation=false;p.AllowedAnglesDegrees={0};p.Geometry.Outer={{0,0},{w,0},{w,h},{0,h}};return p;}
Problem MixedCells()
{
 Problem p;p.Parts={Box("A",60,40,2),Box("B",80,20,1),Box("rectangle",20,20,3),Box("circle-cell",10,10,4)};
 p.Parts[1].AllowedAnglesDegrees={90};p.Stocks={{"stock",100,80,1,1,"m"}};return p;
}
Problem Pinwheel(bool turn=false,double gap=0)
{
 Problem p;p.PartGap=gap;p.Parts={Box("horizontal",4,2,2),Box("vertical",2,4,2),Box("center",2,2,1)};
 if(turn)for(auto& part:p.Parts)part.FreeRotation=true;
 p.Stocks={{"stock",gap?6.3:6,gap?6.3:6,1,1,"m"}};return p;
}
void Complete(const Problem& p,const JobResult& r)
{
 for(const auto& e:r.Checked.Errors)std::cerr<<e<<'\n';
 if(!r.Checked.Complete){std::cerr<<"incomplete first="<<p.Parts.front().ID<<" attempts="<<r.Output.Stats.PlacementAttempts<<" states="<<r.Output.Stats.CandidatePatterns<<'\n';for(const auto& d:r.Output.Diagnostics)std::cerr<<d<<'\n';}
 Require(r.Checked.Valid&&r.Checked.Complete,"complete original geometry and quantity");
 Require(r.Output.Layouts.size()==1,"one physical stock");
 for(const auto& part:p.Parts){std::set<std::size_t> seen;for(const auto& a:r.Output.Layouts[0].Placements)if(a.PartID==part.ID)seen.insert(a.Instance);Require(seen.size()==part.Quantity,"every original repeated instance must remain represented");}
}
void Preserved(const Problem& p,const JobResult& r,Status expected=Status::Partial)
{
 Require(r.Checked.Valid&&!r.Checked.Complete&&r.Output.State==expected,"partial/cancelled geometry and state");
 std::size_t remaining=0,demand=0;for(const auto& x:r.Output.Remaining)remaining+=x.Quantity;for(const auto& x:p.Parts)demand+=x.Quantity;
 Require(r.Output.Layouts.empty()&&remaining==demand,"whole unmet demand retained");
}
}
int main(){try{
 auto mixed=MixedCells();Options o;o.TimeLimitSeconds=2;o.MaxPlacementAttempts=40000;o.MaxPatterns=1;o.MaxPatternMembers=1;o.MaxAssemblyMembers=1;o.EnableCombinations=false;Complete(mixed,Solve(mixed,o));
 Problem repeated;repeated.Parts={Box("same",20,25,20)};repeated.Stocks={{"stock",100,100,1,1,"m"}};Complete(repeated,Solve(repeated,o));
 auto one=repeated;one.Parts[0].Quantity=1;Complete(one,Solve(one,o));
 auto tooMany=repeated;tooMany.Parts[0].Quantity=21;Preserved(tooMany,Solve(tooMany,o));
 auto wrong=mixed;wrong.Parts[1].Material="other";Preserved(wrong,Solve(wrong,o));
 auto gap=mixed;gap.PartGap=.01;Preserved(gap,Solve(gap,o));
 auto circle=mixed;circle.Parts[3].Geometry={};circle.Parts[3].Geometry.Radius=5;Preserved(circle,Solve(circle,o));
 auto hole=mixed;hole.Parts[0].Geometry.Holes={{{10,10},{20,10},{20,20},{10,20}}};Preserved(hole,Solve(hole,o));
 auto fixed=Pinwheel();const auto noTree=Solve(fixed,o);Preserved(fixed,noTree);
 Require(std::any_of(noTree.Output.Diagnostics.begin(),noTree.Output.Diagnostics.end(),[](const auto& s){return s.find("no infeasibility proof")!=std::string::npos;}),"nonslicing pinwheel failure is not an infeasibility proof");
 Complete(Pinwheel(true),Solve(Pinwheel(true),o));Preserved(Pinwheel(false,.1),Solve(Pinwheel(false,.1),o));
 for(const auto& p:{mixed,tooMany,gap,wrong}){auto c=o;c.Cancel=[]{return true;};Preserved(p,Solve(p,c),Status::Cancelled);}
 for(std::size_t pulse:{1u,2u,4u,8u,16u,32u}){std::size_t calls=0;auto c=o;c.Cancel=[&]{return ++calls==pulse;};const auto r=Solve(mixed,c);Require(calls==pulse,"single pulse must be latched");Preserved(mixed,r,Status::Cancelled);}
 for(std::size_t cap=1;cap<=20;++cap){auto c=o;c.MaxPlacementAttempts=cap;const auto r=Solve(mixed,c);Require(r.Checked.Valid,"limited merge attempt result remains valid");Require(r.Output.Stats.PlacementAttempts<=cap,"merge attempt cap");}
 // A large imported subdivision is cancelled during linear preprocessing.
 auto dense=one;auto raw=dense.Parts[0].Geometry.Outer;dense.Parts[0].Geometry.Outer.clear();
 for(std::size_t i=0;i<raw.size();++i){const auto a=raw[i],b=raw[(i+1)%raw.size()];for(std::size_t k=0;k<5000;++k){const double t=static_cast<double>(k)/5000;dense.Parts[0].Geometry.Outer.push_back({a.X+(b.X-a.X)*t,a.Y+(b.Y-a.Y)*t});}}
 for(std::size_t pulse:{1u,8u,16u}){std::size_t calls=0;auto c=o;c.Cancel=[&]{return ++calls==pulse;};const auto r=Detail::RunRectangleMerge(dense,c);Require(calls==pulse&&r.State==Status::Cancelled&&r.Stats.PlacementAttempts==0,"dense preprocessing cancellation remains latched");Require(r.Layouts.empty()&&r.Remaining.size()==1&&r.Remaining[0].Quantity==1,"dense cancellation keeps original demand");}
 std::size_t helperCalls=0;const auto interrupted=Detail::FindRectangleDirections(dense.Parts[0],dense.Tolerance,[&]{return ++helperCalls==4;});Require(interrupted.Interrupted&&helperCalls==4,"common direction helper independently latches one stop pulse");
 const auto described=Detail::FindRectangleDirections(dense.Parts[0],dense.Tolerance,{});Require(described.Supported&&!described.Directions.empty(),"linear subdivision cleanup retains a rectangle");
 for(const auto& d:described.Directions){const auto original=Detail::Bounds(dense.Parts[0].Geometry,{0,0,d.Offset.AngleDegrees});Require(d.Width>=original.Width-dense.Tolerance&&d.Height>=original.Height-dense.Tolerance,"description contains original source bounds");}
 std::cout<<"RectangleMergeTests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
