#include "SheetNestingPlaneStrategies.h"
#include "NestingAxisSafety.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value,const char* message)
{if(!value)throw std::runtime_error(message);}

Problem ParallelSlopes(std::size_t quantity=1)
{
    Problem problem;problem.PartGap=.2;problem.EdgeMargin=.5;
    Part lower;lower.ID="lower-local-17";lower.Quantity=quantity;lower.FreeRotation=false;lower.AllowedAnglesDegrees={0};
    lower.Geometry.Outer={{0,0},{6,0},{6,4},{0,2}};
    Part upper;upper.ID="upper-local-93";upper.Quantity=quantity;upper.FreeRotation=false;upper.AllowedAnglesDegrees={0};
    upper.Geometry.Outer={{0,0},{6,2},{6,8},{0,8}};
    problem.Parts={lower,upper};problem.Stocks={{"narrow-stock",8,15,2*quantity}};return problem;
}
Options Limits()
{
    Options options;options.TimeLimitSeconds=10;options.MaxPlacementAttempts=200000;
    options.EnableCombinations=false;options.MaxPatternMembers=1;
    options.EnableGapFill=false;options.EnableBasicCompaction=false;return options;
}
Result Construct(const Problem& problem,PlaneStrategyKind kind,bool settle,Options options=Limits())
{
    PlaneStrategySettings settings;settings.Kind=kind;settings.MaxNodes=1;settings.EnableContourSettling=settle;
    auto output=MakePlaneStrategyFactory(settings)()->Run(problem,options);
    Require(Validate(problem,output).Valid,"contour settling must preserve original geometry and remaining demand");
    Require(output.Stats.PlacementAttempts<=options.MaxPlacementAttempts,"contour settling escaped the constructor attempt cap");
    return output;
}
using Identity=std::pair<std::string,std::size_t>;
using Provenance=std::tuple<std::string,std::size_t,double,std::size_t,bool>;
std::map<Identity,Provenance> Identities(const Result& result)
{
    std::map<Identity,Provenance> identities;
    for(const auto& board:result.Layouts)for(const auto& item:board.Placements)
        Require(identities.emplace(Identity{item.PartID,item.Instance},Provenance{board.StockID,board.StockInstance,
            item.Transform.AngleDegrees,item.Group,item.GapFiller}).second,"an original identity was duplicated");
    return identities;
}
const Placement& Member(const Layout& board,const char* id)
{
    const auto found=std::find_if(board.Placements.begin(),board.Placements.end(),[&](const Placement& item){return item.PartID==id;});
    Require(found!=board.Placements.end(),"missing parallel-slope original");return *found;
}
double Top(const Problem& problem,const Layout& board)
{
    double top=0;
    for(const auto& item:board.Placements)
    {
        const auto part=std::find_if(problem.Parts.begin(),problem.Parts.end(),[&](const Part& value){return value.ID==item.PartID;});
        const auto box=Detail::Bounds(part->Geometry,item.Transform);top=std::max(top,box.Y+box.Height);
    }
    return top;
}
void Nominal(const Problem& problem,const Result& output)
{
    std::map<std::string,const Shape*> shapes;
    for(const auto& part:problem.Parts)shapes.emplace(part.ID,&part.Geometry);
    for(const auto& board:output.Layouts)for(std::size_t i=0;i<board.Placements.size();++i)
    {
        const auto& item=board.Placements[i];const auto geometry=AxisSafety::Make(*shapes.at(item.PartID),item.Transform);
        Require(AxisSafety::Containment(geometry,board.Width,board.Height,problem.EdgeMargin)==AxisSafety::Verdict::Safe,
            "settling spent the nominal sheet margin");
        for(std::size_t j=0;j<i;++j)
        {
            const auto& other=board.Placements[j];
            Require(AxisSafety::Pair(geometry,AxisSafety::Make(*shapes.at(other.PartID),other.Transform),problem.PartGap,
                problem.AllowCommonEdges,problem.Tolerance)==AxisSafety::Verdict::Safe,
                "settling spent the nominal original-material gap");
        }
    }
}
}

