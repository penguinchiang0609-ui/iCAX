#include "SheetNestingContactPrecision.h"
#include "NestingInternal.h"
#include "NestingAxisSafety.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace iCAX::SheetNesting;
static void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
static bool same(const Result& a,const Result& b){if(a.Layouts.size()!=b.Layouts.size())return false;for(std::size_t i=0;i<a.Layouts.size();++i){const auto& x=a.Layouts[i].Placements;const auto& y=b.Layouts[i].Placements;if(x.size()!=y.size())return false;for(std::size_t j=0;j<x.size();++j)if(x[j].PartID!=y[j].PartID||x[j].Instance!=y[j].Instance||x[j].Transform.X!=y[j].Transform.X||x[j].Transform.Y!=y[j].Transform.Y||x[j].Transform.AngleDegrees!=y[j].Transform.AngleDegrees)return false;}return true;}
static Part piece(const char* id){Part p;p.ID=id;p.Material="m";p.FreeRotation=false;p.AllowedAnglesDegrees={0};p.Geometry.Outer={{0,0},{5,0},{5,5},{0,5}};return p;}
int main() try {
 Problem p;p.Tolerance=1e-6;p.Stocks.push_back({"sheet",20,10,1,1,"m"});p.Parts={piece("a"),piece("b")};
 Result in;in.State=Status::Complete;in.Layouts.push_back({"sheet",0,20,10,{{"a",0,{0,0,0},0,false},{"b",0,{5-2e-8,0,0},0,false}},50});
 require(Validate(p,in).Valid,"test input invalid");ContactPrecisionOptions opts;opts.TimeLimitSeconds=3;auto r=RefineContactPrecision(p,in,opts);
 require(r.State==ContactPrecisionStatus::Applied,"near contact was not repaired");require(Validate(p,r.Output).Valid&&Validate(p,r.Output).Complete,"repaired layout invalid");
 require(!Detail::ShapesConflict(p.Parts[0].Geometry,r.Output.Layouts[0].Placements[0].Transform,p.Parts[1].Geometry,r.Output.Layouts[0].Placements[1].Transform,0,0),"strict overlap remains");
 require(r.MaximumShift<=std::sqrt(2.)*opts.MaxAxisShift+1e-12,"shift exceeds bound");
 for(const auto& pose:r.Output.Layouts[0].Placements)require(pose.Transform.AngleDegrees==0,"angle changed");
 auto fixed=p;fixed.Stocks[0].Width=10;fixed.Stocks[0].Height=5;auto packed=in;packed.Layouts[0].Width=10;packed.Layouts[0].Height=5;packed.Layouts[0].Placements[1].Transform.X=5;
 opts.MaxSweeps=300;auto blocked=RefineContactPrecision(fixed,packed,opts);require(blocked.State!=ContactPrecisionStatus::Applied&&same(blocked.Output,packed),"infeasible repair mutated input");
 auto cancelled=opts;cancelled.Cancel=[] {return true;};auto stopped=RefineContactPrecision(p,in,cancelled);require(stopped.State==ContactPrecisionStatus::LimitReached&&same(stopped.Output,in),"cancellation changed layout");
 auto bad=opts;bad.Clearance=0;require(RefineContactPrecision(p,in,bad).State==ContactPrecisionStatus::InvalidInput,"invalid clearance accepted");
 auto concave=p;concave.Parts[0].Geometry.Outer={{0,0},{5,0},{5,2},{2,2},{2,5},{0,5}};auto concaveResult=in;concaveResult.Layouts[0].MaterialArea=41;auto cr=RefineContactPrecision(concave,concaveResult,opts);
 require(cr.State==ContactPrecisionStatus::Applied&&Validate(concave,cr.Output).Valid,"concave part failed");
 auto spaced=p;spaced.PartGap=.2;spaced.EdgeMargin=.5;spaced.Stocks[0].Quantity=2;for(auto& part:spaced.Parts)part.Quantity=2;
 auto nearGap=in;nearGap.Layouts[0].Placements[0].Transform={.5,.5,0};nearGap.Layouts[0].Placements[1].Transform={5.699999999999999,.5,0};
 nearGap.Layouts.push_back(nearGap.Layouts[0]);nearGap.Layouts[1].StockInstance=1;for(auto& q:nearGap.Layouts[1].Placements)q.Instance=1;
 auto precise=RefineContactPrecision(spaced,nearGap,opts);
 require(precise.State==ContactPrecisionStatus::Applied&&Validate(spaced,precise.Output).Complete,"multi-sheet positive-gap roundoff was not repaired");
 for(const auto& board:precise.Output.Layouts){const auto& a=board.Placements[0];const auto& b=board.Placements[1];require(AxisSafety::Pair(AxisSafety::Make(spaced.Parts[0].Geometry,a.Transform),AxisSafety::Make(spaced.Parts[1].Geometry,b.Transform),.2,false,spaced.Tolerance)==AxisSafety::Verdict::Safe,"micro-translation repair spent nominal gap tolerance");}
 std::cout<<"contact precision controls passed\n";return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
