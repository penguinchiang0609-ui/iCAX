#include "SheetNestingPlaneStrategies.h"
#include "SheetNestingRotationCompaction.h"
#include "SheetNestingAxisCompaction.h"
#include "NestingAxisSafety.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
#include <tuple>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value,const char* message)
{if(!value)throw std::runtime_error(message);}
Problem ReversibleSlopes()
{
    Problem problem;problem.PartGap=.2;problem.EdgeMargin=.5;
    Part lower;lower.ID="fixed-slope-17";lower.FreeRotation=false;lower.AllowedAnglesDegrees={0};
    lower.Geometry.Outer={{0,0},{6,0},{6,2},{0,4}};
    Part upper;upper.ID="turnable-slope-93";upper.FreeRotation=false;upper.AllowedAnglesDegrees={0,180};
    upper.Geometry.Outer={{0,8},{0,0},{6,0},{6,6}};
    problem.Parts={lower,upper};problem.Stocks={{"slope-stock",8,15,1}};return problem;
}
Options Limits()
{
    Options options;options.TimeLimitSeconds=10;options.MaxPlacementAttempts=200000;
    options.EnableCombinations=false;options.MaxPatternMembers=1;
    options.EnableGapFill=false;options.EnableBasicCompaction=false;return options;
}
Result Construct(const Problem& problem,PlaneStrategyKind kind,bool angles,Options options=Limits())
{
    PlaneStrategySettings settings;settings.Kind=kind;settings.MaxNodes=1;
    settings.EnableContourSettling=true;settings.EnableAllowedAngleSettling=angles;
    auto result=MakePlaneStrategyFactory(settings)()->Run(problem,options);
    Require(Validate(problem,result).Valid,"allowed-angle settling must retain valid original geometry and demand");
    Require(result.Stats.PlacementAttempts<=options.MaxPlacementAttempts,"allowed-angle settling exceeded the shared attempt cap");
    return result;
}
using Identity=std::pair<std::string,std::size_t>;
using Provenance=std::tuple<std::string,std::size_t,std::size_t,bool>;
std::map<Identity,Provenance> Identities(const Result& result)
{
    std::map<Identity,Provenance> identities;
    for(const auto& board:result.Layouts)for(const auto& item:board.Placements)
        Require(identities.emplace(Identity{item.PartID,item.Instance},Provenance{board.StockID,board.StockInstance,
            item.Group,item.GapFiller}).second,"angle settling duplicated an original identity");
    return identities;
}
const Placement& Member(const Result& result,const char* id)
{
    for(const auto& board:result.Layouts)for(const auto& item:board.Placements)if(item.PartID==id)return item;
    throw std::runtime_error("missing angle-settling fixture original");
}
double Top(const Problem& problem,const Result& result)
{
    double value=0;
    for(const auto& board:result.Layouts)for(const auto& item:board.Placements)
    {
        const auto found=std::find_if(problem.Parts.begin(),problem.Parts.end(),[&](const Part& part){return part.ID==item.PartID;});
        const auto box=Detail::Bounds(found->Geometry,item.Transform);value=std::max(value,box.Y+box.Height);
    }
    return value;
}
void Nominal(const Problem& problem,const Result& result)
{
    std::map<std::string,const Part*> parts;
    for(const auto& part:problem.Parts)parts.emplace(part.ID,&part);
    for(const auto& board:result.Layouts)for(std::size_t i=0;i<board.Placements.size();++i)
    {
        const auto& item=board.Placements[i];const auto& part=*parts.at(item.PartID);
        Require(Detail::AngleAllowed(part,item.Transform.AngleDegrees,problem.Tolerance),"settling widened a finite original angle domain");
        const auto shape=AxisSafety::Make(part.Geometry,item.Transform);
        Require(AxisSafety::Containment(shape,board.Width,board.Height,problem.EdgeMargin)==AxisSafety::Verdict::Safe,
            "angle settling spent the nominal edge margin");
        for(std::size_t j=0;j<i;++j)
        {
            const auto& other=board.Placements[j];
            Require(AxisSafety::Pair(shape,AxisSafety::Make(parts.at(other.PartID)->Geometry,other.Transform),problem.PartGap,
                problem.AllowCommonEdges,problem.Tolerance)==AxisSafety::Verdict::Safe,
                "angle settling spent the nominal original-material gap");
        }
    }
}
}

