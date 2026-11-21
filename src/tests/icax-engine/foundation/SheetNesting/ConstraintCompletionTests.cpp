#include "SheetNestingConstraint.h"
#include "NestingConstraintArithmetic.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
    Part Rectangle(std::string id,double w,double h,std::size_t quantity=1)
    {
        Part p;p.ID=std::move(id);p.Material="m";p.FreeRotation=false;p.Quantity=quantity;p.AllowedAnglesDegrees={0};
        p.Geometry.Outer={{0,0},{w,0},{w,h},{0,h}};return p;
    }
    Result Empty(const Problem& p)
    {Result r;r.State=Status::Partial;for(const auto& a:p.Parts)if(a.Quantity)r.Remaining.push_back({a.ID,a.Quantity});return r;}
    bool Has(const Result& r,const char* text)
    {for(const auto& s:r.Diagnostics)if(s.find(text)!=std::string::npos)return true;return false;}
}
int main()try
{
    std::int64_t floor=99;
    Require(Detail::FloorConstraintAffine(0,{{3,1./3}},floor)&&floor==0,"fixed affine product rounded across integer boundary");
    Require(Detail::FloorConstraintAffine(-1,{{3,1./3}},floor)&&floor==-1,"negative fixed affine floor truncated toward zero");
    Require(Detail::FloorConstraintAffine(0,{{3,std::nextafter(1./3,1.)}},floor)&&floor==1,"positive fixed affine boundary failed");
    Require(Detail::FloorConstraintAffine(0,{{1,1e12},{-1,1e12},{3,1./3}},floor)&&floor==0,"fixed affine cancellation lost exact remainder");
    Require(Detail::FloorConstraintAffine(1,{{1,-std::nextafter(1.,2.)}},floor)&&floor==-1,"nextafter fixed-origin boundary failed");
    Require(!Detail::FloorConstraintAffine(0,{{1,std::numeric_limits<double>::denorm_min()}},floor),"unsupported arithmetic silently rounded fixed coordinates");
    Require(Detail::FloorConstraintAffine(0,{{9007199254740992LL,1.},{-9007199254740992LL,1.},{1,-0.}},floor)&&floor==0,"exact large cancellation or negative zero failed");
    Require(!Detail::FloorConstraintAffine(9007199254740993LL,{},floor)&&
        !Detail::FloorConstraintAffine(0,{{9007199254740993LL,1.}},floor),"integer outside exact coefficient scope was rounded");
    Require(Detail::FloorConstraintAffine(9007199254740988LL,{{1,-.25}},floor)&&floor==9007199254740987LL,"positive exact-integer scope edge failed");
    Require(Detail::FloorConstraintAffine(-9007199254740988LL,{{1,.25}},floor)&&floor==-9007199254740988LL,"negative exact-integer scope edge failed");
    const auto defaultIDs=DefaultStrategies().IDs();
    Problem p;p.Tolerance=1e-7;p.Stocks.push_back({"sheet",10,10,1,1,"m"});
    p.Parts={Rectangle("narrow",4,10),Rectangle("wide",6,10)};
    Result blocked;blocked.State=Status::Partial;blocked.Layouts.push_back({"sheet",0,10,10,{{"narrow",0,{3,0,0},0,false}},40});blocked.Remaining.push_back({"wide",1});
    Require(Validate(p,blocked).Valid,"invalid source fixture");
    ConstraintCompletionSettings settings;settings.CoordinateScale=1;settings.Workers=1;settings.MaxConflicts=10000;settings.MaxHintCandidates=128;
    Options options;options.TimeLimitSeconds=5;options.MaxPlacementAttempts=10000;
    auto completed=MakeConstraintCompletionStage(settings).Factory()->Run(p,blocked,options);
    Require(Validate(p,completed).Complete&&completed.GeometryValidated,"did not reconstruct all original demand");
    Require(blocked.Layouts[0].Placements[0].Transform.X==3&&blocked.Remaining.size()==1,"mutated original incumbent");
    Require(completed.Stats.PlacementAttempts<=options.MaxPlacementAttempts,"geometry candidate budget exceeded");
    Require(Has(completed,"solver-conflicts=")&&Has(completed,"coordinate-scale=1"),"missing solver/lattice diagnostics");

    auto insufficient=options;insufficient.MaxPlacementAttempts=1;
    auto notVerified=MakeConstraintCompletionStage(settings).Factory()->Run(p,blocked,insufficient);
    Require(!Validate(p,notVerified).Complete&&notVerified.Layouts[0].Placements[0].Transform.X==3,
        "published completion without budget to verify original placements");
    Require(notVerified.Stats.PlacementAttempts<=1,"verification budget was inflated by solver conflicts");

    auto exact=options;exact.MaxPlacementAttempts=2;
    auto exactBudget=MakeConstraintCompletionStage(settings).Factory()->Run(p,blocked,exact);
    Require(Validate(p,exactBudget).Complete&&exactBudget.Stats.PlacementAttempts==2,"full verification budget was not reserved");

    Problem rotated;rotated.Tolerance=1e-7;rotated.Stocks.push_back({"sheet",2,4,1,1,"m"});
    auto only=Rectangle("only",4,2);only.AllowedAnglesDegrees={90};rotated.Parts={only};
    auto turned=MakeConstraintCompletionStage(settings).Factory()->Run(rotated,Empty(rotated),options);
    Require(Validate(rotated,turned).Complete&&turned.Layouts[0].Placements[0].Transform.AngleDegrees==90,
        "lost original allowed angle or empty-incumbent support");

    Problem triangles;triangles.Tolerance=1e-7;triangles.Stocks.push_back({"sheet",4,3,1,1,"m"});
    auto triangle=Rectangle("triangle",4,3,2);triangle.Geometry.Outer={{0,0},{4,0},{0,3}};triangle.AllowedAnglesDegrees={0,180};triangles.Parts={triangle};
    auto halves=MakeConstraintCompletionStage(settings).Factory()->Run(triangles,Empty(triangles),options);
    Require(Validate(triangles,halves).Complete,"convex material was replaced by an outer rectangle");

    // The left-upper/right-lower pair is the same X-sorted geometry after a
    // whole-layout half turn. Both sorted anchors are above the midline: one
    // cannot prune by that copy's Y coordinate and also keep copy-X ordering.
    Problem duplicate;duplicate.Tolerance=1e-7;duplicate.Stocks.push_back({"sheet",10,8,1,1,"m"});
    auto two=Rectangle("diagonal",4,2,2);two.AllowedAnglesDegrees={0,180};
    auto filler=Rectangle("filler",1,1);filler.AllowedAnglesDegrees={0,180};duplicate.Parts={two,filler};
    Result diagonal;diagonal.State=Status::Partial;diagonal.Layouts.push_back({"sheet",0,10,8,
        {{"diagonal",0,{0,6,0},0,false},{"diagonal",1,{6,0,0},0,false}},16});diagonal.Remaining.push_back({"filler",1});
    std::array<Rect,2> sortedOrbit{{{0,6,4,2},{6,0,4,2}}};auto turnedOrbit=sortedOrbit;
    for(auto& b:turnedOrbit){b.X=10-b.X-b.Width;b.Y=8-b.Y-b.Height;}
    std::sort(turnedOrbit.begin(),turnedOrbit.end(),[](Rect a,Rect b){return a.X<b.X;});
    Require(Validate(duplicate,diagonal).Valid&&2*sortedOrbit[0].Y+sortedOrbit[0].Height>8&&
        2*turnedOrbit[0].Y+turnedOrbit[0].Height>8,"invalid copy/half-turn counterexample");
    auto copySettings=settings;copySettings.CompleteSeparationHints=true;
    auto copyCompletion=MakeConstraintCompletionStage(copySettings).Factory()->Run(duplicate,diagonal,options);
    Require(Validate(duplicate,copyCompletion).Complete&&Has(copyCompletion,"anchor-half-turn=0"),
        "unsafe single-copy half-turn pruning was combined with duplicate-X ordering");
    auto unique=duplicate;unique.Parts[0].Quantity=1;
    auto uniqueCompletion=MakeConstraintCompletionStage(copySettings).Factory()->Run(unique,Empty(unique),options);
    Require(Validate(unique,uniqueCompletion).Complete&&Has(uniqueCompletion,"anchor-half-turn=1"),
        "safe unique-type half-turn symmetry was not retained");

    auto nonconvex=p;nonconvex.Stocks[0].Width=nonconvex.Stocks[0].Height=3;nonconvex.Parts={Rectangle("L",3,3),Rectangle("notch",2,2)};
    nonconvex.Parts[0].Geometry.Outer={{0,0},{3,0},{3,1},{1,1},{1,3},{0,3}};
    auto interlocked=MakeConstraintCompletionStage(copySettings).Factory()->Run(nonconvex,Empty(nonconvex),options);
    Require(Validate(nonconvex,interlocked).Complete&&interlocked.GeometryValidated,
        "shared concave decomposition did not retain original notch material");
    auto shiftedConcave=nonconvex;
    for(auto& point:shiftedConcave.Parts[0].Geometry.Outer){point.X-=7;point.Y+=12;}
    shiftedConcave.Parts[0].AllowedAnglesDegrees={90};
    auto shiftedInterlocked=MakeConstraintCompletionStage(settings).Factory()->Run(shiftedConcave,Empty(shiftedConcave),options);
    Require(Validate(shiftedConcave,shiftedInterlocked).Complete,
        "canonical decomposition origin was confused with rotated source bounds");
    Problem cross;cross.Tolerance=1e-7;cross.Stocks.push_back({"sheet",3,3,1,1,"m"});
    auto center=Rectangle("cross",3,3);center.AllowedAnglesDegrees={0,90,180,270};
    center.Geometry.Outer={{1,0},{2,0},{2,1},{3,1},{3,2},{2,2},{2,3},{1,3},{1,2},{0,2},{0,1},{1,1}};
    cross.Parts={center,Rectangle("corner",1,1,4)};
    auto cornerFill=MakeConstraintCompletionStage(copySettings).Factory()->Run(cross,Empty(cross),options);
    Require(Validate(cross,cornerFill).Complete&&cornerFill.Layouts[0].Placements.size()==5,
        "nonconvex convex-pair separation lost corner cavities or original quantities");
    auto holed=p;holed.Parts={Rectangle("holed",3,3)};
    holed.Parts[0].Geometry.Holes={{{1,1},{1,2},{2,2},{2,1}}};
    auto unsupported=MakeConstraintCompletionStage(settings).Factory()->Run(holed,Empty(holed),options);
    Require(Validate(holed,unsupported).Valid&&!Validate(holed,unsupported).Complete&&Has(unsupported,"unsupported"),
        "unsupported holed source silently discarded its holes");
    auto fractional=p;fractional.Parts={Rectangle("fraction",1,1./3)};
    auto gridRejected=MakeConstraintCompletionStage(settings).Factory()->Run(fractional,Empty(fractional),options);
    Require(!Validate(fractional,gridRejected).Complete&&Has(gridRejected,"lossless"),"rounded original geometry to the solver grid");
    auto gap=p;gap.PartGap=.1;
    auto gapRejected=MakeConstraintCompletionStage(settings).Factory()->Run(gap,blocked,options);
    Require(gapRejected.Layouts[0].Placements[0].Transform.X==3&&Has(gapRejected,"unsupported"),"unsupported gap changed source placements");

    auto noTime=options;noTime.TimeLimitSeconds=0;
    auto timed=MakeConstraintCompletionStage(settings).Factory()->Run(p,blocked,noTime);
    Require(timed.State==Status::Partial&&!Validate(p,timed).Complete,"time exhaustion was labelled user cancellation");
    auto cancelled=options;cancelled.Cancel=[] {return true;};
    auto stopped=MakeConstraintCompletionStage(settings).Factory()->Run(p,blocked,cancelled);
    Require(stopped.State==Status::Cancelled&&stopped.Layouts[0].Placements[0].Transform.X==3,"entry cancellation changed original geometry");
    std::size_t buildPolls=0;cancelled.Cancel=[&]{return ++buildPolls>5;};
    auto buildStopped=MakeConstraintCompletionStage(settings).Factory()->Run(p,blocked,cancelled);
    Require(buildStopped.State==Status::Cancelled&&Has(buildStopped,"solver-started=0"),"model construction ignored cancellation");

    Problem many;many.Tolerance=1e-7;many.Stocks.push_back({"sheet",10,10,1,1,"m"});
    for(int i=0;i<24;++i)many.Parts.push_back(Rectangle("copy"+std::to_string(i),2,2));
    auto monitorSettings=settings;monitorSettings.MaxHintCandidates=0;
    const auto caller=std::this_thread::get_id();std::atomic<bool> monitorPolled{false};
    cancelled.Cancel=[&]{if(std::this_thread::get_id()!=caller){monitorPolled.store(true);return true;}return false;};
    auto solvingStopped=MakeConstraintCompletionStage(monitorSettings).Factory()->Run(many,Empty(many),cancelled);
    Require(monitorPolled.load()&&solvingStopped.State==Status::Cancelled&&Has(solvingStopped,"solver-started=1"),
        "solver external atomic cancellation bridge was not polled");
    Require(DefaultStrategies().IDs()==defaultIDs,"optional constraint target changed the default registry");
    std::cout<<"Constraint completion: original material/quantity/angles, budget, unsupported scope and cancellation passed\n";
}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