int main()try
{
    auto disabled=Limits();disabled.MaxPatternMembers=4;
    const auto withoutCombinations=Construct(ParallelSlopes(),PlaneStrategyKind::NfpArray,false,disabled);
    Require(withoutCombinations.Layouts.size()==1&&std::abs(Top(ParallelSlopes(),withoutCombinations.Layouts[0])-12.7)<1e-8,
        "disabling combinations must retain single NFP templates even when deeper combinations are allowed");
    for(auto kind:{PlaneStrategyKind::ArrayMultiSpecificationSearch,PlaneStrategyKind::NfpArray,PlaneStrategyKind::NfpMultiArray})
    {
        const auto problem=ParallelSlopes();
        const auto baseline=Construct(problem,kind,false),settled=Construct(problem,kind,true);
        Require(Validate(problem,baseline).Complete&&Validate(problem,settled).Complete&&
            baseline.Layouts.size()==1&&settled.Layouts.size()==1,"parallel-slope originals should stay on their one physical sheet");
        Require(Identities(baseline)==Identities(settled),"settling changed an original angle, sheet, group or filler provenance");
        Nominal(problem,baseline);Nominal(problem,settled);
        const auto& lower=Member(settled.Layouts[0],"lower-local-17");
        const auto& upper=Member(settled.Layouts[0],"upper-local-93");
        const auto beforeTop=Top(problem,baseline.Layouts[0]),afterTop=Top(problem,settled.Layouts[0]);
        Require(std::abs(beforeTop-12.7)<1e-8,"baseline must expose the rectangular stacking void");
        // The facing edges have slope 1/3. This analytic normal distance uses
        // the actual stored poses and never subtracts the native tolerance.
        const long double slope=1.L/3;
        const long double vertical=static_cast<long double>(upper.Transform.Y)-lower.Transform.Y-2+
            (static_cast<long double>(lower.Transform.X)-upper.Transform.X)*slope;
        const long double normal=vertical/std::sqrt(1+slope*slope);
        Require(normal>=static_cast<long double>(problem.PartGap),"sloped contact lost the nominal normal gap");
        const double expectedTop=10.5+problem.PartGap*std::sqrt(1+1./9);
        Require(beforeTop-afterTop>1.98&&std::abs(afterTop-expectedTop)<1e-5,
            "contour settling must lower the skyline to the real sloped contact instead of the lower AABB top");
    }

    const auto multi=ParallelSlopes(2);
    const auto multiBefore=Construct(multi,PlaneStrategyKind::ArrayMultiSpecificationSearch,false);
    const auto multiAfter=Construct(multi,PlaneStrategyKind::ArrayMultiSpecificationSearch,true);
    Require(Validate(multi,multiBefore).Complete&&Validate(multi,multiAfter).Complete&&multiAfter.Layouts.size()>1,
        "multisheet settling must retain the complete original inventory");
    Require(Identities(multiBefore)==Identities(multiAfter),"single-sheet local settling duplicated or renumbered sparse global identities");
    bool sparse=false;
    for(const auto& board:multiAfter.Layouts)
    {
        std::map<std::string,std::set<std::size_t>> ids;
        for(const auto& item:board.Placements)ids[item.PartID].insert(item.Instance);
        for(const auto& [id,values]:ids){(void)id;if(*values.begin()>0)sparse=true;}
    }
    Require(sparse,"multisheet fixture must exercise a local board whose original IDs do not start at zero");
    Nominal(multi,multiAfter);

    for(auto kind:{PlaneStrategyKind::ArrayMultiSpecificationSearch,PlaneStrategyKind::NfpArray,PlaneStrategyKind::NfpMultiArray})
    {
        auto cancelled=Limits();cancelled.Cancel=[]{return true;};
        const auto stopped=Construct(multi,kind,true,cancelled);
        Require(stopped.State==Status::Cancelled&&stopped.Layouts.empty()&&stopped.Remaining.size()==multi.Parts.size(),
            "cancelled contour construction must retain all original demand");
        for(std::size_t cap:{1u,30u,200u})
        {
            auto limited=Limits();limited.MaxPlacementAttempts=cap;
            const auto bounded=Construct(multi,kind,true,limited);
            Require(bounded.Stats.PlacementAttempts<=cap,"local contour settling overran its shared budget");
        }
    }
    std::cout<<"PlaneSkylineTests passed: true sloped contact, lower skyline, nominal clearance, original inventory and bounded work\n";
    return 0;
}
catch(const std::exception& error){std::cerr<<"PlaneSkylineTests failed: "<<error.what()<<'\n';return 1;}