int main()try
{
    for(auto kind:{PlaneStrategyKind::ArrayMultiSpecificationSearch,PlaneStrategyKind::NfpArray,PlaneStrategyKind::NfpMultiArray})
    {
        const auto problem=ReversibleSlopes();
        const auto fixed=Construct(problem,kind,false),turned=Construct(problem,kind,true);
        Require(Validate(problem,fixed).Complete&&Validate(problem,turned).Complete&&fixed.Layouts.size()==1&&turned.Layouts.size()==1,
            "turning an allowed original must retain the same complete one-sheet inventory");
        Require(Identities(fixed)==Identities(turned),"allowed rotation changed original identity, sheet, group or filler provenance");
        const auto& oldUpper=Member(fixed,"turnable-slope-93");
        const auto& upper=Member(turned,"turnable-slope-93");
        const auto& lower=Member(turned,"fixed-slope-17");
        Require(Detail::AngleAllowed(problem.Parts[1],oldUpper.Transform.AngleDegrees,problem.Tolerance)&&Top(problem,fixed)<=12.7+1e-8,
            "initial construction lost a permitted orientation or exceeded the known feasible skyline");
        Require(Detail::AngleAllowed(problem.Parts[1],upper.Transform.AngleDegrees,problem.Tolerance)&&lower.Transform.AngleDegrees==0,
            "optional settling left the allowed orientation domain or rotated a fixed original");
        std::cout<<"angle-construction kind="<<int(kind)<<" fixedTop="<<Top(problem,fixed)
            <<" turnedTop="<<Top(problem,turned)<<" fixedAngle="<<oldUpper.Transform.AngleDegrees
            <<" turnedAngle="<<upper.Transform.AngleDegrees<<'\n';
        Require(Top(problem,turned)<=Top(problem,fixed)+1e-8,
            "optional legal rotation worsened the same constructor's initial skyline");
        // After the exact half-turn, original (6,6) is the upper piece's
        // lower-left sloped vertex. Compare the actual stored binary64 poses
        // with the fixed lower edge, without a native-tolerance discount.
        if(std::abs(std::remainder(upper.Transform.AngleDegrees-180.,360.))<1e-10)
        {
            const long double x=static_cast<long double>(upper.Transform.X)-6;
            const long double y=static_cast<long double>(upper.Transform.Y)-6;
            const long double floor=static_cast<long double>(lower.Transform.Y)+4-
                (x-lower.Transform.X)/3;
            const long double distance=(y-floor)/std::sqrt(1+1.L/9);
            Require(distance>=static_cast<long double>(problem.PartGap),"half-turn settling lost the strict normal clearance");
        }
        Nominal(problem,fixed);Nominal(problem,turned);

        auto locked=problem;locked.Parts[1].AllowedAnglesDegrees={0};
        const auto onlyZero=Construct(locked,kind,true);
        Require(Validate(locked,onlyZero).Complete&&Member(onlyZero,"turnable-slope-93").Transform.AngleDegrees==0&&
            Top(locked,onlyZero)<=12.7+1e-8,"a singleton finite domain must not be widened to improve the skyline");

        auto free=problem;free.Parts[1].FreeRotation=true;free.Parts[1].AllowedAnglesDegrees.clear();
        const auto freeFixed=Construct(free,kind,false),freeInitial=Construct(free,kind,true);
        Require(Validate(free,freeFixed).Complete&&Validate(free,freeInitial).Complete&&Identities(freeFixed)==Identities(freeInitial),
            "finite-only initial settling changed the free-domain inventory");
        Require(Member(freeFixed,"turnable-slope-93").Transform.AngleDegrees==Member(freeInitial,"turnable-slope-93").Transform.AngleDegrees,
            "initial finite-only settling must not add a continuous free-angle search");
        Nominal(free,freeInitial);

        auto cancelled=Limits();cancelled.Cancel=[]{return true;};
        const auto stopped=Construct(problem,kind,true,cancelled);
        Require(stopped.State==Status::Cancelled&&stopped.Layouts.empty()&&stopped.Remaining.size()==problem.Parts.size(),
            "cancelled allowed-angle construction must preserve every original demand");
        for(std::size_t cap:{1u,30u,200u})
        {
            auto limited=Limits();limited.MaxPlacementAttempts=cap;
            const auto bounded=Construct(problem,kind,true,limited);
            Require(bounded.Stats.PlacementAttempts<=cap,"rotation and following axis work escaped the shared construction budget");
        }
    }
    // Keep the half-turn stage regression independent of how strong the
    // constructor has become: supply the known loose original layout directly.
    const auto reversible=ReversibleSlopes();
    Result loose;loose.State=Status::Complete;
    loose.Layouts={{"slope-stock",0,8,15,{{"fixed-slope-17",0,{.5,.5,0},41,false},
        {"turnable-slope-93",0,{.5,4.7,0},72,false}},60}};
    Require(Validate(reversible,loose).Complete,"explicit half-turn source must be complete and legal");
    RotationCompactionSettings halfTurn;halfTurn.MaxPasses=1;halfTurn.MaxAnglesPerPart=8;
    halfTurn.MaxPivotsPerPart=2;halfTurn.FiniteAnglesOnly=true;
    auto rotated=MakeRotationCompactionStage(halfTurn).Factory()->Run(reversible,loose,Limits());
    AxisCompactionSettings settle;settle.MaxPasses=4;settle.MaxCandidatesPerMove=256;settle.MaxContactEventsPerMove=2048;
    rotated=MakeAxisCompactionStage(settle).Factory()->Run(reversible,rotated,Limits());
    Require(Validate(reversible,rotated).Complete&&Identities(loose)==Identities(rotated)&&
        std::abs(std::remainder(Member(rotated,"turnable-slope-93").Transform.AngleDegrees-180.,360.))<1e-10&&
        Top(reversible,loose)-Top(reversible,rotated)>1.98,
        "explicit finite half-turn and contour settling lost the known compact original layout");
    Nominal(reversible,rotated);
    // Exercise the finite-only stage while another original really has a
    // finite alternative. A board containing only a free part could otherwise
    // skip the entire stage in the constructor before testing this guard.
    auto mixed=ReversibleSlopes();mixed.Parts[0].AllowedAnglesDegrees={0,180};
    mixed.Parts[1].FreeRotation=true;mixed.Parts[1].AllowedAnglesDegrees.clear();
    Result source;source.State=Status::Complete;
    source.Layouts={{"slope-stock",0,8,15,{{"fixed-slope-17",0,{.5,.5,0},41,false},
        {"turnable-slope-93",0,{.5,4.7,0},72,false}},60}};
    Require(Validate(mixed,source).Complete,"mixed finite/free stage fixture must retain valid original geometry");
    RotationCompactionSettings finite;finite.MaxPasses=1;finite.MaxAnglesPerPart=8;
    finite.MaxPivotsPerPart=2;finite.FiniteAnglesOnly=true;
    const auto freeGuard=MakeRotationCompactionStage(finite).Factory()->Run(mixed,source,Limits());
    Require(Validate(mixed,freeGuard).Complete&&Identities(source)==Identities(freeGuard)&&freeGuard.Stats.PlacementAttempts>0,
        "finite-only stage must execute bounded finite work while preserving the mixed inventory");
    Require(Member(freeGuard,"turnable-slope-93").Transform.AngleDegrees==0,
        "finite-only stage itself introduced a free-angle rotation");
    Nominal(mixed,freeGuard);
    std::cout<<"PlaneAngleSettlingTests passed: allowed half-turn and settling, strict normal gap, finite domains, provenance and bounded work\n";
    return 0;
}
catch(const std::exception& error){std::cerr<<"PlaneAngleSettlingTests failed: "<<error.what()<<'\n';return 1;}
