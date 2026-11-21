#include "SheetNestingPlaneStrategies.h"
#include "SheetNestingConstructionTrace.h"
#include "NestingAxisSafety.h"
#include "NestingClearancePlacement.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Options Limits()
{Options o;o.TimeLimitSeconds=10;o.MaxPlacementAttempts=1000000;o.EnableBasicCompaction=false;o.EnableGapFill=false;o.MaxPatternMembers=2;return o;}
Part Polygon(std::string id,std::vector<Point> outer,std::size_t quantity=1)
{Part p;p.ID=std::move(id);p.Geometry.Outer=std::move(outer);p.Quantity=quantity;p.FreeRotation=false;p.AllowedAnglesDegrees={0,180};return p;}
bool Has(const Result& r,const std::string& text)
{return std::any_of(r.Diagnostics.begin(),r.Diagnostics.end(),[&](const std::string& value){return value.find(text)!=std::string::npos;});}
Result Run(const Problem& p,PlaneStrategyKind kind,bool independent=true)
{
    PlaneStrategySettings settings;settings.Kind=kind;settings.EnableContourSettling=false;
    settings.EnableIndependentPairArrays=independent;
    auto r=MakePlaneStrategyFactory(settings)()->Run(p,Limits());
    const auto valid=Validate(p,r);Require(valid.Valid,"advanced array violated original geometry/inventory");
    Require(r.Stats.PlacementAttempts<=Limits().MaxPlacementAttempts,"advanced array overspent attempts");
    return r;
}
PlaneStrategySettings OriginalNfpSettings(PlaneStrategyKind kind)
{
    PlaneStrategySettings settings;settings.Kind=kind;settings.NfpTrials=1;
    settings.EnableContourSettling=false;settings.EnableAllowedAngleSettling=false;
    settings.EnableLayoutRefinement=false;settings.EnableHoleNesting=false;
    settings.EnableSheetTemplateReuse=false;settings.EnableIndependentPairArrays=false;
    settings.MaxArrayAxis=1; // Isolate ordinary single-original NFP commits from periodic copies.
    return settings;
}
Options OriginalNfpLimits(std::vector<ConstructionTraceEvent>& events)
{
    auto options=Limits();options.EnableCombinations=false;
    options.ConstructionTrace=[&events](const ConstructionTraceEvent& event){events.push_back(event);};
    return options;
}
double Measure(const ConstructionTraceEvent& event,const char* name)
{
    const auto found=std::find_if(event.Measures.begin(),event.Measures.end(),
        [&](const auto& value){return value.first==name;});
    Require(found!=event.Measures.end(),"NFP orientation observation lost a required measurement");
    return found->second;
}
bool SamePose(const Pose& a,const Pose& b)
{return a.X==b.X&&a.Y==b.Y&&a.AngleDegrees==b.AngleDegrees&&a.Mirrored==b.Mirrored;}
void UnchangedOriginals(const Problem& before,const Problem& after)
{
    Require(before.Parts.size()==after.Parts.size(),"NFP construction replaced the original specifications");
    for(std::size_t i=0;i<before.Parts.size();++i)
    {
        const auto& a=before.Parts[i];const auto& b=after.Parts[i];
        Require(a.ID==b.ID&&a.Quantity==b.Quantity&&a.AllowMirror==b.AllowMirror&&
            a.FreeRotation==b.FreeRotation&&a.AllowedAnglesDegrees==b.AllowedAnglesDegrees&&
            a.Geometry.Holes.size()==b.Geometry.Holes.size()&&a.Geometry.Outer.size()==b.Geometry.Outer.size()&&
            std::equal(a.Geometry.Outer.begin(),a.Geometry.Outer.end(),b.Geometry.Outer.begin(),
                [](Point x,Point y){return x.X==y.X&&x.Y==y.Y;}),
            "NFP orientation competition rewrote original geometry, permissions or demand");
    }
}
void RequireRawInterlock(const Problem& problem,const Result& result,
    const std::vector<ConstructionTraceEvent>& events)
{
    const auto checked=Validate(problem,result);
    Require(checked.Valid&&checked.Complete&&checked.PlacedCount==2&&result.Layouts.size()==1,
        "ordinary initial NFP construction did not finish both original concave pieces on one sheet");
    std::vector<const ConstructionTraceEvent*> commits;
    const ConstructionTraceEvent* raw=nullptr;
    for(const auto& event:events)
    {
        Require(event.Source!="fill.commit","concave initial construction relied on a gap-fill commit");
        if(event.Source=="constructor.commit")
        {
            Require(event.Stage==ConstructionTraceStage::Commit&&event.Members.size()==1,
                "concave initial construction used a compound carrier instead of an original-member commit");
            commits.push_back(&event);
        }
        if(event.Source=="board.raw-construction"&&event.Layouts.size()==1&&
            event.Layouts.front().Placements.size()==2)raw=&event;
    }
    Require(commits.size()==2&&raw&&commits[1]->ParentStateID==commits[0]->StateID,
        "concave originals were not placed successively in one ordinary construction state");
    Require(commits[1]->ProposalID!=0&&std::any_of(events.begin(),events.end(),[&](const auto& event)
        {return event.Source=="placement.rank"&&event.ProposalID==commits[1]->ProposalID&&
            event.CandidateID==commits[1]->CandidateID&&event.StateID==commits[1]->ParentStateID;}),
        "interlocking second original bypassed the real NFP proposal and same-state scoring chain");
    const auto& board=raw->Layouts.front();
    const auto part=[&](const Placement& placement)->const Part&
    {
        const auto found=std::find_if(problem.Parts.begin(),problem.Parts.end(),
            [&](const Part& value){return value.ID==placement.PartID;});
        Require(found!=problem.Parts.end(),"raw interlock contains a private carrier identity");return *found;
    };
    const auto& a=board.Placements[0];const auto& b=board.Placements[1];
    const auto ab=Detail::Bounds(part(a).Geometry,a.Transform),bb=Detail::Bounds(part(b).Geometry,b.Transform);
    Require(std::min(ab.X+ab.Width,bb.X+bb.Width)>std::max(ab.X,bb.X)+.01&&
        std::min(ab.Y+ab.Height,bb.Y+bb.Height)>std::max(ab.Y,bb.Y)+.01,
        "concave regression did not require genuinely overlapping individual AABBs");
    Require(AxisSafety::PairExact(AxisSafety::MakeExact(part(a).Geometry,a.Transform),
        AxisSafety::MakeExact(part(b).Geometry,b.Transform),problem.PartGap,false,problem.Tolerance)==AxisSafety::Verdict::Safe,
        "overlapping AABBs concealed an original-material collision or insufficient gap");
    for(const auto& placement:board.Placements)
    {
        Require(!placement.GapFiller&&AxisSafety::Containment(AxisSafety::Make(part(placement).Geometry,placement.Transform),
            board.Width,board.Height,problem.EdgeMargin)==AxisSafety::Verdict::Safe,
            "raw concave interlock used a filler or violated the original sheet margin");
        const auto found=std::find_if(result.Layouts.front().Placements.begin(),result.Layouts.front().Placements.end(),
            [&](const Placement& value){return value.PartID==placement.PartID&&value.Instance==placement.Instance;});
        Require(found!=result.Layouts.front().Placements.end()&&SamePose(found->Transform,placement.Transform)&&
            found->Group==placement.Group,"a later repair stage supplied the tested concave placement");
    }
}
void ConcaveOriginalsInterlockDuringPrimaryConstruction()
{
    Problem problem;problem.PartGap=.2;problem.EdgeMargin=.5;problem.Tolerance=1e-7;
    problem.AllowCommonEdges=false;
    problem.Parts={Polygon("notched-original",{{605,920},{605,0},{0,0},{0,250},
        {315,250},{315,580},{100,580},{100,920}},2)};
    // Each original has a 605 x 920 AABB. Neither axis can separate two such
    // boxes inside this stock; the real concave materials must interlock.
    problem.Stocks={{"tight-original-sheet",1111.3,921.1,1}};
    const auto original=problem;
    for(const auto kind:{PlaneStrategyKind::NfpAllSpecifications,PlaneStrategyKind::NfpClosest,
        PlaneStrategyKind::NfpDirectional,PlaneStrategyKind::NfpArray,PlaneStrategyKind::NfpMultiArray})
    {
        std::vector<ConstructionTraceEvent> events;auto options=OriginalNfpLimits(events);
        const auto result=MakePlaneStrategyFactory(OriginalNfpSettings(kind))()->Run(problem,options);
        RequireRawInterlock(problem,result,events);UnchangedOriginals(original,problem);
        Require(result.Stats.PlacementAttempts<=options.MaxPlacementAttempts,
            "primary concave NFP construction exceeded its attempt budget");
    }
}
void MidConstructionOrientationsCompeteInTheSameOriginalState()
{
    Problem problem;problem.PartGap=.2;problem.EdgeMargin=.5;problem.Tolerance=1e-7;
    problem.AllowCommonEdges=false;
    auto frame=Polygon("open-concave-frame",{{1.5,0},{4.5,0},{4.5,5.5},{0,5.5},
        {0,4.5},{3.5,4.5},{3.5,3},{1.5,3}});frame.AllowedAnglesDegrees={0};
    auto moving=Polygon("chiral-concave-original",{{0,0},{3,0},{3,1},{1,1},{1,4},{0,4}});
    moving.AllowedAnglesDegrees={0,90,180,270};moving.AllowMirror=true;
    problem.Parts={frame,moving};problem.Stocks={{"chiral-cavity-sheet",5.5001,6.5001,1}};
    // The open recess is 1.5 wide below its 3.5 x 1.5 top pocket. A mirrored
    // zero-degree L fits with .25 clearance. Ordinary 0/180-degree Ls have the
    // wrong handedness; 90/270-degree Ls are width 4. No pose is supplied to Run.
    const auto original=problem;
    for(const auto kind:{PlaneStrategyKind::NfpArray,PlaneStrategyKind::NfpMultiArray})
    for(const bool allowMirror:{true,false})
    {
        auto input=problem;input.Parts[1].AllowMirror=allowMirror;
        const auto before=input;std::vector<ConstructionTraceEvent> events;
        auto options=OriginalNfpLimits(events);
        const auto result=MakePlaneStrategyFactory(OriginalNfpSettings(kind))()->Run(input,options);
        UnchangedOriginals(before,input);const auto checked=Validate(input,result);
        Require(checked.Valid&&checked.Complete==allowMirror,
            "mid-construction NFP lost the only legal reflection or bypassed an explicit mirror prohibition");
        const auto first=std::find_if(events.begin(),events.end(),[](const auto& event)
            {return event.Source=="constructor.commit";});
        Require(first!=events.end()&&first->Members.size()==1&&first->Members.front().PartIndex==0,
            "chiral regression did not reach the intended intermediate original frame state");
        std::set<std::pair<int,bool>> orientations;
        bool scoredMirror=false;
        for(const auto& event:events)
        {
            if(event.Source=="nfp.query"&&event.Stage==ConstructionTraceStage::Begin&&
                event.PartIndex==1&&event.StateID==first->StateID)
                orientations.emplace(static_cast<int>(std::lround(Measure(event,"angle"))),Measure(event,"moving-mirrored")!=0);
            if(event.Source=="placement.rank"&&event.PartIndex==1&&event.StateID==first->StateID)
                for(const auto& member:event.Members)scoredMirror|=member.Transform.Mirrored;
            if(!allowMirror)for(const auto& member:event.Members)
                Require(member.PartIndex!=1||!member.Transform.Mirrored,
                    "forbidden mirrored original entered a native proposal, score or commit");
        }
        for(const int angle:{0,90,180,270})
        {
            Require(orientations.contains({angle,false}),
                "original and rotated directions did not compete against the same intermediate state");
            Require(orientations.contains({angle,true})==allowMirror,
                "mirror directions were split into another constructor or ignored the hard permission");
        }
        if(allowMirror)
        {
            Require(scoredMirror,"legal mirrored original never reached same-state candidate scoring");
            RequireRawInterlock(input,result,events);
            const auto selected=std::find_if(result.Layouts.front().Placements.begin(),result.Layouts.front().Placements.end(),
                [&](const Placement& value){return value.PartID==moving.ID;});
            Require(selected!=result.Layouts.front().Placements.end()&&selected->Transform.Mirrored&&
                selected->Transform.AngleDegrees==0,"initial construction selected an incompatible chiral pose");
        }
        else Require(checked.PlacedCount==1&&result.Layouts.size()==1&&
            result.Layouts.front().Placements.front().PartID==frame.ID,
            "mirror prohibition discarded the valid original frame or fabricated a complete layout");
        Require(result.Stats.PlacementAttempts<=options.MaxPlacementAttempts,
            "same-state orientation competition exceeded its attempt budget");
    }
    UnchangedOriginals(original,problem);
}
void PositiveGapFullChannelsImproveAlreadySafeBoundaryFits()
{
    Problem problem;problem.PartGap=.2;problem.EdgeMargin=.5;problem.Tolerance=1e-7;
    problem.AllowCommonEdges=false;
    // Match the production private obstacle offset. The source materials have
    // a real gap greater than .2; only the private translation domain collapses.
    const auto clearance=problem.PartGap+std::max(1e-7,4*problem.Tolerance);
    auto host=Polygon("positive-gap-U",{{0,0},{5,0},{5,3+clearance},
        {15+2*clearance,3+clearance},{15+2*clearance,0},{20+2*clearance,0},
        {20+2*clearance,5+clearance},{0,5+clearance}});
    auto plug=Polygon("positive-gap-plug",{{0,0},{10,0},{10,3},{0,3}});
    host.AllowedAnglesDegrees={0};plug.AllowedAnglesDegrees={0};problem.Parts={host,plug};
    problem.Stocks={{"positive-gap-channel-sheet",40,6+clearance+.001,1}};
    const auto original=problem;
    for(const auto kind:{PlaneStrategyKind::NfpArray,PlaneStrategyKind::NfpMultiArray})
    {
        std::vector<ConstructionTraceEvent> events;auto options=OriginalNfpLimits(events);
        const auto result=MakePlaneStrategyFactory(OriginalNfpSettings(kind))()->Run(problem,options);
        RequireRawInterlock(problem,result,events);UnchangedOriginals(original,problem);
        const auto first=std::find_if(events.begin(),events.end(),[](const auto& event)
            {return event.Source=="constructor.commit";});
        Require(first!=events.end()&&first->Members.front().PartIndex==0&&first->Layouts.size()==1,
            "positive-gap channel fixture did not construct its original U host first");
        const auto& fixed=first->Layouts.front();const auto& fixedHost=fixed.Placements.front();
        const auto hostBox=Detail::Bounds(host.Geometry,fixedHost.Transform);
        const auto internal=[&](Pose pose)
        {
            const auto box=Detail::Bounds(plug.Geometry,pose);
            return std::abs(box.X-(hostBox.X+5+clearance))<1e-6&&
                std::abs(box.Y-hostBox.Y)<1e-6;
        };
        const auto occupiedArea=[&](Pose pose)
        {
            const auto box=Detail::Bounds(plug.Geometry,pose);
            return (std::max(hostBox.X+hostBox.Width,box.X+box.Width)-std::min(hostBox.X,box.X))*
                (std::max(hostBox.Y+hostBox.Height,box.Y+box.Height)-std::min(hostBox.Y,box.Y));
        };
        // Read-only probes use the host actually committed by production above,
        // never a hand-placed seed supplied to the initial constructor.
        Detail::ClearanceNfpPlacement generator(problem,problem.Stocks.front(),{},{});
        Detail::NfpPlacementLimits vertices;vertices.BoundaryVerticesOnly=true;
        vertices.IncludeCommonEdgeProposals=false;vertices.MaxCandidates=0;
        const auto boundary=generator.Find(fixed,1,0,vertices,{});
        Require(boundary.Supported&&!boundary.Fits.empty(),
            "positive-gap regression needs existing legal exterior boundary vertices");
        Require(std::none_of(boundary.Fits.begin(),boundary.Fits.end(),internal),
            "positive-gap fixture no longer distinguishes the collapsed interior channel");
        auto complete=vertices;complete.BoundaryVerticesOnly=false;
        const auto channels=generator.Find(fixed,1,0,complete,{});
        Require(channels.Supported&&std::any_of(channels.Fits.begin(),channels.Fits.end(),internal),
            "complete ordinary NFP channels lost a physically legal positive-gap interior pose");
        double boundaryArea=std::numeric_limits<double>::infinity();
        for(const auto pose:boundary.Fits)boundaryArea=std::min(boundaryArea,occupiedArea(pose));
        double internalArea=std::numeric_limits<double>::infinity();
        for(const auto pose:channels.Fits)if(internal(pose))
        {
            internalArea=std::min(internalArea,occupiedArea(pose));
            Require(AxisSafety::PairExact(AxisSafety::MakeExact(host.Geometry,fixedHost.Transform),
                AxisSafety::MakeExact(plug.Geometry,pose),problem.PartGap,false,problem.Tolerance)==AxisSafety::Verdict::Safe,
                "collapsed private domain was mistaken for a legal physical clearance");
        }
        Require(internalArea+1<boundaryArea,
            "full channel does not improve on the already legal exterior boundary candidate");
        std::map<std::uint64_t,bool> boundaryQueries;bool scoredBoundary=false,scoredInternalChannel=false;
        for(const auto& event:events)
        {
            if(event.PartIndex!=1||event.StateID!=first->StateID)continue;
            if(event.Source=="nfp.query-result")
                boundaryQueries[event.ProposalID]=Measure(event,"ordinaryBoundaryPass")!=0;
            if(event.Source=="placement.rank")
            {
                const auto query=boundaryQueries.find(event.ProposalID);
                Require(query!=boundaryQueries.end(),"channel scoring lost its actual construction query phase");
                for(const auto& member:event.Members)
                {
                    if(query->second)scoredBoundary=true;
                    else scoredInternalChannel|=internal(member.Transform);
                }
            }
        }
        const auto selected=std::find_if(result.Layouts.front().Placements.begin(),result.Layouts.front().Placements.end(),
            [&](const Placement& value){return value.PartID==plug.ID;});
        Require(scoredBoundary&&scoredInternalChannel&&selected!=result.Layouts.front().Placements.end()&&
            internal(selected->Transform),
            "an already safe boundary vertex suppressed the better full-channel pose in primary construction");
        Require(result.Stats.PlacementAttempts<=options.MaxPlacementAttempts,
            "positive-gap two-pass competition exceeded its common attempt budget");
    }
}
void RigidArrayPriorityMatchesTheFinalAxisRank()
{
    Problem problem;problem.Tolerance=1e-7;
    auto host=Polygon("diagonal-host",{{0,0},{2,0},{10,8},{10,10},{8,10},{0,2}});
    auto tile=Polygon("two-tiles",{{0,0},{1,0},{1,1},{0,1}},2);
    host.AllowedAnglesDegrees={0};tile.AllowedAnglesDegrees={0};
    problem.Parts={host,tile};problem.Stocks={{"axis-sheet",10,10,1}};const auto original=problem;
    // Both pockets lie inside the host's 10x10 AABB. Every safe two-tile
    // placement there has U=.38, net material=2 and occupied area=100.
    // A one-fit rigid query must therefore use the final rank's chosen axis:
    // left/up for XFirst, down/right otherwise, before its fit cap is applied.
    for(const bool xFirst:{false,true})
    {
        // Multi-array retains the global rigid-domain search. The greedy
        // constructor's certified-unit growth is exercised separately below.
        auto settings=OriginalNfpSettings(PlaneStrategyKind::NfpMultiArray);
        settings.XFirst=xFirst;settings.MaxCandidates=1;settings.MaxArrayAxis=2;
        std::vector<ConstructionTraceEvent> events;auto options=OriginalNfpLimits(events);
        const auto result=MakePlaneStrategyFactory(settings)()->Run(problem,options);
        const auto checked=Validate(problem,result);
        Require(checked.Valid&&checked.Complete&&checked.PlacedCount==3&&result.Layouts.size()==1,
            "axis-ranked rigid construction lost original demand or material safety");
        std::vector<const ConstructionTraceEvent*> commits;
        for(const auto& event:events)if(event.Source=="constructor.commit")commits.push_back(&event);
        Require(commits.size()==2&&commits[0]->Members.size()==1&&commits[0]->Members[0].PartIndex==0&&
            commits[0]->Layouts.size()==1&&commits[1]->Members.size()==2&&commits[1]->ProposalID!=0&&
            commits[1]->ParentStateID==commits[0]->StateID,
            "axis fixture bypassed the real intermediate host or two-member rigid query");
        const auto& fixed=commits[0]->Layouts[0];
        Require(fixed.Placements[0].Transform.X==0&&fixed.Placements[0].Transform.Y==0,
            "empty-board intrinsic ties changed with the later rigid axis setting");
        const auto prefix=[&](const auto& members)
        {
            auto occupied=Detail::Bounds(host.Geometry,fixed.Placements[0].Transform);
            double x=std::numeric_limits<double>::infinity(),y=x,material=0;
            for(const auto& member:members)
            {
                const auto box=Detail::Bounds(problem.Parts[member.PartIndex].Geometry,member.Transform);
                x=std::min(x,box.X);y=std::min(y,box.Y);material+=Detail::Area(problem.Parts[member.PartIndex].Geometry);
                const auto left=std::min(occupied.X,box.X),bottom=std::min(occupied.Y,box.Y);
                occupied={left,bottom,std::max(occupied.X+occupied.Width,box.X+box.Width)-left,
                    std::max(occupied.Y+occupied.Height,box.Y+box.Height)-bottom};
            }
            const auto used=fixed.Width*std::clamp(occupied.Y+occupied.Height+problem.EdgeMargin,0.,fixed.Height);
            return std::array<double,5>{-(fixed.MaterialArea+material)/used,-material,
                occupied.Width*occupied.Height,xFirst?x:y,xFirst?y:x};
        };
        Detail::ClearanceNfpPlacement oracle(problem,problem.Stocks[0],{},{});
        Require(oracle.Ready(),"rigid axis oracle does not support the original fixture");
        std::size_t certified=0;
        for(const auto& event:events)
        {
            if(event.Source!="nfp.rigid-placement"||event.Stage!=ConstructionTraceStage::Filter||
                !event.Accepted||event.StateID!=commits[0]->StateID)continue;
            Require(event.Members.size()==2&&std::all_of(event.Members.begin(),event.Members.end(),
                [](const auto& member){return member.PartIndex==1;}),"rigid axis query changed its original members");
            ++certified;std::vector<Detail::Member> moving;
            for(const auto& member:event.Members)moving.push_back({member.PartIndex,member.Transform});
            Detail::NfpPlacementLimits limits;limits.MaxCandidates=0;limits.MaxAttempts=10000;limits.MaxRawCandidates=10000;
            const auto all=oracle.FindRigid(fixed,moving,limits,{});
            Require(all.Supported&&!all.Fits.empty(),"unlimited rigid axis oracle lost its certified input pose");
            const auto actual=prefix(moving);auto best=actual;
            for(const auto delta:all.Fits)
            {
                auto proposed=moving;for(auto& member:proposed)
                {member.Transform.X+=delta.X;member.Transform.Y+=delta.Y;}
                best=std::min(best,prefix(proposed));
            }
            for(std::size_t i=0;i<actual.size();++i)
                Require(std::abs(actual[i]-best[i])<1e-9,
                    "one-fit rigid preordering discarded the better final-axis candidate");
            Require(std::abs(actual[0]+.38)<1e-12&&actual[1]==-2&&actual[2]==100&&
                std::abs(actual[3])<1e-9&&actual[4]>2,
                "rigid fixture failed to expose equal-utilization opposite-axis pockets");
            const auto ranked=std::find_if(events.begin(),events.end(),[&](const auto& candidate)
                {return candidate.Source=="array.rank"&&candidate.ProposalID==event.ProposalID;});
            Require(ranked!=events.end()&&ranked->Rank.size()>=6,
                "certified rigid position did not reach final array ranking");
            for(std::size_t i=0;i<actual.size();++i)
                Require(std::abs(actual[i]-ranked->Rank[i<3?i:i+1].second)<1e-9,
                    "rigid preordering and final array Rank use different tie prefixes");
        }
        Require(certified>0&&std::abs(prefix(commits[1]->Members)[3])<1e-9,
            "final construction did not select the independently ranked rigid pocket");
        for(const auto& member:commits[1]->Members)
        {
            const auto placed=std::find_if(result.Layouts[0].Placements.begin(),result.Layouts[0].Placements.end(),
                [&](const auto& value){return value.PartID==tile.ID&&value.Instance==member.Instance;});
            Require(placed!=result.Layouts[0].Placements.end()&&SamePose(placed->Transform,member.Transform)&&
                placed->Group==member.Group,"later finishing supplied the tested rigid axis choice");
        }
        Require(result.Stats.PlacementAttempts<=options.MaxPlacementAttempts,"rigid axis construction exceeded its budget");
        UnchangedOriginals(original,problem);
    }
}
void OccupiedBoardArraysShrinkWithoutDiscardingBulkDemand()
{
    Problem problem;problem.PartGap=0;problem.EdgeMargin=.5;problem.Tolerance=1e-7;
    auto carrier=Polygon("large-carrier",{{0,0},{8,0},{8,30},{0,30}});
    auto unit=Polygon("unit",{{0,0},{1,0},{1,1},{0,1}},200);
    carrier.AllowedAnglesDegrees={0};unit.AllowedAnglesDegrees={0};
    problem.Parts={carrier,unit};problem.Stocks={{"tall-sheet",9,10000,1}};
    const auto original=problem;
    const auto rigidCap=Detail::NfpPlacementLimits{}.MaxRigidMembers;
    Require(rigidCap>2&&rigidCap<unit.Quantity,"bulk-demand fixture no longer exceeds the rigid query capacity");
    // The carrier starts with 240/(9*31) utilization, strictly above the
    // complete unit grid's 200/(9*26). Its first placement is selected by the
    // actual objective even when the constructor compares all specifications.
    for(const auto kind:{PlaneStrategyKind::NfpArray,PlaneStrategyKind::NfpMultiArray})
    {
        std::vector<ConstructionTraceEvent> commits;
        std::vector<ConstructionTraceEvent> mediumCandidates;
        std::map<std::uint64_t,std::size_t> nonemptyStateUnits;
        std::map<std::uint64_t,ConstructionTraceEvent> rigidResults;
        std::ostringstream observations;observations<<std::setprecision(17);
        std::ostringstream choiceObservations;choiceObservations<<std::setprecision(17);
        auto options=Limits();options.EnableCombinations=false;
        options.TimeLimitSeconds=std::numeric_limits<double>::max();
        options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
        options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
        {
            if(event.Stage==ConstructionTraceStage::Commit)
            {
                commits.push_back(event);
                if(event.Layouts.size()==1&&std::any_of(event.Layouts[0].Placements.begin(),event.Layouts[0].Placements.end(),
                    [&](const Placement& item){return item.PartID==carrier.ID;}))
                    nonemptyStateUnits[event.StateID]=static_cast<std::size_t>(std::count_if(
                        event.Layouts[0].Placements.begin(),event.Layouts[0].Placements.end(),
                        [&](const Placement& item){return item.PartID==unit.ID;}));
            }
            if(event.Stage==ConstructionTraceStage::Filter&&
                (event.Source=="array.contour-pose"||event.Source=="array.seed-growth")&&event.Accepted&&
                event.Members.size()>2&&event.Members.size()<=rigidCap&&
                std::all_of(event.Members.begin(),event.Members.end(),
                    [](const ConstructionTraceMember& member){return member.PartIndex==1;}))
            {
                const auto state=nonemptyStateUnits.find(event.StateID);
                if(state!=nonemptyStateUnits.end()&&state->second<unit.Quantity&&unit.Quantity-state->second>rigidCap)
                    mediumCandidates.push_back(event);
            }
            if(event.Source=="placement.choice"&&event.StateID<=4)
            {
                choiceObservations<<"\nCHOICE state="<<event.StateID<<" family="<<event.Reason<<" members="<<event.Members.size()
                    <<" candidate="<<event.CandidateID<<" proposal="<<event.ProposalID;
                for(std::size_t i=0;i<std::min<std::size_t>(3,event.Rank.size());++i)
                    choiceObservations<<" rank."<<event.Rank[i].first<<'='<<event.Rank[i].second;
                double x0=std::numeric_limits<double>::infinity(),y0=x0,x1=-x0,y1=-x0;
                for(const auto& member:event.Members)if(member.PartIndex<problem.Parts.size())
                {
                    const auto box=Detail::Bounds(problem.Parts[member.PartIndex].Geometry,member.Transform);
                    x0=std::min(x0,box.X);y0=std::min(y0,box.Y);
                    x1=std::max(x1,box.X+box.Width);y1=std::max(y1,box.Y+box.Height);
                }
                choiceObservations<<" bounds=("<<x0<<','<<y0<<','<<x1-x0<<','<<y1-y0<<')';
            }
            if(event.Source=="nfp.rigid-placement"&&event.Stage==ConstructionTraceStage::Complete&&event.StateID<=3)
                rigidResults[event.ProposalID]=event;
            if(event.Source=="array.rigid-query-result"&&event.StateID<=3)
            {
                const auto raw=rigidResults.find(event.ProposalID);
                if(raw!=rigidResults.end())
                {
                    const auto members=std::find_if(raw->second.Measures.begin(),raw->second.Measures.end(),
                        [](const auto& value){return value.first=="members";});
                    if(members!=raw->second.Measures.end()&&members->second>8)
                    {
                        observations<<"\nRIGID state="<<event.StateID<<" proposal="<<event.ProposalID
                            <<" reason="<<raw->second.Reason;
                        for(const auto& [name,value]:event.Measures)observations<<' '<<name<<'='<<value;
                        for(const auto& [name,value]:raw->second.Measures)observations<<" raw."<<name<<'='<<value;
                    }
                }
            }
        };
        auto settings=OriginalNfpSettings(kind);settings.MaxArrayAxis=128;
        const auto result=MakePlaneStrategyFactory(settings)()->Run(problem,options);
        std::ostringstream timeline;
        for(const auto& event:commits)
        {
            timeline<<" [state="<<event.StateID<<" members="<<event.Members.size()<<" parts=";
            for(const auto& member:event.Members)timeline<<member.PartIndex<<',';
            timeline<<']';
        }
        timeline<<" safeMediumCandidates="<<mediumCandidates.size();
        timeline<<choiceObservations.str()<<observations.str();
        const auto require=[&](bool value,const char* message)
        {
            if(!value)throw std::runtime_error(std::string(message)+"; kind="+
                std::to_string(static_cast<int>(kind))+" rigidCap="+std::to_string(rigidCap)+timeline.str());
        };
        const auto checked=Validate(problem,result);
        require(checked.Valid&&checked.Complete&&checked.PlacedCount==201&&result.Layouts.size()==1,
            "shrinking a rigid array lost legal complete bulk demand");
        require(!commits.empty()&&commits.front().Members.size()==1&&
            commits.front().Members.front().PartIndex==0,
            "the fixture did not establish its higher-utilization carrier before the bulk demand");
        std::size_t unitsCommitted=0;bool mediumArray=false,grownFromMediumArray=false;
        std::set<std::size_t> unitInstances;
        const auto& board=result.Layouts.front();
        for(std::size_t i=1;i<commits.size();++i)
        {
            const auto& event=commits[i];
            require(!event.Members.empty()&&std::all_of(event.Members.begin(),event.Members.end(),
                [](const ConstructionTraceMember& member){return member.PartIndex==1;}),
                "a bulk continuation changed specification or recommitted the carrier");
            const auto remaining=unit.Quantity-unitsCommitted;
            if(remaining>rigidCap&&event.Members.size()>2&&event.Members.size()<=rigidCap)mediumArray=true;
            // A source-checked seed may grow past the rigid query's capacity;
            // multi-array may also extend a certified rigid base with WithTail.
            // Require the actual same-state proposal and unchanged prefix
            // poses, rather than an unrelated large complete submission.
            if(remaining>rigidCap&&event.Members.size()>rigidCap)
                grownFromMediumArray|=std::any_of(mediumCandidates.begin(),mediumCandidates.end(),
                    [&](const ConstructionTraceEvent& candidate)
                    {
                        const bool supportedSource=candidate.Source=="array.seed-growth"||
                            (kind==PlaneStrategyKind::NfpMultiArray&&candidate.Source=="array.contour-pose");
                        return supportedSource&&candidate.StateID==event.ParentStateID&&candidate.ProposalID==event.ProposalID&&
                            std::all_of(candidate.Members.begin(),candidate.Members.end(),[&](const auto& member)
                            {
                                return std::any_of(event.Members.begin(),event.Members.end(),[&](const auto& placed)
                                    {return placed.PartIndex==member.PartIndex&&SamePose(placed.Transform,member.Transform);});
                            });
                    });
            require(event.Members.size()<=remaining,"a smaller rigid array exceeded remaining original demand");
            unitsCommitted+=event.Members.size();
            for(const auto& member:event.Members)
            {
                require(unitInstances.insert(member.Instance).second,
                    "a smaller rigid array reused an original unit instance");
                const auto placed=std::find_if(board.Placements.begin(),board.Placements.end(),
                    [&](const Placement& value){return value.PartID==unit.ID&&value.Instance==member.Instance;});
                require(placed!=board.Placements.end()&&SamePose(placed->Transform,member.Transform)&&
                    placed->Group==member.Group&&!placed->GapFiller&&
                    placed->Transform.AngleDegrees==0&&!placed->Transform.Mirrored,
                    "a later repair or changed permission supplied the tested bulk placement");
            }
        }
        require(mediumArray||grownFromMediumArray,
            "bulk demand above the rigid capacity never produced a legal intermediate-sized array or its verified tail extension");
        require(unitsCommitted==unit.Quantity&&unitInstances.size()==unit.Quantity&&
            *unitInstances.begin()==0&&*unitInstances.rbegin()==unit.Quantity-1,
            "smaller arrays changed the complete original inventory");
        require(!options.ConstructionObservation->Failed.load()&&
            result.Stats.PlacementAttempts<=options.MaxPlacementAttempts,
            "smaller-array construction failed observation or exceeded the original budget");
        UnchangedOriginals(original,problem);
        require(problem.Stocks[0].Quantity==original.Stocks[0].Quantity&&
            problem.Stocks[0].Width==original.Stocks[0].Width&&problem.Stocks[0].Height==original.Stocks[0].Height,
            "smaller-array construction changed the physical stock");
        std::cout<<"bulk-array kind="<<static_cast<int>(kind)<<" placed="<<checked.PlacedCount
            <<" seconds="<<result.Stats.ElapsedSeconds<<choiceObservations.str()<<'\n';
    }
}
void CachedOriginalBoardCertificatesStillCheckTheGrownPrefix()
{
    Problem problem;problem.PartGap=.2;problem.EdgeMargin=.5;problem.Tolerance=1e-7;
    problem.AllowCommonEdges=true;
    auto carrier=Polygon("cache-carrier",{{0,0},{16,0},{16,30},{0,30}});
    auto unit=Polygon("cache-unit",{{0,0},{2,0},{2,2},{0,2}},12);
    carrier.AllowedAnglesDegrees={0};unit.AllowedAnglesDegrees={0};
    carrier.AllowMirror=false;unit.AllowMirror=false;unit.Geometry.CommonEdgeRings={false};
    problem.Parts={carrier,unit};problem.Stocks={{"cache-sheet",24,40,1}};
    const auto original=problem;
    // After the carrier, the right gutter admits at least three 2-wide
    // columns. Contact pitches still propose dx=2, but this original forbids
    // shared edges: the first added unit in columns=2 and columns=3 has the
    // same stored pose, is safe against the original board, and conflicts
    // with its seed at the positive gap. The second query must reuse only
    // the original-board certificate, never the prefix certificate.
    Layout witnessBoard{"cache-sheet",0,24,40,{{carrier.ID,0,{.5,.5,0,false},1,false}},480};
    for(std::size_t i=0;i<unit.Quantity;++i)
        witnessBoard.Placements.push_back({unit.ID,i,
            {16.70001+(i%3)*2.20001,.5+(i/3)*2.20001,0,false},i+2,false});
    witnessBoard.MaterialArea+=unit.Quantity*4.;
    Result witness;witness.State=Status::Complete;witness.Layouts={witnessBoard};
    Require(Validate(problem,witness).Valid&&Validate(problem,witness).Complete,
        "cache-boundary fixture has no valid complete original placement");

    std::map<std::uint64_t,ConstructionTraceEvent> proposals;
    std::set<std::uint64_t> cacheHitConflicts,rejectedCacheHitConflicts;
    bool carrierFirst=false,originalBoardSafe=true,unsafeAcceptance=false;
    Layout originalBoard;
    auto options=Limits();options.EnableCombinations=false;
    options.TimeLimitSeconds=std::numeric_limits<double>::max();
    options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
    options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source=="constructor.commit"&&event.ParentStateID==0)
        {
            carrierFirst=event.Members.size()==1&&event.Members[0].PartIndex==0&&event.Layouts.size()==1;
            if(carrierFirst)originalBoard=event.Layouts[0];
        }
        if(event.Source=="array.seed-growth"&&event.Stage==ConstructionTraceStage::Generate)
            proposals.insert_or_assign(event.CandidateID,event);
        if(event.Source=="array.unit-original-proof"&&event.StateID==1&&
            Measure(event,"originalBoardCacheHit")==1&&Measure(event,"originalBoardCertified")==1&&
            Measure(event,"prefixChecked")==1&&Measure(event,"prefixCertified")==0)
        {
            const auto proposed=proposals.find(event.CandidateID);
            const auto added=static_cast<std::size_t>(Measure(event,"unitMembers"));
            if(proposed==proposals.end()||added!=1||proposed->second.Members.size()<2)return;
            const auto& members=proposed->second.Members;
            const auto copy=static_cast<std::size_t>(Measure(event,"unitCopy"));
            if(copy<1||copy>=members.size())return;
            const auto& moving=members[copy];bool prefixCollision=false;
            if(moving.PartIndex!=1)return;
            for(const auto& fixed:originalBoard.Placements)
            {
                const auto fixedPart=std::find_if(problem.Parts.begin(),problem.Parts.end(),
                    [&](const Part& part){return part.ID==fixed.PartID;});
                originalBoardSafe&=fixedPart!=problem.Parts.end()&&
                    AxisSafety::Pair(AxisSafety::Make(unit.Geometry,moving.Transform),
                        AxisSafety::Make(fixedPart->Geometry,fixed.Transform),problem.PartGap,
                        problem.AllowCommonEdges,problem.Tolerance)==AxisSafety::Verdict::Safe;
            }
            for(std::size_t i=0;i<copy;++i)
                prefixCollision|=Detail::ShapesConflict(unit.Geometry,moving.Transform,
                    problem.Parts[members[i].PartIndex].Geometry,members[i].Transform,
                    problem.PartGap,problem.Tolerance,problem.AllowCommonEdges);
            if(prefixCollision)
            {cacheHitConflicts.insert(event.CandidateID);unsafeAcceptance|=event.Accepted;}
        }
        if(event.Source=="array.seed-growth"&&event.Stage==ConstructionTraceStage::Filter&&
            cacheHitConflicts.contains(event.CandidateID))
        {
            unsafeAcceptance|=event.Accepted;
            if(!event.Accepted&&event.Reason=="fixed-material-conflict-or-interrupted")
                rejectedCacheHitConflicts.insert(event.CandidateID);
        }
    };
    auto settings=OriginalNfpSettings(PlaneStrategyKind::NfpArray);settings.MaxArrayAxis=8;
    const auto result=MakePlaneStrategyFactory(settings)()->Run(problem,options);
    const auto checked=Validate(problem,result);
    Require(carrierFirst&&!originalBoard.Placements.empty(),
        "cache-boundary construction never established the real fixed carrier");
    Require(!cacheHitConflicts.empty()&&rejectedCacheHitConflicts==cacheHitConflicts&&
        originalBoardSafe&&!unsafeAcceptance,
        "an original-board cache hit failed to reject a real grown-prefix gap conflict");
    Require(checked.Valid&&checked.Complete&&checked.PlacedCount==13&&result.Layouts.size()==1,
        "split original-board/prefix certificates lost complete legal original demand");
    Require(!options.ConstructionObservation->Failed.load()&&
        result.Stats.PlacementAttempts<options.MaxPlacementAttempts,
        "cache-boundary rejection depended on failed observation or exhausted attempts");
    std::map<std::string,std::set<std::size_t>> instances;
    for(const auto& board:result.Layouts)for(const auto& placed:board.Placements)
        Require(instances[placed.PartID].insert(placed.Instance).second&&!placed.GapFiller&&
            placed.Transform.AngleDegrees==0&&!placed.Transform.Mirrored,
            "cache-boundary construction duplicated or changed an original instance");
    Require(instances[carrier.ID]==std::set<std::size_t>{0}&&instances[unit.ID].size()==unit.Quantity&&
        *instances[unit.ID].begin()==0&&*instances[unit.ID].rbegin()==unit.Quantity-1,
        "cache-boundary construction exceeded or omitted original quantities");
    UnchangedOriginals(original,problem);
    Require(problem.Parts[1].Geometry.CommonEdgeRings==original.Parts[1].Geometry.CommonEdgeRings,
        "cache-boundary construction changed the original common-edge permission");
}
void OrdinaryArraysUseCompactOriginalUnitsDuringConstruction()
{
    for(const auto kind:{PlaneStrategyKind::ArrayQuantityDeferred,PlaneStrategyKind::ArrayGeometryDeferred})
    for(const bool allowMirror:{true,false})
    {
        Problem problem;problem.PartGap=0;problem.EdgeMargin=.5;problem.Tolerance=1e-7;
        problem.AllowCommonEdges=false;
        auto carrier=Polygon("carrier",{{0,0},{9,0},{9,15},{0,15}});
        auto left=Polygon("self-L",{{0,0},{2,0},{2,1},{1,1},{1,2},{0,2}},10);
        carrier.AllowedAnglesDegrees={0};left.AllowedAnglesDegrees={90,270};
        left.AllowMirror=allowMirror;
        problem.Parts={carrier,left};problem.Stocks={{"compact-unit-sheet",12,16,2}};
        const auto original=problem;
        const auto require=[&](bool value,const char* message)
        {
            if(!value)throw std::runtime_error(std::string(message)+"; kind="+
                std::to_string(static_cast<int>(kind))+" mirror="+std::to_string(allowMirror));
        };
        // Two instances of the same L at opposite legal angles form a 2 x 3
        // unit. Five self units tile the carrier's 2 x 15 right gutter.
        // The distinct carrier must never become a member of those units.
        // This witness supplies no seed, contour rewrite or pose to Run.
        Result witness;witness.State=Status::Complete;
        Layout frame{"compact-unit-sheet",0,12,16,{{"carrier",0,{.5,.5,0,false},1,false}},135};
        for(std::size_t i=0;i<5;++i)
        {
            frame.Placements.push_back({left.ID,2*i,{11.5,.5+3*i,90,false},i+2,false});
            frame.Placements.push_back({left.ID,2*i+1,{9.5,3.5+3*i,270,false},i+2,false});
        }
        frame.MaterialArea=165;witness.Layouts={frame};
        const auto witnessCheck=Validate(problem,witness);
        require(witnessCheck.Valid&&witnessCheck.Complete&&witnessCheck.PlacedCount==11,
            "compact original-unit physical witness is invalid");

        bool compactSummary=false,retainedCompactUnits=false,assemblyExpansion=false,noFill=true,crossTypeUnit=false;
        std::vector<Layout> rawBoards;
        auto options=Limits();options.TimeLimitSeconds=std::numeric_limits<double>::max();
        options.EnableCombinations=true;options.MaxPatternMembers=4;
        options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
        options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
        {
            noFill&=event.Source!="fill.commit";
            if(!event.Members.empty())for(const auto& member:event.Members)
                crossTypeUnit|=member.PartIndex!=event.Members.front().PartIndex;
            if(event.Source=="board.raw-construction")
                rawBoards.insert(rawBoards.end(),event.Layouts.begin(),event.Layouts.end());
            if(event.Source!="preparation.combination-summary"||event.Stage!=ConstructionTraceStage::Complete)return;
            const auto expand=std::find_if(event.Measures.begin(),event.Measures.end(),
                [](const auto& value){return value.first=="expandAssemblies";});
            const auto retained=std::find_if(event.Measures.begin(),event.Measures.end(),
                [](const auto& value){return value.first=="retainedPatterns";});
            if(expand!=event.Measures.end())
            {
                compactSummary|=expand->second==0;assemblyExpansion|=expand->second!=0;
                retainedCompactUnits|=expand->second==0&&event.Accepted&&
                    retained!=event.Measures.end()&&retained->second>0;
            }
        };
        PlaneStrategySettings settings;settings.Kind=kind;
        settings.EnableLayoutRefinement=false;settings.EnableContourSettling=false;
        settings.EnableAllowedAngleSettling=false;settings.EnableHoleNesting=false;
        settings.EnableSheetTemplateReuse=false;
        const auto result=MakePlaneStrategyFactory(settings)()->Run(problem,options);
        const auto checked=Validate(problem,result);
        require(checked.Valid&&checked.Complete&&checked.PlacedCount==11&&result.Layouts.size()==1,
            "ordinary array compact units lost complete original demand or permissions");
        require(compactSummary&&!assemblyExpansion&&(!allowMirror||retainedCompactUnits),
            "ordinary arrays did not prepare compact units without global assembly expansion");
        require(noFill&&!crossTypeUnit&&!options.ConstructionObservation->Failed.load()&&
            result.Stats.PlacementAttempts<=options.MaxPlacementAttempts,
            "ordinary compact-unit initial construction used a filler, lost observation or exceeded attempts");
        std::map<std::string,std::set<std::size_t>> instances;
        bool interlocked=false;
        for(const auto& board:result.Layouts)
        {
            const auto raw=std::find_if(rawBoards.begin(),rawBoards.end(),[&](const Layout& value)
                {return value.StockID==board.StockID&&value.StockInstance==board.StockInstance;});
            require(raw!=rawBoards.end()&&raw->Placements.size()==board.Placements.size(),
                "a later stage supplied the ordinary compact-unit complete layout");
            for(std::size_t i=0;i<board.Placements.size();++i)
            {
                const auto& placed=board.Placements[i];
                const auto part=std::find_if(problem.Parts.begin(),problem.Parts.end(),
                    [&](const Part& value){return value.ID==placed.PartID;});
                require(part!=problem.Parts.end()&&instances[placed.PartID].insert(placed.Instance).second&&!placed.GapFiller,
                    "ordinary compact units emitted a private, repeated or filler original");
                require((placed.PartID==carrier.ID?placed.Transform.AngleDegrees==0:
                    (placed.Transform.AngleDegrees==90||placed.Transform.AngleDegrees==270))&&
                    (!placed.Transform.Mirrored||(placed.PartID==left.ID&&allowMirror)),
                    "ordinary compact units bypassed the original hard orientation or mirror permission");
                const auto originalPose=std::find_if(raw->Placements.begin(),raw->Placements.end(),[&](const Placement& value)
                    {return value.PartID==placed.PartID&&value.Instance==placed.Instance;});
                require(originalPose!=raw->Placements.end()&&SamePose(originalPose->Transform,placed.Transform)&&
                    originalPose->Group==placed.Group,"ordinary compact-unit final output changed a raw original pose");
                if(placed.PartID==carrier.ID)continue;
                const auto a=Detail::Bounds(part->Geometry,placed.Transform);
                for(std::size_t j=0;j<i;++j)
                {
                    const auto& fixed=board.Placements[j];if(fixed.PartID==carrier.ID)continue;
                    const auto fixedPart=std::find_if(problem.Parts.begin(),problem.Parts.end(),
                        [&](const Part& value){return value.ID==fixed.PartID;});
                    require(fixedPart!=problem.Parts.end(),"ordinary compact unit contains an unknown fixed original");
                    const auto b=Detail::Bounds(fixedPart->Geometry,fixed.Transform);
                    if(std::min(a.X+a.Width,b.X+b.Width)>std::max(a.X,b.X)+.1&&
                        std::min(a.Y+a.Height,b.Y+b.Height)>std::max(a.Y,b.Y)+.1)
                    {
                        require(AxisSafety::PairExact(AxisSafety::MakeExact(part->Geometry,placed.Transform),
                            AxisSafety::MakeExact(fixedPart->Geometry,fixed.Transform),problem.PartGap,
                            problem.AllowCommonEdges,problem.Tolerance)==AxisSafety::Verdict::Safe,
                            "ordinary compact units concealed an original-material collision");
                        interlocked=true;
                    }
                }
            }
        }
        require(instances[carrier.ID]==std::set<std::size_t>{0}&&
            instances[left.ID]==std::set<std::size_t>{0,1,2,3,4,5,6,7,8,9},
            "ordinary compact units changed the original inventory");
        require(interlocked,"ordinary compact-unit one-sheet result lost its real self interlock");
        UnchangedOriginals(original,problem);
        require(problem.Stocks[0].ID==original.Stocks[0].ID&&problem.Stocks[0].Quantity==original.Stocks[0].Quantity&&
            problem.Stocks[0].Width==original.Stocks[0].Width&&problem.Stocks[0].Height==original.Stocks[0].Height,
            "ordinary compact units changed the physical stock");
    }
}
bool BoundsOverlapInBothAxes(const Rect& a,const Rect& b)
{
    return std::min(a.X+a.Width,b.X+b.Width)>std::max(a.X,b.X)+.01&&
        std::min(a.Y+a.Height,b.Y+b.Height)>std::max(a.Y,b.Y)+.01;
}
void RequireOrdinaryContourContinuation(const Problem& problem,const Result& result,
    const std::vector<ConstructionTraceEvent>& events,std::size_t movingCount,bool embedded)
{
    const auto checked=Validate(problem,result);
    Require(checked.Valid&&checked.Complete&&checked.PlacedCount==1+movingCount&&result.Layouts.size()==1,
        "ordinary contour continuation lost complete original demand on one sheet");
    std::vector<const ConstructionTraceEvent*> commits;
    const ConstructionTraceEvent* raw=nullptr;
    for(const auto& event:events)
    {
        Require(event.Source!="fill.commit","ordinary contour continuation relied on gap filling");
        if(event.Source=="constructor.commit")commits.push_back(&event);
        if(event.Source=="board.raw-construction"&&event.Layouts.size()==1&&
            event.Layouts.front().Placements.size()==1+movingCount)raw=&event;
    }
    Require(commits.size()==2&&commits[0]->Members.size()==1&&
        commits[0]->Members.front().PartIndex==0&&commits[1]->Members.size()==movingCount&&
        commits[1]->ParentStateID==commits[0]->StateID&&commits[1]->CandidateID!=0,
        "ordinary contour regression did not commit the frame followed directly by the original unit");
    for(const auto* commit:commits)
    {
        Require(commit->Stage==ConstructionTraceStage::Commit&&commit->Layouts.size()==1,
            "ordinary contour commit omitted its real state snapshot");
        const std::map<std::string,double> rank(commit->Rank.begin(),commit->Rank.end());
        Require(rank.size()==commit->Rank.size()&&rank.contains("negativeUsedSectionUtilization")&&
            rank.contains("negativeNetMaterial")&&rank.contains("strategyContact"),
            "ordinary contour commit lost named primary utilization, net material or contact scoring");
        double addedMaterial=0,occupiedMaterial=0;
        for(const auto& member:commit->Members)addedMaterial+=Detail::Area(problem.Parts.at(member.PartIndex).Geometry);
        for(const auto& placed:commit->Layouts.front().Placements)
        {
            const auto part=std::find_if(problem.Parts.begin(),problem.Parts.end(),
                [&](const auto& value){return value.ID==placed.PartID;});
            Require(part!=problem.Parts.end(),"rank proof contains a private rather than demanded original");
            occupiedMaterial+=Detail::Area(part->Geometry);
        }
        const auto committed=std::find_if(events.begin(),events.end(),[&](const auto& event){return &event==commit;});
        const auto proof=std::find_if(events.begin(),committed,[&](const auto& event)
        {
            if(event.Source!="placement.choice"||event.Scope!=commit->Scope||event.StateID!=commit->ParentStateID||
                event.CandidateID!=commit->CandidateID||event.ProposalID!=commit->ProposalID||
                event.Members.size()!=commit->Members.size())return false;
            const std::map<std::string,double> prior(event.Rank.begin(),event.Rank.end());
            return prior==rank&&std::all_of(commit->Members.begin(),commit->Members.end(),[&](const auto& member)
            {
                return std::any_of(event.Members.begin(),event.Members.end(),[&](const auto& proposed)
                    {return proposed.PartIndex==member.PartIndex&&SamePose(proposed.Transform,member.Transform);});
            });
        });
        Require(proof!=committed,"commit's named rank or actual member poses differ from its preceding same-state choice");
        const double section=Measure(*proof,"usedSectionArea");
        Require(section>0&&std::isfinite(section)&&
            std::abs(rank.at("negativeNetMaterial")+addedMaterial)<1e-10*std::max(1.,addedMaterial)&&
            std::abs(rank.at("negativeUsedSectionUtilization")+occupiedMaterial/section)<1e-12&&
            rank.at("strategyContact")==Measure(*commit,"strategyContact")&&
            (Measure(*commit,"spaceScoreValid")==0||Measure(*commit,"spaceScoreValid")==1),
            "common placement rank does not describe the committed originals or the same choice's full-width section");
    }
    const auto& second=*commits[1];
    for(const auto& member:second.Members)
        Require(member.PartIndex==1&&member.Group!=0&&member.Group==second.Members.front().Group,
            "ordinary contour unit was split or replaced by a cross-type carrier");
    const auto sameMembers=[&](const ConstructionTraceEvent& event)
    {
        return event.Members.size()==second.Members.size()&&
            std::all_of(second.Members.begin(),second.Members.end(),[&](const auto& committed)
            {
                return std::any_of(event.Members.begin(),event.Members.end(),[&](const auto& proposed)
                    {return proposed.PartIndex==committed.PartIndex&&SamePose(proposed.Transform,committed.Transform);});
            });
    };
    bool generated=false,accepted=false;
    for(const auto& event:events)
        if(event.StateID==commits[0]->StateID&&event.CandidateID==second.CandidateID&&
            event.ProposalID==second.ProposalID&&sameMembers(event))
        {
            generated|=event.Stage==ConstructionTraceStage::Generate;
            accepted|=event.Stage==ConstructionTraceStage::Filter&&event.Accepted;
        }
    Require(generated&&accepted&&raw&&raw->StateID==second.StateID,
        "ordinary contour winner bypassed same-state proposal generation or original-material filtering");
    const auto& board=result.Layouts.front();const auto& rawBoard=raw->Layouts.front();
    Require(board.StockID==rawBoard.StockID&&board.StockInstance==rawBoard.StockInstance&&
        board.Width==rawBoard.Width&&board.Height==rawBoard.Height&&
        rawBoard.Placements.size()==board.Placements.size(),
        "ordinary contour final stock differs from the raw construction");
    std::map<std::string,std::set<std::size_t>> instances;
    std::map<std::size_t,std::string> groupParts;
    for(std::size_t i=0;i<board.Placements.size();++i)
    {
        const auto& placed=board.Placements[i];
        const auto part=std::find_if(problem.Parts.begin(),problem.Parts.end(),
            [&](const Part& value){return value.ID==placed.PartID;});
        Require(part!=problem.Parts.end()&&instances[placed.PartID].insert(placed.Instance).second&&!placed.GapFiller,
            "ordinary contour output contains private identities, repeated instances or fillers");
        Require(Detail::PoseAllowed(*part,placed.Transform,problem.Tolerance)&&
            AxisSafety::Containment(AxisSafety::Make(part->Geometry,placed.Transform),
                board.Width,board.Height,problem.EdgeMargin)==AxisSafety::Verdict::Safe,
            "ordinary contour output violated an original pose permission or physical stock margin");
        const auto original=std::find_if(rawBoard.Placements.begin(),rawBoard.Placements.end(),
            [&](const Placement& value){return value.PartID==placed.PartID&&value.Instance==placed.Instance;});
        Require(original!=rawBoard.Placements.end()&&SamePose(original->Transform,placed.Transform)&&
            original->Group==placed.Group&&original->GapFiller==placed.GapFiller,
            "a post-construction stage supplied the tested ordinary contour position");
        bool committed=false;
        for(const auto* commit:commits)for(const auto& member:commit->Members)
            committed|=problem.Parts[member.PartIndex].ID==placed.PartID&&member.Instance==placed.Instance&&
                member.Group==placed.Group&&SamePose(member.Transform,placed.Transform);
        Require(committed,"ordinary final output differs from its actual original-member commit");
        if(placed.Group)
        {
            const auto [group,inserted]=groupParts.emplace(placed.Group,placed.PartID);
            Require(inserted||group->second==placed.PartID,"ordinary contour output mixed IDs in a self unit");
        }
        for(std::size_t j=0;j<i;++j)
        {
            const auto& fixed=board.Placements[j];
            const auto fixedPart=std::find_if(problem.Parts.begin(),problem.Parts.end(),
                [&](const Part& value){return value.ID==fixed.PartID;});
            Require(fixedPart!=problem.Parts.end()&&AxisSafety::PairExact(
                AxisSafety::MakeExact(part->Geometry,placed.Transform),
                AxisSafety::MakeExact(fixedPart->Geometry,fixed.Transform),problem.PartGap,
                problem.AllowCommonEdges,problem.Tolerance)==AxisSafety::Verdict::Safe,
                "ordinary contour continuation concealed an original-material collision or gap violation");
        }
    }
    Require(instances[problem.Parts[0].ID]==std::set<std::size_t>{0}&&
        instances[problem.Parts[1].ID].size()==movingCount&&
        *instances[problem.Parts[1].ID].begin()==0&&*instances[problem.Parts[1].ID].rbegin()==movingCount-1,
        "ordinary contour continuation changed the requested original quantities");
    const auto frameBounds=Detail::Bounds(problem.Parts[0].Geometry,commits[0]->Members.front().Transform);
    for(const auto& member:second.Members)
    {
        const auto box=Detail::Bounds(problem.Parts[1].Geometry,member.Transform);
        if(embedded)
            Require(BoundsOverlapInBothAxes(frameBounds,box)&&
                box.Y+box.Height<=frameBounds.Y+frameBounds.Height+1e-5,
                "ordinary contour proposal remained above the frame AABB instead of occupying its low recess");
        else Require(box.Y>=frameBounds.Y+frameBounds.Height+problem.PartGap-1e-5,
            "mirror-prohibited original crossed the frame's closed top without a legal pose");
    }
}
void OrdinaryBulkArraysGrowCertifiedContourUnits(bool diagnostic=false,int onlyKind=-1)
{
    Problem problem;problem.PartGap=.2;problem.EdgeMargin=.5;problem.Tolerance=1e-7;
    auto carrier=Polygon("bulk-full-width-carrier",{{0,0},{8,0},{8,30},{0,30}});
    auto unit=Polygon("bulk-original-unit",{{0,0},{1,0},{1,1},{0,1}},200);
    carrier.AllowedAnglesDegrees={0};unit.AllowedAnglesDegrees={0};
    problem.Parts={carrier,unit};problem.Stocks={{"bulk-original-sheet",9.4,10000,1}};
    const auto original=problem;
    for(const auto kind:{PlaneStrategyKind::ArrayQuantityDeferred,PlaneStrategyKind::ArrayGeometryDeferred})
    {
        if(onlyKind>=0&&int(kind)!=onlyKind)continue;
        bool bulkUnits=false,startedWithCarrier=false,directRectangularGrid=false;std::size_t commits=0,maxRigid=0;
        std::map<std::pair<std::uint64_t,std::string>,std::size_t> generatedSources;
        std::ostringstream timeline;
        auto settings=OriginalNfpSettings(kind);settings.MaxArrayAxis=128;
        auto options=Limits();options.EnableCombinations=false;
        options.TimeLimitSeconds=std::numeric_limits<double>::max();options.MaxPlacementAttempts=10000000;
        options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
        {
            if(diagnostic&&event.Stage==ConstructionTraceStage::Generate)
                ++generatedSources[{event.ProposalID,event.Source}];
            if(event.Source=="constructor.commit")
            {
                if(diagnostic)
                {
                    std::cout<<"BULK-COMMIT kind="<<int(kind)<<" state="<<event.StateID<<" parent="<<event.ParentStateID
                        <<" candidate="<<event.CandidateID<<" proposal="<<event.ProposalID<<" members="<<event.Members.size();
                    for(const auto& [name,value]:event.Rank)std::cout<<" rank."<<name<<'='<<std::setprecision(17)<<value;
                    double minX=std::numeric_limits<double>::infinity(),minY=minX,maxX=-minX,maxY=-minX;
                    std::map<double,std::size_t> rows;
                    for(const auto& member:event.Members)
                    {
                        const auto box=Detail::Bounds(problem.Parts.at(member.PartIndex).Geometry,member.Transform);
                        minX=std::min(minX,box.X);minY=std::min(minY,box.Y);
                        maxX=std::max(maxX,box.X+box.Width);maxY=std::max(maxY,box.Y+box.Height);++rows[box.Y];
                    }
                    std::cout<<" actualMinX="<<minX<<" actualMinY="<<minY<<" actualMaxX="<<maxX<<" actualMaxY="<<maxY;
                    for(const auto& [y,count]:rows)std::cout<<" row["<<y<<"]="<<count;
                    std::cout<<'\n';
                }
                if(!commits)startedWithCarrier=event.Members.size()==1&&event.Members[0].PartIndex==0;
                if(!event.Members.empty()&&event.Members[0].PartIndex==1)
                {
                    const std::map<std::string,double> rank(event.Rank.begin(),event.Rank.end());
                    directRectangularGrid|=event.Members.size()>1&&rank.contains("columns")&&rank.contains("rows")&&
                        rank.at("columns")*rank.at("rows")==double(event.Members.size())&&
                        std::all_of(event.Members.begin(),event.Members.end(),[](const auto& member){return member.PartIndex==1;});
                    double x=std::numeric_limits<double>::infinity(),y=x;
                    for(const auto& member:event.Members)
                    {const auto b=Detail::Bounds(unit.Geometry,member.Transform);x=std::min(x,b.X);y=std::min(y,b.Y);}
                    timeline<<" ["<<event.Members.size()<<" @ "<<std::setprecision(17)<<x<<','<<y<<']';
                }
                ++commits;
            }
            if(event.Reason=="ordinary-array-bulk-contour-unit-growth")
                bulkUnits=bulkUnits||(Measure(event,"remainingOriginals")>Detail::NfpPlacementLimits{}.MaxRigidMembers&&
                    Measure(event,"certifiedUnitSeeds")>0);
            if(event.Source=="array.rigid-query-result")
            {
                maxRigid=std::max(maxRigid,std::size_t(Measure(event,"members")));
                if(diagnostic)
                {
                    std::cout<<"BULK-RIGID-QUERY kind="<<int(kind)<<" state="<<event.StateID<<" proposal="<<event.ProposalID
                        <<" reason="<<event.Reason<<" truncated="<<event.SearchTruncated;
                    for(const auto& [name,value]:event.Measures)std::cout<<' '<<name<<'='<<std::setprecision(17)<<value;
                    std::cout<<'\n';
                }
            }
            if(diagnostic&&(event.Source=="array.original-query-result"||event.Source=="array.query-profile"||
                event.Reason=="ordinary-array-bulk-contour-unit-growth"))
            {
                std::cout<<"BULK-PROFILE kind="<<int(kind)<<" state="<<event.StateID<<" proposal="<<event.ProposalID
                    <<" source="<<event.Source<<" reason="<<event.Reason;
                for(const auto& [name,value]:event.Measures)std::cout<<' '<<name<<'='<<std::setprecision(17)<<value;
                std::cout<<'\n';
            }
        };
        const auto result=MakePlaneStrategyFactory(settings)()->Run(problem,options);
        const auto checked=Validate(problem,result);
        if(diagnostic)
        {
            double finalTop=0;
            for(const auto& board:result.Layouts)for(const auto& placed:board.Placements)
            {
                const auto part=std::find_if(problem.Parts.begin(),problem.Parts.end(),
                    [&](const auto& value){return value.ID==placed.PartID;});
                const auto box=Detail::Bounds(part->Geometry,placed.Transform);finalTop=std::max(finalTop,box.Y+box.Height);
            }
            std::cout<<"BULK-SUMMARY kind="<<int(kind)<<" complete="<<checked.Complete<<" valid="<<checked.Valid
                <<" placed="<<checked.PlacedCount<<" attempts="<<result.Stats.PlacementAttempts<<" commits="<<commits
                <<" startedWithCarrier="<<startedWithCarrier<<" bulkUnits="<<bulkUnits
                <<" directRectangularGrid="<<directRectangularGrid<<" maxRigid="<<maxRigid<<" actualTop="<<finalTop<<'\n';
            for(const auto& [key,count]:generatedSources)
                std::cout<<"BULK-GENERATED kind="<<int(kind)<<" proposal="<<key.first<<" source="<<key.second<<" count="<<count<<'\n';
        }
        Require(checked.Valid&&checked.Complete&&checked.PlacedCount==201&&result.Layouts.size()==1,
            "ordinary bulk unit growth lost original demand, clearance or inventory");
        Require(startedWithCarrier&&maxRigid<=2&&(bulkUnits||(directRectangularGrid&&maxRigid==0)),
            "ordinary bulk continuation lacked either a direct original-safe rectangular grid or bounded certified contour-unit growth");
        double top=0;
        for(const auto& placed:result.Layouts[0].Placements)
        {
            const auto index=placed.PartID==carrier.ID?0:1;
            const auto box=Detail::Bounds(problem.Parts[index].Geometry,placed.Transform);
            top=std::max(top,box.Y+box.Height);
        }
        if(!(top<=66&&result.Stats.PlacementAttempts<=options.MaxPlacementAttempts))
            throw std::runtime_error("bulk seed growth displaced the compact baseline; kind="+
                std::to_string(static_cast<int>(kind))+" top="+std::to_string(top)+
                " attempts="+std::to_string(result.Stats.PlacementAttempts)+timeline.str());
        UnchangedOriginals(original,problem);
    }
}
void OrdinarySmallRepeatsGrowInsideOriginalRecess()
{
    Problem problem;problem.PartGap=.2;problem.EdgeMargin=.5;problem.Tolerance=1e-7;
    auto frame=Polygon("repeat-recess-frame",{{0,0},{10,0},{10,8},{3.5,8},{3.5,4},{0,4}});
    auto tile=Polygon("repeat-recess-tile",{{0,0},{1,0},{1,1},{0,1}},3);
    frame.AllowedAnglesDegrees={0};tile.AllowedAnglesDegrees={0};
    problem.Parts={frame,tile};problem.Stocks={{"repeat-recess-sheet",11,20,1}};
    const auto original=problem;
    for(const auto kind:{PlaneStrategyKind::ArrayQuantityDeferred,PlaneStrategyKind::ArrayGeometryDeferred})
    {
        bool repeatSeeds=false,originalFrameFirst=false;std::size_t commits=0;
        auto settings=OriginalNfpSettings(kind);settings.MaxArrayAxis=8;
        auto options=Limits();options.EnableCombinations=false;
        options.TimeLimitSeconds=std::numeric_limits<double>::max();
        options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
        {
            if(event.Source=="constructor.commit")
            {if(!commits)originalFrameFirst=event.Members.size()==1&&event.Members[0].PartIndex==0;++commits;}
            if(event.Reason=="ordinary-array-bulk-contour-unit-growth")
                repeatSeeds=repeatSeeds||(Measure(event,"remainingOriginals")==3&&Measure(event,"certifiedUnitSeeds")>0);
        };
        const auto result=MakePlaneStrategyFactory(settings)()->Run(problem,options);
        const auto checked=Validate(problem,result);
        Require(checked.Valid&&checked.Complete&&checked.PlacedCount==4&&result.Layouts.size()==1&&
            originalFrameFirst&&repeatSeeds,"ordinary three-copy continuation did not use its real contour unit seeds");
        for(const auto& placed:result.Layouts[0].Placements)if(placed.PartID==tile.ID)
        {
            const auto box=Detail::Bounds(tile.Geometry,placed.Transform);
            Require(box.Y+box.Height<=8.5+1e-6&&box.X+box.Width<=4.-problem.PartGap+1e-6,
                "ordinary repeat growth abandoned the frame's actual low recess for its external AABB strip");
        }
        UnchangedOriginals(original,problem);
    }
}
void IndependentArrayControllersUseActualFillDomains()
{
    Problem problem;problem.PartGap=.2;problem.EdgeMargin=.5;problem.Tolerance=1e-7;
    auto main=Polygon("timing-notched-main",{{0,0},{8,0},{8,2},{5,2},{5,5},{0,5}});
    auto tall=Polygon("timing-tall-next-main",{{0,0},{3,0},{3,7},{0,7}});
    auto small=Polygon("timing-small",{{0,0},{2,0},{2,2},{0,2}},2);
    main.AllowedAnglesDegrees=tall.AllowedAnglesDegrees=small.AllowedAnglesDegrees={0};
    problem.Parts={main,tall,small};problem.Stocks={{"timing-sheet",14,9,2}};
    const auto original=problem;
    // The tall original cannot fit below the first main's five-mm skyline.
    // One small original fits inside its real 3x3 notch; the second requires
    // another part of the sheet. No witness placement is supplied to Run.
    for(const auto kind:{PlaneStrategyKind::ArrayQuantityDeferred,PlaneStrategyKind::ArrayGeometryDeferred,
        PlaneStrategyKind::ArrayGeometryImmediate})
    {
        std::vector<ConstructionTraceEvent> events;
        auto options=Limits();options.EnableCombinations=false;options.EnableGapFill=true;
        options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
        options.ConstructionTrace=[&](const auto& event){events.push_back(event);};
        auto settings=OriginalNfpSettings(kind);settings.MaxArrayAxis=8;
        const auto result=MakePlaneStrategyFactory(settings)()->Run(problem,options);
        const auto checked=Validate(problem,result);
        Require(checked.Valid&&checked.Complete&&checked.PlacedCount==4&&
            !options.ConstructionObservation->Failed.load(),"controller timing fixture lost actual original demand");
        std::map<std::size_t,Layout> prefixes;
        std::map<std::size_t,std::uint64_t> parents;
        std::map<std::size_t,Rect> batches;
        std::map<std::size_t,std::size_t> lastMain;
        std::set<std::size_t> filledBoards;
        std::size_t mains=0,fills=0;
        const auto top=[&](const Layout& board)
        {
            double value=0;
            for(const auto& placed:board.Placements)
            {
                const auto part=std::find_if(problem.Parts.begin(),problem.Parts.end(),
                    [&](const auto& p){return p.ID==placed.PartID;});
                Require(part!=problem.Parts.end(),"timing snapshot contains a private original");
                const auto box=Detail::Bounds(part->Geometry,placed.Transform);value=std::max(value,box.Y+box.Height);
            }
            return value;
        };
        for(std::size_t ei=0;ei<events.size();++ei)
        {
            const auto& event=events[ei];
            if(event.Source!="constructor.commit"&&event.Source!="fill.commit")continue;
            Require(event.Layouts.size()==1&&!event.Members.empty(),"timing commit lost its actual state and original delta");
            const auto& board=event.Layouts.front();const auto instance=board.StockInstance;
            Require(event.ParentStateID==parents[instance],"controller timing crossed independent board parent states");
            const auto& prefix=prefixes[instance];
            Require(board.Placements.size()==prefix.Placements.size()+event.Members.size(),
                "timing snapshot is not the preceding state plus its original delta");
            for(const auto& placed:prefix.Placements)
                Require(std::any_of(board.Placements.begin(),board.Placements.end(),[&](const auto& after)
                    {return after.PartID==placed.PartID&&after.Instance==placed.Instance&&SamePose(after.Transform,placed.Transform);}),
                    "controller timing moved an already committed original");
            if(event.Source=="constructor.commit")
            {
                ++mains;
                if(kind==PlaneStrategyKind::ArrayGeometryDeferred)
                    Require(!filledBoards.contains(instance),"deferred controller resumed main arrays after board-end gap fill");
                const ConstructionTraceEvent* generated=nullptr;
                for(std::size_t pi=0;pi<ei;++pi)
                {
                    const auto& candidate=events[pi];
                    const bool coordinates=std::any_of(candidate.Measures.begin(),candidate.Measures.end(),
                        [](const auto& value){return value.first=="x";});
                    if(candidate.Stage==ConstructionTraceStage::Generate&&coordinates&&candidate.StateID==event.ParentStateID&&
                        candidate.CandidateID==event.CandidateID&&candidate.ProposalID==event.ProposalID)generated=&candidate;
                }
                Require(generated&&generated->Width>0&&generated->Height>0,"main batch lacked its real generated planning box");
                batches[instance]={Measure(*generated,"x"),Measure(*generated,"y"),generated->Width,generated->Height};
                lastMain[instance]=ei;
            }
            else
            {
                ++fills;const bool first=filledBoards.insert(instance).second;
                Require(batches.contains(instance),"gap original was committed before any real main batch");
                const Rect band{Measure(event,"fillBandX"),Measure(event,"fillBandY"),
                    Measure(event,"fillBandWidth"),Measure(event,"fillBandHeight")};
                const auto& batch=batches.at(instance);
                if(kind==PlaneStrategyKind::ArrayGeometryImmediate)
                    Require(std::abs(band.X-batch.X)<1e-7&&std::abs(band.Y-batch.Y)<1e-7&&
                        std::abs(band.Width-batch.Width)<1e-7&&std::abs(band.Height-batch.Height)<1e-7,
                        "immediate gap domain was not the actual preceding main batch's generated box");
                if(kind==PlaneStrategyKind::ArrayQuantityDeferred)
                    Require(band.X==problem.EdgeMargin&&band.Y==problem.EdgeMargin&&
                        band.Width==board.Width-2*problem.EdgeMargin&&
                        std::abs(band.Y+band.Height-top(prefix))<1e-7&&top(board)<=top(prefix)+1e-7,
                        "layered gap escaped its full-width frozen original skyline");
                if(kind==PlaneStrategyKind::ArrayGeometryDeferred&&first)
                {
                    std::size_t stalled=ei;bool queried=false;
                    for(std::size_t pi=lastMain.at(instance)+1;pi<ei;++pi)
                    {
                        const auto& prior=events[pi];
                        if(prior.Source=="construction.phase"&&prior.Stage==ConstructionTraceStage::Complete&&
                            prior.Reason=="all-main-arrays-before-gap-fill"&&
                            prior.StateID==event.ParentStateID&&!prior.SearchTruncated)stalled=pi;
                    }
                    for(std::size_t pi=lastMain.at(instance)+1;pi<stalled;++pi)
                        queried|=events[pi].Source=="array.query-profile"&&events[pi].StateID==event.ParentStateID;
                    Require(stalled<ei&&queried,"deferred first gap commit preceded an actual completed main-array query pass");
                }
                for(const auto& member:event.Members)
                {
                    const auto box=Detail::Bounds(problem.Parts.at(member.PartIndex).Geometry,member.Transform);
                    Require(box.X>=band.X-1e-7&&box.Y>=band.Y-1e-7&&
                        box.X+box.Width<=band.X+band.Width+1e-7&&box.Y+box.Height<=band.Y+band.Height+1e-7,
                        "a committed filler lay outside the actual controller gap domain");
                }
                Require(std::any_of(events.begin(),events.begin()+ei,[&](const auto& proof)
                    {return proof.Stage==ConstructionTraceStage::Filter&&proof.Accepted&&proof.StateID==event.ParentStateID&&
                        proof.CandidateID==event.CandidateID&&proof.ProposalID==event.ProposalID;}),
                    "gap timing was labelled without a same-parent original-material proof");
            }
            prefixes[instance]=board;parents[instance]=event.StateID;
        }
        Require(mains>=2&&(kind==PlaneStrategyKind::ArrayGeometryDeferred||fills>0),
            "controller timing fixture did not exercise multiple real main batches and immediate/layered gap originals");
        UnchangedOriginals(original,problem);
    }
}
void OrdinaryArraysFillTheFrozenBandBeforeTheNextMainArray()
{
    Problem problem;problem.PartGap=.2;problem.EdgeMargin=.5;problem.Tolerance=1e-7;
    problem.AllowCommonEdges=false;
    auto small=Polygon("band-small-L",{{0,0},{2,0},{2,1},{1,1},{1,2},{0,2}},4);
    auto medium=Polygon("band-medium-L",{{0,0},{7,0},{7,2},{2,2},{2,5},{0,5}},2);
    auto large=Polygon("band-large-U",{{0,0},{8,0},{8,6},{6,6},{6,2},{2,2},{2,6},{0,6}},2);
    medium.AllowedAnglesDegrees={0};large.AllowedAnglesDegrees={0};
    // Source order is deliberately opposite to primary size priority. The
    // U's net area is 32, the medium L's 20 and the small L's 3. All three
    // originals are concave, so rectangle-only construction cannot supply it.
    problem.Parts={small,medium,large};problem.Stocks={{"frozen-band-sheet",17.5,12.5,1}};
    const auto original=problem;
    Result witness;witness.State=Status::Complete;
    Layout known{"frozen-band-sheet",0,17.5,12.5,{
        {large.ID,0,{.5,.5,0,false},1,false},{large.ID,1,{8.75,.5,0,false},1,false},
        {small.ID,0,{2.75,2.75,0,false},2,true},{small.ID,1,{6,5,180,false},2,true},
        {small.ID,2,{11,2.75,0,false},3,true},{small.ID,3,{14.25,5,180,false},3,true},
        {medium.ID,0,{.5,6.75,0,false},4,false},{medium.ID,1,{7.75,6.75,0,false},4,false}},116};
    witness.Layouts={known};
    const auto witnessCheck=Validate(problem,witness);
    Require(witnessCheck.Valid&&witnessCheck.Complete&&witnessCheck.PlacedCount==8,
        "array/fill/array fixture has no complete original-material witness");
    // Two U's fit only side by side. Each real 4x4 notch accepts the small
    // opposite-angle L pair, while the 7x5 medium L cannot fit in that band.
    // This is the layered controller's frozen-band contract. Deferred arrays
    // fill after their main pass and are tested separately for that behavior.
    // The witness is checked independently and is never passed to Run.
    for(const auto kind:{PlaneStrategyKind::ArrayQuantityDeferred})
    for(const bool combinations:{false,true})
    {
        std::vector<ConstructionTraceEvent> commits;std::vector<Layout> rawBoards;
        const auto require=[&](bool value,const char* message)
        {
            if(value)return;
            std::ostringstream sequence;
            if(commits.empty())sequence<<"none";
            else
            {
                const auto& first=commits.front();
                sequence<<"first="<<first.Source<<" members="<<first.Members.size()<<" indices=";
                for(std::size_t i=0;i<first.Members.size();++i)
                    sequence<<(i?",":"")<<first.Members[i].PartIndex;
                sequence<<"; sequence=";
                for(const auto& event:commits)
                {
                    sequence<<'['<<(event.Source=="fill.commit"?'F':'C')<<" n="<<event.Members.size()<<" p=";
                    for(std::size_t i=0;i<event.Members.size();++i)
                        sequence<<(i?",":"")<<event.Members[i].PartIndex;
                    sequence<<" state="<<event.ParentStateID<<"->"<<event.StateID<<']';
                }
            }
            throw std::runtime_error(std::string(message)+"; kind="+
                std::to_string(static_cast<int>(kind))+" combinations="+std::to_string(combinations)+
                "; commits="+sequence.str());
        };
        auto options=Limits();options.EnableGapFill=true;options.EnableCombinations=combinations;
        options.TimeLimitSeconds=std::numeric_limits<double>::max();
        options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
        options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
        {
            if(event.Stage==ConstructionTraceStage::Commit&&
                (event.Source=="constructor.commit"||event.Source=="fill.commit"))commits.push_back(event);
            if(event.Source=="board.raw-construction")
                rawBoards.insert(rawBoards.end(),event.Layouts.begin(),event.Layouts.end());
        };
        auto settings=OriginalNfpSettings(kind);settings.MaxArrayAxis=8;
        const auto result=MakePlaneStrategyFactory(settings)()->Run(problem,options);
        const auto checked=Validate(problem,result);
        require(checked.Valid&&checked.Complete&&checked.PlacedCount==8&&result.Layouts.size()==1,
            "array/fill/array construction lost complete original demand on one physical sheet");
        require(!options.ConstructionObservation->Failed.load()&&
            result.Stats.PlacementAttempts<=options.MaxPlacementAttempts,
            "array/fill/array ordering depended on failed observation or an exhausted attempt budget");
        require(!commits.empty()&&commits.front().Source=="constructor.commit"&&
            commits.front().Members.size()==2&&std::all_of(commits.front().Members.begin(),commits.front().Members.end(),
                [](const auto& member){return member.PartIndex==2;}),
            "small source-first originals displaced the initial largest-original array");
        const auto partFor=[&](const Placement& placed)->const Part&
        {
            const auto found=std::find_if(problem.Parts.begin(),problem.Parts.end(),
                [&](const Part& part){return part.ID==placed.PartID;});
            require(found!=problem.Parts.end(),"filled-band state contains a private original identity");return *found;
        };
        const auto topOf=[&](const Layout& board)
        {
            double top=0;
            for(const auto& placed:board.Placements)
            {const auto box=Detail::Bounds(partFor(placed).Geometry,placed.Transform);top=std::max(top,box.Y+box.Height);}
            return top;
        };
        require(commits.front().Layouts.size()==1&&commits.front().Layouts.front().Placements.size()==2,
            "initial largest array did not establish a real committed band");
        const double frozenTop=topOf(commits.front().Layouts.front());
        std::size_t mainCommits=0,filledSmall=0,fillCommits=0;
        bool nextArray=false,compactSelfFill=false;
        Layout prefix;std::uint64_t parent=0;
        for(const auto& event:commits)
        {
            require(event.Layouts.size()==1&&event.ParentStateID==parent,
                "filled-band commits did not follow the actual preceding original state");
            const auto& board=event.Layouts.front();
            require(board.StockID==problem.Stocks.front().ID&&board.StockInstance==0&&
                !event.Members.empty()&&std::all_of(event.Members.begin(),event.Members.end(),[&](const auto& member)
                    {return member.PartIndex==event.Members.front().PartIndex;}),
                "filled-band scheduling changed stock or made a cross-original combination");
            for(const auto& old:prefix.Placements)
            {
                const auto found=std::find_if(board.Placements.begin(),board.Placements.end(),[&](const Placement& placed)
                    {return placed.PartID==old.PartID&&placed.Instance==old.Instance;});
                require(found!=board.Placements.end()&&SamePose(found->Transform,old.Transform)&&
                    found->Group==old.Group&&found->GapFiller==old.GapFiller,
                    "filled-band scheduling moved, regrouped or replaced an already committed original");
            }
            if(event.Source=="constructor.commit")
            {
                ++mainCommits;
                if(mainCommits>1)
                {
                    require(mainCommits==2&&filledSmall==small.Quantity&&fillCommits>0&&
                        event.Members.size()==2&&std::all_of(event.Members.begin(),event.Members.end(),
                            [](const auto& member){return member.PartIndex==1;}),
                        "the next primary array ran before the existing band consumed its small originals");
                    nextArray=true;
                }
            }
            else
            {
                require(mainCommits==1&&!nextArray&&std::all_of(event.Members.begin(),event.Members.end(),
                    [](const auto& member){return member.PartIndex==0;}),
                    "band filling skipped the established gap or consumed the next large primary array");
                require(topOf(board)<=frozenTop&&topOf(board)<=topOf(prefix),
                    "a filled-band commit raised the original-material height instead of using existing gaps");
                const auto recordedTop=std::find_if(event.Measures.begin(),event.Measures.end(),
                    [](const auto& measure){return measure.first=="frozenFillTop";});
                require(recordedTop!=event.Measures.end()&&recordedTop->second==frozenTop,
                    "the unsampled original fill commit lost its actual frozen band boundary");
                for(const auto& placed:board.Placements)if(placed.PartID==small.ID)
                    require(placed.GapFiller,"a filled-band original lost its real filler commit marking");
                filledSmall+=event.Members.size();++fillCommits;
                for(std::size_t i=0;i<event.Members.size();++i)for(std::size_t j=0;j<i;++j)
                {
                    const auto& a=event.Members[i];const auto& b=event.Members[j];
                    if(!BoundsOverlapInBothAxes(Detail::Bounds(small.Geometry,a.Transform),
                        Detail::Bounds(small.Geometry,b.Transform)))continue;
                    require(AxisSafety::PairExact(AxisSafety::MakeExact(small.Geometry,a.Transform),
                        AxisSafety::MakeExact(small.Geometry,b.Transform),problem.PartGap,false,
                        problem.Tolerance)==AxisSafety::Verdict::Safe,
                        "compact band filler bypassed actual original-material clearance");
                    compactSelfFill=true;
                }
            }
            prefix=board;parent=event.StateID;
        }
        require(mainCommits==2&&nextArray&&filledSmall==4&&fillCommits>0&&(!combinations||compactSelfFill),
            "ordinary construction did not execute largest array, legal band fill and next primary array");
        const auto& final=result.Layouts.front();
        const auto raw=std::find_if(rawBoards.begin(),rawBoards.end(),[&](const Layout& board)
            {return board.StockID==final.StockID&&board.StockInstance==final.StockInstance&&board.Placements.size()==8;});
        require(raw!=rawBoards.end()&&prefix.Placements.size()==8,
            "a later stage supplied the claimed complete array/fill/array layout");
        std::map<std::string,std::set<std::size_t>> instances;
        for(const auto& placed:final.Placements)
        {
            require(instances[placed.PartID].insert(placed.Instance).second,
                "filled-band scheduling reused an original instance");
            for(const auto* snapshot:{&prefix,&*raw})
            {
                const auto originalPose=std::find_if(snapshot->Placements.begin(),snapshot->Placements.end(),[&](const Placement& item)
                    {return item.PartID==placed.PartID&&item.Instance==placed.Instance;});
                require(originalPose!=snapshot->Placements.end()&&SamePose(originalPose->Transform,placed.Transform)&&
                    originalPose->Group==placed.Group&&originalPose->GapFiller==placed.GapFiller,
                    "the raw/final layout changed an actual array or filler original pose");
            }
        }
        require(instances[small.ID]==std::set<std::size_t>{0,1,2,3}&&
            instances[medium.ID]==std::set<std::size_t>{0,1}&&instances[large.ID]==std::set<std::size_t>{0,1},
            "filled-band construction changed the original quantities");
        UnchangedOriginals(original,problem);
    }
}
void OrdinaryBandFillPreservesTheNextPrimaryContourInterlock()
{
    Problem problem;problem.PartGap=.2;problem.EdgeMargin=.5;problem.Tolerance=1e-7;
    problem.AllowCommonEdges=false;
    auto small=Polygon("protected-band-small-square",{{0,0},{1.5,0},{1.5,1.5},{0,1.5}},5);
    auto large=Polygon("protected-band-large-L",{{0,0},{8,0},{8,2},{2,2},{2,4},{0,4}},2);
    small.AllowedAnglesDegrees={0};small.AllowMirror=false;large.AllowMirror=false;
    problem.Parts={small,large};problem.Stocks={{"protected-band-sheet",14,7.5,2}};
    const auto original=problem;
    // Neither a two-column nor a two-row same-direction L array fits. Filling
    // the entire right strip first blocks a tighter opposite-angle L. The
    // rightmost strip still accepts small fillers without losing that interlock.
    // This independent witness is never supplied to construction.
    Result witness;witness.State=Status::Complete;
    witness.Layouts={{"protected-band-sheet",0,14,7.5,{
        {large.ID,0,{.5,.5,0,false},1,false},{large.ID,1,{10.75,4.75,180,false},2,false},
        {small.ID,0,{11,.5,0,false},3,true},{small.ID,1,{11,2.25,0,false},3,true},
        {small.ID,2,{.5,5,0,false},4,false},{small.ID,3,{2.25,5,0,false},4,false},
        {small.ID,4,{4,5,0,false},4,false}},51.25}};
    const auto witnessCheck=Validate(problem,witness);
    Require(witnessCheck.Valid&&witnessCheck.Complete&&witnessCheck.PlacedCount==7,
        "next-primary protection fixture has no complete original-material witness");
    for(const auto kind:{PlaneStrategyKind::ArrayQuantityDeferred})
    {
        std::vector<ConstructionTraceEvent> commits;std::vector<Layout> rawBoards;
        std::uint64_t completedBandState=0;bool bandCompletionOrderOK=true;
        std::vector<ConstructionTraceEvent> choiceSnapshots;
        const auto require=[&](bool value,const char* message)
        {
            if(value)return;
            std::ostringstream sequence;sequence<<std::setprecision(17);
            for(const auto& event:commits)
            {
                sequence<<'['<<(event.Source=="fill.commit"?'F':'C')<<" state="
                    <<event.ParentStateID<<"->"<<event.StateID;
                for(const auto& member:event.Members)sequence<<" p="<<member.PartIndex<<"#"<<member.Instance
                    <<"@"<<member.Transform.X<<','<<member.Transform.Y<<','<<member.Transform.AngleDegrees;
                sequence<<']';
            }
            throw std::runtime_error(std::string(message)+"; kind="+
                std::to_string(static_cast<int>(kind))+"; commits="+sequence.str());
        };
        auto settings=OriginalNfpSettings(kind);settings.MaxArrayAxis=8;
        auto options=Limits();options.EnableGapFill=true;options.EnableCombinations=false;
        options.TimeLimitSeconds=std::numeric_limits<double>::max();
        options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
        options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
        {
            if(event.Source=="placement.choice"&&!event.Layouts.empty())choiceSnapshots.push_back(event);
            if(event.Source=="construction.fill-band-complete")
            {completedBandState=event.StateID;bandCompletionOrderOK&=!event.SearchTruncated;}
            if(event.Source=="constructor.commit"&&event.ParentStateID&&completedBandState!=event.ParentStateID)
                bandCompletionOrderOK=false;
            if(event.Stage==ConstructionTraceStage::Commit&&
                (event.Source=="constructor.commit"||event.Source=="fill.commit"))commits.push_back(event);
            if(event.Source=="board.raw-construction")
                rawBoards.insert(rawBoards.end(),event.Layouts.begin(),event.Layouts.end());
        };
        const auto result=MakePlaneStrategyFactory(settings)()->Run(problem,options);
        const auto checked=Validate(problem,result);
        require(checked.Valid&&checked.Complete&&checked.PlacedCount==7&&result.Layouts.size()==1,
            "frozen-band filling blocked a legal next primary interlock and lost the one-sheet construction");
        require(!options.ConstructionObservation->Failed.load()&&
            result.Stats.PlacementAttempts<=options.MaxPlacementAttempts&&bandCompletionOrderOK,
            "next-primary protection failed observation, exhausted its budget or started a main array before the old band ended");
        require(!commits.empty()&&commits.front().Source=="constructor.commit"&&
            commits.front().Members.size()==1&&commits.front().Members.front().PartIndex==1,
            "next-primary regression did not start with one largest original L");
        require(!choiceSnapshots.empty(),"next-primary regression observed no real placement-choice snapshots");
        for(const auto& choice:choiceSnapshots)
        {
            if(!choice.StateID)
            {
                require(choice.Layouts.size()==1&&choice.Layouts.front().Placements.empty()&&
                    choice.Layouts.front().MaterialArea==0&&choice.Layouts.front().StockID==problem.Stocks.front().ID&&
                    choice.Layouts.front().StockInstance==0&&choice.Layouts.front().Width==14&&choice.Layouts.front().Height==7.5,
                    "uncommitted next-primary reservation appeared as placed material in an initial choice snapshot");
                continue;
            }
            const auto state=std::find_if(commits.begin(),commits.end(),[&](const ConstructionTraceEvent& committed)
                {return committed.StateID==choice.StateID;});
            require(state!=commits.end()&&choice.Layouts.size()==state->Layouts.size(),
                "placement-choice snapshot refers to an uncommitted original state");
            for(std::size_t i=0;i<choice.Layouts.size();++i)
            {
                const auto& shown=choice.Layouts[i];const auto& committed=state->Layouts[i];
                require(shown.StockID==committed.StockID&&shown.StockInstance==committed.StockInstance&&
                    shown.Width==committed.Width&&shown.Height==committed.Height&&
                    shown.MaterialArea==committed.MaterialArea&&shown.Placements.size()==committed.Placements.size()&&
                    std::equal(shown.Placements.begin(),shown.Placements.end(),committed.Placements.begin(),
                        [](const Placement& a,const Placement& b)
                        {return a.PartID==b.PartID&&a.Instance==b.Instance&&SamePose(a.Transform,b.Transform)&&
                            a.Group==b.Group&&a.GapFiller==b.GapFiller;}),
                    "placement-choice replay displayed protected next-array witnesses as already committed originals");
            }
        }
        const auto partFor=[&](const Placement& placed)->const Part&
        {
            const auto found=std::find_if(problem.Parts.begin(),problem.Parts.end(),
                [&](const Part& part){return part.ID==placed.PartID;});
            require(found!=problem.Parts.end(),"protected-band commit contains a private original identity");return *found;
        };
        const auto topOf=[&](const Layout& board)
        {
            double top=0;
            for(const auto& placed:board.Placements)
            {const auto box=Detail::Bounds(partFor(placed).Geometry,placed.Transform);top=std::max(top,box.Y+box.Height);}
            return top;
        };
        Layout prefix;std::uint64_t parent=0;std::size_t largeCommits=0,smallCommitted=0,filledBeforeNext=0;
        double frozenTop=0;const ConstructionTraceEvent* nextMain=nullptr;
        for(const auto& event:commits)
        {
            require(event.Layouts.size()==1&&event.ParentStateID==parent&&!event.Members.empty(),
                "protected-band commits skipped an actual preceding original state");
            const auto& board=event.Layouts.front();
            require(board.StockID==problem.Stocks.front().ID&&board.StockInstance==0&&
                board.Placements.size()==prefix.Placements.size()+event.Members.size(),
                "protected-band construction changed physical stock or replaced committed members");
            for(const auto& old:prefix.Placements)
            {
                const auto found=std::find_if(board.Placements.begin(),board.Placements.end(),[&](const Placement& placed)
                    {return placed.PartID==old.PartID&&placed.Instance==old.Instance;});
                require(found!=board.Placements.end()&&SamePose(found->Transform,old.Transform)&&
                    found->Group==old.Group&&found->GapFiller==old.GapFiller,
                    "next-primary protection moved, regrouped or replaced an existing original");
            }
            if(event.Source=="constructor.commit")
            {
                if(event.Members.front().PartIndex==1)
                {
                    ++largeCommits;
                    require(largeCommits<=2&&event.Members.size()==1,
                        "protected-band construction bypassed the intended successive primary L commits");
                    if(largeCommits==2)
                    {
                        require(smallCommitted>=1&&smallCommitted<=2,
                            "band filling skipped safe right-strip space or obstructed the tight next-primary contour");
                        filledBeforeNext=smallCommitted;nextMain=&event;
                        const auto& next=event.Members.front();
                        const auto nextBounds=Detail::Bounds(large.Geometry,next.Transform);
                        require(nextBounds.Y+nextBounds.Height<=4.70001,
                            "first-band fillers displaced the next primary L above its tight certified interlock");
                        for(const auto& old:prefix.Placements)
                            require(AxisSafety::PairExact(AxisSafety::MakeExact(partFor(old).Geometry,old.Transform),
                                AxisSafety::MakeExact(large.Geometry,next.Transform),problem.PartGap,false,
                                problem.Tolerance)==AxisSafety::Verdict::Safe,
                                "a first-band filler obstructed the actual next-primary contour placement");
                    }
                }
                else require(nextMain&&std::all_of(event.Members.begin(),event.Members.end(),
                    [](const auto& member){return member.PartIndex==0;}),
                    "a new small-part primary array ran before the protected next L");
                frozenTop=topOf(board);
            }
            else
            {
                require(largeCommits>=1&&largeCommits<=2&&std::all_of(event.Members.begin(),event.Members.end(),
                    [](const auto& member){return member.PartIndex==0;}),
                    "protected band consumed the next primary L as a filler");
                require(topOf(board)<=frozenTop&&topOf(board)<=topOf(prefix),
                    "protected-band filling raised the actual original-material height");
                require(Measure(event,"frozenFillTop")==frozenTop,
                    "protected-band fill did not record the current actual frozen boundary");
                for(const auto& member:event.Members)
                {
                    const auto placed=std::find_if(board.Placements.begin(),board.Placements.end(),[&](const Placement& value)
                        {return value.PartID==small.ID&&value.Instance==member.Instance;});
                    require(placed!=board.Placements.end()&&placed->GapFiller,
                        "protected-band small original lost its actual filler provenance");
                }
            }
            smallCommitted+=std::count_if(event.Members.begin(),event.Members.end(),
                [](const auto& member){return member.PartIndex==0;});
            prefix=board;parent=event.StateID;
        }
        require(largeCommits==2&&nextMain&&filledBeforeNext>=1&&filledBeforeNext<=2&&
            smallCommitted==5&&prefix.Placements.size()==7,
            "construction omitted safe first-band fill, the tight next primary or remaining small originals");
        const auto& first=commits.front().Members.front();const auto& next=nextMain->Members.front();
        require(first.Transform.AngleDegrees!=next.Transform.AngleDegrees&&
            BoundsOverlapInBothAxes(Detail::Bounds(large.Geometry,first.Transform),Detail::Bounds(large.Geometry,next.Transform)),
            "protected next primary used separated envelopes instead of opposite-angle original-material interlocking");
        const auto raw=std::find_if(rawBoards.begin(),rawBoards.end(),[](const Layout& board)
            {return board.StockInstance==0&&board.Placements.size()==7;});
        require(raw!=rawBoards.end(),"a later processing stage supplied the protected one-sheet construction");
        for(const auto& final:result.Layouts.front().Placements)for(const auto* snapshot:{&prefix,&*raw})
        {
            const auto placed=std::find_if(snapshot->Placements.begin(),snapshot->Placements.end(),[&](const Placement& value)
                {return value.PartID==final.PartID&&value.Instance==final.Instance;});
            require(placed!=snapshot->Placements.end()&&SamePose(placed->Transform,final.Transform)&&
                placed->Group==final.Group&&placed->GapFiller==final.GapFiller,
                "raw/final output changed a protected primary or filled-band original");
        }
        UnchangedOriginals(original,problem);
    }
}
void OrdinaryUnchangedBandReusesItsCertifiedNextMainArray()
{
    Problem problem;problem.PartGap=.2;problem.EdgeMargin=.5;problem.Tolerance=1e-7;
    problem.AllowCommonEdges=false;
    auto small=Polygon("unchanged-band-small",{{0,0},{.5,0},{.5,1},{0,1}},3);
    auto large=Polygon("unchanged-band-large",{{0,0},{2,0},{2,2},{0,2}});
    small.AllowedAnglesDegrees={0};large.AllowedAnglesDegrees={0};
    small.AllowMirror=false;large.AllowMirror=false;
    problem.Parts={small,large};problem.Stocks={{"unchanged-band-sheet",3,8,1}};
    const auto original=problem;
    Result witness;witness.State=Status::Complete;
    witness.Layouts={{"unchanged-band-sheet",0,3,8,{
        {large.ID,0,{.5,.5,0,false},1,false},{small.ID,0,{.5,2.75,0,false},2,false},
        {small.ID,1,{1.25,2.75,0,false},2,false},{small.ID,2,{2,2.75,0,false},2,false}},5.5}};
    const auto witnessCheck=Validate(problem,witness);
    Require(witnessCheck.Valid&&witnessCheck.Complete&&witnessCheck.PlacedCount==4,
        "unchanged-band reuse fixture has no complete original-material witness");
    // The large square exactly fills the usable width. There is no legal
    // small-part placement below its top. All three 0.5 x 1 originals fit
    // in one row above it, so adding all three improves utilization at the
    // same height. The completed next-array search must survive the zero-fill
    // pass and supply that naturally preferred three-original commit.
    for(const auto kind:{PlaneStrategyKind::ArrayQuantityDeferred})
    {
        std::vector<ConstructionTraceEvent> timeline;
        auto options=Limits();options.EnableGapFill=true;options.EnableCombinations=false;
        options.TimeLimitSeconds=std::numeric_limits<double>::max();
        options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
        options.ConstructionTrace=[&](const ConstructionTraceEvent& event){timeline.push_back(event);};
        auto settings=OriginalNfpSettings(kind);settings.MaxArrayAxis=8;
        const auto result=MakePlaneStrategyFactory(settings)()->Run(problem,options);
        const auto require=[&](bool value,const char* message)
        {if(!value)throw std::runtime_error(std::string(message)+"; kind="+std::to_string(static_cast<int>(kind)));};
        const auto checked=Validate(problem,result);
        require(checked.Valid&&checked.Complete&&checked.PlacedCount==4&&result.Layouts.size()==1&&
            !options.ConstructionObservation->Failed.load()&&result.Stats.PlacementAttempts<=options.MaxPlacementAttempts,
            "unchanged-band continuation lost complete one-sheet original construction");
        std::vector<std::size_t> commits;
        for(std::size_t i=0;i<timeline.size();++i)
        {
            require(timeline[i].Source!="fill.commit","a full-width original band admitted a fictitious filler");
            if(timeline[i].Source=="constructor.commit")commits.push_back(i);
        }
        require(commits.size()==2,"unchanged-band continuation did not retain the full next-original array");
        const auto& first=timeline[commits[0]];const auto& next=timeline[commits[1]];
        require(first.Members.size()==1&&first.Members.front().PartIndex==1&&next.Members.size()==3&&
            std::all_of(next.Members.begin(),next.Members.end(),[](const auto& member){return member.PartIndex==0;})&&
            next.ParentStateID==first.StateID&&first.Layouts.size()==1&&next.Layouts.size()==1&&
            first.Layouts.front().Placements.size()==1&&next.Layouts.front().Placements.size()==4,
            "unchanged-band fixture did not execute largest square then one complete three-original primary array");
        std::size_t bandComplete=timeline.size();bool forwardSearch=false,reused=false;
        for(std::size_t i=commits[0]+1;i<commits[1];++i)
        {
            const auto& event=timeline[i];
            if(event.StateID!=first.StateID)continue;
            if(event.Source=="construction.fill-band-complete")
            {require(!event.SearchTruncated,"unchanged band ended on an exhausted search budget");bandComplete=i;}
            if(event.Source=="array.query-profile")
            {
                require(bandComplete==timeline.size(),
                    "unchanged original state repeated its complete array search after the zero-fill band ended");
                forwardSearch=true;
            }
            if(event.Source=="array.unchanged-state-continuation"&&
                event.Stage==ConstructionTraceStage::Filter&&event.Accepted)
            {
                require(bandComplete< i,"certified unchanged continuation ran before its zero-fill band completed");
                reused=true;
            }
        }
        require(forwardSearch&&bandComplete<commits[1]&&reused,
            "zero-fill continuation bypassed its completed forward search or failed to reuse its real original certificate");
        const auto& before=first.Layouts.front().Placements.front();
        for(const auto* layout:{&next.Layouts.front(),&result.Layouts.front()})
        {
            const auto kept=std::find_if(layout->Placements.begin(),layout->Placements.end(),[&](const Placement& placed)
                {return placed.PartID==before.PartID&&placed.Instance==before.Instance;});
            require(kept!=layout->Placements.end()&&SamePose(kept->Transform,before.Transform)&&
                kept->Group==before.Group&&kept->GapFiller==before.GapFiller,
                "unchanged-state reuse moved or replaced the already committed full-width original");
        }
        UnchangedOriginals(original,problem);
    }
}
void OrdinaryCombinationArraysFinishGapArraysAndSingleNfpBeforeTheNextBlank()
{
    Problem problem;problem.PartGap=.2;problem.EdgeMargin=.5;problem.Tolerance=1e-7;
    problem.AllowCommonEdges=false;
    auto small=Polygon("phased-small-L",{{0,0},{2,0},{2,1},{1,1},{1,2},{0,2}},5);
    auto medium=Polygon("phased-medium-L",{{0,0},{7,0},{7,2},{2,2},{2,5},{0,5}},2);
    auto large=Polygon("phased-large-U",{{0,0},{8,0},{8,6},{6,6},{6,2},{2,2},{2,6},{0,6}},2);
    problem.Parts={small,medium,large};problem.Stocks={{"phased-combo-sheet",15.5,15,1}};
    const auto original=problem;
    Result witness;witness.State=Status::Complete;
    witness.Layouts={{"phased-combo-sheet",0,15.5,15,{
        {large.ID,0,{.5,.5,0,false},1,false},{large.ID,1,{10.75,8.75,180,false},1,false},
        {small.ID,0,{11,.5,0,false},2,true},{small.ID,1,{14.25,2.75,180,false},2,true},
        {small.ID,2,{11,3,0,false},3,true},{small.ID,3,{14.25,5.25,180,false},3,true},
        {small.ID,4,{11,5.5,0,false},4,true},
        {medium.ID,0,{.5,9,0,false},5,false},{medium.ID,1,{9.75,14.25,180,false},5,false}},119}};
    const auto witnessCheck=Validate(problem,witness);
    Require(witnessCheck.Valid&&witnessCheck.Complete&&witnessCheck.PlacedCount==9,
        "phased combination-array fixture has no complete original-material witness");
    // The largest U pair and smaller L pairs genuinely interlock. The right
    // strip accepts the small pairs and their odd single-original tail below
    // the U pair's actual top. The medium originals need the next blank area.
    // This witness is checked independently, never passed to construction.
    for(const auto kind:{PlaneStrategyKind::ArrayQuantityDeferred})
    {
        std::vector<ConstructionTraceEvent> timeline;std::vector<Layout> rawBoards;
        auto options=Limits();options.EnableCombinations=true;options.EnableGapFill=true;
        options.MaxPatternMembers=2;options.TimeLimitSeconds=std::numeric_limits<double>::max();
        options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
        options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
        {
            if(event.Source=="construction.phase"||event.Source=="constructor.commit"||event.Source=="fill.commit"||
                event.Source=="construction.pool-recheck"||event.Source=="placement.choice"||
                ((event.Source=="nfp.query"||event.Source=="nfp.original-clearance")&&event.Stage==ConstructionTraceStage::Begin))
                timeline.push_back(event);
            if(event.Source=="board.raw-construction")
                rawBoards.insert(rawBoards.end(),event.Layouts.begin(),event.Layouts.end());
        };
        auto settings=OriginalNfpSettings(kind);settings.MaxArrayAxis=8;
        const auto result=MakePlaneStrategyFactory(settings)()->Run(problem,options);
        const auto require=[&](bool value,const char* message)
        {
            if(value)return;
            std::ostringstream sequence;
            for(const auto& event:timeline)
                if(event.Source=="construction.phase"||event.Source=="constructor.commit"||event.Source=="fill.commit")
                    sequence<<'['<<event.Source<<':'<<event.Reason<<" stage="<<static_cast<int>(event.Stage)
                        <<" state="<<event.ParentStateID<<"->"<<event.StateID<<" n="<<event.Members.size()<<']';
            throw std::runtime_error(std::string(message)+"; kind="+std::to_string(static_cast<int>(kind))+
                "; sequence="+sequence.str());
        };
        const auto checked=Validate(problem,result);
        require(checked.Valid&&checked.Complete&&checked.PlacedCount==9&&result.Layouts.size()==1,
            "phased combination construction lost complete one-sheet original demand");
        require(!options.ConstructionObservation->Failed.load()&&result.Stats.PlacementAttempts<=options.MaxPlacementAttempts,
            "phased construction depended on failed observation or an exhausted attempt budget");
        const auto partFor=[&](const Placement& placed)->const Part&
        {
            const auto found=std::find_if(problem.Parts.begin(),problem.Parts.end(),
                [&](const Part& part){return part.ID==placed.PartID;});
            require(found!=problem.Parts.end(),"phased construction contains a private original identity");return *found;
        };
        const auto topOf=[&](const Layout& board)
        {
            double top=0;
            for(const auto& placed:board.Placements)
            {const auto box=Detail::Bounds(partFor(placed).Geometry,placed.Transform);top=std::max(top,box.Y+box.Height);}
            return top;
        };
        const auto interlockedMembers=[&](const ConstructionTraceEvent& event)
        {
            for(std::size_t i=0;i<event.Members.size();++i)for(std::size_t j=0;j<i;++j)
            {
                const auto& a=event.Members[i];const auto& b=event.Members[j];
                if(a.PartIndex!=b.PartIndex||!BoundsOverlapInBothAxes(
                    Detail::Bounds(problem.Parts[a.PartIndex].Geometry,a.Transform),
                    Detail::Bounds(problem.Parts[b.PartIndex].Geometry,b.Transform)))continue;
                require(AxisSafety::PairExact(AxisSafety::MakeExact(problem.Parts[a.PartIndex].Geometry,a.Transform),
                    AxisSafety::MakeExact(problem.Parts[b.PartIndex].Geometry,b.Transform),problem.PartGap,false,
                    problem.Tolerance)==AxisSafety::Verdict::Safe,
                    "phased combined originals concealed an actual material collision");
                return true;
            }
            return false;
        };
        Layout prefix;std::uint64_t parent=0;std::string phase;std::set<std::uint64_t> ancestorStates{0};
        bool comboDone=false,singleDone=false,nextBlank=false,mainInterlock=false,gapInterlock=false,singleNfp=false;
        std::size_t mainCommits=0,firstBandCombo=0,firstBandSingles=0,firstBandSmall=0;
        double frozenTop=0;
        for(std::size_t ei=0;ei<timeline.size();++ei)
        {
            const auto& event=timeline[ei];
            if(event.Source=="construction.phase")
            {
                if(event.Stage==ConstructionTraceStage::Begin)
                {
                    if(event.Reason=="main-combination-array")
                    {
                        require(!mainCommits||(comboDone&&singleDone&&nextBlank),
                            "next primary combination array started before both gap phases ended");
                        comboDone=false;singleDone=false;nextBlank=false;
                    }
                    if(event.Reason=="gap-single-nfp")
                        require(comboDone,"single-original NFP fill started before gap combination arrays finished");
                    if(event.Reason=="next-blank-area")
                    {
                        require(comboDone&&singleDone,"next blank area was opened before both gap stages finished");
                        nextBlank=true;
                    }
                    phase=event.Reason;
                }
                else if(event.Stage==ConstructionTraceStage::Complete)
                {
                    require(!event.SearchTruncated,"phased gap processing ended on a truncated construction search");
                    if(event.Reason=="gap-combination-array")comboDone=true;
                    if(event.Reason=="gap-single-nfp")singleDone=true;
                    if(phase==event.Reason)phase.clear();
                }
                continue;
            }
            if(event.Source!="constructor.commit"&&event.Source!="fill.commit")continue;
            require(event.Stage==ConstructionTraceStage::Commit&&event.Layouts.size()==1&&
                event.ParentStateID==parent&&!event.Members.empty(),
                "phased commits did not preserve the real preceding original state");
            const auto& board=event.Layouts.front();
            require(board.StockID==problem.Stocks.front().ID&&board.StockInstance==0&&
                board.Placements.size()==prefix.Placements.size()+event.Members.size(),
                "phased construction changed physical stock or replaced committed originals");
            require(std::all_of(event.Members.begin(),event.Members.end(),[&](const auto& member)
                {return member.PartIndex==event.Members.front().PartIndex&&member.Group!=0;}),
                "phased construction used a mixed-original or unrecorded planning group");
            for(const auto& old:prefix.Placements)
            {
                const auto kept=std::find_if(board.Placements.begin(),board.Placements.end(),[&](const Placement& placed)
                    {return placed.PartID==old.PartID&&placed.Instance==old.Instance;});
                require(kept!=board.Placements.end()&&SamePose(kept->Transform,old.Transform)&&
                    kept->Group==old.Group&&kept->GapFiller==old.GapFiller,
                    "a phased array or NFP gap fill moved, regrouped or replaced an existing original");
            }
            if(event.Source=="constructor.commit")
            {
                require(phase=="main-combination-array"&&Measure(event,"constructionPhase")==1,
                    "primary originals bypassed the genuine combination-array phase");
                require(Measure(event,"unitMembers")>=2&&Measure(event,"arrayCells")>=1&&
                    Measure(event,"unitMembers")*Measure(event,"arrayCells")==event.Members.size(),
                    "primary combination-array commit lost its actual combined unit or array-cell evidence");
                if(!mainCommits)
                {
                    require(event.Members.size()>=2&&event.Members.front().PartIndex==2,
                        "small source-first originals displaced the largest compact combination array");
                    mainInterlock=interlockedMembers(event);
                }
                else require(firstBandCombo>=2&&firstBandSingles>=1&&firstBandSmall==small.Quantity,
                    "the next main array ran before small combination arrays and the genuine odd NFP tail filled the old band");
                ++mainCommits;frozenTop=topOf(board);
            }
            else
            {
                require(phase=="gap-combination-array"||phase=="gap-single-nfp",
                    "a filler commit bypassed both declared gap mechanisms");
                require(topOf(board)<=frozenTop&&topOf(board)<=topOf(prefix)&&Measure(event,"frozenFillTop")==frozenTop,
                    "a phased gap commit increased actual original-material height or changed its frozen band");
                for(const auto& member:event.Members)
                {
                    const auto placed=std::find_if(board.Placements.begin(),board.Placements.end(),[&](const Placement& item)
                        {return item.PartID==problem.Parts[member.PartIndex].ID&&item.Instance==member.Instance;});
                    require(placed!=board.Placements.end()&&placed->GapFiller,
                        "phased gap original lost its actual filler provenance");
                }
                if(phase=="gap-combination-array")
                {
                    require(Measure(event,"constructionPhase")==2&&event.Members.size()>=2&&event.Members.size()%2==0&&
                        Measure(event,"unitMembers")==2&&Measure(event,"arrayCells")>=1&&
                        Measure(event,"unitMembers")*Measure(event,"arrayCells")==event.Members.size(),
                        "gap combination-array phase supplied singleton originals instead of two-member units");
                    gapInterlock=interlockedMembers(event)||gapInterlock;
                    if(mainCommits==1)firstBandCombo+=event.Members.size();
                }
                else
                {
                    require(comboDone&&event.Members.size()==1&&Measure(event,"constructionPhase")==3,
                        "single NFP gap phase placed a disguised array or ran before combination fill ended");
                    // A pool may retain an original NFP proposal from an
                    // ancestor board. Its stored pose must pass the current
                    // board's original gate and receive a current score before
                    // it can be committed; a new full NFP query is unnecessary.
                    const bool queried=event.ProposalID&&std::any_of(timeline.begin(),timeline.begin()+ei,[&](const auto& query)
                        {return (query.Source=="nfp.query"||query.Source=="nfp.original-clearance")&&
                            query.Stage==ConstructionTraceStage::Begin&&query.Scope==event.Scope&&ancestorStates.contains(query.StateID)&&
                            query.ProposalID==event.ProposalID&&query.PartIndex==event.Members.front().PartIndex;});
                    const auto sameStoredCandidate=[&](const ConstructionTraceEvent& proof)
                    {
                        return proof.Scope==event.Scope&&proof.StateID==event.ParentStateID&&
                            proof.CandidateID==event.CandidateID&&proof.ProposalID==event.ProposalID&&
                            proof.Members.size()==event.Members.size()&&
                            std::equal(proof.Members.begin(),proof.Members.end(),event.Members.begin(),
                                [](const auto& a,const auto& b)
                                {return a.PartIndex==b.PartIndex&&SamePose(a.Transform,b.Transform);});
                    };
                    std::size_t rechecked=timeline.size();bool rescored=false;
                    for(std::size_t pi=0;pi<ei;++pi)
                    {
                        const auto& proof=timeline[pi];
                        if(!sameStoredCandidate(proof))continue;
                        if(proof.Source=="construction.pool-recheck"&&proof.Stage==ConstructionTraceStage::Filter&&proof.Accepted)
                            rechecked=pi;
                        if(proof.Source=="placement.choice"&&proof.Accepted&&rechecked<pi&&
                            !proof.Rank.empty()&&proof.Rank.size()==event.Rank.size()&&
                            std::equal(proof.Rank.begin(),proof.Rank.end(),event.Rank.begin(),
                                [](const auto& a,const auto& b){return a.second==b.second;}))rescored=true;
                    }
                    require(queried&&rechecked<ei&&rescored,
                        "single-original pool filler lost its original NFP provenance, current-state recheck or current rank");
                    singleNfp=true;
                    if(mainCommits==1)++firstBandSingles;
                }
                if(mainCommits==1)
                {
                    require(event.Members.front().PartIndex==0,"first band consumed an unrelated next-primary original");
                    firstBandSmall+=event.Members.size();
                }
            }
            prefix=board;parent=event.StateID;ancestorStates.insert(parent);
        }
        require(mainCommits>=2&&mainInterlock&&gapInterlock&&singleNfp&&prefix.Placements.size()==9,
            "phased construction omitted a genuine primary combination, gap combination array, NFP tail or next blank cycle");
        const auto raw=std::find_if(rawBoards.begin(),rawBoards.end(),[](const Layout& board)
            {return board.StockInstance==0&&board.Placements.size()==9;});
        require(raw!=rawBoards.end(),"a later processing stage supplied the phased complete construction");
        for(const auto& final:result.Layouts.front().Placements)for(const auto* snapshot:{&prefix,&*raw})
        {
            const auto kept=std::find_if(snapshot->Placements.begin(),snapshot->Placements.end(),[&](const Placement& placed)
                {return placed.PartID==final.PartID&&placed.Instance==final.Instance;});
            require(kept!=snapshot->Placements.end()&&SamePose(kept->Transform,final.Transform)&&
                kept->Group==final.Group&&kept->GapFiller==final.GapFiller,
                "raw/final output changed a phased combination-array or single-NFP original");
        }
        UnchangedOriginals(original,problem);
    }
}
void LayeredSingleNfpRechecksOneAncestorPoolAcrossSeparateRecesses()
{
    Problem problem;problem.PartGap=.2;problem.EdgeMargin=.5;problem.Tolerance=1e-7;
    auto small=Polygon("pool-recess-square",{{0,0},{1,0},{1,1},{0,1}},3);
    auto frame=Polygon("pool-comb-frame",{{0,0},{8.5,0},{8.5,3},{7.5,3},{7.5,1},{6,1},{6,3},{5,3},
        {5,1},{3.5,1},{3.5,3},{2.5,3},{2.5,1},{1,1},{1,3},{0,3}});
    small.AllowedAnglesDegrees={0};frame.AllowedAnglesDegrees={0};
    problem.Parts={small,frame};problem.Stocks={{"pool-three-recess-sheet",9.5,4,1}};
    const auto original=problem;
    Result witness;witness.State=Status::Complete;
    witness.Layouts={{"pool-three-recess-sheet",0,9.5,4,{
        {frame.ID,0,{.5,.5,0,false},1,false},{small.ID,0,{1.75,1.75,0,false},2,true},
        {small.ID,1,{4.25,1.75,0,false},3,true},{small.ID,2,{6.75,1.75,0,false},4,true}},19.5}};
    const auto witnessCheck=Validate(problem,witness);
    Require(witnessCheck.Valid&&witnessCheck.Complete&&witnessCheck.PlacedCount==4,
        "ancestor-pool recess fixture has no complete original-material witness");
    // The frame fills the usable stock bounds. Each 1.5 x 2 open recess
    // admits one square with clearance, but no tight two-square unit. All
    // three same-angle singleton positions should come from one NFP pool.
    // Squares have no intrinsic pairing gain, so preparation need not retain
    // a pair. The combination gap phase must still actually run and finish.
    // The independent witness supplies no construction seed or fixed pose.
    for(const auto kind:{PlaneStrategyKind::ArrayQuantityDeferred})
    {
        std::vector<ConstructionTraceEvent> timeline;bool preparedPair=false;
        auto options=Limits();options.EnableCombinations=true;options.EnableGapFill=true;
        options.MaxPatternMembers=2;options.TimeLimitSeconds=std::numeric_limits<double>::max();
        options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
        options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
        {
            if(event.Source=="preparation.retained-template"&&event.Members.size()==2&&
                std::all_of(event.Members.begin(),event.Members.end(),[](const auto& member){return member.PartIndex==0;}))
                preparedPair=true;
            if(event.Source=="construction.phase"||event.Source=="constructor.commit"||event.Source=="fill.commit"||
                event.Source=="placement.choice"||event.Source=="construction.pool-recheck"||
                (event.Source=="nfp.query"&&event.Stage==ConstructionTraceStage::Begin))timeline.push_back(event);
        };
        auto settings=OriginalNfpSettings(kind);settings.MaxArrayAxis=8;settings.MaxCandidates=8;
        const auto result=MakePlaneStrategyFactory(settings)()->Run(problem,options);
        const auto checked=Validate(problem,result);
        const auto require=[&](bool value,const char* message)
        {
            if(value)return;
            std::ostringstream detail;
            detail<<message<<"; kind="<<static_cast<int>(kind)<<" status="<<static_cast<int>(result.State)
                <<" valid="<<checked.Valid<<" complete="<<checked.Complete<<" placed="<<checked.PlacedCount
                <<" layouts="<<result.Layouts.size()<<" pairPrepared="<<preparedPair
                <<" observerFailed="<<options.ConstructionObservation->Failed.load()
                <<" attempts="<<result.Stats.PlacementAttempts<<" validateErrors=";
            for(const auto& error:checked.Errors)detail<<'['<<error<<']';
            detail<<" remaining=";
            for(const auto& item:result.Remaining)detail<<'['<<item.PartID<<'x'<<item.Quantity<<']';
            detail<<" sequence=";
            for(const auto& event:timeline)
            {
                if(event.Source!="construction.phase"&&event.Source!="constructor.commit"&&event.Source!="fill.commit")continue;
                detail<<'['<<event.Source<<':'<<event.Reason<<" stage="<<static_cast<int>(event.Stage)
                    <<" state="<<event.ParentStateID<<"->"<<event.StateID<<" proposal="<<event.ProposalID;
                for(const auto& member:event.Members)detail<<" p="<<member.PartIndex<<'#'<<member.Instance<<'@'
                    <<std::setprecision(17)<<member.Transform.X<<','<<member.Transform.Y<<','<<member.Transform.AngleDegrees;
                for(const auto& [name,number]:event.Measures)
                    if(name=="constructionPhase"||name=="frozenFillTop")detail<<' '<<name<<'='<<number;
                detail<<']';
            }
            throw std::runtime_error(detail.str());
        };
        require(checked.Valid&&checked.Complete&&checked.PlacedCount==4&&result.Layouts.size()==1&&
            !options.ConstructionObservation->Failed.load()&&result.Stats.PlacementAttempts<=options.MaxPlacementAttempts,
            "layered ancestor-pool construction lost original demand or one-sheet legality");
        const auto topOf=[&](const Layout& board)
        {
            double top=0;
            for(const auto& placed:board.Placements)
            {
                const auto& part=placed.PartID==small.ID?small:frame;
                const auto box=Detail::Bounds(part.Geometry,placed.Transform);top=std::max(top,box.Y+box.Height);
            }
            return top;
        };
        std::string phase;std::map<std::uint64_t,std::uint64_t> originStates;
        std::map<std::uint64_t,std::set<std::uint64_t>> queriesByState;
        Layout prefix;std::uint64_t parent=0,frameState=0,sharedProposal=0;
        std::size_t singles=0,ancestorReuses=0,conflictingOldPoses=0,combinationFills=0;double frozenTop=0;
        bool combinationBegan=false,combinationCompleted=false;
        for(std::size_t ei=0;ei<timeline.size();++ei)
        {
            const auto& event=timeline[ei];
            if(event.Source=="construction.phase")
            {
                if(event.Stage==ConstructionTraceStage::Begin)
                {
                    if(event.Reason=="gap-combination-array")
                    {
                        require(frameState&&event.StateID==parent&&Measure(event,"constructionPhase")==2,
                            "combination gap stage did not begin against the actual committed frame");
                        combinationBegan=true;
                    }
                    if(event.Reason=="gap-single-nfp")
                        require(combinationBegan&&combinationCompleted,
                            "singleton pool ran before the real combination gap stage finished");
                    phase=event.Reason;
                }
                else if(event.Stage==ConstructionTraceStage::Complete)
                {
                    if(event.Reason=="gap-combination-array")
                    {
                        require(combinationBegan&&phase==event.Reason&&event.StateID==parent&&
                            Measure(event,"constructionPhase")==2&&!event.SearchTruncated,
                            "combination gap stage did not finish its actual frame search");
                        combinationCompleted=true;
                    }
                    if(phase==event.Reason)phase.clear();
                }
                continue;
            }
            if(phase=="gap-single-nfp"&&event.Source=="nfp.query"&&event.PartIndex==0)
            {
                require(event.ProposalID!=0,"single-NFP pool lost its actual original query identity");
                const auto [entry,inserted]=originStates.emplace(event.ProposalID,event.StateID);
                require(inserted||entry->second==event.StateID,"one NFP proposal was reassigned to a different original state");
                queriesByState[event.StateID].insert(event.ProposalID);
                require(queriesByState[event.StateID].size()==1,"unchanged singleton state repeated a full NFP pool query");
            }
            if(event.Source=="construction.pool-recheck"&&event.Stage==ConstructionTraceStage::Filter&&
                !event.Accepted&&singles&&event.StateID==parent&&event.Reason=="fixed-material-conflict-or-interrupted")
                ++conflictingOldPoses;
            if(event.Source!="constructor.commit"&&event.Source!="fill.commit")continue;
            require(event.Layouts.size()==1&&event.ParentStateID==parent&&event.Members.size()==1,
                "three-recess construction skipped a real parent or disguised an array as a singleton");
            const auto& board=event.Layouts.front();
            for(const auto& old:prefix.Placements)
            {
                const auto kept=std::find_if(board.Placements.begin(),board.Placements.end(),[&](const Placement& item)
                    {return item.PartID==old.PartID&&item.Instance==old.Instance;});
                require(kept!=board.Placements.end()&&SamePose(kept->Transform,old.Transform)&&
                    kept->Group==old.Group&&kept->GapFiller==old.GapFiller,
                    "ancestor-pool reuse moved or replaced an already committed original");
            }
            if(event.Source=="constructor.commit")
            {
                require(!frameState&&event.Members.front().PartIndex==1&&Measure(event,"constructionPhase")==5,
                    "small units displaced the full-width original frame or reopened a main array inside its recesses");
                frameState=event.StateID;frozenTop=topOf(board);
            }
            else
            {
                if(phase=="gap-combination-array")++combinationFills;
                require(frameState&&phase=="gap-single-nfp"&&Measure(event,"constructionPhase")==3&&
                    event.Members.front().PartIndex==0&&topOf(board)<=frozenTop&&topOf(board)<=topOf(prefix)&&
                    Measure(event,"frozenFillTop")==frozenTop,
                    "a grouped filler entered separate recesses or a singleton raised the frozen material band");
                const auto origin=originStates.find(event.ProposalID);
                require(origin!=originStates.end()&&origin->second==frameState,
                    "singleton did not originate in the initial three-recess NFP pool");
                if(!sharedProposal)sharedProposal=event.ProposalID;
                require(event.ProposalID==sharedProposal,"later recesses discarded the still-usable ancestor pool");
                std::size_t rechecked=timeline.size();bool rescored=false;
                for(std::size_t pi=0;pi<ei;++pi)
                {
                    const auto& proof=timeline[pi];
                    if(proof.StateID!=event.ParentStateID||proof.Scope!=event.Scope||proof.CandidateID!=event.CandidateID||
                        proof.ProposalID!=event.ProposalID||proof.Members.size()!=1||
                        proof.Members.front().PartIndex!=0||!SamePose(proof.Members.front().Transform,event.Members.front().Transform))continue;
                    if(proof.Source=="construction.pool-recheck"&&proof.Stage==ConstructionTraceStage::Filter&&proof.Accepted)
                        rechecked=pi;
                    if(proof.Source=="placement.choice"&&rechecked<pi&&!proof.Rank.empty()&&
                        proof.Rank.size()==event.Rank.size()&&std::equal(proof.Rank.begin(),proof.Rank.end(),event.Rank.begin(),
                            [](const auto& a,const auto& b){return a.second==b.second;}))rescored=true;
                }
                require(rechecked<ei&&rescored,"ancestor pool reused stale original legality or a stale utilization score");
                ++singles;if(event.ParentStateID!=origin->second)++ancestorReuses;
            }
            prefix=board;parent=event.StateID;
        }
        require(combinationBegan&&combinationCompleted&&!combinationFills&&singles==3&&ancestorReuses>=2&&
            originStates.size()==1&&queriesByState.size()==1&&conflictingOldPoses>0,
            "separate recesses did not reuse one original NFP pool with current-state collision rechecks");
        std::set<std::size_t> instances;
        for(const auto& placed:result.Layouts.front().Placements)
        {
            const auto kept=std::find_if(prefix.Placements.begin(),prefix.Placements.end(),[&](const Placement& item)
                {return item.PartID==placed.PartID&&item.Instance==placed.Instance;});
            require(kept!=prefix.Placements.end()&&SamePose(kept->Transform,placed.Transform)&&
                kept->Group==placed.Group&&kept->GapFiller==placed.GapFiller,
                "final output repaired a stale ancestor-pool placement after construction");
            if(placed.PartID==small.ID)
            {require(placed.GapFiller,"pool singleton lost its actual filler provenance");instances.insert(placed.Instance);}
        }
        require(instances==std::set<std::size_t>{0,1,2},"ancestor pool changed or duplicated the original requested inventory");
        UnchangedOriginals(original,problem);
    }
}
void EqualUtilizationPrefersConnectedReusableContourSpace()
{
    Problem problem;problem.PartGap=.2;problem.EdgeMargin=.5;problem.Tolerance=1e-7;
    auto bar=Polygon("local-remnant-bar",{{0,0},{2,0},{2,5.75},{0,5.75}});
    auto frame=Polygon("local-remnant-notched-U",{{0,0},{12,0},{12,8},{10,8},{10,2},
        {4.75,2},{4.75,3},{2,3},{2,8},{0,8}});
    bar.AllowedAnglesDegrees={0};frame.AllowedAnglesDegrees={0};
    problem.Parts={bar,frame};problem.Stocks={{"local-remnant-sheet",13,9,1}};
    const auto original=problem;
    const auto witness=[&](double x)
    {
        Result result;result.State=Status::Complete;
        result.Layouts={{"local-remnant-sheet",0,13,9,{
            {frame.ID,0,{.5,.5,0,false},1,false},{bar.ID,0,{x,2.75,0,false},2,true}},62.25}};
        return result;
    };
    const auto fragmented=witness(5.5),compact=witness(8.25);
    const auto badCheck=Validate(problem,fragmented),goodCheck=Validate(problem,compact);
    Require(badCheck.Valid&&badCheck.Complete&&goodCheck.Valid&&goodCheck.Complete&&
        badCheck.MaterialArea==goodCheck.MaterialArea,
        "equal-utilization contour-space witnesses are not independently complete original layouts");
    // Both witnesses use the same material and full-width stock section. The
    // frame fixes the occupied AABB, so the old low/left tie prefers x=5.5.
    // Its bar divides usable clearance space into two small islands; moving
    // the same bar toward the right wall leaves one much larger open region.
    const auto badBox=Detail::Bounds(bar.Geometry,fragmented.Layouts.front().Placements.back().Transform);
    const auto goodBox=Detail::Bounds(bar.Geometry,compact.Layouts.front().Placements.back().Transform);
    Require(badBox.Y==goodBox.Y&&badBox.X<goodBox.X&&
        badBox.Y+badBox.Height==8.5&&goodBox.Y+goodBox.Height==8.5,
        "contour-space witness does not oppose the old equal-height low/left tie");
    const auto badRemnant=FindRegularRemnant(problem,fragmented.Layouts.front(),32);
    const auto goodRemnant=FindRegularRemnant(problem,compact.Layouts.front(),32);
    Require(badRemnant.Verified&&goodRemnant.Verified&&goodRemnant.Area>badRemnant.Area+2,
        "contour-space witnesses do not have distinctly different verified reusable remnants");
    // Neither witness is supplied to Run. The final choice is checked by the
    // existing public original-contour rectangle certificate, independently
    // of the new evaluator's private component, perimeter or contact scores.
    // This fixture specifically requires the layered phase-3 gap decision.
    // Deferred arrays may legitimately consume the bar in their main pass.
    for(const auto kind:{PlaneStrategyKind::ArrayQuantityDeferred})
    {
        std::vector<ConstructionTraceEvent> commits,ranks,queries;std::vector<Layout> rawBoards;
        auto options=Limits();options.EnableCombinations=true;options.EnableGapFill=true;
        options.TimeLimitSeconds=std::numeric_limits<double>::max();
        options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
        options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
        {
            if(event.Source=="constructor.commit"||event.Source=="fill.commit")commits.push_back(event);
            if(event.Source=="placement.rank"&&event.PartIndex==0)ranks.push_back(event);
            if(event.Source=="nfp.query"&&event.Stage==ConstructionTraceStage::Begin&&event.PartIndex==0)queries.push_back(event);
            if(event.Source=="board.raw-construction")
                rawBoards.insert(rawBoards.end(),event.Layouts.begin(),event.Layouts.end());
        };
        auto settings=OriginalNfpSettings(kind);settings.MaxArrayAxis=8;settings.MaxCandidates=8;
        const auto result=MakePlaneStrategyFactory(settings)()->Run(problem,options);
        const auto require=[&](bool value,const char* message)
        {if(!value)throw std::runtime_error(std::string(message)+"; kind="+std::to_string(static_cast<int>(kind)));};
        const auto checked=Validate(problem,result);
        require(checked.Valid&&checked.Complete&&checked.PlacedCount==2&&result.Layouts.size()==1&&
            checked.MaterialArea==badCheck.MaterialArea&&!options.ConstructionObservation->Failed.load()&&
            result.Stats.PlacementAttempts<=options.MaxPlacementAttempts,
            "local contour-space choice lost original material, complete demand or the physical sheet");
        require(commits.size()==2&&commits.front().Source=="constructor.commit"&&commits.front().Members.size()==1&&
            commits.front().Members.front().PartIndex==1&&commits.back().Source=="fill.commit"&&
            commits.back().Members.size()==1&&commits.back().Members.front().PartIndex==0&&
            commits.front().Layouts.size()==1&&commits.back().Layouts.size()==1&&
            commits.back().ParentStateID==commits.front().StateID&&Measure(commits.back(),"constructionPhase")==3,
            "local contour-space regression did not reach a genuine same-state single-NFP gap choice");
        const auto& selected=commits.back();
        require(std::any_of(queries.begin(),queries.end(),[&](const auto& query)
            {return query.StateID==selected.ParentStateID&&query.ProposalID==selected.ProposalID;}),
            "local contour-space winner bypassed its actual original NFP query");
        double left=std::numeric_limits<double>::infinity(),right=-left;
        bool selectedWasCertified=false;
        for(const auto& ranked:ranks)
        {
            if(ranked.StateID!=selected.ParentStateID||ranked.Members.size()!=1)continue;
            const auto pose=ranked.Members.front().Transform;
            left=std::min(left,pose.X);right=std::max(right,pose.X);
            require(std::abs(Measure(ranked,"usedSectionUtilization")-badCheck.MaterialArea/117.)<1e-12,
                "local remnant preference changed the primary net-material utilization tier");
            if(ranked.ProposalID==selected.ProposalID&&ranked.CandidateID==selected.CandidateID&&
                SamePose(pose,selected.Members.front().Transform))selectedWasCertified=true;
        }
        require(right-left>2&&selectedWasCertified,
            "real NFP candidate pool lost the distinct equal-utilization contour positions before local scoring");
        const auto measured=FindRegularRemnant(problem,result.Layouts.front(),32);
        require(measured.Verified&&measured.Area>badRemnant.Area+2,
            "equal-utilization initial NFP choice preferred fragmented gaps over a demonstrably larger reusable remnant");
        const auto& oldFrame=commits.front().Layouts.front().Placements.front();
        const auto& final=result.Layouts.front();
        const auto retained=std::find_if(final.Placements.begin(),final.Placements.end(),[&](const Placement& placed)
            {return placed.PartID==oldFrame.PartID&&placed.Instance==oldFrame.Instance;});
        require(retained!=final.Placements.end()&&SamePose(retained->Transform,oldFrame.Transform)&&
            retained->Group==oldFrame.Group&&retained->GapFiller==oldFrame.GapFiller,
            "local contour-space evaluation moved the already committed frame");
        const auto raw=std::find_if(rawBoards.begin(),rawBoards.end(),[](const Layout& board){return board.Placements.size()==2;});
        require(raw!=rawBoards.end(),"a later refinement stage supplied the claimed contour-space choice");
        for(const auto& placed:final.Placements)
        {
            const auto kept=std::find_if(raw->Placements.begin(),raw->Placements.end(),[&](const Placement& item)
                {return item.PartID==placed.PartID&&item.Instance==placed.Instance;});
            require(kept!=raw->Placements.end()&&SamePose(kept->Transform,placed.Transform)&&
                kept->Group==placed.Group&&kept->GapFiller==placed.GapFiller,
                "raw/final layout changed an original after local contour-space scoring");
        }
        UnchangedOriginals(original,problem);
    }
}
void RequireGreedyContourRankScreen(const std::vector<ConstructionTraceEvent>& events)
{
    double calls=0,rejected=0;
    for(const auto& event:events)
        if(event.Source=="array.original-query-result"||event.Source=="array.rigid-query-result")
            for(const auto& [name,value]:event.Measures)
            {
                if(name=="rankScreenCalls")calls+=value;
                if(name=="rankScreenRejected")rejected+=value;
            }
    Require(calls>0&&rejected>0&&rejected<=calls,
        "greedy contour construction did not reject dominated final poses before deep geometry checks");
}
void OrdinarySingleOriginalSearchesTheWholeContourDomain()
{
    for(const auto kind:{PlaneStrategyKind::ArrayQuantityDeferred,PlaneStrategyKind::ArrayGeometryDeferred})
    for(const bool allowMirror:{false,true})try
    {
        Problem problem;problem.PartGap=.2;problem.EdgeMargin=.5;problem.Tolerance=1e-7;
        problem.AllowCommonEdges=false;
        auto frame=Polygon("ordinary-open-frame",{{1.5,0},{4.5,0},{4.5,5.5},{0,5.5},
            {0,4.5},{3.5,4.5},{3.5,3},{1.5,3}});frame.AllowedAnglesDegrees={0};
        auto moving=Polygon("ordinary-chiral-L",{{0,0},{3,0},{3,1},{1,1},{1,4},{0,4}});
        moving.AllowedAnglesDegrees={0};moving.AllowMirror=allowMirror;
        problem.Parts={frame,moving};problem.Stocks={{"ordinary-contour-sheet",5.5,12,1}};
        const auto original=problem;
        // This independent witness is checked, never supplied as a seed to Run.
        Result witness;witness.State=Status::Complete;
        witness.Layouts={{"ordinary-contour-sheet",0,5.5,12,
            {{frame.ID,0,{.5,.5,0,false},1,false},
             {moving.ID,0,allowMirror?Pose{.75,4.75,0,true}:Pose{.75,6.25,0,false},2,false}},21}};
        const auto witnessCheck=Validate(problem,witness);
        Require(witnessCheck.Valid&&witnessCheck.Complete,"ordinary chiral contour witness is invalid");
        auto settings=OriginalNfpSettings(kind);
        std::vector<ConstructionTraceEvent> events;auto options=OriginalNfpLimits(events);
        options.TimeLimitSeconds=std::numeric_limits<double>::max();
        options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
        const auto result=MakePlaneStrategyFactory(settings)()->Run(problem,options);
        RequireOrdinaryContourContinuation(problem,result,events,1,allowMirror);
        RequireGreedyContourRankScreen(events);
        Require(!options.ConstructionObservation->Failed.load()&&
            result.Stats.PlacementAttempts<=options.MaxPlacementAttempts,
            "ordinary single contour search failed observation or exhausted its attempt budget");
        const auto movingPlacement=std::find_if(result.Layouts.front().Placements.begin(),
            result.Layouts.front().Placements.end(),[&](const Placement& value){return value.PartID==moving.ID;});
        Require(movingPlacement!=result.Layouts.front().Placements.end()&&
            movingPlacement->Transform.Mirrored==allowMirror,
            "ordinary contour search failed to select the only tight legal handedness");
        UnchangedOriginals(original,problem);
        Require(problem.Stocks[0].ID==original.Stocks[0].ID&&
            problem.Stocks[0].Quantity==original.Stocks[0].Quantity&&
            problem.Stocks[0].Width==original.Stocks[0].Width&&problem.Stocks[0].Height==original.Stocks[0].Height,
            "ordinary single contour search changed the physical stock");
    }
    catch(const std::exception& error)
    {
        throw std::runtime_error(std::string(error.what())+"; ordinary-single kind="+
            std::to_string(static_cast<int>(kind))+" mirror="+std::to_string(allowMirror));
    }
}
void OrdinarySelfUnitSearchesTheWholeContourDomain()
{
    for(const auto kind:{PlaneStrategyKind::ArrayQuantityDeferred,PlaneStrategyKind::ArrayGeometryDeferred})
    for(const bool allowMirror:{false,true})try
    {
        Problem problem;problem.PartGap=.2;problem.EdgeMargin=.5;problem.Tolerance=1e-7;
        problem.AllowCommonEdges=false;
        auto frame=Polygon("ordinary-large-L",{{0,0},{10,0},{10,8},{3.5,8},{3.5,4},{0,4}});
        frame.AllowedAnglesDegrees={0};
        auto moving=Polygon("ordinary-rotated-self-L",{{0,0},{2,0},{2,1},{1,1},{1,2},{0,2}},2);
        // Rotate the input exactly -90 degrees. Legal output must retain the
        // original source and use its hard 90/270-degree angle permissions.
        for(auto& point:moving.Geometry.Outer)point={point.Y,-point.X};
        moving.AllowedAnglesDegrees={90,270};moving.AllowMirror=allowMirror;
        problem.Parts={frame,moving};problem.Stocks={{"ordinary-rigid-recess-sheet",11,20,1}};
        const auto original=problem;
        Result witness;witness.State=Status::Complete;
        witness.Layouts={{"ordinary-rigid-recess-sheet",0,11,20,
            {{frame.ID,0,{.5,.5,0,false},1,false},
             {moving.ID,0,{.5,4.75,90,false},2,false},
             {moving.ID,1,{3.75,7,270,false},2,false}},72}};
        const auto witnessCheck=Validate(problem,witness);
        Require(witnessCheck.Valid&&witnessCheck.Complete,
            "ordinary rigid self-unit recess witness is invalid");
        auto settings=OriginalNfpSettings(kind); // One cell, not a two-column singleton grid.
        std::vector<ConstructionTraceEvent> events;auto options=OriginalNfpLimits(events);
        options.TimeLimitSeconds=std::numeric_limits<double>::max();options.EnableCombinations=true;
        options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
        const auto result=MakePlaneStrategyFactory(settings)()->Run(problem,options);
        RequireOrdinaryContourContinuation(problem,result,events,2,true);
        RequireGreedyContourRankScreen(events);
        Require(!options.ConstructionObservation->Failed.load()&&
            result.Stats.PlacementAttempts<=options.MaxPlacementAttempts,
            "ordinary rigid contour search failed observation or exhausted its attempt budget");
        std::vector<const Placement*> pair;
        for(const auto& placed:result.Layouts.front().Placements)
            if(placed.PartID==moving.ID)pair.push_back(&placed);
        const bool compactPair=pair.size()==2&&pair[0]->Group==pair[1]->Group&&pair[0]->Group!=0&&
            BoundsOverlapInBothAxes(Detail::Bounds(moving.Geometry,pair[0]->Transform),
                Detail::Bounds(moving.Geometry,pair[1]->Transform));
        if(!compactPair)
        {
            // Retain the hard failure, but expose whether preparation, rigid
            // translation or final ranking lost the physically compact unit.
            std::cerr<<std::setprecision(17)<<"ordinary-self-unit diagnostic kind="<<static_cast<int>(kind)
                <<" mirror="<<allowMirror<<'\n';
            std::uint64_t frameState=0;
            for(const auto& event:events)if(event.Source=="constructor.commit")
            {
                if(!frameState)frameState=event.StateID;
                std::cerr<<"selected state="<<event.StateID<<" parent="<<event.ParentStateID
                    <<" proposal="<<event.ProposalID<<" candidate="<<event.CandidateID;
                for(const auto& value:event.Rank)std::cerr<<' '<<value.first<<'='<<value.second;
                std::cerr<<'\n';
                for(const auto& member:event.Members)std::cerr<<" member part="<<member.PartIndex
                    <<" instance="<<member.Instance<<" group="<<member.Group<<" pose="
                    <<member.Transform.X<<','<<member.Transform.Y<<','<<member.Transform.AngleDegrees
                    <<','<<member.Transform.Mirrored<<'\n';
            }
            std::size_t compactPrepared=0,compactGenerated=0,compactAccepted=0;
            std::map<std::string,std::size_t> rigidRejections;
            for(const auto& event:events)
            {
                const bool selfPair=event.Members.size()==2&&event.Members[0].PartIndex==1&&
                    event.Members[1].PartIndex==1;
                const bool interlocked=selfPair&&BoundsOverlapInBothAxes(
                    Detail::Bounds(moving.Geometry,event.Members[0].Transform),
                    Detail::Bounds(moving.Geometry,event.Members[1].Transform));
                if(event.Source=="preparation.certified-pattern"&&selfPair)
                {
                    compactPrepared+=interlocked;
                    std::cerr<<"template compact="<<interlocked<<" size="<<event.Width<<','<<event.Height
                        <<" fill="<<event.Fill;
                    for(const auto& member:event.Members)std::cerr<<" pose="<<member.Transform.X<<','
                        <<member.Transform.Y<<','<<member.Transform.AngleDegrees<<','<<member.Transform.Mirrored;
                    std::cerr<<'\n';
                }
                if(event.StateID!=frameState)continue;
                if(interlocked&&event.Stage==ConstructionTraceStage::Generate)++compactGenerated;
                if(interlocked&&event.Stage==ConstructionTraceStage::Filter&&event.Accepted)++compactAccepted;
                if(event.Source=="nfp.rigid-placement"&&event.Stage==ConstructionTraceStage::Filter&&!event.Accepted)
                    ++rigidRejections[event.Reason];
                if((event.Source=="array.rigid-query-result"||event.Source=="nfp.rigid-placement")&&
                    (event.Stage==ConstructionTraceStage::Complete||event.Stage==ConstructionTraceStage::Analysis))
                {
                    std::cerr<<"rigid-summary proposal="<<event.ProposalID<<" source="<<event.Source
                        <<" reason="<<event.Reason<<" truncated="<<event.SearchTruncated;
                    for(const auto& value:event.Measures)std::cerr<<' '<<value.first<<'='<<value.second;
                    std::cerr<<'\n';
                }
            }
            std::cerr<<"compact prepared="<<compactPrepared<<" generated="<<compactGenerated
                <<" accepted="<<compactAccepted<<'\n';
            for(const auto& value:rigidRejections)std::cerr<<"rigid-rejected "<<value.first<<'='<<value.second<<'\n';
        }
        Require(compactPair,
            "ordinary rigid continuation used an unpacked self unit instead of a real contour interlock");
        UnchangedOriginals(original,problem);
        Require(problem.Stocks[0].ID==original.Stocks[0].ID&&
            problem.Stocks[0].Quantity==original.Stocks[0].Quantity&&
            problem.Stocks[0].Width==original.Stocks[0].Width&&problem.Stocks[0].Height==original.Stocks[0].Height,
            "ordinary rigid contour search changed the physical stock");
    }
    catch(const std::exception& error)
    {
        throw std::runtime_error(std::string(error.what())+"; ordinary-self-unit kind="+
            std::to_string(static_cast<int>(kind))+" mirror="+std::to_string(allowMirror));
    }
}
}
int main(int argc,char** argv)try
{
    if(argc==2&&std::string(argv[1])=="--bulk-contour-only")
    {OrdinaryBulkArraysGrowCertifiedContourUnits(true);return 0;}
    if(argc==2&&std::string(argv[1])=="--bulk-deferred-only")
    {OrdinaryBulkArraysGrowCertifiedContourUnits(true,int(PlaneStrategyKind::ArrayGeometryDeferred));return 0;}
    IndependentArrayControllersUseActualFillDomains();
    ConcaveOriginalsInterlockDuringPrimaryConstruction();
    MidConstructionOrientationsCompeteInTheSameOriginalState();
    PositiveGapFullChannelsImproveAlreadySafeBoundaryFits();
    RigidArrayPriorityMatchesTheFinalAxisRank();
    OccupiedBoardArraysShrinkWithoutDiscardingBulkDemand();
    CachedOriginalBoardCertificatesStillCheckTheGrownPrefix();
    OrdinaryArraysUseCompactOriginalUnitsDuringConstruction();
    OrdinaryBulkArraysGrowCertifiedContourUnits();
    OrdinarySmallRepeatsGrowInsideOriginalRecess();
    OrdinaryArraysFillTheFrozenBandBeforeTheNextMainArray();
    OrdinaryBandFillPreservesTheNextPrimaryContourInterlock();
    OrdinaryUnchangedBandReusesItsCertifiedNextMainArray();
    OrdinaryCombinationArraysFinishGapArraysAndSingleNfpBeforeTheNextBlank();
    LayeredSingleNfpRechecksOneAncestorPoolAcrossSeparateRecesses();
    EqualUtilizationPrefersConnectedReusableContourSpace();
    OrdinarySingleOriginalSearchesTheWholeContourDomain();
    OrdinarySelfUnitSearchesTheWholeContourDomain();
    Problem pairs;pairs.Parts={Polygon("triangle",{{0,0},{2,0},{0,2}},21)};
    pairs.Stocks={{"pair-sheet",8,6,1}};
    const auto unequal=Run(pairs,PlaneStrategyKind::NfpArray);
    Require(Validate(pairs,unequal).Complete&&unequal.Layouts.size()==1,
        "independent partner groups must supply odd demand without ordinary gap fill");
    Require(Has(unequal,"plane.independent-pair-arrays:"),"independent partner group path was not exercised");
    std::size_t first=0,partner=0;
    for(const auto& item:unequal.Layouts[0].Placements)
        if(item.Transform.AngleDegrees==0)++first;else ++partner;
    Require(first+partner==21&&first!=partner,"original count was rounded up to full paired cells");
    auto transposed=pairs;transposed.Stocks[0].Width=6;transposed.Stocks[0].Height=8;
    Require(Validate(transposed,Run(transposed,PlaneStrategyKind::NfpMultiArray)).Complete,
        "independent rows and columns must work on the transposed stock");

    Problem holes;holes.PartGap=.2;holes.EdgeMargin=.5;
    auto host=Polygon("host",{{0,0},{20,0},{20,20},{0,20}});
    host.Geometry.Holes={{{2,2},{18,2},{18,18},{2,18}}};
    auto child=Polygon("small-polygon",{{0,0},{3,0},{3,3},{0,3}},3);
    Part disk;disk.ID="disk";disk.Quantity=2;disk.FreeRotation=false;disk.AllowedAnglesDegrees={0};disk.Geometry.Radius=1;
    holes.Parts={host,child,disk};holes.Stocks={{"hole-sheet",21,21,1}};
    for(const auto kind:{PlaneStrategyKind::ArrayQuantityDeferred,PlaneStrategyKind::ArrayMultiSpecificationSearch,PlaneStrategyKind::NfpArray})
    {
        const auto nested=Run(holes,kind);
        std::cout<<"integrated-hole kind="<<static_cast<int>(kind)<<" placed="<<Validate(holes,nested).PlacedCount
            <<" boards="<<nested.Layouts.size()<<" attempts="<<nested.Stats.PlacementAttempts<<'\n';
        if(!Validate(holes,nested).Complete)for(const auto& diagnostic:nested.Diagnostics)std::cout<<diagnostic<<'\n';
        Require(Validate(holes,nested).Complete&&nested.Layouts.size()==1,
            "new Plane construction did not jointly consume originals inside the host hole");
        Require(Has(nested,"plane.hole-nesting:"),"hole nesting service was not used by construction");
        const auto& board=nested.Layouts[0];
        Require(board.Placements.size()==6,"hole construction duplicated or lost an original instance");
        std::set<std::pair<std::string,std::size_t>> identities;
        for(const auto& placed:board.Placements)
            Require(identities.emplace(placed.PartID,placed.Instance).second,"hole construction reused an original identity");
    }
    Require(holes.Parts[0].Geometry.Holes.size()==1&&holes.Parts[2].Geometry.Radius==1,
        "proposal geometry replaced the original holed/circular contour");
    Problem center;auto strip=Polygon("perimeter-strip",{{0,0},{3,0},{3,2},{0,2}},4);
    strip.AllowedAnglesDegrees={0,90,180,270};
    center.Parts={strip,Polygon("center-tile",{{0,0},{1,0},{1,1},{0,1}})};center.Stocks={{"pinwheel",5,5,1}};
    PlaneStrategySettings pinwheel;pinwheel.Kind=PlaneStrategyKind::FourBlockArray;
    auto centerLimits=Limits();centerLimits.EnableGapFill=true;
    bool partialCandidate=false,pinwheelCandidate=false,sectionFormula=true,beforeFirstCommit=true;
    centerLimits.ConstructionTrace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source=="constructor.commit")beforeFirstCommit=false;
        if(event.Source!="array.rank"||!beforeFirstCommit)return;
        double material=0,top=0,reportedArea=0,reportedUtilization=0;
        for(const auto& member:event.Members)
        {material+=Detail::Area(center.Parts[member.PartIndex].Geometry);
            const auto box=Detail::Bounds(center.Parts[member.PartIndex].Geometry,member.Transform);top=std::max(top,box.Y+box.Height);}
        const auto actualArea=center.Stocks[0].Width*std::clamp(top+center.EdgeMargin,0.,center.Stocks[0].Height);
        for(const auto& [name,value]:event.Measures)
        {if(name=="usedSectionArea")reportedArea=value;if(name=="usedSectionUtilization")reportedUtilization=value;}
        sectionFormula&=std::abs(actualArea-reportedArea)<1e-10&&std::abs(material/actualArea-reportedUtilization)<1e-12;
        if(event.Members.size()==2)partialCandidate=true;
        if(event.Members.size()==4)pinwheelCandidate=true;
    };
    const auto filled=MakePlaneStrategyFactory(pinwheel)()->Run(center,centerLimits);
    Require(Validate(center,filled).Complete&&filled.Layouts.size()==1&&Has(filled,"plane.four-block-fill:"),
        "four-block construction did not refill its actual central free space");
    const auto& centerBoard=filled.Layouts.front();
    const auto tile=std::find_if(centerBoard.Placements.begin(),centerBoard.Placements.end(),[](const Placement& placed){return placed.PartID=="center-tile";});
    Require(tile!=centerBoard.Placements.end()&&tile->Transform.X==2&&tile->Transform.Y==2&&tile->GapFiller,
        "central refill moved perimeter originals or used an unrelated outside region");
    Require(partialCandidate&&pinwheelCandidate&&sectionFormula&&Has(filled,"plane.four-block-fill: parts=1;"),
        "four-block construction misreported actual whole-width section utilization or selected center filler counts");
    // Twenty small originals have several equal-count compact envelopes.
    // The first 4x5 and 5x4 envelopes block the full-width bar, while selecting
    // that different demand vector first leaves a 10x2 strip for all tiles.
    // A tiny frontier must not spend both branches on poses of the same demand.
    Problem competition;
    auto small=Polygon("small-tile",{{0,0},{1,0},{1,1},{0,1}},20);
    small.AllowedAnglesDegrees={0,90,180,270};
    auto bar=Polygon("full-width-bar",{{0,0},{10,0},{10,4},{0,4}});
    bar.AllowedAnglesDegrees={0};competition.Parts={small,bar};
    competition.Stocks={{"competing-demands",10,6,2}};
    const auto original=competition;
    PlaneStrategySettings diverse;diverse.Kind=PlaneStrategyKind::ArrayMultiSpecificationSearch;
    diverse.MaxCandidates=2;diverse.BranchWidth=2;diverse.BranchDecisions=3;diverse.MaxNodes=64;
    diverse.EnableContourSettling=false;diverse.EnableHoleNesting=false;
    diverse.EnableSheetTemplateReuse=false;diverse.EnableLayoutRefinement=false;
    auto competitionLimits=Limits();competitionLimits.EnableCombinations=false;
    competitionLimits.MaxPlacementAttempts=50000;
    const auto competed=MakePlaneStrategyFactory(diverse)()->Run(competition,competitionLimits);
    const auto competedCheck=Validate(competition,competed);
    Require(competed.State==Status::Complete&&competedCheck.Valid&&competedCheck.Complete&&
        competed.Layouts.size()==1,"bounded multi-specification search lost a different original-demand branch");
    Require(competed.Stats.PlacementAttempts<=competitionLimits.MaxPlacementAttempts,
        "demand diversity exceeded the shared attempt budget");
    const auto& competedBoard=competed.Layouts.front();
    std::set<std::pair<std::string,std::size_t>> competedIDs;
    std::size_t tiles=0,bars=0;
    for(std::size_t i=0;i<competedBoard.Placements.size();++i)
    {
        const auto& member=competedBoard.Placements[i];
        Require(competedIDs.emplace(member.PartID,member.Instance).second,
            "demand competition repeated an original identity");
        tiles+=member.PartID==small.ID;bars+=member.PartID==bar.ID;
        const auto part=std::find_if(competition.Parts.begin(),competition.Parts.end(),
            [&](const Part& value){return value.ID==member.PartID;});
        Require(part!=competition.Parts.end(),"demand competition emitted a private original");
        const auto nominal=AxisSafety::Make(part->Geometry,member.Transform);
        Require(AxisSafety::Containment(nominal,competedBoard.Width,competedBoard.Height,
            competition.EdgeMargin)==AxisSafety::Verdict::Safe,"demand winner violated nominal stock containment");
        for(std::size_t j=0;j<i;++j)
        {
            const auto& fixed=competedBoard.Placements[j];
            const auto fixedPart=std::find_if(competition.Parts.begin(),competition.Parts.end(),
                [&](const Part& value){return value.ID==fixed.PartID;});
            Require(AxisSafety::Pair(nominal,AxisSafety::Make(fixedPart->Geometry,fixed.Transform),
                competition.PartGap,competition.AllowCommonEdges,competition.Tolerance)==AxisSafety::Verdict::Safe,
                "demand winner violated strict original-material spacing");
        }
    }
    Require(tiles==20&&bars==1&&competedIDs.size()==21,"demand competition changed per-original quantities");
    for(std::size_t i=0;i<competition.Parts.size();++i)
    {
        const auto& before=original.Parts[i];const auto& after=competition.Parts[i];
        Require(before.Quantity==after.Quantity&&before.AllowedAnglesDegrees==after.AllowedAnglesDegrees&&
            before.Geometry.Outer.size()==after.Geometry.Outer.size()&&
            std::equal(before.Geometry.Outer.begin(),before.Geometry.Outer.end(),after.Geometry.Outer.begin(),
                [](Point a,Point b){return a.X==b.X&&a.Y==b.Y;}),
            "candidate diversity changed immutable demand or source geometry");
    }
    auto cancelled=Limits();cancelled.Cancel=[]{return true;};
    PlaneStrategySettings s;s.Kind=PlaneStrategyKind::ArrayMultiSpecificationSearch;
    auto stopped=MakePlaneStrategyFactory(s)()->Run(holes,cancelled);
    Require(stopped.State==Status::Cancelled&&stopped.Layouts.empty(),"cancelled advanced construction emitted a draft");
    std::cout<<"PlaneAdvancedArrayTests passed: primary concave NFP interlocks, same-state original/mirror/rotation competition, independent partners, true holes and bounded demand diversity\n";
    return 0;
}
catch(const std::exception& e){std::cerr<<"PlaneAdvancedArrayTests failed: "<<e.what()<<'\n';return 1;}
