#include "SheetNestingPlaneStrategies.h"
#include "NestingAxisSafety.h"
#include <iostream>
#include <set>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Problem Diagonal(double width,double height)
{
    Problem problem;Part part;part.ID="diagonal";part.Quantity=4;part.FreeRotation=false;
    part.AllowedAnglesDegrees={0};part.Geometry.Outer={{0,0},{1,0},{6,5},{5,5}};
    problem.Parts={part};problem.Stocks={{"sheet",width,height,1}};return problem;
}
Options Limits()
{
    Options options;options.TimeLimitSeconds=5;options.MaxPlacementAttempts=500000;
    options.EnableCombinations=false;options.MaxPatternMembers=1;options.EnableGapFill=false;
    options.EnableBasicCompaction=false;return options;
}
Result Run(const Problem& problem,Options options=Limits(),PlaneStrategyKind kind=PlaneStrategyKind::NfpArray)
{
    PlaneStrategySettings settings;settings.Kind=kind;
    auto output=MakePlaneStrategyFactory(settings)()->Run(problem,options);
    Require(Validate(problem,output).Valid,"pitch candidate failed original-material validation");
    Require(output.Stats.PlacementAttempts<=options.MaxPlacementAttempts,"periodic preparation exceeded the shared attempt cap");return output;
}
void StrictPairs(const Problem& problem,const Result& result)
{
    const auto& shape=problem.Parts.front().Geometry;
    for(const auto& board:result.Layouts)for(std::size_t i=0;i<board.Placements.size();++i)
    {
        const auto a=AxisSafety::Make(shape,board.Placements[i].Transform);
        Require(AxisSafety::Containment(a,board.Width,board.Height,problem.EdgeMargin)==AxisSafety::Verdict::Safe,
            "periodic expansion violated the nominal board margin");
        for(std::size_t j=0;j<i;++j)
            Require(AxisSafety::Pair(a,AxisSafety::Make(shape,board.Placements[j].Transform),problem.PartGap,false,problem.Tolerance)==AxisSafety::Verdict::Safe,
                "periodic expansion missed a non-adjacent or diagonal neighbour");
    }
}
}
int main()
{
    try
    {
        // A narrow diagonal parallelogram has coupled alternatives (1,5) and
        // (5,1). Compressing them to a single minimum-product pitch loses the
        // layout for one of these stock aspect ratios.
        auto wide=Diagonal(9,5),tall=Diagonal(6,8);
        auto rows=Run(wide),columns=Run(tall);
        Require(rows.State==Status::Complete&&columns.State==Status::Complete,
            "both directional periodic alternatives must survive until board fitting");
        Require(rows.Layouts.size()==1&&columns.Layouts.size()==1,"directional pitches should keep all four originals on one board");
        std::set<double> rowX,rowY,columnX,columnY;
        for(const auto& p:rows.Layouts[0].Placements){rowX.insert(p.Transform.X);rowY.insert(p.Transform.Y);}
        for(const auto& p:columns.Layouts[0].Placements){columnX.insert(p.Transform.X);columnY.insert(p.Transform.Y);}
        Require(rowX.size()==4&&rowY.size()==1&&columnX.size()==1&&columnY.size()==4,
            "stock shape must choose horizontal or vertical periodic expansion");
        StrictPairs(wide,rows);StrictPairs(tall,columns);

        auto positive=Diagonal(6.5,9.5);positive.PartGap=.2;positive.EdgeMargin=.25;
        auto cleared=Run(positive);
        Require(cleared.State==Status::Complete,"positive-gap vertical alternative should fit the narrow stock");
        StrictPairs(positive,cleared);

        auto once=Diagonal(6,5);once.Parts[0].Quantity=1;auto oneAttempt=Limits();oneAttempt.MaxPlacementAttempts=1;
        auto single=Run(once,oneAttempt);
        Require(single.State==Status::Complete&&single.Stats.PlacementAttempts==1,
            "one-off templates must not spend the only attempt on periodic neighbours");

        Problem circle;Part disk;disk.ID="disk";disk.Quantity=2;disk.FreeRotation=false;disk.AllowedAnglesDegrees={0};
        disk.Geometry.Radius=1;circle.Parts={disk};circle.Stocks={{"sheet",5,3,1}};
        auto circular=Run(circle);
        Require(Validate(circle,circular).Complete&&circular.Layouts.size()==1,
            "analytic circles must retain original material while using conservative safe periodic proposals");
        Require(circle.Parts[0].Geometry.Outer.empty()&&circle.Parts[0].Geometry.Radius==1,
            "circle proposal geometry replaced the original analytic contour");
        auto ordinary=Run(circle,Limits(),PlaneStrategyKind::ArrayQuantityDeferred);
        Require(ordinary.State==Status::Complete,"ordinary non-NFP template fallback must remain available");
        std::cout<<"PlaneArrayPitchTests passed: directional pitches, original periodic clearance, one-off budget and conservative analytic-circle proposals\n";
        return 0;
    }
    catch(const std::exception& error){std::cerr<<"PlaneArrayPitchTests failed: "<<error.what()<<'\n';return 1;}
}
