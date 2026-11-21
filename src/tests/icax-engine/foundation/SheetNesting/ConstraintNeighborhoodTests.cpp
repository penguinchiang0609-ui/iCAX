#include "SheetNestingConstraint.h"
#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
    Part Rectangle(std::string id,double width,double height)
    {Part part;part.ID=std::move(id);part.Material="m";part.Quantity=1;part.FreeRotation=false;part.AllowedAnglesDegrees={0};
        part.Geometry.Outer={{0,0},{width,0},{width,height},{0,height}};return part;}
    bool Has(const Result& result,const char* text)
    {for(const auto& message:result.Diagnostics)if(message.find(text)!=std::string::npos)return true;return false;}
    const Placement& PlacementOf(const Result& result,const char* id)
    {for(const auto& placement:result.Layouts.at(0).Placements)if(placement.PartID==id)return placement;throw std::runtime_error("missing original placement");}
}
int main()try
{
    const auto registryBefore=DefaultStrategies().IDs();
    Problem problem;problem.Tolerance=1e-7;problem.Stocks.push_back({"sheet",10,10,1,1,"m"});
    problem.Parts={Rectangle("fixed",10,1),Rectangle("narrow",4,8),Rectangle("wide",6,8)};
    Result partial;partial.State=Status::Partial;partial.Layouts.push_back({"sheet",0,10,10,
        {{"fixed",0,{0,1./3,0},7,false},{"narrow",0,{3,1.5,0},0,false}},42});partial.Remaining={{"wide",1}};
    Require(Validate(problem,partial).Valid,"invalid off-grid fixed-obstacle fixture");
    ConstraintNeighborhoodSettings settings;settings.Solver.CoordinateScale=2;settings.Solver.Workers=1;
    settings.Solver.CompleteSeparationHints=true;settings.Solver.MaxHintCandidates=256;settings.MaxMovableInstances=2;settings.MaxPlans=2;
    Options options;options.TimeLimitSeconds=5;options.MaxPlacementAttempts=10000;
    auto complete=MakeConstraintNeighborhoodStage(settings).Factory()->Run(problem,partial,options);
    Require(Validate(problem,complete).Complete&&complete.GeometryValidated,"local constraint stage did not reconstruct original demand");
    const auto& fixed=PlacementOf(complete,"fixed");const auto& before=PlacementOf(partial,"fixed");
    Require(std::bit_cast<std::uint64_t>(fixed.Transform.X)==std::bit_cast<std::uint64_t>(before.Transform.X)&&
        std::bit_cast<std::uint64_t>(fixed.Transform.Y)==std::bit_cast<std::uint64_t>(before.Transform.Y)&&
        std::bit_cast<std::uint64_t>(fixed.Transform.AngleDegrees)==std::bit_cast<std::uint64_t>(before.Transform.AngleDegrees)&&fixed.Group==7,
        "fixed original double pose or planning tag was changed or snapped to a grid");
    Require(Has(complete,"fixed-originals=1")&&Has(complete,"solver-started=1"),"fixture bypassed the actual fixed-obstacle solver path");
    Require(PlacementOf(partial,"narrow").Transform.X==3&&partial.Remaining.size()==1,"neighborhood stage mutated incumbent input");
    Require(complete.Stats.PlacementAttempts<=options.MaxPlacementAttempts,"neighborhood placement budget exceeded");
    auto wideScope=settings;wideScope.MaxMovableInstances=10;
    auto wideComplete=MakeConstraintNeighborhoodStage(wideScope).Factory()->Run(problem,partial,options);
    Require(Validate(problem,wideComplete).Complete&&Has(wideComplete,"solver-started=1"),"larger movable scope exceeded the NFP blocker-query limit");

    Problem quarterProblem;quarterProblem.Tolerance=1e-7;quarterProblem.Stocks.push_back({"sheet",10,3,1,1,"m"});
    auto offOrigin=Rectangle("fixed",4,1);offOrigin.AllowedAnglesDegrees={0,180};
    quarterProblem.Parts={offOrigin,Rectangle("narrow",4,1.5),Rectangle("wide",6,1.5)};
    Result quarterPartial;quarterPartial.State=Status::Partial;quarterPartial.Layouts.push_back({"sheet",0,10,3,
        {{"fixed",0,{6,1+1./3,180},11,false},{"narrow",0,{.5,1.5,0},0,false}},10});quarterPartial.Remaining={{"wide",1}};
    Require(Validate(quarterProblem,quarterPartial).Valid,"invalid off-origin half-turn fixture");
    auto quarterComplete=MakeConstraintNeighborhoodStage(settings).Factory()->Run(quarterProblem,quarterPartial,options);
    Require(Validate(quarterProblem,quarterComplete).Complete&&PlacementOf(quarterComplete,"fixed").Transform.AngleDegrees==180&&
        PlacementOf(quarterComplete,"fixed").Transform.X==6&&PlacementOf(quarterComplete,"fixed").Transform.Y==1+1./3,
        "normalized direction deduplication lost the fixed original quarter bounds");
    auto turnedProblem=quarterProblem;turnedProblem.Stocks[0].Width=3;turnedProblem.Stocks[0].Height=10;
    for(auto& part:turnedProblem.Parts)for(auto& angle:part.AllowedAnglesDegrees)angle+=90;
    auto turnedPartial=quarterPartial;turnedPartial.Layouts[0].Width=3;turnedPartial.Layouts[0].Height=10;
    for(auto& placed:turnedPartial.Layouts[0].Placements)
    {const auto old=placed.Transform;placed.Transform={3-old.Y,old.X,old.AngleDegrees+90};}
    auto turnedComplete=MakeConstraintNeighborhoodStage(settings).Factory()->Run(turnedProblem,turnedPartial,options);
    Require(Validate(turnedProblem,turnedComplete).Complete&&PlacementOf(turnedComplete,"fixed").Transform.AngleDegrees==270&&
        PlacementOf(turnedComplete,"fixed").Transform.X==3-(1+1./3),"fixed quarter-turn source bounds changed under whole-layout rotation");

    Problem concave;concave.Tolerance=1e-7;concave.Stocks.push_back({"sheet",4,4,1,1,"m"});
    auto l=Rectangle("fixed",4,4);l.Geometry.Outer={{0,0},{4,0},{4,1},{1,1},{1,4},{0,4}};
    concave.Parts={l,Rectangle("narrow",1.5,3),Rectangle("wide",1.5,3)};
    Result concavePartial;concavePartial.State=Status::Partial;concavePartial.Layouts.push_back({"sheet",0,4,4,
        {{"fixed",0,{0,0,0},0,false},{"narrow",0,{1.1,1,0},0,false}},11.5});concavePartial.Remaining={{"wide",1}};
    auto concaveComplete=MakeConstraintNeighborhoodStage(settings).Factory()->Run(concave,concavePartial,options);
    Require(Validate(concave,concaveComplete).Complete&&Has(concaveComplete,"solver-started=1")&&Has(concaveComplete,"fixed-originals=1"),
        "fixed nonconvex material was replaced by its hull or lost a convex-pair constraint");

    // A fully occupied board makes every conditional plan infeasible, so both
    // expansion strategies are exercised past their identical blocker seed.
    // The actual contact frontier contains an earlier large neighbor and a
    // later small neighbor. Material order must choose the smaller original.
    Problem order;order.Tolerance=1e-7;order.Stocks.push_back({"sheet",4,4,1,1,"m"});
    order.Parts={Rectangle("large",2,2),Rectangle("anchor",2,2),Rectangle("fill",2,1),Rectangle("top",4,1),
        Rectangle("other",1,1),Rectangle("small",1,1),Rectangle("missing",1,1)};
    Result orderPartial;orderPartial.State=Status::Partial;orderPartial.Layouts.push_back({"sheet",0,4,4,
        {{"large",0,{2,0,0},0,false},{"anchor",0,{0,0,0},0,false},{"fill",0,{2,2,0},0,false},{"top",0,{0,3,0},0,false},
         {"other",0,{1,2,0},0,false},{"small",0,{0,2,0},0,false}},16});orderPartial.Remaining={{"missing",1}};
    Require(Validate(order,orderPartial).Valid,"invalid contact expansion order fixture");
    auto layoutOrder=settings;layoutOrder.MaxMovableInstances=3;layoutOrder.MaxPlans=2;
    auto oldOrder=MakeConstraintNeighborhoodStage(layoutOrder).Factory()->Run(order,orderPartial,options);
    auto materialOrder=layoutOrder;materialOrder.ContactOrder=ConstraintContactOrder::SmallMaterialContact;
    auto smallOrder=MakeConstraintNeighborhoodStage(materialOrder).Factory()->Run(order,orderPartial,options);
    Require(Validate(order,oldOrder).Valid&&Validate(order,smallOrder).Valid&&!Validate(order,oldOrder).Complete&&!Validate(order,smallOrder).Complete,
        "contact order fixture published an overlap or lost demanded quantities");
    Require(Has(oldOrder,"released=large#0,anchor#0,")&&Has(oldOrder,"contact-order=layout"),"layout contact order was changed by the new strategy");
    Require(Has(smallOrder,"released=anchor#0,small#0,")&&Has(smallOrder,"contact-order=small-material"),
        "small-material order did not choose the least-material actual contact neighbor");

    auto arithmeticPartial=partial;arithmeticPartial.Layouts[0].Placements[0].Transform.Y=std::numeric_limits<double>::denorm_min();
    auto unsupported=MakeConstraintNeighborhoodStage(settings).Factory()->Run(problem,arithmeticPartial,options);
    Require(Validate(problem,unsupported).Valid&&!Validate(problem,unsupported).Complete&&Has(unsupported,"exact fixed right-hand-side"),
        "unsupported fixed-origin arithmetic silently rounded the obstacle");
    Require(PlacementOf(unsupported,"fixed").Transform.Y==std::numeric_limits<double>::denorm_min(),"failed arithmetic path changed the fixed original");

    auto insufficient=options;insufficient.MaxPlacementAttempts=2;
    auto limited=MakeConstraintNeighborhoodStage(settings).Factory()->Run(problem,partial,insufficient);
    Require(!Validate(problem,limited).Complete&&limited.Stats.PlacementAttempts<=2,"local completion escaped its verification budget");
    auto cancelled=options;cancelled.Cancel=[]{return true;};
    auto stopped=MakeConstraintNeighborhoodStage(settings).Factory()->Run(problem,partial,cancelled);
    Require(stopped.State==Status::Cancelled&&PlacementOf(stopped,"narrow").Transform.X==3,"local cancellation changed input geometry");
    auto timed=options;timed.TimeLimitSeconds=0;
    auto exhausted=MakeConstraintNeighborhoodStage(settings).Factory()->Run(problem,partial,timed);
    Require(exhausted.State==Status::Partial&&!Validate(problem,exhausted).Complete,"local time exhaustion was labelled cancellation or completion");
    Require(DefaultStrategies().IDs()==registryBefore,"optional neighborhood stage changed the default registry");
    std::cout<<"Constraint neighborhood: original fixed double poses, partial-derived blockers, quantity, budgets and cancellation passed\n";
}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
