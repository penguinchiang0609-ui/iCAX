#include "NestingNfpPlacement.h"
#include "NestingInternal.h"
#include "NestingAxisSafety.h"
#include "NestingClearancePlacement.h"
#include "SheetNestingNfp.h"
#include "SheetNestingConstructionTrace.h"
#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
using namespace iCAX::SheetNesting;
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static Part polygon(const char* id,std::vector<Point> ring)
{Part p;p.ID=id;p.Material="m";p.FreeRotation=false;p.AllowedAnglesDegrees={0};p.Geometry.Outer=std::move(ring);return p;}
static void requireNominalFit(const Problem& problem,const Layout& board,std::size_t index,Pose pose)
{
    const auto moving=AxisSafety::Make(problem.Parts[index].Geometry,pose);
    require(AxisSafety::Containment(moving,board.Width,board.Height,problem.EdgeMargin)==AxisSafety::Verdict::Safe,
        "fit violates nominal stock boundary");
    for(const auto& fixed:board.Placements)
    {
        const auto part=std::find_if(problem.Parts.begin(),problem.Parts.end(),[&](const Part& item){return item.ID==fixed.PartID;});
        require(part!=problem.Parts.end(),"unknown nominal obstacle");
        require(AxisSafety::Pair(moving,AxisSafety::Make(part->Geometry,fixed.Transform),problem.PartGap,
            problem.AllowCommonEdges,problem.Tolerance)==AxisSafety::Verdict::Safe,"fit penetrates original nominal material");
    }
}
static bool requireCompleteBlockers(const Problem& problem,const Layout& board,std::size_t index,
    const Detail::NfpBlockedPlacement& candidate)
{
    const auto moving=AxisSafety::Make(problem.Parts[index].Geometry,candidate.Transform);
    std::vector<std::size_t> expected;
    bool nativeBlocked=false;
    for(std::size_t i=0;i<board.Placements.size();++i)
    {
        const auto& fixed=board.Placements[i];
        const auto part=std::find_if(problem.Parts.begin(),problem.Parts.end(),[&](const Part& item){return item.ID==fixed.PartID;});
        require(part!=problem.Parts.end(),"unknown blocker original");
        const bool native=Detail::ShapesConflict(part->Geometry,fixed.Transform,problem.Parts[index].Geometry,
            candidate.Transform,problem.PartGap,problem.Tolerance,problem.AllowCommonEdges);
        const auto nominal=AxisSafety::Pair(moving,AxisSafety::Make(part->Geometry,fixed.Transform),problem.PartGap,
            problem.AllowCommonEdges,problem.Tolerance);
        require(nominal!=AxisSafety::Verdict::Uncertain,"blocked proposal has uncertain original geometry");
        nativeBlocked=nativeBlocked||native;
        if(native||nominal==AxisSafety::Verdict::Blocked) expected.push_back(i);
    }
    require(!expected.empty()&&candidate.Blockers==expected,"original-material blocker set is incomplete or false");
    return !nativeBlocked;
}
static void GeneratorOwnsItsImmutableProblemSnapshot()
{
    Problem input;input.Tolerance=1e-7;input.Stocks.push_back({"board",10,10,1,1,"m"});
    input.Parts={polygon("fixed",{{0,0},{4,0},{4,10},{0,10}}),
        polygon("moving",{{0,0},{6,0},{6,10},{0,10}})};
    const Problem original=input;
    Layout board{"board",0,10,10,{{"fixed",0,{},0,false}},40};
    Detail::NfpPlacementLimits limits;limits.MaxCandidates=0;
    Detail::NfpPlacementGenerator bound(input),freshOriginal(original);
    const auto expected=freshOriginal.Find(board,1,0,limits);
    require(!expected.Fits.empty(),"immutable snapshot fixture has no original fit");
    const auto same=[&](const Detail::NfpPlacementQuery& result,const Detail::NfpPlacementQuery& reference)
    {
        require(result.Supported==reference.Supported&&result.Truncated==reference.Truncated&&
            result.Attempts==reference.Attempts&&result.Generated==reference.Generated&&
            result.DomainMeasured==reference.DomainMeasured&&result.FreeTranslationArea==reference.FreeTranslationArea&&
            result.Fits.size()==reference.Fits.size()&&result.Blocked.size()==reference.Blocked.size(),
            "external Problem edit changed a bound generator query");
        for(std::size_t i=0;i<result.Fits.size();++i)
            require(result.Fits[i].X==reference.Fits[i].X&&result.Fits[i].Y==reference.Fits[i].Y&&
                result.Fits[i].AngleDegrees==reference.Fits[i].AngleDegrees,
                "world or pair cache crossed immutable Problem snapshots");
    };
    same(bound.Find(board,1,0,limits),expected);
    input.Parts[0].Geometry.Outer={{0,0},{8,0},{8,10},{0,10}};
    input.Parts[1].Geometry.Outer={{0,0},{2,0},{2,10},{0,10}};
    Detail::NfpPlacementGenerator changed(input),freshChanged(input);
    const auto altered=freshChanged.Find(board,1,0,limits);
    require(!altered.Fits.empty()&&altered.Fits.front().X!=expected.Fits.front().X,
        "different Problem fixture did not change its original-material placement");
    same(changed.Find(board,1,0,limits),altered);
    same(bound.Find(board,1,0,limits),expected);
    for(const auto pose:altered.Fits)requireNominalFit(input,board,1,pose);
    for(int mutation=0;mutation<4;++mutation)
    {
        input=original;
        auto& shape=input.Parts[0].Geometry;
        if(mutation==0)shape.Holes={{{1,1},{3,1},{3,3},{1,3}}};
        if(mutation==1)shape.CommonEdgeRings={false};
        if(mutation==2){shape.Outer.clear();shape.Radius=2;shape.Center={2,5};}
        if(mutation==3){shape.Outer.clear();shape.Radius=3;shape.Center={3,5};}
        // New generators bind the changed complete material, including fields
        // unsupported by this NFP entry. Existing generators retain their own
        // complete original snapshot, not a mixture of new and cached fields.
        Detail::NfpPlacementGenerator fresh(input),separate(input);
        same(separate.Find(board,1,0,limits),fresh.Find(board,1,0,limits));
        same(bound.Find(board,1,0,limits),expected);
        same(changed.Find(board,1,0,limits),altered);
    }
    for(const auto pose:bound.Find(board,1,0,limits).Fits)requireNominalFit(original,board,1,pose);
    std::cout<<"PASS generators own isolated immutable Problem snapshots across complete source-material edits\n";
}
static void BoundaryVerticesRetainTheCompleteContactFallback()
{
    Problem cavity;cavity.Tolerance=1e-7;cavity.Stocks.push_back({"board",14,5,1,1,"m"});
    cavity.Parts={polygon("U",{{0,0},{2,0},{2,3},{12,3},{12,0},{14,0},{14,5},{0,5}}),
        polygon("plug",{{0,0},{10,0},{10,3},{0,3}})};
    Layout board{"board",0,14,5,{{"U",0,{},0,false}},40};
    Detail::NfpPlacementGenerator reused(cavity),fresh(cavity);
    Detail::NfpPlacementLimits boundary;boundary.BoundaryVerticesOnly=true;boundary.PrioritizeFreeDomainRetreat=true;boundary.MaxCandidates=0;
    std::size_t sampled=0;
    boundary.Trace=[&](const ConstructionTraceEvent& event)
    {if(event.Stage==ConstructionTraceStage::Generate&&(event.Source=="nfp.free-domain-midpoint"||
        event.Source=="nfp.pair-forbidden-vertex"||event.Source=="nfp.contact-midpoint"))++sampled;};
    const auto vertices=reused.Find(board,1,0,boundary);
    require(vertices.Supported&&vertices.Fits.empty()&&sampled==0,
        "boundary-first mode fabricated a touch-only fit or enumerated deferred sampling");
    // A zero-width legal cavity is swallowed by regularized subtraction.
    // The full query remains available and must not reuse an incomplete pair.
    auto complete=boundary;complete.BoundaryVerticesOnly=false;complete.Trace={};
    const auto fallback=reused.Find(board,1,0,complete),expected=fresh.Find(board,1,0,complete);
    require(!fallback.Fits.empty()&&fallback.Fits.size()==expected.Fits.size()&&fallback.Generated==expected.Generated&&
        fallback.Attempts==expected.Attempts,"full contact fallback changed after a boundary-only cached pair");
    for(std::size_t i=0;i<fallback.Fits.size();++i)
    {
        require(fallback.Fits[i].X==expected.Fits[i].X&&fallback.Fits[i].Y==expected.Fits[i].Y,
            "boundary preparation polluted the complete contact query");
        requireNominalFit(cavity,board,1,fallback.Fits[i]);
    }
    std::cout<<"PASS boundary vertices defer sampling without losing the complete touch-only fallback\n";
}
static void VerifiedFreeDomainRetreatUsesTheOriginalSafetyGate()
{
    Problem p;p.Tolerance=1e-7;p.EdgeMargin=.5;p.Stocks.push_back({"board",80,1000000,1,1,"m"});
    auto moving=polygon("moving",{});moving.FreeRotation=true;moving.AllowedAnglesDegrees.clear();
    for(std::size_t i=0;i<48;++i)
    {const double angle=2*Detail::Pi*double(i)/48,radius=i%2?2.5:3;moving.Geometry.Outer.push_back({radius*std::cos(angle),radius*std::sin(angle)});}
    p.Parts={moving,polygon("fixed",{{0,0},{8,0},{8,6},{0,6}})};
    Layout board{"board",0,80,1000000,{{"fixed",0,{12,10000,0},0,false}},48};
    Detail::NfpPlacementGenerator generator(p);Detail::NfpPlacementLimits limits;
    limits.BoundaryVerticesOnly=true;limits.PrioritizeFreeDomainRetreat=true;limits.MaxCandidates=0;
    std::size_t hints=0;
    limits.Trace=[&](const ConstructionTraceEvent& event)
    {if(event.Stage==ConstructionTraceStage::Generate&&event.Source=="nfp.free-domain-inward-retreat")++hints;};
    const auto query=generator.Find(board,0,37,limits);
    require(query.Supported&&!query.Fits.empty()&&hints>0,"strict free-domain side probes did not exercise an inward retreat");
    for(const auto pose:query.Fits)requireNominalFit(p,board,0,pose);
    // An extra heuristic proposal must leave room for the complete established
    // fallback. With a two-attempt cap it is therefore not attempted at all.
    auto few=limits;few.MaxAttempts=2;hints=0;
    const auto limited=generator.Find(board,0,37,few);
    auto ordinary=few;ordinary.PrioritizeFreeDomainRetreat=false;ordinary.Trace={};
    Detail::NfpPlacementGenerator fresh(p);const auto expected=fresh.Find(board,0,37,ordinary);
    require(hints==0&&limited.Attempts<=2&&limited.Attempts==expected.Attempts&&limited.Fits.size()==expected.Fits.size(),
        "domain hint consumed the established retreat's strict attempt budget");
    for(std::size_t i=0;i<limited.Fits.size();++i)
        require(limited.Fits[i].X==expected.Fits[i].X&&limited.Fits[i].Y==expected.Fits[i].Y,
            "strict-budget fallback changed after enabling a domain hint");
    std::cout<<"PASS strict free-domain side proposals retain original material checks, holes and bounded fallback\n";
}
static void ReusedWorldGeometryMatchesFreshQueries()
{
    Problem p;p.Tolerance=1e-7;p.Stocks.push_back({"board",2000000,2000000,1,1,"m"});
    auto dense=polygon("dense",{});dense.Quantity=2;dense.FreeRotation=true;dense.AllowedAnglesDegrees.clear();
    for(std::size_t i=0;i<48;++i)
    {
        const double angle=2*Detail::Pi*double(i)/48,radius=i%2?2.5:3;
        dense.Geometry.Outer.push_back({radius*std::cos(angle),radius*std::sin(angle)});
    }
    auto moving=dense;moving.ID="moving";
    p.Parts={dense,moving,polygon("cover",{{0,0},{10,0},{10,10},{0,10}}),
        polygon("small",{{0,0},{3,0},{3,3},{0,3}})};
    p.Parts[2].AllowedAnglesDegrees={0,90};
    Detail::NfpPlacementGenerator reused(p);require(reused.Ready(),"reuse input unsupported");
    Detail::NfpPlacementLimits limits;limits.FastAdjacency=true;limits.StopAfterFirstFit=true;
    const auto compare=[&](const Layout& board,std::size_t part,double angle,const Detail::NfpPlacementLimits& settings)
    {
        Detail::NfpPlacementGenerator fresh(p);
        const auto expected=fresh.Find(board,part,angle,settings),actual=reused.Find(board,part,angle,settings);
        require(actual.Supported==expected.Supported&&actual.Truncated==expected.Truncated&&
            actual.Attempts==expected.Attempts&&actual.Generated==expected.Generated&&
            actual.DomainMeasured==expected.DomainMeasured&&actual.FreeTranslationArea==expected.FreeTranslationArea&&
            actual.Fits.size()==expected.Fits.size()&&actual.Blocked.size()==expected.Blocked.size(),
            "cached world geometry changed query events, budgets or result counts");
        for(std::size_t i=0;i<actual.Fits.size();++i)
        {
            const auto a=actual.Fits[i],b=expected.Fits[i];
            require(a.X==b.X&&a.Y==b.Y&&a.AngleDegrees==b.AngleDegrees,"cached world geometry changed a candidate pose");
            requireNominalFit(p,board,part,a);
        }
        for(std::size_t i=0;i<actual.Blocked.size();++i)
        {
            const auto& a=actual.Blocked[i];const auto& b=expected.Blocked[i];
            require(a.Transform.X==b.Transform.X&&a.Transform.Y==b.Transform.Y&&a.Transform.AngleDegrees==b.Transform.AngleDegrees&&
                a.Blockers==b.Blockers&&a.OverlapArea==b.OverlapArea,"cached world geometry changed original blocker identities");
            requireCompleteBlockers(p,board,part,a);
        }
        return actual;
    };
    Layout board{"board",0,80,60,{{"dense",0,{15,15,0},0,false},{"cover",0,{40,20,0},0,false}},0};
    for(double angle:{0.,90.,37.,270.})compare(board,1,angle,limits);
    // The same generator sees moved, rotated, removed and reordered placements.
    // No cached board membership or blocker index may survive those changes.
    board.Placements[0].Transform={3,3,23.7};board.Placements[1].Transform={40,20,90};
    for(double angle:{0.,90.,37.})compare(board,1,angle,limits);
    std::reverse(board.Placements.begin(),board.Placements.end());compare(board,1,180,limits);
    board.Placements.pop_back();compare(board,1,270,limits);
    board.Width=board.Height=2000000;board.Placements={{"dense",0,{1000000.125,1000000.25,37},0,false}};
    compare(board,1,0,limits);compare(board,1,37,limits);
    Layout covered{"board",0,10,10,{{"cover",0,{},0,false}},100};
    auto blockers=limits;blockers.MaxBlockers=1;blockers.MaxBlockedCandidates=0;
    require(!compare(covered,3,0,blockers).Blocked.empty(),"reuse blocker regression did not exercise blocked placements");
    const int mode=std::fegetround();
    {
        struct Restore {int Mode;~Restore(){std::fesetround(Mode);}} restore{mode};
        require(std::fesetround(FE_UPWARD)==0,"could not exercise cached arithmetic uncertainty");
        const auto uncertain=reused.Find(board,1,0,limits);
        require(uncertain.Fits.empty()&&uncertain.Blocked.empty()&&uncertain.Truncated,
            "supported-mode cached geometry bypassed uncertain arithmetic");
    }
    compare(board,1,0,limits);
    std::cout<<"PASS reused fixed world geometry preserves changing layouts, exact poses, blockers and arithmetic modes\n";
}
static void QuickPlacementControls()
{
    Problem p;p.Tolerance=1e-7;p.Stocks.push_back({"board",20,10,1,1,"m"});
    p.Parts={polygon("fixed",{{0,0},{5,0},{5,2},{0,2}}),
        polygon("moving",{{0,0},{4,0},{4,2},{0,2}})};
    Layout board{"board",0,20,10,{{"fixed",0,{},0,false}},10};
    auto legacyCache=std::make_shared<NfpCache>();Detail::NfpPlacementGenerator legacy(p,legacyCache);
    Detail::NfpPlacementLimits original;original.StopAfterFirstFit=true;
    const auto scored=legacy.Find(board,1,0,original);
    require(!original.FastAdjacency&&scored.DomainMeasured&&legacyCache->Statistics().RegionBuilds>0,
        "default first-fit placement no longer preserves original NFP event construction");
    for(const auto pose:scored.Fits)requireNominalFit(p,board,1,pose);
    auto cache=std::make_shared<NfpCache>();Detail::NfpPlacementGenerator generator(p,cache);
    Detail::NfpPlacementLimits limits;limits.StopAfterFirstFit=true;limits.FastAdjacency=true;limits.MaxAttempts=100;
    const auto quick=generator.Find(board,1,0,limits);
    require(quick.Supported&&quick.Fits.size()==1&&quick.Truncated&&!quick.DomainMeasured,
        "cheap first fit did not preserve unmeasured-domain semantics");
    require(quick.Fits.front().X==5&&quick.Fits.front().Y==0&&cache->Statistics().RegionBuilds==0,
        "cheap first fit missed low-left adjacency or built unnecessary NFP geometry");
    requireNominalFit(p,board,1,quick.Fits.front());
    require(quick.Attempts>1&&quick.Attempts<=limits.MaxAttempts&&quick.Generated>0,
        "cheap first-fit attempts or candidate generation were not charged");
    auto one=limits;one.MaxAttempts=1;
    const auto capped=generator.Find(board,1,0,one);
    require(capped.Fits.empty()&&capped.Attempts==1&&capped.Truncated,
        "cheap first fit exceeded its actual attempt allowance");
    auto noEvents=limits;noEvents.MaxRawCandidates=0;
    const auto none=generator.Find(board,1,0,noEvents);
    require(none.Fits.empty()&&none.Attempts==0&&none.Truncated,
        "cheap first fit invented candidates beyond the raw event cap");
    std::size_t polls=0;
    const auto cancelled=generator.Find(board,1,0,limits,[&]{return ++polls==4;});
    require(cancelled.Fits.empty()&&cancelled.Truncated,
        "cheap first fit ignored an in-progress cancellation");
    require(cache->Statistics().RegionBuilds==0,"cancelled cheap first fit entered NFP construction");

    Problem cavity;cavity.Tolerance=1e-7;cavity.Stocks.push_back({"board",14,5,1,1,"m"});
    cavity.Parts={polygon("U",{{0,0},{2,0},{2,3},{12,3},{12,0},{14,0},{14,5},{0,5}}),
        polygon("plug",{{0,0},{10,0},{10,3},{0,3}})};
    Layout enclosed{"board",0,14,5,{{"U",0,{},0,false}},40};
    auto cavityCache=std::make_shared<NfpCache>();Detail::NfpPlacementGenerator cavities(cavity,cavityCache);
    const auto fallback=cavities.Find(enclosed,1,0,limits);
    require(fallback.Supported&&fallback.Fits.size()==1&&fallback.DomainMeasured&&
        cavityCache->Statistics().RegionBuilds>0,"failed cheap fits did not fall back to original NFP events");
    requireNominalFit(cavity,enclosed,1,fallback.Fits.front());
    require(std::abs(fallback.Fits.front().X-2)<1e-6&&std::abs(fallback.Fits.front().Y)<1e-6,
        "NFP fallback lost the original interlocking cavity");

    const auto mode=std::fegetround();
    struct RestoreRounding {int Mode;~RestoreRounding(){std::fesetround(Mode);}} restore{mode};
    require(std::fesetround(FE_UPWARD)==0,"could not exercise cheap-fit arithmetic uncertainty");
    const auto uncertain=generator.Find(board,1,0,limits);
    require(uncertain.Fits.empty()&&uncertain.Truncated,
        "cheap first fit accepted uncertain nominal geometry");
    std::cout<<"PASS cheap first fits share original safety, budgets, cancellation and NFP fallback\n";
}
static void CallerPriorityOrdersCompleteProposals()
{
    Problem p;p.Tolerance=1e-7;p.Stocks.push_back({"board",20,10,1,1,"m"});
    p.Parts={polygon("fixed",{{0,0},{5,0},{5,2},{0,2}}),polygon("moving",{{0,0},{4,0},{4,2},{0,2}})};
    Layout board{"board",0,20,10,{{"fixed",0,{},0,false}},10};
    auto cache=std::make_shared<NfpCache>();Detail::NfpPlacementGenerator generator(p,cache);
    Detail::NfpPlacementLimits limits;limits.FastAdjacency=true;limits.StopAfterFirstFit=true;limits.MaxAttempts=1;
    std::size_t keys=0,adjacencyEvents=0;bool announced=false;
    limits.CandidatePriority=[&](Pose pose){++keys;return std::array<double,4>{-pose.X,pose.Y,0,0};};
    limits.Trace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source=="nfp.fast-adjacency"||event.Source=="nfp.fast-board-corner")++adjacencyEvents;
        if(event.Source=="nfp.query"&&event.Stage==ConstructionTraceStage::Begin)
            for(const auto& [name,value]:event.Measures)if(name=="caller-priority"&&value==1)announced=true;
    };
    const auto query=generator.Find(board,1,0,limits);
    require(query.Supported&&query.Fits.size()==1&&query.Fits[0].X==16&&query.Fits[0].Y==0,
        "caller priority did not order the complete contact stream before the first validation");
    require(keys==query.Generated&&keys>1&&query.Attempts==1&&announced&&adjacencyEvents==0&&
        query.DomainMeasured&&cache->Statistics().RegionBuilds>0,
        "priority repeated callback keys, bypassed complete NFP construction or exceeded its attempt budget");
    requireNominalFit(p,board,1,query.Fits[0]);
    auto invalid=limits;invalid.CandidatePriority=[](Pose){return std::array<double,4>{NAN,0,0,0};};
    const auto rejected=generator.Find(board,1,0,invalid);
    require(!rejected.Supported&&rejected.Truncated&&rejected.Fits.empty()&&rejected.Attempts==0,
        "nonfinite caller priorities reached an invalid sorting predicate");
    std::cout<<"PASS caller priority orders complete proposals once before bounded exact validation\n";
}
static void AdditionalFitGuardContinuesThroughOriginalSafety()
{
    Problem p;p.Tolerance=1e-7;p.Stocks.push_back({"board",10,10,1,1,"m"});
    p.Parts={polygon("moving",{{0,0},{2,0},{2,2},{0,2}})};
    Layout board{"board",0,10,10,{},0};Detail::NfpPlacementGenerator generator(p);
    Detail::NfpPlacementLimits limits;limits.StopAfterFirstFit=true;limits.MaxAttempts=100;
    std::size_t rejected=0,rejectionEvents=0;std::uint64_t acceptedID=0;
    Pose firstGuarded;bool sawFirst=false,sawAcceptedMidpoint=false;
    limits.Trace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source=="nfp.validation"&&event.Reason=="additional-fit-guard-rejected")++rejectionEvents;
        if(event.Source=="nfp.free-domain-midpoint"&&event.Stage==ConstructionTraceStage::Generate&&
            !event.Members.empty()&&event.Members[0].Transform.X==4&&event.Members[0].Transform.Y==0)sawAcceptedMidpoint=true;
    };
    limits.AcceptFit=[&](Pose pose,std::uint64_t id)
    {
        requireNominalFit(p,board,0,pose);
        if(!sawFirst){firstGuarded=pose;sawFirst=true;}
        if(pose.X<4){++rejected;return false;}
        acceptedID=id;return true;
    };
    const auto query=generator.Find(board,0,0,limits);
    // Complete mode includes the bottom-edge midpoint (4,0), before the
    // right-hand corner (8,0). Rejecting the left edge must continue to this
    // earlier complete-contact proposal, not silently revert to corners only.
    require(rejected>0&&rejectionEvents==rejected&&sawFirst&&firstGuarded.X==0&&firstGuarded.Y==0&&
        sawAcceptedMidpoint&&query.Fits.size()==1&&query.Fits[0].X==4&&query.Fits[0].Y==0&&query.Blocked.empty(),
        "additional original guard rejection ended the query or published a private-only fit");
    require(acceptedID!=0&&query.FitsIDs.size()==1&&query.FitsIDs[0]==acceptedID&&query.Attempts<=limits.MaxAttempts,
        "additional fit guard lost candidate identity or escaped the shared attempt bound");
    auto retreat=limits;retreat.MaxAttempts=32;
    retreat.AcceptFit=[&](Pose pose,std::uint64_t){requireNominalFit(p,board,0,pose);return pose.X>0&&pose.Y>0;};
    const auto adjusted=generator.Find(board,0,0,retreat);
    require(adjusted.Fits.size()==1&&adjusted.Fits[0].X>0&&adjusted.Fits[0].Y>0&&
        adjusted.Fits[0].X<1e-4&&adjusted.Fits[0].Y<1e-4,
        "additional fit guard did not permit the established finite representable retreat");
    auto bounded=limits;bounded.MaxAttempts=2;bounded.AcceptFit=[](Pose,std::uint64_t){return false;};
    const auto none=generator.Find(board,0,0,bounded);
    require(none.Fits.empty()&&none.Blocked.empty()&&none.Attempts==2&&none.Truncated,
        "rejected additional guards fabricated blockers or bypassed the exact attempt limit");
    std::cout<<"PASS additional fit guards retain original safety, candidate identities and bounded continuation\n";
}
static void NecessaryFitOnlyRejectsStoredPosesAndRetainsFiniteRetries()
{
    Problem p;p.Tolerance=1e-7;p.Stocks={{"board",10,10,1,1,"m"}};
    p.Parts={polygon("moving",{{0,0},{2,0},{2,2},{0,2}})};
    Layout empty{"board",0,10,10,{},0};Detail::NfpPlacementGenerator generator(p);
    Detail::NfpPlacementLimits limits;limits.StopAfterFirstFit=true;limits.MaxCandidates=1;limits.MaxAttempts=10000;
    limits.CandidatePriority=[](Pose pose){return std::array<double,4>{pose.Y,pose.X,0,0};};
    std::map<std::uint64_t,std::size_t> families;std::map<std::string,double> phases;std::size_t rejected=0,certified=0;
    limits.Trace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source=="nfp.roundoff-retreat"&&event.Stage==ConstructionTraceStage::Generate)++families[event.CandidateID];
        if(event.Reason=="necessary-fit-guard-rejected")++rejected;
        if(event.Source=="nfp.query"&&event.Stage==ConstructionTraceStage::Complete)
            for(const auto& [name,value]:event.Measures)phases[name]=value;
    };
    limits.NecessaryFit=[](Pose,std::uint64_t){return false;};
    limits.AcceptFit=[&](Pose,std::uint64_t){++certified;return true;};
    const auto none=generator.Find(empty,0,0,limits);
    require(none.Fits.empty()&&none.Blocked.empty()&&rejected>0&&certified==0&&none.Attempts<=limits.MaxAttempts&&
        std::any_of(families.begin(),families.end(),[](const auto& family){return family.second==24;}),
        "a necessary rejection approved a fit, invented blockers or suppressed the established complete finite retreat");
    require(phases["phaseBoundaryGeometryCalls"]>0&&phases["phaseBoundaryContainmentCalls"]>0&&
        phases["phaseFullGeometryCalls"]==0&&phases["phaseFullContainmentCalls"]==0,
        "a material necessary screen moved ahead of boundary containment or prepared full moving convex/hull geometry");
    limits.NecessaryFit=[](Pose pose,std::uint64_t){return pose.X>0&&pose.Y>0;};
    const auto repaired=generator.Find(empty,0,0,limits);
    require(repaired.Fits.size()==1&&repaired.Fits[0].X>0&&repaired.Fits[0].Y>0&&
        repaired.Fits[0].X<1e-4&&repaired.Fits[0].Y<1e-4&&certified>0,
        "a necessary rejection ended the raw proposal instead of preserving its legal representable retry");
    requireNominalFit(p,empty,0,repaired.Fits[0]);
    require(phases["phaseFullGeometryCalls"]>0&&phases["phaseFullContainmentCalls"]>0,
        "a surviving necessary retry skipped full geometry or its final original containment gate");

    auto covered=p;covered.Stocks[0].Width=covered.Stocks[0].Height=5;
    covered.Parts.push_back(polygon("cover",{{0,0},{5,0},{5,5},{0,5}}));
    Layout full{"board",0,5,5,{{"cover",0,{},0,false}},25};
    Detail::NfpPlacementGenerator blocked(covered);std::size_t screens=0,unknown=0;certified=0;
    limits.Trace=[&](const ConstructionTraceEvent& event){if(event.Reason=="necessary-fit-guard-unproved")++unknown;};
    for(const bool unproved:{false,true})
    {
        limits.NecessaryFit=[&](Pose,std::uint64_t)
        {++screens;if(unproved)throw std::runtime_error("unproved necessary screen");return true;};
        const auto result=blocked.Find(full,0,0,limits);
        require(result.Fits.empty()&&result.Blocked.empty()&&certified==0&&screens>0,
            "a passing or unproved necessary screen bypassed the complete original-material gate");
    }
    require(unknown>0,"an unproved necessary screen did not continue through the original hard verification");
    std::cout<<"PASS reject-only necessary screens retain all finite retries and complete original certification\n";
}
static void CompetitivePosePrecedesGeometryAndMaterialNecessaryFit()
{
    Problem p;p.Tolerance=1e-7;p.Stocks={{"board",10,10,1,1,"m"}};
    auto moving=polygon("moving",{{0,0},{2,0},{2,2},{0,2}});moving.AllowMirror=true;p.Parts={moving};
    Layout empty{"board",0,10,10,{},0};
    for(const bool mirror:{false,true})
    {
        Detail::NfpPlacementGenerator generator(p);Detail::NfpPlacementLimits limits;
        limits.StopAfterFirstFit=true;limits.MaxCandidates=1;limits.MaxAttempts=10000;limits.Mirrored=mirror;
        limits.CandidatePriority=[](Pose pose){return std::array<double,4>{pose.Y,pose.X,0,0};};
        std::map<std::string,double> phases;std::size_t early=0,material=0,accepted=0,orderViolations=0;
        std::optional<std::tuple<double,double,double,bool>> lastEarly;
        const auto key=[](Pose pose){return std::tuple(pose.X,pose.Y,pose.AngleDegrees,pose.Mirrored);};
        limits.Trace=[&](const ConstructionTraceEvent& event)
        {if(event.Source=="nfp.query"&&event.Stage==ConstructionTraceStage::Complete)
            for(const auto& [name,value]:event.Measures)phases[name]=value;};
        limits.NecessaryFit=[&](Pose pose,std::uint64_t)
        {++material;if(early&&(!lastEarly||*lastEarly!=key(pose)))++orderViolations;return true;};
        limits.AcceptFit=[&](Pose pose,std::uint64_t)
        {++accepted;requireNominalFit(p,empty,0,pose);return true;};
        const auto reference=generator.Find(empty,0,0,limits);
        require(reference.Fits.size()==1,"competitive screen fixture lost its unscreened safe reference");
        for(const bool unproved:{false,true})
        {
            phases.clear();early=material=accepted=orderViolations=0;lastEarly.reset();
            limits.CompetitivePose=[&](Pose pose,std::uint64_t)->bool
            {++early;lastEarly=key(pose);if(unproved)throw AxisSafety::ArithmeticUncertainty{};return true;};
            const auto actual=generator.Find(empty,0,0,limits);
            require(actual.Supported==reference.Supported&&actual.Truncated==reference.Truncated&&
                actual.Generated==reference.Generated&&actual.Attempts==reference.Attempts&&actual.FitsIDs==reference.FitsIDs&&
                actual.Fits.size()==1&&actual.Fits[0].X==reference.Fits[0].X&&actual.Fits[0].Y==reference.Fits[0].Y&&
                actual.Fits[0].Mirrored==mirror&&early>0&&material>0&&accepted==1&&orderViolations==0&&
                phases["phaseFullGeometryCalls"]>0&&phases["phaseFullContainmentCalls"]>0&&
                (unproved?phases["phaseCompetitivePoseUnknown"]>0:phases["phaseCompetitivePoseUnknown"]==0),
                "a passing/unproved competitive screen changed stored poses or bypassed the later original material chain");
        }
        phases.clear();early=material=accepted=0;
        limits.CompetitivePose=[&](Pose,std::uint64_t){++early;return false;};
        const auto none=generator.Find(empty,0,0,limits);
        require(none.Fits.empty()&&none.Blocked.empty()&&early>0&&material==0&&accepted==0&&
            none.Attempts==0&&
            phases["phaseCompetitivePoseRejected"]>0&&phases["phaseBoundaryContainmentCalls"]==0&&
            phases["phaseFullGeometryCalls"]==0&&phases["phaseFullContainmentCalls"]==0,
            "a noncompetitive stored pose ran numerical containment/full geometry or the expensive material screen");
        if(!mirror)
        {
            early=material=accepted=0;lastEarly.reset();limits.MaxAttempts=1;
            limits.CompetitivePose=[](Pose pose,std::uint64_t){return pose.X>0&&pose.Y>0;};
            const auto repaired=generator.Find(empty,0,0,limits);
            require(repaired.Fits.size()==1&&repaired.Fits[0].X>0&&repaired.Fits[0].Y>0&&
                repaired.Fits[0].X<1e-4&&repaired.Fits[0].Y<1e-4&&repaired.Attempts==1,
                "score-only rejects consumed the single material-attempt allowance or suppressed actual-pose retreats");
            requireNominalFit(p,empty,0,repaired.Fits[0]);
        }
    }
    Problem blocked;blocked.Tolerance=1e-7;blocked.Stocks={{"board",15,10,1,1,"m"}};
    blocked.Parts={polygon("left",{{0,0},{5,0},{5,6},{0,6}}),polygon("right",{{0,0},{3,0},{3,5},{0,5}}),
        polygon("moving",{{0,0},{4,0},{4,2},{0,2}})};
    Layout obstacles{"board",0,15,10,{{"left",0,{},0,false},{"right",0,{7,2,0},0,false}},45};
    Detail::NfpPlacementGenerator generator(blocked);Detail::NfpPlacementLimits staged;
    staged.MaxCandidates=1;staged.StopAfterFirstFit=true;staged.MaxAttempts=30000;
    staged.CandidatePriority=[](Pose pose){return std::array<double,4>{std::hypot(pose.X-3,pose.Y-3.5),pose.Y,pose.X,0};};
    using PoseKey=std::tuple<double,double,double,bool>;std::set<PoseKey> earlySeen,materialSeen;
    const auto key=[](Pose pose){return PoseKey{pose.X,pose.Y,pose.AngleDegrees,pose.Mirrored};};
    std::size_t strictBeforeMaterial=0,orderViolations=0;
    staged.CompetitivePose=[&](Pose pose,std::uint64_t){earlySeen.insert(key(pose));return true;};
    staged.NecessaryFit=[&](Pose pose,std::uint64_t)
    {if(!earlySeen.contains(key(pose)))++orderViolations;materialSeen.insert(key(pose));return true;};
    staged.Trace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Reason!="strict-nfp-interior"||event.Members.empty())return;
        const auto pose=key(event.Members[0].Transform);
        if(!earlySeen.contains(pose)||materialSeen.contains(pose))++orderViolations;
        ++strictBeforeMaterial;
    };
    const auto actual=generator.Find(obstacles,2,0,staged);
    require(actual.Fits.size()==1&&strictBeforeMaterial>0&&!materialSeen.empty()&&orderViolations==0,
        "expensive NecessaryFit ran before the cheap strict NFP interior rejection or ahead of CompetitivePose");
    requireNominalFit(blocked,obstacles,2,actual.Fits[0]);
    staged.Trace={};staged.NecessaryFit={};
    staged.CompetitivePose=[](Pose,std::uint64_t)->bool{throw std::runtime_error("unproved competitive screen");};
    const auto unproved=generator.Find(obstacles,2,0,staged);
    require(!unproved.Fits.empty(),"unproved competitive screen suppressed the original complete safety chain");
    for(const auto pose:unproved.Fits)requireNominalFit(blocked,obstacles,2,pose);
    std::cout<<"PASS separate competitive/material screens preserve early rejection, strict-NFP order, mirrors, unknown fallbacks and actual retreats\n";
}

static void LazyMovingMaterialRetainsCompleteOriginalCertification()
{
    Problem p;p.Tolerance=1e-7;p.EdgeMargin=.5;p.Stocks={{"board",60,50,1,1,"m"}};
    auto moving=polygon("moving",{});moving.AllowMirror=true;moving.FreeRotation=true;
    for(std::size_t i=0;i<48;++i)
    {
        const double a=2*Detail::Pi*double(i)/48,r=i%2?2.5:3;
        moving.Geometry.Outer.push_back({r*std::cos(a),r*std::sin(a)});
    }
    p.Parts={polygon("fixed",{{0,0},{8,0},{8,6},{0,6}}),moving};
    Layout board{"board",0,60,50,{{"fixed",0,{20,18,0},0,false}},48};
    for(const double angle:{0.,23.7})for(const bool mirror:{false,true})
    {
        Detail::NfpPlacementGenerator reused(p);Detail::NfpPlacementLimits limits;
        limits.MaxCandidates=1;limits.StopAfterFirstFit=true;limits.Mirrored=mirror;
        limits.CandidatePriority=[](Pose pose){return std::array<double,4>{pose.Y,pose.X,0,0};};
        std::optional<Detail::NfpPlacementQuery> reference;
        using Raw=std::tuple<double,double,double,bool>;std::vector<Raw> referenceRaw;
        for(int mode=0;mode<3;++mode)
        {
            std::map<std::string,double> phases;std::vector<Raw> raw;std::size_t certified=0;
            limits.NecessaryFit={};
            if(mode==1)limits.NecessaryFit=[](Pose,std::uint64_t){return true;};
            if(mode==2)limits.NecessaryFit=[](Pose,std::uint64_t)->bool{throw AxisSafety::ArithmeticUncertainty{};};
            limits.AcceptFit=[&](Pose pose,std::uint64_t)
            {requireNominalFit(p,board,1,pose);++certified;return true;};
            limits.Trace=[&](const ConstructionTraceEvent& event)
            {
                if(event.Source=="nfp.query"&&event.Stage==ConstructionTraceStage::Complete)
                    for(const auto& [name,value]:event.Measures)phases[name]=value;
                if(event.Reason=="raw-candidate"&&!event.Members.empty())
                {const auto pose=event.Members[0].Transform;raw.emplace_back(pose.X,pose.Y,pose.AngleDegrees,pose.Mirrored);}
            };
            const auto query=reused.Find(board,1,angle,limits);
            require(query.Supported&&query.Fits.size()==1&&certified==1&&
                phases["phaseFullGeometryCalls"]>0&&phases["phaseFullContainmentCalls"]>0,
                "a passed or unproved lazy necessary screen bypassed full original preparation and certification");
            require((mode==0?phases["phaseBoundaryGeometryCalls"]==0:phases["phaseBoundaryGeometryCalls"]>0)&&
                (mode!=2||phases["phaseNecessaryFitUnknown"]>0),
                "lazy geometry changed the legacy path or failed to retain an unproved necessary screen");
            auto complete=board;const auto pose=query.Fits[0];
            complete.Placements.push_back({moving.ID,0,pose,0,false});complete.MaterialArea+=Detail::Area(moving.Geometry);
            Result result;result.State=Status::Complete;result.Layouts={complete};const auto checked=Validate(p,result);
            require(checked.Valid&&checked.Complete&&pose.Mirrored==mirror&&Detail::PoseAllowed(moving,pose,p.Tolerance),
                "lazy moving material accepted an incomplete or invalid original arbitrary-angle/reflected layout");
            if(!reference){reference=query;referenceRaw=std::move(raw);}
            else require(raw==referenceRaw&&query.Generated==reference->Generated&&query.Attempts==reference->Attempts&&
                pose.X==reference->Fits[0].X&&pose.Y==reference->Fits[0].Y&&pose.AngleDegrees==reference->Fits[0].AngleDegrees&&
                pose.Mirrored==reference->Fits[0].Mirrored,
                "lazy/full preparation changed original raw proposals, retry attempts or the selected complete legal layout");
        }
        if(mirror)
        {
            auto forbidden=p;forbidden.Parts[1].AllowMirror=false;
            Detail::NfpPlacementGenerator denied(forbidden);limits.Trace={};limits.AcceptFit={};limits.NecessaryFit=[](Pose,std::uint64_t){return true;};
            const auto rejected=denied.Find(board,1,angle,limits);
            require(rejected.Fits.empty()&&rejected.Attempts==0,"lazy material preparation bypassed the source mirror prohibition");
        }
    }
    std::cout<<"PASS lazy boundary/full preparation retains legacy proposals, arbitrary angles, mirrors, unknown screens and full original certification\n";
}
static void ImplicitMaterialProofMatchesExplicitOrderedProposals()
{
    const auto compare=[](const Problem& p,const Layout& board,std::size_t moving,double angle,bool mirrored,
        Point target,bool requireInterior,bool requireSeam)
    {
        using Raw=std::tuple<std::string,double,double,double,bool>;
        Detail::NfpPlacementGenerator reused(p);
        Detail::NfpPlacementLimits limits;limits.MaxCandidates=1;limits.MaxAttempts=30000;limits.Mirrored=mirrored;
        limits.CandidatePriority=[=](Pose pose)
        {return std::array<double,4>{std::hypot(pose.X-target.X,pose.Y-target.Y),pose.Y,pose.X,0};};
        std::vector<Raw> expectedRaw;std::optional<Pose> expectedFit;
        for(const auto [first,indexed]:std::array<std::pair<bool,bool>,4>{{{true,false},{false,false},{false,true},{false,false}}})
        {
            std::vector<Raw> raw;std::size_t strict=0;bool announced=false;
            limits.StopAfterFirstFit=first;limits.IndexedMaterialProof=indexed;
            limits.Trace=[&](const ConstructionTraceEvent& event)
            {
                if(event.Reason=="raw-candidate"&&!event.Members.empty())
                {
                    const auto pose=event.Members[0].Transform;
                    raw.emplace_back(event.Source,pose.X,pose.Y,pose.AngleDegrees,pose.Mirrored);
                }
                if(event.Reason=="strict-nfp-interior")++strict;
                if(event.Source=="nfp.query"&&event.Stage==ConstructionTraceStage::Begin)
                    for(const auto& [name,value]:event.Measures)if(name=="material-contact-proof-implicit")
                        announced=value==((first||indexed)?1.:0.);
            };
            const auto result=reused.Find(board,moving,angle,limits);
            require(announced&&result.Supported&&result.Fits.size()==1&&result.Attempts<=limits.MaxAttempts,
                "explicit/implicit material proof comparison did not exercise both original query modes");
            requireNominalFit(p,board,moving,result.Fits[0]);
            if(requireInterior)require(strict>0,"implicit material proof bypassed deep-interior rejection instead of indexing contact loci");
            const auto pose=result.Fits[0];
            require(pose.Mirrored==mirrored&&Detail::PoseAllowed(p.Parts[moving],pose,p.Tolerance),
                "implicit material indexing changed the original reflected orientation permission");
            if(requireSeam)
                require(AxisSafety::CommonEdge(AxisSafety::Make(p.Parts[moving].Geometry,pose),
                    AxisSafety::Make(p.Parts[0].Geometry,board.Placements[0].Transform),p.Tolerance,true),
                    "implicit material index swallowed an actual stored-coordinate oblique common-edge phase");
            if(!expectedFit){expectedRaw=std::move(raw);expectedFit=pose;}
            else require(raw==expectedRaw&&pose.X==expectedFit->X&&pose.Y==expectedFit->Y&&
                pose.AngleDegrees==expectedFit->AngleDegrees&&pose.Mirrored==expectedFit->Mirrored,
                "implicit/explicit proof or a reused pair cache changed raw proposals or the first original-safe fit");
        }
        using Final=std::tuple<double,double,double,bool>;
        std::vector<Final> expectedFull;std::vector<Raw> expectedFullRaw;bool haveFull=false;
        limits.StopAfterFirstFit=false;limits.MaxCandidates=0;
        for(const bool indexed:{false,true,false})
        {
            limits.IndexedMaterialProof=indexed;std::vector<Raw> raw;
            limits.Trace=[&](const ConstructionTraceEvent& event)
            {
                if(event.Reason=="raw-candidate"&&!event.Members.empty())
                {const auto pose=event.Members[0].Transform;
                    raw.emplace_back(event.Source,pose.X,pose.Y,pose.AngleDegrees,pose.Mirrored);}
            };
            const auto result=reused.Find(board,moving,angle,limits);std::vector<Final> all;
            require(result.Supported&&!result.Fits.empty()&&result.Attempts<=limits.MaxAttempts,
                "complete indexed material-proof query lost its original-safe result");
            for(const auto pose:result.Fits)
            {requireNominalFit(p,board,moving,pose);all.emplace_back(pose.X,pose.Y,pose.AngleDegrees,pose.Mirrored);}
            std::sort(all.begin(),all.end());
            if(!haveFull){haveFull=true;expectedFull=all;expectedFullRaw=raw;}
            else require(all==expectedFull&&raw==expectedFullRaw,
                "indexed proof changed complete-query candidate events or the complete certified stored-pose set");
        }
        if(mirrored)
        {
            auto forbidden=p;forbidden.Parts[moving].AllowMirror=false;
            Detail::NfpPlacementGenerator denied(forbidden);limits.StopAfterFirstFit=true;limits.Trace={};
            const auto result=denied.Find(board,moving,angle,limits);
            require(result.Fits.empty()&&result.Attempts==0,"implicit material index bypassed a forbidden original reflection");
        }
    };
    Problem blocked;blocked.Tolerance=1e-7;blocked.Stocks={{"board",15,10,1,1,"m"}};
    blocked.Parts={polygon("left",{{0,0},{5,0},{5,6},{0,6}}),polygon("right",{{0,0},{3,0},{3,5},{0,5}}),
        polygon("moving",{{0,0},{4,0},{4,2},{0,2}})};
    Layout obstacles{"board",0,15,10,{{"left",0,{},0,false},{"right",0,{7,2,0},0,false}},45};
    compare(blocked,obstacles,2,0,false,{3,3.5},true,false);

    Problem cavity;cavity.Tolerance=1e-7;cavity.Stocks={{"board",14,5,1,1,"m"}};
    cavity.Parts={polygon("U",{{0,0},{2,0},{2,3},{12,3},{12,0},{14,0},{14,5},{0,5}}),
        polygon("plug",{{0,0},{10,0},{10,3},{0,3}})};
    for(auto& part:cavity.Parts)part.AllowMirror=true;
    Layout channel{"board",0,14,5,{{"U",0,{0,5,0,true},0,false}},40};
    compare(cavity,channel,1,0,true,{2,5},false,false);

    Problem oblique;oblique.Tolerance=1e-7;oblique.AllowCommonEdges=true;oblique.Stocks={{"board",40,40,1,1,"m"}};
    oblique.Parts={polygon("upper",{{0,0},{10,0},{0,10}}),polygon("lower",{{0,0},{0,-10},{10,0}})};
    for(auto& part:oblique.Parts){part.AllowMirror=true;part.FreeRotation=true;}
    for(const bool mirror:{false,true})
    {
        Layout seam{"board",0,40,40,{{"upper",0,{10,20,23.7,mirror},0,false}},50};
        compare(oblique,seam,1,23.7,mirror,{10,20},false,true);
    }
    std::cout<<"PASS implicit indexed and explicit material proofs retain ordered candidates, strict rejection, mirrored channels and oblique seams\n";
}
static void VerificationGapCertifiesTheCompleteOriginalMaterial()
{
    const auto originalFit=[](const Problem& original,const Layout& board,std::size_t index,Pose pose)
    {
        const auto& moving=original.Parts[index].Geometry;
        if(!Detail::InsideSheet(moving,pose,board.Width,board.Height,original.EdgeMargin,original.Tolerance))return false;
        const auto geometry=AxisSafety::Make(moving,pose);
        if(AxisSafety::Containment(geometry,board.Width,board.Height,original.EdgeMargin)!=AxisSafety::Verdict::Safe)return false;
        for(const auto& placed:board.Placements)
        {
            const auto part=std::find_if(original.Parts.begin(),original.Parts.end(),[&](const Part& p){return p.ID==placed.PartID;});
            require(part!=original.Parts.end(),"verification-gap reference has an unknown original");
            const auto fixed=AxisSafety::Make(part->Geometry,placed.Transform);
            if(AxisSafety::EnvelopesSeparated(geometry,fixed,AxisSafety::Clearance(geometry,fixed,original.PartGap)))continue;
            if(Detail::ShapesConflict(moving,pose,part->Geometry,placed.Transform,original.PartGap,
                original.Tolerance,original.AllowCommonEdges))return false;
            if(AxisSafety::Pair(geometry,fixed,original.PartGap,original.AllowCommonEdges,
                original.Tolerance)!=AxisSafety::Verdict::Safe)return false;
        }
        return true;
    };
    for(int fixture=0;fixture<5;++fixture)
    {
        Problem proposal;proposal.Tolerance=1e-7;proposal.AllowCommonEdges=fixture==1||fixture==2||fixture==4;
        proposal.Stocks.push_back({"board",9.2,10,1,1,"m"});
        proposal.Parts={polygon("fixed",{{0,0},{4,0},{4,10},{0,10}}),
            polygon("moving",{{0,0},{4,0},{4,10},{0,10}})};
        Layout board{"board",0,9.2,10,{{"fixed",0,{1.1,0,0},0,false}},40};
        double angle=0;
        // Quarter turns retain exact sums of the binary64 source and pose.
        // 1.1 + 4 is not the same exact coordinate as the rounded pose 5.1;
        // use a representable sum for the positive seam fixture and retain
        // the decimal version below as a required false-seam rejection.
        if(fixture==1)board.Placements[0].Transform.X=1.125;
        if(fixture==2)proposal.Parts[0].Geometry.CommonEdgeRings={false};
        if(fixture==3)
        {
            proposal.Stocks[0].Width=board.Width=20;proposal.Stocks[0].Height=board.Height=15;
            proposal.EdgeMargin=.5;proposal.Parts[0].FreeRotation=proposal.Parts[1].FreeRotation=true;
            proposal.Parts[0].Geometry.Outer={{0,0},{5,0},{5,2},{2,2},{2,4},{0,4}};
            proposal.Parts[1].Geometry.Outer={{0,0},{4,0},{4,2},{0,2}};
            board.Placements[0].Transform={6,4,37};angle=37;
        }
        auto original=proposal;original.PartGap=.2;
        if(fixture==1)
        {
            const Pose seam{5.125,0,0};
            const auto moving=AxisSafety::Make(original.Parts[1].Geometry,seam);
            const auto fixed=AxisSafety::Make(original.Parts[0].Geometry,board.Placements[0].Transform);
            require(AxisSafety::CommonEdge(moving,fixed,original.Tolerance,true)&&originalFit(original,board,1,seam),
                "positive seam fixture was not certified by the complete original oracle");
        }
        if(fixture==4)
            require(!originalFit(original,board,1,{5.1,0,0}),
                "a rounded decimal contact was incorrectly certified as an exact common edge");
        auto cache=std::make_shared<NfpCache>();Detail::NfpPlacementGenerator generator(proposal,cache);
        for(const bool first:{false,true})
        {
            Detail::NfpPlacementLimits legacy;legacy.MaxCandidates=0;legacy.StopAfterFirstFit=first;
            legacy.CandidatePriority=[](Pose pose){return std::array<double,4>{pose.Y,pose.X,0,0};};
            std::size_t legacyRetreats=0,strongerRetreats=0,retryableGapRejects=0;
            legacy.Trace=[&](const ConstructionTraceEvent& event)
            {if(event.Source=="nfp.roundoff-retreat"&&event.Stage==ConstructionTraceStage::Generate)++legacyRetreats;};
            legacy.AcceptFit=[&](Pose pose,std::uint64_t){return originalFit(original,board,1,pose);};
            const auto expected=generator.Find(board,1,angle,legacy);
            auto stronger=legacy;stronger.VerificationGap=.2;stronger.AcceptFit={};
            bool announced=false;
            stronger.Trace=[&](const ConstructionTraceEvent& event)
            {
                if(event.Source=="nfp.roundoff-retreat"&&event.Stage==ConstructionTraceStage::Generate)++strongerRetreats;
                if(event.Source=="nfp.query"&&event.Stage==ConstructionTraceStage::Begin)
                    for(const auto& [name,value]:event.Measures)if(name=="verification-gap"&&value==.2)announced=true;
                if(event.Reason=="verification-gap-rejected")for(const auto& [name,value]:event.Measures)
                    if(name=="roundoff-retryable"&&value==1)++retryableGapRejects;
            };
            const auto actual=generator.Find(board,1,angle,stronger);
            require(actual.Supported==expected.Supported&&actual.Generated==expected.Generated&&actual.Attempts<=stronger.MaxAttempts&&
                actual.DomainMeasured==expected.DomainMeasured&&actual.FreeTranslationArea==expected.FreeTranslationArea&&
                actual.FitsIDs.size()==actual.Fits.size()&&actual.Blocked.empty()&&announced,
                "stronger verification changed its proposal domain, lost fit identity or escaped the shared attempt bound");
            if(!expected.Fits.empty())require(!actual.Fits.empty(),"stronger verification lost all prior original-safe fits");
            for(std::size_t i=0;i<actual.Fits.size();++i)
            {
                require(actual.FitsIDs[i]!=0&&originalFit(original,board,1,actual.Fits[i]),
                    "stronger verification published an unidentified or uncertified final world pose");
                requireNominalFit(original,board,1,actual.Fits[i]);
            }
            if(!first)for(const auto prior:expected.Fits)
                require(std::any_of(actual.Fits.begin(),actual.Fits.end(),[&](Pose pose)
                    {return pose.X==prior.X&&pose.Y==prior.Y&&pose.AngleDegrees==prior.AngleDegrees;}),
                    "the complete stronger stream dropped a prior original-safe final pose");
            if(fixture==0||fixture==2||fixture==4)
                require(actual.Fits.empty()&&retryableGapRejects>0&&legacyRetreats>0&&strongerRetreats>0,
                    "a disallowed seam or separated sub-gap bounding box bypassed clearance or retreat");
            if(fixture==1)
                require(std::any_of(actual.Fits.begin(),actual.Fits.end(),[](Pose pose)
                    {return pose.X==5.125&&pose.Y==0&&pose.AngleDegrees==0;}),
                    "positive verification gap rejected the oracle-certified zero-gap seam");
            if(fixture==3)require(!actual.Fits.empty(),"arbitrary-angle verification fixture produced no original-safe fit");
            auto bounded=stronger;bounded.MaxAttempts=2;
            const auto stopped=generator.Find(board,1,angle,bounded);
            require(stopped.Attempts<=2&&stopped.Blocked.empty(),"stronger verification bypassed the shared attempt bound");
            const auto cancelled=generator.Find(board,1,angle,stronger,[]{return true;});
            require(cancelled.Fits.empty()&&cancelled.Attempts==0&&cancelled.Truncated,
                "stronger verification ignored cancellation");
        }
        Detail::NfpPlacementLimits invalid;
        for(const double gap:{-.1,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        {
            invalid.VerificationGap=gap;
            const auto rejected=generator.Find(board,1,angle,invalid);
            require(!rejected.Supported&&rejected.Fits.empty()&&rejected.Attempts==0&&rejected.Generated==0,
                "invalid stronger verification gap was silently weakened");
        }
        invalid.VerificationGap=.2;invalid.MaxBlockers=1;
        const auto blockers=generator.Find(board,1,angle,invalid);
        require(!blockers.Supported&&blockers.Fits.empty()&&blockers.Blocked.empty()&&blockers.Attempts==0,
            "stronger verification invented displacement/blocker semantics");
        Detail::NfpPlacementLimits unchanged;unchanged.MaxCandidates=0;
        const auto unset=generator.Find(board,1,angle,unchanged);unchanged.VerificationGap=0;
        const auto zero=generator.Find(board,1,angle,unchanged);
        require(zero.Supported==unset.Supported&&zero.Truncated==unset.Truncated&&zero.Attempts==unset.Attempts&&
            zero.Generated==unset.Generated&&zero.DomainMeasured==unset.DomainMeasured&&
            zero.FreeTranslationArea==unset.FreeTranslationArea&&zero.Fits.size()==unset.Fits.size(),
            "an equal verification gap changed the established default query");
        for(std::size_t i=0;i<zero.Fits.size();++i)
            require(zero.Fits[i].X==unset.Fits[i].X&&zero.Fits[i].Y==unset.Fits[i].Y&&
                zero.Fits[i].AngleDegrees==unset.Fits[i].AngleDegrees,
                "an equal verification gap changed a default final world pose");
    }
    std::cout<<"PASS stronger verification retains zero-gap proposals, complete original clearance, seam permissions and bounded retreat\n";
}
static void MirroredOriginalsRetainTheirOwnDomainsCertificatesAndCaches()
{
    Problem p;p.Tolerance=1e-7;p.Stocks={{"board",3,4,1,1,"m"}};
    auto fixed=polygon("fixed",{{1,-1},{3,-1},{3,2},{1,2}});fixed.AllowMirror=true;
    auto moving=polygon("moving",{{0,0},{3,0},{3,1},{1,1},{1,4},{0,4}});moving.AllowMirror=true;
    p.Parts={fixed,moving};
    auto cache=std::make_shared<NfpCache>();Detail::NfpPlacementGenerator generator(p,cache);
    Detail::NfpPlacementLimits limits;limits.MaxCandidates=0;
    const auto verify=[&](const Problem& original,const Layout& board,const Detail::NfpPlacementQuery& query,bool mirrored)
    {
        for(const auto pose:query.Fits)
        {
            require(pose.Mirrored==mirrored&&Detail::PoseAllowed(original.Parts[1],pose,original.Tolerance),
                "mirrored query lost its original orientation permission or returned parity");
            requireNominalFit(original,board,1,pose);
            require(!Detail::ShapesConflict(original.Parts[0].Geometry,board.Placements[0].Transform,
                original.Parts[1].Geometry,pose,original.PartGap,original.Tolerance,original.AllowCommonEdges),
                "a mirrored candidate reused an incompatible original-material certificate");
            Result result;auto complete=board;complete.Placements.push_back({"moving",0,pose,0,false});
            complete.MaterialArea=Detail::Area(original.Parts[0].Geometry)+Detail::Area(original.Parts[1].Geometry);
            result.Layouts={complete};const auto checked=Validate(original,result);
            require(checked.Valid&&checked.Complete,"mirrored final original output failed full validation");
        }
    };
    std::size_t firstBuilds=0;
    for(const bool fixedMirror:{false,true})for(const bool movingMirror:{false,true})
    {
        Layout board{"board",0,3,4,{{"fixed",0,{0,2,0,fixedMirror},0,false}},6};
        limits.Mirrored=movingMirror;
        const auto query=generator.Find(board,1,0,limits);
        require(query.Supported&&query.Fits.empty()==(fixedMirror!=movingMirror),
            "a mirrored-only concave cavity used the other orientation's translation domain");
        verify(p,board,query,movingMirror);
        if(!fixedMirror&&!movingMirror)firstBuilds=cache->Statistics().RegionBuilds;
        Detail::NfpPlacementGenerator fresh(p);
        const auto expected=fresh.Find(board,1,0,limits);
        require(query.Attempts==expected.Attempts&&query.Generated==expected.Generated&&query.Fits.size()==expected.Fits.size(),
            "mirror-specific cached geometry changed the complete proposal or attempt stream");
        for(std::size_t i=0;i<query.Fits.size();++i)
            require(query.Fits[i].X==expected.Fits[i].X&&query.Fits[i].Y==expected.Fits[i].Y&&
                query.Fits[i].AngleDegrees==expected.Fits[i].AngleDegrees&&query.Fits[i].Mirrored==expected.Fits[i].Mirrored,
                "mirror-specific cache reuse changed an actual final original pose");
        auto bounded=limits;bounded.MaxAttempts=1;
        require(generator.Find(board,1,0,bounded).Attempts<=1,"mirrored validation exceeded the shared attempt budget");
        const auto stopped=generator.Find(board,1,0,limits,[]{return true;});
        require(stopped.Fits.empty()&&stopped.Attempts==0&&stopped.Truncated,"mirrored proposals ignored cancellation");
    }
    require(firstBuilds>0&&cache->Statistics().RegionBuilds>=firstBuilds+3,
        "NFP region keys merged distinct fixed/moving mirror pairs");
    const auto built=cache->Statistics().RegionBuilds;
    const auto a=cache->Prepare(fixed.Geometry,{},true),b=cache->Prepare(moving.Geometry,{},true);
    const auto prepared=cache->Get(a,0,b,0),shapes=cache->Get(fixed.Geometry,0,moving.Geometry,0,{},true,true);
    require(prepared.State==NfpStatus::Ready&&shapes.State==NfpStatus::Ready&&prepared.CacheHit&&shapes.CacheHit&&
        cache->Statistics().RegionBuilds==built,"prepared and direct mirror NFP APIs disagreed on their immutable geometry key");
    auto rotated=p;rotated.Stocks[0].Width=rotated.Stocks[0].Height=20;
    for(auto& part:rotated.Parts){part.FreeRotation=true;part.AllowedAnglesDegrees.clear();}
    Layout rotatedBoard{"board",0,20,20,{{"fixed",0,{6,6,23.7,true},0,false}},6};
    Detail::NfpPlacementGenerator arbitrary(rotated,cache);auto arbitraryLimits=limits;
    arbitraryLimits.Mirrored=true;arbitraryLimits.MaxCandidates=8;
    const auto arbitraryFits=arbitrary.Find(rotatedBoard,1,37,arbitraryLimits);
    require(!arbitraryFits.Fits.empty(),"mirrored NFP generation lost arbitrary original rotation angles");
    verify(rotated,rotatedBoard,arbitraryFits,true);

    auto forbidden=p;forbidden.Parts[1].AllowMirror=false;limits.Mirrored=true;
    Detail::NfpPlacementGenerator deniedMoving(forbidden,cache);
    Layout mirrorBoard{"board",0,3,4,{{"fixed",0,{0,2,0,true},0,false}},6};
    const auto rejected=deniedMoving.Find(mirrorBoard,1,0,limits);
    require(!rejected.Supported&&rejected.Fits.empty()&&rejected.Attempts==0,
        "a hot mirror cache bypassed the original moving part's reflection prohibition");
    forbidden=p;forbidden.Parts[0].AllowMirror=false;Detail::NfpPlacementGenerator deniedFixed(forbidden,cache);
    require(!deniedFixed.Find(mirrorBoard,1,0,limits).Supported,
        "a mirrored fixed obstacle bypassed its original reflection prohibition");

    auto common=p;common.PartGap=.2;common.AllowCommonEdges=true;
    Detail::ClearanceNfpPlacement clearance(common,common.Stocks[0],cache,{});
    limits.StopAfterFirstFit=true;limits.MaxCandidates=1;limits.MaxContactSegments=0;
    limits.CandidatePriority=[](Pose pose){return std::array<double,4>{pose.Y,pose.X,0,0};};
    bool nativeMirrorCertificate=false;
    limits.Trace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source!="nfp.original-clearance"||event.Reason!="original-clearance-fit"||!event.Accepted)return;
        for(const auto& [name,value]:event.Measures)if(name=="native-original-verification"&&value==1)
            nativeMirrorCertificate=!event.Members.empty()&&event.Members[0].Transform.Mirrored;
    };
    const auto commonFit=clearance.Find(mirrorBoard,1,0,limits,{});
    require(!commonFit.Fits.empty()&&nativeMirrorCertificate,
        "the same-original positive-gap stream lost the mirrored exact common-edge certificate");
    verify(common,mirrorBoard,commonFit,true);
    require(clearance.ContactRegion(0,0,1,0,{},true,true,true).State==NfpStatus::Ready&&
        clearance.ContactRegion(0,0,1,0,{},false,true,true).State==NfpStatus::Ready,
        "common and conservative-clearance NFP domains did not prepare both mirrored originals");
    forbidden=common;forbidden.Parts[1].AllowMirror=false;
    Detail::ClearanceNfpPlacement deniedClearance(forbidden,forbidden.Stocks[0],cache,{});
    require(deniedClearance.Find(mirrorBoard,1,0,limits,{}).Fits.empty()&&
        deniedClearance.ContactRegion(0,0,1,0,{},true,true,true).State==NfpStatus::Unsupported,
        "clearance proposal reflection bypassed original permissions");
    auto noSeam=common;noSeam.Parts[0].Geometry.CommonEdgeRings={false};
    Detail::ClearanceNfpPlacement disallowedSeam(noSeam,noSeam.Stocks[0],cache,{});
    require(disallowedSeam.Find(mirrorBoard,1,0,limits,{}).Fits.empty(),
        "mirrored common-edge preparation lost the original ring's seam prohibition");
    for(const bool circle:{false,true})
    {
        Problem material;material.Tolerance=1e-7;material.PartGap=.2;material.EdgeMargin=.5;
        material.AllowCommonEdges=true;material.Stocks={{"board",5,5,1,1,"m"}};
        auto source=polygon("source",{{0,0},{4,0},{4,4},{0,4}});source.AllowMirror=true;
        if(circle){source.Geometry={};source.Geometry.Center={1,2};source.Geometry.Radius=.75;}
        else
        {source.Geometry.Holes={{{.5,.5},{1.5,.5},{1.5,1.5},{.5,1.5}}};source.Geometry.CommonEdgeRings={true,false};}
        material.Parts={source};Detail::ClearanceNfpPlacement exactMaterial(material,material.Stocks[0],cache,{});
        Layout empty{"board",0,5,5,{},0};auto settings=limits;settings.Trace={};
        const auto result=exactMaterial.Find(empty,0,0,settings,{});
        require(!result.Fits.empty(),"mirrored circle or holed original lost its conservative proposal fallback");
        for(const auto pose:result.Fits)
        {
            require(pose.Mirrored&&Detail::InsideSheet(source.Geometry,pose,5,5,.5,material.Tolerance)&&
                AxisSafety::Containment(AxisSafety::Make(source.Geometry,pose),5,5,.5)==AxisSafety::Verdict::Safe,
                "mirrored fallback certified a private replacement instead of the full original material");
            Result candidate;auto board=empty;board.Placements.push_back({"source",0,pose,0,false});
            board.MaterialArea=Detail::Area(source.Geometry);candidate.Layouts={board};
            require(Validate(material,candidate).Valid&&Validate(material,candidate).Complete,
                "mirrored circle or holed original failed final original validation");
        }
        require(material.Parts[0].Geometry.Radius==source.Geometry.Radius&&
            material.Parts[0].Geometry.Holes.size()==source.Geometry.Holes.size()&&
            material.Parts[0].Geometry.CommonEdgeRings==source.Geometry.CommonEdgeRings,
            "private mirrored fallback altered original circle, holes or seam permissions");
    }
    require(p.Parts[1].Geometry.Outer[2].Y==1&&p.Parts[0].Geometry.Outer[0].Y==-1,
        "mirrored preparation changed immutable original geometry");
    std::cout<<"PASS mirrored concave fits, fixed obstacles, permissions, cache isolation, final certificates and budgets\n";
}
static void AFullPhasePoolCanEvictOnlyByTheCallersScore()
{
    constexpr double angle=23.7,side=3;
    const double radians=angle*Detail::Pi/180,c=std::cos(radians),s=std::sin(radians);
    Problem p;p.Tolerance=1e-7;p.EdgeMargin=.5;p.PartGap=.2;p.AllowCommonEdges=true;
    auto part=polygon("oblique",{{0,0},{side,0},{side,side},{0,side}});
    part.Quantity=2;part.AllowedAnglesDegrees={angle};p.Parts={part};
    p.Stocks={{"board",2*side*c+side*s+1+2e-5,2*side*s+side*c+1+2e-5,1,1,"m"}};
    Layout board{"board",0,p.Stocks[0].Width,p.Stocks[0].Height,{},0};
    const auto same=[](Pose a,Pose b)
    {return a.X==b.X&&a.Y==b.Y&&a.AngleDegrees==b.AngleDegrees&&a.Mirrored==b.Mirrored;};
    for(int mode=0;mode<3;++mode)
    {
        Detail::ClearanceNfpPlacement generator(p,p.Stocks[0],{},{});
        Detail::NfpPlacementLimits limits;limits.MaxCandidates=1;limits.MaxAttempts=8192;
        if(mode==1)limits.CandidatePriority=[](Pose pose)
        {
            // Coarse location precedes the strict actual-coordinate preference.
            // Inside one cell, moving lower/left is deliberately a worse score.
            return std::array<double,4>{std::floor(pose.X),std::floor(pose.Y),-pose.X,-pose.Y};
        };
        if(mode==2)limits.CandidatePriority=[](Pose){return std::array<double,4>{0,0,0,0};};
        std::optional<Pose> originalSeed;std::size_t certified=0,capRejected=0;
        limits.Trace=[&](const ConstructionTraceEvent& event)
        {
            if(event.Source!="nfp.original-clearance")return;
            if(event.Reason=="hypothetical-same-source-common-continuation-certified"&&event.Accepted)++certified;
            if(event.Reason.starts_with("first-seed-fit-cap-")&&!event.Accepted&&
                event.Reason!="first-seed-fit-cap-replaced-by-caller-priority")++capRejected;
            if(originalSeed||event.Reason!="original-clearance-fit"||!event.Accepted||event.Members.empty())return;
            for(const auto& [name,value]:event.Measures)if(name=="nominal-common-domain"&&value==0)
                originalSeed=event.Members[0].Transform;
        };
        const auto query=generator.Find(board,0,angle,limits,{});
        require(originalSeed&&certified>0&&query.Fits.size()==1&&query.Blocked.empty()&&
            query.Attempts<=limits.MaxAttempts&&query.Truncated,
            "full-pool fixture did not exercise an actual certified phase under the unchanged finite budget");
        const auto selected=query.Fits.front();requireNominalFit(p,board,0,selected);
        if(mode==0)
            require(same(selected,*originalSeed)&&capRejected>0,
                "a full pool without a caller score replaced an existing legal seed by coordinate dominance");
        else
        {
            const auto before=limits.CandidatePriority(*originalSeed),after=limits.CandidatePriority(selected);
            require(after<=before,"a lower coordinate overwrote a better existing caller-scored seed");
            if(after==before&&!same(selected,*originalSeed))
                require(selected.AngleDegrees==originalSeed->AngleDegrees&&selected.Mirrored==originalSeed->Mirrored&&
                    selected.X<=originalSeed->X&&selected.Y<=originalSeed->Y,
                    "an equal-score phase evicted an existing seed without coordinate-wise dominance");
            if(mode==1)require(capRejected>0,"caller-score counterexample did not reject a lower-priority certified phase");
        }
    }
    std::cout<<"PASS full phase pools preserve better caller scores and unscored original candidates\n";
}
static void TranslationWindowsBoundRawDomainsBeforeTheirCaps()
{
    Problem p;p.Tolerance=1e-7;p.Stocks={{"board",20,12,1,1,"m"}};
    auto fixed=polygon("window-fixed",{{0,0},{8,0},{8,12},{0,12}});
    auto moving=polygon("window-moving",{{0,0},{1,0},{1,1},{0,1}});moving.Quantity=2;
    p.Parts={fixed,moving};const auto original=p;
    Layout board{"board",0,20,12,{{fixed.ID,0,{},0,false}},96};const auto originalBoard=board;
    const std::vector<Detail::Member> members{{1,{}},{1,{2,0,0,false}}};
    Detail::NfpPlacementGenerator single(p),freshSingle(p);
    Detail::ClearanceNfpPlacement rigid(p,p.Stocks[0],{},{}),freshRigid(p,p.Stocks[0],{},{});
    require(single.Ready()&&rigid.Ready(),"translation-window fixture did not prepare its unchanged originals");
    const auto same=[](const Detail::NfpPlacementQuery& a,const Detail::NfpPlacementQuery& b)
    {
        require(a.Supported==b.Supported&&a.Truncated==b.Truncated&&a.Attempts==b.Attempts&&
            a.Generated==b.Generated&&a.DomainMeasured==b.DomainMeasured&&
            a.FreeTranslationArea==b.FreeTranslationArea&&a.FitsIDs==b.FitsIDs&&
            a.Fits.size()==b.Fits.size()&&a.Blocked.size()==b.Blocked.size(),
            "an unset or enclosing translation window changed the original query accounting");
        const auto pose=[](Pose a,Pose b)
        {
            return std::bit_cast<std::uint64_t>(a.X)==std::bit_cast<std::uint64_t>(b.X)&&
                std::bit_cast<std::uint64_t>(a.Y)==std::bit_cast<std::uint64_t>(b.Y)&&
                std::bit_cast<std::uint64_t>(a.AngleDegrees)==std::bit_cast<std::uint64_t>(b.AngleDegrees)&&
                a.Mirrored==b.Mirrored;
        };
        for(std::size_t i=0;i<a.Fits.size();++i)
            require(pose(a.Fits[i],b.Fits[i]),"translation-window preparation changed a later original stored pose");
        for(std::size_t i=0;i<a.Blocked.size();++i)
            require(pose(a.Blocked[i].Transform,b.Blocked[i].Transform)&&a.Blocked[i].Blockers==b.Blocked[i].Blockers&&
                a.Blocked[i].OverlapArea==b.Blocked[i].OverlapArea,"translation-window preparation changed original blockers");
    };
    const auto verifyRigid=[&](const Detail::NfpPlacementQuery& query)
    {
        for(const auto delta:query.Fits)
        {
            auto actual=board;
            for(std::size_t i=0;i<members.size();++i)
            {
                auto pose=members[i].Transform;pose.X+=delta.X;pose.Y+=delta.Y;
                requireNominalFit(p,actual,1,pose);
                actual.Placements.push_back({moving.ID,i,pose,0,false});actual.MaterialArea+=1;
            }
            Result result;result.State=Status::Complete;result.Layouts={actual};
            const auto checked=Validate(p,result);
            require(checked.Valid&&checked.Complete,"a rigid translation window bypassed physical stock, inventory or material checks");
        }
    };
    Detail::NfpPlacementLimits unrestricted;unrestricted.MaxCandidates=0;unrestricted.MaxRawCandidates=64;
    unrestricted.IncludeContactChannels=false;
    const auto beforeSingle=single.Find(board,1,0,unrestricted);
    const auto beforeRigid=rigid.FindRigid(board,members,unrestricted,{});
    require(!beforeSingle.Fits.empty()&&!beforeRigid.Fits.empty(),"translation-window oracle has no unrestricted original-safe fit");
    auto enclosing=unrestricted;enclosing.TranslationWindow=Rect{-1,-1,30,20};
    same(single.Find(board,1,0,enclosing),beforeSingle);
    same(rigid.FindRigid(board,members,enclosing,{}),beforeRigid);
    auto scarce=unrestricted;scarce.MaxRawCandidates=1;
    // The original first board corner is blocked. A post-cap acceptance filter
    // cannot recover either this interior pose or its rigid delta from one raw
    // slot; intersecting first makes the window boundary itself an event.
    require(single.Find(board,1,0,scarce).Fits.empty()&&rigid.FindRigid(board,members,scarce,{}).Fits.empty(),
        "translation-window fixture did not exercise a blocked first raw slot");
    scarce.TranslationWindow=Rect{10,4,2,2};
    const auto middleSingle=single.Find(board,1,0,scarce),middleRigid=rigid.FindRigid(board,members,scarce,{});
    require(middleSingle.Supported&&middleSingle.Generated==1&&middleSingle.Fits.size()==1&&
        middleSingle.Fits[0].X==10&&middleSingle.Fits[0].Y==4&&middleSingle.DomainMeasured&&middleSingle.FreeTranslationArea==4,
        "an interior absolute translation window was applied after the single raw cap or lost its boundary");
    require(middleRigid.Supported&&middleRigid.Generated==1&&middleRigid.Fits.size()==1&&
        middleRigid.Fits[0].X==10&&middleRigid.Fits[0].Y==4&&middleRigid.DomainMeasured&&middleRigid.FreeTranslationArea==4,
        "an interior delta translation window was applied after the rigid raw cap or used absolute member poses");
    for(const auto pose:middleSingle.Fits)requireNominalFit(p,board,1,pose);
    verifyRigid(middleRigid);
    for(const Rect window:{Rect{10,4,0,2},Rect{10,4,2,0},Rect{10,4,0,0}})
    {
        auto limits=scarce;limits.TranslationWindow=window;
        const auto a=single.Find(board,1,0,limits),b=rigid.FindRigid(board,members,limits,{});
        require(a.Supported&&b.Supported&&a.Fits.size()==1&&b.Fits.size()==1&&
            a.Fits[0].X==10&&a.Fits[0].Y==4&&b.Fits[0].X==10&&b.Fits[0].Y==4&&
            a.DomainMeasured&&b.DomainMeasured&&a.FreeTranslationArea==0&&b.FreeTranslationArea==0,
            "a valid line/point translation window was rejected or its only boundary was discarded");
        requireNominalFit(p,board,1,a.Fits[0]);verifyRigid(b);
    }
    std::string singleReason,rigidReason;
    auto observed=scarce;
    observed.Trace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Stage!=ConstructionTraceStage::Complete)return;
        if(event.Source=="nfp.query")singleReason=event.Reason;
        if(event.Source=="nfp.rigid-placement")rigidReason=event.Reason;
    };
    observed.TranslationWindow=Rect{30,4,0,0};
    const auto emptySingle=single.Find(board,1,0,observed),emptyRigid=rigid.FindRigid(board,members,observed,{});
    require(emptySingle.Supported&&emptyRigid.Supported&&emptySingle.Fits.empty()&&emptyRigid.Fits.empty()&&
        emptySingle.Attempts==0&&emptyRigid.Attempts==0&&emptySingle.Generated==0&&emptyRigid.Generated==0&&
        emptySingle.DomainMeasured&&emptyRigid.DomainMeasured&&
        singleReason=="empty-translation-window-intersection"&&rigidReason=="empty-rigid-translation-window-intersection",
        "an empty translation-window intersection was hidden or confused with unsupported geometry");
    // Empty means an actual empty intersection, including separations smaller
    // than the physical gate's tolerance; no corner may be invented outside it.
    observed.TranslationWindow=Rect{19+5e-8,4,0,0};
    const auto disjoint=single.Find(board,1,0,observed);
    require(disjoint.Supported&&disjoint.Fits.empty()&&disjoint.Generated==0&&
        singleReason=="empty-translation-window-intersection","translation-window clipping widened a disjoint point by tolerance");
    const auto inf=std::numeric_limits<double>::infinity(),nan=std::numeric_limits<double>::quiet_NaN();
    const auto largest=std::numeric_limits<double>::max();
    for(const Rect invalid:{Rect{nan,4,1,1},Rect{10,inf,1,1},Rect{10,4,inf,1},Rect{10,4,1,nan},
        Rect{10,4,-1,1},Rect{10,4,1,-1},Rect{largest,4,largest,1},Rect{10,largest,1,largest}})
    {
        observed.TranslationWindow=invalid;
        const auto a=single.Find(board,1,0,observed),b=rigid.FindRigid(board,members,observed,{});
        require(!a.Supported&&!b.Supported&&a.Truncated&&b.Truncated&&a.Fits.empty()&&b.Fits.empty()&&
            a.Attempts==0&&b.Attempts==0&&a.Generated==0&&b.Generated==0&&
            singleReason=="invalid-translation-window"&&rigidReason=="invalid-rigid-translation-window",
            "an invalid or overflowing translation window silently widened or entered either solver stream");
    }
    same(single.Find(board,1,0,unrestricted),beforeSingle);
    same(rigid.FindRigid(board,members,unrestricted,{}),beforeRigid);
    same(freshSingle.Find(board,1,0,unrestricted),beforeSingle);
    same(freshRigid.FindRigid(board,members,unrestricted,{}),beforeRigid);
    require(board.Width==originalBoard.Width&&board.Height==originalBoard.Height&&board.StockID==originalBoard.StockID&&
        board.StockInstance==originalBoard.StockInstance&&board.Placements.size()==1&&
        board.Placements[0].PartID==fixed.ID&&board.Placements[0].Transform.X==0&&board.Placements[0].Transform.Y==0&&
        p.Stocks[0].Width==original.Stocks[0].Width&&p.Stocks[0].Height==original.Stocks[0].Height&&
        p.Stocks[0].Quantity==original.Stocks[0].Quantity,"translation-window clipping rewrote the physical stock or fixed layout");
    for(std::size_t i=0;i<p.Parts.size();++i)
    {
        require(p.Parts[i].ID==original.Parts[i].ID&&p.Parts[i].Quantity==original.Parts[i].Quantity&&
            p.Parts[i].AllowedAnglesDegrees==original.Parts[i].AllowedAnglesDegrees&&
            p.Parts[i].Geometry.Outer.size()==original.Parts[i].Geometry.Outer.size(),
            "translation-window clipping rewrote an original identity, direction or demand");
        for(std::size_t j=0;j<p.Parts[i].Geometry.Outer.size();++j)
            require(p.Parts[i].Geometry.Outer[j].X==original.Parts[i].Geometry.Outer[j].X&&
                p.Parts[i].Geometry.Outer[j].Y==original.Parts[i].Geometry.Outer[j].Y,
                "translation-window clipping changed an original material contour");
    }
    std::cout<<"PASS absolute/delta translation windows precede raw caps, retain line/point boundaries and expose empty/invalid domains\n";
}
static void RigidOriginalMembersUseTheirJointTranslationDomain()
{
    const auto verify=[](const Problem& p,const Layout& initial,const std::vector<Detail::Member>& members,
        const Detail::NfpPlacementQuery& query)
    {
        for(const auto delta:query.Fits)
        {
            require(delta.AngleDegrees==0&&!delta.Mirrored,"rigid placement returned a new group orientation");
            auto board=initial;std::map<std::string,std::size_t> instances;
            for(const auto& item:board.Placements)instances[item.PartID]=std::max(instances[item.PartID],item.Instance+1);
            for(const auto& member:members)
            {
                const auto& part=p.Parts[member.PartIndex];auto pose=member.Transform;pose.X+=delta.X;pose.Y+=delta.Y;
                require(Detail::PoseAllowed(part,pose,p.Tolerance),"rigid delta changed an original orientation permission");
                board.Placements.push_back({part.ID,instances[part.ID]++,pose,0,false});
            }
            Result result;result.State=Status::Complete;result.Layouts={board};const auto checked=Validate(p,result);
            require(checked.Valid&&checked.Complete,"rigid output failed independent full original-material validation");
        }
    };
    Problem p;p.Tolerance=1e-7;p.Stocks={{"board",4,4,1,1,"m"}};
    auto center=polygon("fixed-center",{{0,0},{2,0},{2,2},{0,2}});
    auto bar=polygon("rigid-bar",{{0,0},{4,0},{4,1},{0,1}});bar.Quantity=2;
    p.Parts={center,bar};const auto original=p;
    Layout board{"board",0,4,4,{{center.ID,0,{1,1,0},0,false}},4};
    const std::vector<Detail::Member> frame{{1,{0,0,0}},{1,{0,3,0}}};
    Detail::ClearanceNfpPlacement generator(p,p.Stocks[0],{},{});
    require(generator.Ready()&&generator.InitializationComplete()&&!generator.WasInterrupted(),
        "complete rigid preparation was confused with an interrupted scene");
    Detail::NfpPlacementLimits limits;limits.MaxCandidates=0;limits.MaxAttempts=1000;
    const auto fitted=generator.FindRigid(board,frame,limits,{});
    require(fitted.Supported&&!fitted.Fits.empty()&&fitted.Fits[0].X==0&&fitted.Fits[0].Y==0,
        "rigid member AABBs replaced the actual central material void or lost a collapsed sheet intersection");
    verify(p,board,frame,fitted);
    auto narrow=board;narrow.Width=3.9;
    const auto impossible=generator.FindRigid(narrow,frame,limits,{});
    require(impossible.Supported&&impossible.Fits.empty()&&impossible.Attempts==0,
        "rigid containment used only the first member's sheet domain");
    const std::vector<Detail::Member> overlapping{{1,{0,0,0}},{1,{0,0,0}}};
    require(generator.FindRigid(board,overlapping,limits,{}).Fits.empty(),
        "rigid world certification omitted member/member material collisions");

    // Neither member alone supplies this compound free-domain corner. The
    // vertical obstacle constrains the lower member; the top obstacle constrains
    // the upper member. Their shifted NFP boundaries meet at delta (.5,.5).
    Problem joint;joint.Tolerance=1e-7;joint.Stocks=p.Stocks;
    auto vertical=polygon("vertical",{{0,0},{2.5,0},{2.5,1.4},{0,1.4}});
    auto top=polygon("top",{{0,0},{4,0},{4,.5},{0,.5}});
    auto square=polygon("member",{{0,0},{1,0},{1,1},{0,1}});square.Quantity=2;
    joint.Parts={vertical,top,square};
    Layout obstacles{"board",0,4,4,{{vertical.ID,0,{1.5,0,0},0,false},{top.ID,0,{0,3.5,0},0,false}},5.5};
    const std::vector<Detail::Member> diagonal{{2,{0,0,0}},{2,{2,2,0}}};
    Detail::ClearanceNfpPlacement jointGenerator(joint,joint.Stocks[0],{},{});
    auto cornerLimits=limits;cornerLimits.StopAfterFirstFit=true;
    cornerLimits.CandidatePriority=[](Pose delta){return std::array<double,4>{-delta.X-delta.Y,-delta.X,-delta.Y,0};};
    const auto jointFit=jointGenerator.FindRigid(obstacles,diagonal,cornerLimits,{});
    require(jointFit.Supported&&jointFit.Fits.size()==1&&jointFit.Fits[0].X==.5&&jointFit.Fits[0].Y==.5,
        "rigid placement failed to intersect the different members' actual forbidden-translation boundaries");
    verify(joint,obstacles,diagonal,jointFit);

    auto common=p;common.PartGap=.2;common.AllowCommonEdges=true;common.Parts[1].AllowMirror=true;
    const std::vector<Detail::Member> reflectedFrame{{1,{0,0,0}},{1,{0,4,0,true}}};
    Detail::ClearanceNfpPlacement commonGenerator(common,common.Stocks[0],{},{});
    const auto seams=commonGenerator.FindRigid(board,reflectedFrame,limits,{});
    require(seams.Supported&&!seams.Fits.empty(),"rigid positive-gap original query lost its mirrored exact common edges");
    verify(common,board,reflectedFrame,seams);
    auto denied=common;denied.Parts[1].AllowMirror=false;
    Detail::ClearanceNfpPlacement forbidden(denied,denied.Stocks[0],{},{});
    require(!forbidden.FindRigid(board,reflectedFrame,limits,{}).Supported&&
        forbidden.FindRigid(board,reflectedFrame,limits,{}).Fits.empty(),
        "rigid placement widened the source reflection permission");
    auto noSeam=common;noSeam.Parts[1].Geometry.CommonEdgeRings={false};
    Detail::ClearanceNfpPlacement noSeamGenerator(noSeam,noSeam.Stocks[0],{},{});
    require(noSeamGenerator.FindRigid(board,reflectedFrame,limits,{}).Fits.empty(),
        "rigid common-edge domain waived a forbidden original ring's positive gap");
    auto bounded=limits;bounded.MaxAttempts=1;
    require(commonGenerator.FindRigid(board,reflectedFrame,bounded,{}).Attempts<=1,
        "rigid candidate certification exceeded the caller's shared attempt allowance");
    bounded=limits;bounded.MaxRigidPairQueries=0;
    const auto noRegions=commonGenerator.FindRigid(board,reflectedFrame,bounded,{});
    require(noRegions.Truncated&&!noRegions.Supported&&noRegions.Fits.empty()&&noRegions.Attempts==0,
        "a rigid region-work limit masqueraded as a complete no-fit query");
    const auto stopped=commonGenerator.FindRigid(board,reflectedFrame,limits,[]{return true;});
    require(stopped.Truncated&&stopped.Fits.empty()&&stopped.Attempts==0,"rigid geometry work ignored cancellation");
    Detail::ClearanceNfpPlacement interrupted(common,common.Stocks[0],{},[]{return true;});
    require(!interrupted.InitializationComplete()&&interrupted.WasInterrupted(),
        "partial clearance preparation was marked complete");
    require(p.Parts[1].Quantity==original.Parts[1].Quantity&&!p.Parts[1].AllowMirror&&
        std::equal(p.Parts[1].Geometry.Outer.begin(),p.Parts[1].Geometry.Outer.end(),original.Parts[1].Geometry.Outer.begin(),
            [](Point a,Point b){return a.X==b.X&&a.Y==b.Y;}),"rigid preparation altered original material, parity permission or quantity");

    auto spacious=common;spacious.Stocks[0].Width=spacious.Stocks[0].Height=8;
    Layout remote{"board",0,8,8,{{center.ID,0,{6,6,0},0,false}},4};
    Detail::ClearanceNfpPlacement spaciousGenerator(spacious,spacious.Stocks[0],{},{});
    const auto oneMissingStream=Detail::FindRigidNfpPlacements(spacious,remote,frame,limits,
        [&](std::size_t fixed,Pose fp,std::size_t moving,Pose mp,bool nominal)
        {return nominal?NfpLookup{}:spaciousGenerator.ContactRegion(fixed,fp.AngleDegrees,moving,mp.AngleDegrees,{},false,fp.Mirrored,mp.Mirrored);});
    require(oneMissingStream.Supported&&oneMissingStream.Truncated&&!oneMissingStream.Fits.empty(),
        "an unavailable optional rigid stream discarded the complete ordinary domain's certified fits");
    verify(spacious,remote,frame,oneMissingStream);
    auto pairBound=limits;pairBound.MaxRigidPairQueries=2;
    const auto partialWork=spaciousGenerator.FindRigid(remote,frame,pairBound,{});
    require(partialWork.Supported&&partialWork.Truncated&&!partialWork.Fits.empty(),
        "the rigid pair-work bound discarded an already complete ordinary stream");
    verify(spacious,remote,frame,partialWork);
    auto used=remote;used.Placements.push_back({bar.ID,0,{4,0,0},0,false});
    const auto overDemand=spaciousGenerator.FindRigid(used,frame,limits,{});
    require(!overDemand.Supported&&overDemand.Fits.empty()&&overDemand.Attempts==0,
        "rigid member demand omitted originals already placed on the board");

    Problem tight;tight.Tolerance=1e-7;
    auto angled=polygon("tight-angle",{{0,0},{3,0},{3,2},{0,2}});angled.Quantity=2;angled.AllowedAnglesDegrees={23.7};
    const auto extent=Detail::Bounds(angled.Geometry,{0,0,23.7});
    const std::vector<Detail::Member> angledMembers{{0,{-extent.X,-extent.Y,23.7}},
        {0,{-extent.X+2*extent.Width+1,-extent.Y,23.7}}};
    tight.Parts={angled};tight.Stocks={{"board",3*extent.Width+1,extent.Height,1,1,"m"}};
    Layout empty{"board",0,tight.Stocks[0].Width,tight.Stocks[0].Height,{},0};
    const auto tightQuery=Detail::FindRigidNfpPlacements(tight,empty,angledMembers,limits,
        [](std::size_t,Pose,std::size_t,Pose,bool){return NfpLookup{};});
    require(!tightQuery.Fits.empty()||tightQuery.Truncated,
        "uncertain arbitrary-angle rigid boundary failure was presented as complete no-fit");
    verify(tight,empty,angledMembers,tightQuery);
    std::cout<<"PASS rigid original-member joint domains, central voids, common edges, parity and shared work bounds\n";
}

static void RigidNecessaryFitOnlyRejectsFinalDeltasBeforeMaterialWork()
{
    Problem p;p.Tolerance=1e-7;p.Stocks={{"board",10,8,1,1,"m"}};
    auto member=polygon("member",{{0,0},{2,0},{2,1},{0,1}});member.Quantity=2;member.AllowMirror=true;
    p.Parts={member};Layout empty{"board",0,10,8,{},0};
    const std::vector<Detail::Member> members{{0,{.25,.5,0}},{0,{5.25,1.5,0,true}}};
    Detail::ClearanceNfpPlacement generator(p,p.Stocks[0],{},{});
    Detail::NfpPlacementLimits limits;limits.MaxCandidates=0;limits.MaxAttempts=1000;
    limits.CandidatePriority=[](Pose delta){return std::array<double,4>{delta.Y,delta.X,0,0};};
    std::map<std::string,double> measures;
    limits.Trace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source=="nfp.rigid-placement"&&event.Stage==ConstructionTraceStage::Complete)
            for(const auto& [name,value]:event.Measures)measures[name]=value;
    };
    const auto same=[&](const Detail::NfpPlacementQuery& a,const Detail::NfpPlacementQuery& b)
    {
        require(a.Supported==b.Supported&&a.Truncated==b.Truncated&&a.Generated==b.Generated&&
            a.Attempts==b.Attempts&&a.DomainMeasured==b.DomainMeasured&&a.FreeTranslationArea==b.FreeTranslationArea&&
            a.Fits.size()==b.Fits.size()&&a.FitsIDs==b.FitsIDs&&a.Blocked.size()==b.Blocked.size(),
            "a passing/unproved rigid necessary screen changed query metadata, candidates or source identities");
        for(std::size_t i=0;i<a.Fits.size();++i)
            require(a.Fits[i].X==b.Fits[i].X&&a.Fits[i].Y==b.Fits[i].Y&&
                a.Fits[i].AngleDegrees==b.Fits[i].AngleDegrees&&a.Fits[i].Mirrored==b.Fits[i].Mirrored,
                "a passing/unproved rigid necessary screen changed an actual final delta");
    };
    const auto verify=[&](const Detail::NfpPlacementQuery& query)
    {
        for(const auto delta:query.Fits)
        {
            auto board=empty;
            for(std::size_t i=0;i<members.size();++i)
            {
                auto pose=members[i].Transform;pose.X+=delta.X;pose.Y+=delta.Y;
                board.Placements.push_back({member.ID,i,pose,0,false});
                board.MaterialArea+=Detail::Area(member.Geometry);
            }
            Result completed;completed.State=Status::Complete;completed.Layouts={board};const auto checked=Validate(p,completed);
            require(checked.Valid&&checked.Complete,
                "a rigid necessary screen bypassed independent complete original-material/mirror validation");
        }
    };
    const auto reference=generator.FindRigid(empty,members,limits,{});
    require(reference.Supported&&reference.Fits.size()>1&&measures["necessaryFitCalls"]==0&&
        measures["movingGeometryPreparations"]>0,"rigid necessary-screen fixture lost its unscreened complete candidate set");
    verify(reference);
    std::size_t screens=0,accepted=0;
    limits.AcceptFit=[&](Pose,std::uint64_t){++accepted;return true;};
    for(const bool unproved:{false,true})
    {
        measures.clear();screens=accepted=0;
        limits.NecessaryFit=[&](Pose delta,std::uint64_t id)->bool
        {
            ++screens;require(id>0&&delta.AngleDegrees==0&&!delta.Mirrored,
                "rigid necessary screen did not receive the final pure delta and its candidate identity");
            if(unproved)throw AxisSafety::ArithmeticUncertainty{};
            return true;
        };
        const auto actual=generator.FindRigid(empty,members,limits,{});same(actual,reference);verify(actual);
        require(screens==reference.Attempts&&accepted==reference.Fits.size()&&
            measures["movingGeometryPreparations"]>0&&measures["necessaryFitRejected"]==0&&
            measures["necessaryFitUnknown"]==(unproved?double(screens):0.),
            "a passing/unproved rigid necessary screen skipped the full material chain or was not called for each final delta");
    }
    measures.clear();screens=accepted=0;
    limits.NecessaryFit=[&](Pose,std::uint64_t){++screens;return false;};
    const auto denied=generator.FindRigid(empty,members,limits,{});
    require(denied.Supported&&denied.Fits.empty()&&denied.Blocked.empty()&&denied.Truncated&&
        denied.Generated==reference.Generated&&denied.Attempts==reference.Attempts&&screens==reference.Attempts&&
        accepted==0&&measures["necessaryFitRejected"]==double(screens)&&measures["movingGeometryPreparations"]==0,
        "a rejected rigid necessary pose prepared moving geometry, approved a fit, fabricated blockers or removed raw events");
    const auto selected=[&](Pose delta){return delta.Y>reference.Fits.front().Y;};
    limits.NecessaryFit=[&](Pose delta,std::uint64_t){return selected(delta);};
    const auto subset=generator.FindRigid(empty,members,limits,{});
    std::vector<Pose> expected;for(const auto delta:reference.Fits)if(selected(delta))expected.push_back(delta);
    require(!expected.empty()&&expected.size()<reference.Fits.size()&&subset.Fits.size()==expected.size()&&
        subset.Generated==reference.Generated&&subset.Attempts==reference.Attempts,
        "a rigid necessary screen changed the proposal stream instead of only rejecting final deltas");
    for(std::size_t i=0;i<expected.size();++i)
        require(subset.Fits[i].X==expected[i].X&&subset.Fits[i].Y==expected[i].Y,
            "rigid necessary rejection changed the retained safe-delta order");
    verify(subset);

    std::size_t early=0,material=0,orderViolations=0;std::uint64_t lastEarly=0;
    for(const bool unproved:{false,true})
    {
        measures.clear();early=material=orderViolations=accepted=0;lastEarly=0;
        limits.CompetitivePose=[&](Pose,std::uint64_t id)->bool
        {++early;lastEarly=id;if(unproved)throw AxisSafety::ArithmeticUncertainty{};return true;};
        limits.NecessaryFit=[&](Pose,std::uint64_t id)
        {++material;if(id!=lastEarly)++orderViolations;return true;};
        const auto actual=generator.FindRigid(empty,members,limits,{});same(actual,reference);verify(actual);
        require(early==reference.Attempts&&material==reference.Attempts&&orderViolations==0&&
            accepted==reference.Fits.size()&&measures["movingGeometryPreparations"]>0&&
            measures["competitivePoseUnknown"]==(unproved?double(early):0.),
            "a passing/unproved rigid competitive screen changed the complete chain or ran after NecessaryFit");
    }
    measures.clear();early=material=accepted=0;
    limits.CompetitivePose=[&](Pose,std::uint64_t){++early;return false;};
    const auto noncompetitive=generator.FindRigid(empty,members,limits,{});
    require(noncompetitive.Fits.empty()&&noncompetitive.Blocked.empty()&&
        noncompetitive.Generated==reference.Generated&&noncompetitive.Attempts==0&&early>0&&
        material==0&&accepted==0&&measures["movingGeometryPreparations"]==0&&
        measures["competitivePoseRejected"]==double(early),
        "a rejected rigid competitive delta consumed material attempts, ran later checks or changed raw proposals");
    limits.CompetitivePose={};

    auto blocked=p;blocked.Parts.push_back(polygon("cover",{{0,0},{10,0},{10,8},{0,8}}));
    Layout covered=empty;covered.Placements={{"cover",0,{},0,false}};covered.MaterialArea=80;
    Detail::ClearanceNfpPlacement coveredGenerator(blocked,blocked.Stocks[0],{},{});
    limits.NecessaryFit={};const auto blockedReference=coveredGenerator.FindRigid(covered,members,limits,{});
    require(blockedReference.Fits.empty(),"covered rigid fixture retained a material collision");
    for(const bool competitive:{false,true})for(const bool unproved:{false,true})
    {
        measures.clear();screens=accepted=0;
        const auto screen=[&](Pose,std::uint64_t)->bool
        {++screens;if(unproved)throw std::runtime_error("unproved rigid necessary screen");return true;};
        limits.CompetitivePose={};limits.NecessaryFit={};
        if(competitive)limits.CompetitivePose=screen;else limits.NecessaryFit=screen;
        const auto actual=coveredGenerator.FindRigid(covered,members,limits,{});same(actual,blockedReference);
        require(screens>0&&accepted==0&&actual.Fits.empty()&&actual.Blocked.empty()&&
            measures["movingGeometryPreparations"]>0&&
            measures[competitive?"competitivePoseUnknown":"necessaryFitUnknown"]==(unproved?double(screens):0.),
            "a passing/unproved rigid screen bypassed the fixed original-material safety gate");
    }
    std::cout<<"PASS rigid reject-only final-delta screens preserve raw events, mirrors, unknown fallbacks and full original certification\n";
}

static void RigidFixedGeometryReusePreservesExactQueries()
{
    Problem p;p.Tolerance=1e-7;p.EdgeMargin=.5;p.PartGap=.2;p.AllowCommonEdges=true;
    p.Stocks={{"board",8,8,2,1,"m"}};
    auto fixed=polygon("fixed",{{0,0},{2,0},{2,1},{1,1},{1,2},{0,2}});
    fixed.FreeRotation=true;fixed.AllowMirror=true;
    auto other=fixed;other.ID="other-fixed";
    auto moving=polygon("member",{{0,0},{1,0},{1,1},{0,1}});moving.Quantity=2;moving.AllowMirror=true;
    p.Parts={fixed,other,moving};
    Layout board{"board",0,8,8,{{fixed.ID,0,{1,1,0},0,false}},3};
    const std::vector<Detail::Member> pair{{2,{0,0,0}},{2,{2,0,0,true}}};
    Detail::ClearanceNfpPlacement reused(p,p.Stocks[0],{},{});
    Detail::NfpPlacementLimits limits;limits.MaxCandidates=0;limits.MaxAttempts=1000;
    const auto sameNumber=[](double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);};
    const auto samePose=[&](Pose a,Pose b)
    {return sameNumber(a.X,b.X)&&sameNumber(a.Y,b.Y)&&sameNumber(a.AngleDegrees,b.AngleDegrees)&&a.Mirrored==b.Mirrored;};
    const auto same=[&](const Detail::NfpPlacementQuery& actual,const Detail::NfpPlacementQuery& expected)
    {
        require(actual.Supported==expected.Supported&&actual.Truncated==expected.Truncated&&
            actual.Attempts==expected.Attempts&&actual.Generated==expected.Generated&&
            actual.DomainMeasured==expected.DomainMeasured&&sameNumber(actual.FreeTranslationArea,expected.FreeTranslationArea)&&
            actual.Fits.size()==expected.Fits.size()&&actual.FitsIDs==expected.FitsIDs&&
            actual.Blocked.size()==expected.Blocked.size(),"rigid fixed preparation changed queries, work bounds or IDs");
        for(std::size_t i=0;i<actual.Fits.size();++i)
            require(samePose(actual.Fits[i],expected.Fits[i]),"rigid fixed preparation changed a stored delta pose");
    };
    std::map<std::string,double> measured;
    limits.Trace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source=="nfp.rigid-placement"&&event.Stage==ConstructionTraceStage::Complete)
            for(const auto& [name,value]:event.Measures)measured[name]=value;
    };
    auto uncached=limits;
    uncached.RigidFixedGeometry=[](std::size_t,Pose){return std::shared_ptr<const AxisSafety::Geometry>{};};
    uncached.Trace=[](const ConstructionTraceEvent&){};
    const auto compare=[&](const Layout& current,std::size_t hits,std::size_t misses)
    {
        measured.clear();const auto actual=reused.FindRigid(current,pair,limits,{});
        const auto expected=reused.FindRigid(current,pair,uncached,{});same(actual,expected);
        require(measured.contains("fixedGeometryCacheHits")&&measured.at("fixedGeometryCacheHits")==double(hits)&&
            measured.at("fixedGeometryCacheMisses")==double(misses),"rigid fixed cache reused a different original or stored pose");
        require(measured.at("fixedGeometryCacheEntries")<=4096&&measured.at("fixedGeometryCacheVertices")<=65536,
            "rigid fixed geometry cache exceeded its bounded preparation budget");
        for(const auto delta:actual.Fits)
        {
            auto placed=current;std::size_t instance=0;
            for(const auto& member:pair)
            {
                auto pose=member.Transform;pose.X+=delta.X;pose.Y+=delta.Y;
                requireNominalFit(p,placed,member.PartIndex,pose);
                placed.Placements.push_back({p.Parts[member.PartIndex].ID,instance++,pose,0,false});
            }
        }
        return actual;
    };
    require(!compare(board,0,1).Fits.empty(),"rigid fixed cache fixture has no original-safe joint pose");
    compare(board,1,0);
    for(const Pose pose:std::vector<Pose>{{std::nextafter(1.,2.),1,0},{1,std::nextafter(1.,2.),0},
        {1,1,90},{1,1,360},{1,1,37},{1,1,37,true},{0,1,0},{-0.,1,0}})
    {
        auto changed=board;changed.Placements[0].Transform=pose;compare(changed,0,1);compare(changed,1,0);
    }
    auto changed=board;changed.Placements[0].PartID=other.ID;compare(changed,0,1);compare(changed,1,0);
    changed=board;changed.StockInstance=1;compare(changed,1,0);
    changed=board;changed.Placements.push_back({other.ID,0,{4,4,0},0,false});compare(changed,1,1);
    std::reverse(changed.Placements.begin(),changed.Placements.end());compare(changed,2,0);
    changed=board;changed.Placements.push_back({moving.ID,0,{4,4,0},0,false});
    require(!compare(changed,1,1).Supported,"rigid fixed reuse lost the already-placed member inventory check");

    std::size_t calls=0,index=std::numeric_limits<std::size_t>::max();Pose requested;
    auto provided=limits;
    provided.RigidFixedGeometry=[&](std::size_t original,Pose pose)
    {
        ++calls;index=original;requested=pose;
        return std::make_shared<const AxisSafety::Geometry>(AxisSafety::Make(p.Parts[original].Geometry,pose));
    };
    same(reused.FindRigid(board,pair,provided,{}),reused.FindRigid(board,pair,uncached,{}));
    require(calls==1&&index==0&&samePose(requested,board.Placements[0].Transform),
        "rigid fixed provider did not receive the original identity and complete stored pose");
    provided.RigidFixedGeometry=[](std::size_t,Pose)->std::shared_ptr<const AxisSafety::Geometry>
        {throw std::runtime_error("preparation unavailable");};
    same(reused.FindRigid(board,pair,provided,{}),reused.FindRigid(board,pair,uncached,{}));
    {
        const int mode=std::fegetround();struct Restore{int Mode;~Restore(){std::fesetround(Mode);}}restore{mode};
        require(std::fesetround(FE_UPWARD)==0,"could not exercise uncertain rigid cached arithmetic");
        require(compare(board,0,1).Fits.empty(),"rigid cache bypassed the unsupported arithmetic environment");
    }
    compare(board,1,0);

    // Analytic circles cost one retained preparation vertex. A zero pair-work
    // allowance exercises cache admission without running placement searches.
    auto bounded=p;bounded.Parts[0].Geometry={};bounded.Parts[0].Geometry.Radius=.25;
    Detail::ClearanceNfpPlacement capped(bounded,bounded.Stocks[0],{},{});
    auto preparationOnly=limits;preparationOnly.MaxRigidPairQueries=0;
    for(std::size_t i=0;i<=4096;++i)
    {
        auto current=board;current.Placements[0].Transform.X=1+double(i)/8192;
        measured.clear();const auto result=capped.FindRigid(current,pair,preparationOnly,{});
        require(result.Truncated&&result.Fits.empty()&&measured.at("fixedGeometryCacheMisses")==1&&
            measured.at("fixedGeometryCacheEntries")==double(std::min<std::size_t>(i+1,4096))&&
            measured.at("fixedGeometryCacheVertices")==measured.at("fixedGeometryCacheEntries"),
            "rigid fixed preparation admission lost its finite entry or vertex accounting");
    }
    auto overflow=board;overflow.Placements[0].Transform.X=1+4096./8192;
    measured.clear();capped.FindRigid(overflow,pair,preparationOnly,{});
    require(measured.at("fixedGeometryCacheMisses")==1&&measured.at("fixedGeometryCacheEntries")==4096,
        "an over-capacity fixed geometry was retained instead of recomputed");
    measured.clear();capped.FindRigid(board,pair,preparationOnly,{});
    require(measured.at("fixedGeometryCacheHits")==1,"full rigid cache lost its existing immutable entries");
    std::cout<<"PASS rigid fixed geometry reuses only complete original poses, preserves all queries and remains bounded\n";
}

static void RigidLoopAndMovingEdgeReusePreservesEveryBoundedQuery()
{
    using Loops=std::vector<std::vector<Point>>;
    using Raw=std::array<std::uint64_t,2>;
    struct Observed{Detail::NfpPlacementQuery Query;std::vector<Raw> RawDeltas;std::map<std::string,double> Measures;};
    const auto capture=[](Detail::ClearanceNfpPlacement& generator,const Layout& board,const std::vector<Detail::Member>& members,
        Detail::NfpPlacementLimits limits,const std::function<bool()>& stop=std::function<bool()>{})
    {
        Observed result;
        limits.Trace=[&](const ConstructionTraceEvent& event)
        {
            if(event.Source!="nfp.rigid-placement")return;
            if(event.Stage==ConstructionTraceStage::Generate&&event.Reason=="rigid-domain-event")
            {
                std::optional<double> x,y;
                for(const auto& [name,value]:event.Measures)
                {if(name=="translationX")x=value;if(name=="translationY")y=value;}
                if(x&&y)result.RawDeltas.push_back({std::bit_cast<std::uint64_t>(*x),std::bit_cast<std::uint64_t>(*y)});
            }
            if(event.Stage==ConstructionTraceStage::Complete)
                for(const auto& [name,value]:event.Measures)result.Measures[name]=value;
        };
        result.Query=generator.FindRigid(board,members,limits,stop);return result;
    };
    const auto same=[](const Observed& actual,const Observed& expected)
    {
        const auto& a=actual.Query;const auto& b=expected.Query;
        require(actual.RawDeltas==expected.RawDeltas&&a.Supported==b.Supported&&a.Truncated==b.Truncated&&
            a.Attempts==b.Attempts&&a.Generated==b.Generated&&a.DomainMeasured==b.DomainMeasured&&
            std::bit_cast<std::uint64_t>(a.FreeTranslationArea)==std::bit_cast<std::uint64_t>(b.FreeTranslationArea)&&
            a.FitsIDs==b.FitsIDs&&a.Fits.size()==b.Fits.size()&&a.Blocked.size()==b.Blocked.size(),
            "rigid conversion/edge reuse changed ordered raw deltas, complete query state or work counters");
        for(std::size_t i=0;i<a.Fits.size();++i)
            require(std::bit_cast<std::uint64_t>(a.Fits[i].X)==std::bit_cast<std::uint64_t>(b.Fits[i].X)&&
                std::bit_cast<std::uint64_t>(a.Fits[i].Y)==std::bit_cast<std::uint64_t>(b.Fits[i].Y)&&
                a.Fits[i].AngleDegrees==b.Fits[i].AngleDegrees&&a.Fits[i].Mirrored==b.Fits[i].Mirrored,
                "rigid immutable preparation changed a fully certified actual pose");
        for(const char* name:{"pairRegionQueries","regionVertices","contactIntersections","collapsedDomainEvents","movingGeometryPreparations"})
            require(actual.Measures.at(name)==expected.Measures.at(name),
                "rigid immutable preparation changed a logical geometry-work budget");
    };
    Problem p;p.Tolerance=1e-7;p.EdgeMargin=.5;p.PartGap=.2;p.AllowCommonEdges=true;
    p.Stocks={{"board",20,20,1,1,"m"}};
    auto fixed=polygon("fixed",{{0,0},{2,0},{2,1},{1,1},{1,2},{0,2}});fixed.FreeRotation=true;fixed.AllowMirror=true;
    auto other=polygon("other",{{0,0},{2,0},{2,2},{0,2}});other.FreeRotation=true;other.AllowMirror=true;
    auto moving=polygon("member",{{-.125,.25},{.875,.25},{.875,1.25},{-.125,1.25}});
    moving.Quantity=2;moving.FreeRotation=true;moving.AllowMirror=true;p.Parts={fixed,other,moving};const auto original=p;
    Layout board{"board",0,20,20,{{fixed.ID,0,{1,1,0},0,false},{other.ID,0,{12,12,23.7,true},0,false}},7};
    std::vector<Detail::Member> members{{2,{0,0,0}},{2,{2.25,0,0,true}}};
    auto cache=std::make_shared<NfpCache>();Detail::ClearanceNfpPlacement generator(p,p.Stocks[0],cache,{});
    Detail::NfpPlacementLimits limits;limits.MaxCandidates=0;limits.MaxAttempts=100000;
    limits.CandidatePriority=[](Pose delta){return std::array<double,4>{delta.Y,delta.X,0,0};};
    auto off=limits;off.ReuseRigidMovingEdges=false;
    off.RigidRegionLoops=[](const NfpLookup&){return std::shared_ptr<const Loops>{};};
    const auto compare=[&](const Layout& current,const std::vector<Detail::Member>& group,bool reverseWarm)
    {
        const auto reset=[&]
        {
            cache->Clear();
            if(reverseWarm)for(const auto& placed:current.Placements)for(const auto& member:group)
            {
                const auto part=std::find_if(p.Parts.begin(),p.Parts.end(),[&](const Part& source){return source.ID==placed.PartID;});
                require(part!=p.Parts.end(),"rigid reuse reverse fixture has an unknown original");
                generator.ContactRegion(member.PartIndex,member.Transform.AngleDegrees,std::size_t(part-p.Parts.begin()),
                    placed.Transform.AngleDegrees,{},true,member.Transform.Mirrored,placed.Transform.Mirrored);
            }
        };
        reset();const auto expected=capture(generator,current,group,off);
        reset();const auto cold=capture(generator,current,group,limits);same(cold,expected);
        const auto hot=capture(generator,current,group,limits);same(hot,expected);
        require(hot.Measures.at("regionLoopCacheHits")>0&&hot.Measures.at("regionLoopCacheBytes")>0&&
            hot.Measures.at("regionLoopCacheBytes")<=32*1024*1024&&hot.Measures.at("regionLoopCacheEntries")<=256,
            "rigid loop cache failed to reuse exact owners/frames or exceeded its retained payload budget");
        require(hot.Measures.at("movingEdgesPreparations")<=expected.Measures.at("movingEdgesPreparations")&&
            hot.Measures.at("movingEdgesPreparations")<=double(group.size()),
            "moving source edges were not lazily prepared at most once per rigid member");
        require(hot.Measures.at("phaseRigidLookupCalls")==hot.Measures.at("pairRegionQueries")&&
            hot.Measures.at("phaseRigidLoopConversionCalls")>0&&hot.Measures.at("phaseRigidBooleanCalls")>0&&
            hot.Measures.at("phaseRigidContactGenerationCalls")>0&&hot.Measures.at("phaseRigidCertificationCalls")>0,
            "rigid query phases omitted lookup, exact loop conversion, Boolean, contacts or original certification");
        for(const auto delta:hot.Query.Fits)
        {
            auto placed=current;
            for(const auto& member:group)
            {
                auto pose=member.Transform;pose.X+=delta.X;pose.Y+=delta.Y;
                requireNominalFit(p,placed,member.PartIndex,pose);
                placed.Placements.push_back({p.Parts[member.PartIndex].ID,placed.Placements.size(),pose,0,false});
            }
        }
        if(!hot.Query.Fits.empty())
        {
            const auto best=[&](const Observed& query)
            {auto value=limits.CandidatePriority(query.Query.Fits.front());for(const auto pose:query.Query.Fits)value=std::min(value,limits.CandidatePriority(pose));return value;};
            require(best(hot)==best(expected),"rigid immutable preparation changed the best complete stored-pose rank");
        }
        return hot;
    };
    compare(board,members,false);compare(board,members,true);
    auto shifted=board;shifted.Placements[0].Transform.X=std::nextafter(1.,2.);
    shifted.Placements[0].Transform.Y=std::nextafter(1.,0.);
    auto precise=members;precise[0].Transform.X=std::nextafter(.5e-8,0.);precise[1].Transform.X+=std::nextafter(.5e-8,1.);
    compare(shifted,precise,true);
    auto angled=members;angled[0].Transform={0,0,23.7,true};angled[1].Transform={4.25,0,-13.75,false};
    compare(board,angled,true);
    const auto reference=capture(generator,board,members,off);
    for(const int mode:{0,1})
    {
        auto failed=limits;
        failed.RigidRegionLoops=[&](const NfpLookup&)->std::shared_ptr<const Loops>
        {if(mode)throw std::runtime_error("loop provider unavailable");return std::make_shared<const Loops>();};
        same(capture(generator,board,members,failed),reference);
    }
    auto capped=limits;capped.MaxRigidRegionVertices=0;std::size_t providerCalls=0;
    capped.RigidRegionLoops=[&](const NfpLookup&){++providerCalls;return std::shared_ptr<const Loops>{};};
    auto cappedOff=off;cappedOff.MaxRigidRegionVertices=0;
    same(capture(generator,board,members,capped),capture(generator,board,members,cappedOff));
    require(providerCalls==0,"complete-loop preparation ran beyond the unchanged rigid vertex-work cap");
    same(capture(generator,board,members,limits,[]{return true;}),capture(generator,board,members,off,[]{return true;}));
    {
        const int prior=std::fegetround();struct Restore{int Mode;~Restore(){std::fesetround(Mode);}}restore{prior};
        require(std::fesetround(FE_UPWARD)==0,"could not exercise unsupported rigid loop arithmetic");
        const auto actual=capture(generator,board,members,limits);same(actual,capture(generator,board,members,off));
        require(actual.Measures.at("regionLoopCacheHits")==0&&actual.Measures.at("regionLoopCacheMisses")==0,
            "unsupported arithmetic consumed a formerly cached rigid loop representation");
    }
    Problem collapsed;collapsed.Tolerance=1e-7;collapsed.EdgeMargin=.5;collapsed.Stocks={{"board",9,12,1,1,"m"}};
    auto host=polygon("host",{{0,0},{8,0},{8,2},{0,2}}),bar=polygon("bar",{{0,0},{8,0},{8,1},{0,1}});bar.Quantity=2;
    collapsed.Parts={host,bar};Layout line{"board",0,9,12,{{host.ID,0,{.5,.5,0},0,false}},16};
    const std::vector<Detail::Member> pair{{1,{0,0,0}},{1,{0,2,0}}};
    Detail::ClearanceNfpPlacement lineGenerator(collapsed,collapsed.Stocks[0],{},{});
    auto lineLimits=limits;lineLimits.IncludeContactChannels=false;lineLimits.MaxRigidContactIntersections=0;
    auto lineOff=lineLimits;lineOff.ReuseRigidMovingEdges=false;lineOff.RigidRegionLoops=off.RigidRegionLoops;
    const auto lineResult=capture(lineGenerator,line,pair,lineLimits);same(lineResult,capture(lineGenerator,line,pair,lineOff));
    require(!lineResult.Query.Fits.empty()&&lineResult.Measures.at("collapsedDomainEvents")>0,
        "rigid loop reuse lost the original collapsed translation channel");

    // Small polygons keep this an inexpensive native check while forcing both
    // native NFP eviction and the independent, retained Region-owner cache cap.
    auto bounded=p;bounded.AllowCommonEdges=false;bounded.PartGap=0;
    auto smallCache=std::make_shared<NfpCache>(NfpCacheSettings{16,16,4096});
    Detail::ClearanceNfpPlacement evicting(bounded,bounded.Stocks[0],smallCache,{});
    Layout center=board;center.Placements.resize(1);center.Placements[0].Transform={10,10,0};
    auto probe=limits;probe.MaxCandidates=1;probe.MaxAttempts=1;probe.MaxRawCandidates=1;probe.IncludeContactChannels=false;
    Observed last;
    for(std::size_t i=0;i<260;++i)
    {
        center.Placements[0].Transform.AngleDegrees=double(i)*360./512.;
        last=capture(evicting,center,members,probe);
        require(last.Measures.at("regionLoopCacheBytes")<=32*1024*1024&&last.Measures.at("regionLoopCacheEntries")<=256,
            "rigid loop cache retained unbounded Region/loop payload after native eviction");
        if(i%37==0)
        {auto oracle=probe;oracle.ReuseRigidMovingEdges=false;oracle.RigidRegionLoops=off.RigidRegionLoops;same(last,capture(evicting,center,members,oracle));}
    }
    require(smallCache->Statistics().Evictions>0&&last.Measures.at("regionLoopCacheEvictions")>0,
        "rigid loop ownership/eviction fixture failed to exercise both bounded caches");
    for(std::size_t i=0;i<p.Parts.size();++i)
        require(p.Parts[i].Quantity==original.Parts[i].Quantity&&p.Parts[i].Geometry.Outer.size()==original.Parts[i].Geometry.Outer.size()&&
            std::equal(p.Parts[i].Geometry.Outer.begin(),p.Parts[i].Geometry.Outer.end(),original.Parts[i].Geometry.Outer.begin(),
                [](Point a,Point b){return a.X==b.X&&a.Y==b.Y;}),"rigid preparation altered immutable originals or demand");
    std::cout<<"PASS rigid exact loop/edge reuse preserves cold/reverse/evicted raw events, every certified pose, ranks, caps, arithmetic and collapsed channels\n";
}

static void CollapsedRigidDomainsRetainTheirLowIntervalWithoutContactChannels()
{
    for(const bool horizontal:{false,true})
    {
        Problem p;p.Tolerance=1e-7;p.EdgeMargin=.5;
        p.Stocks={{"board",horizontal?100.:9.,horizontal?9.:100.,1,1,"m"}};
        const double width=horizontal?30.:8.,height=horizontal?8.:30.;
        auto carrier=polygon("carrier",{{0,0},{width,0},{width,height},{0,height}});
        auto unit=polygon("unit",{{0,0},{1,0},{1,1},{0,1}});unit.Quantity=120;
        p.Parts={carrier,unit};const auto original=p;
        const auto transpose=[&](double x,double y){return horizontal?Point{y,x}:Point{x,y};};
        Layout board{"board",0,p.Stocks[0].Width,p.Stocks[0].Height,
            {{carrier.ID,0,{.5,.5,0},0,false}},304};
        for(std::size_t row=0;row<8;++row)for(std::size_t column=0;column<8;++column)
        {
            const auto at=transpose(.5+double(column),30.5+double(row));
            board.Placements.push_back({unit.ID,row*8+column,{at.X,at.Y,0},1,false});
        }
        Result initial;initial.State=Status::Partial;initial.Layouts={board};initial.Remaining={{unit.ID,56}};
        const auto initialChecked=Validate(p,initial);
        require(initialChecked.Valid&&!initialChecked.Complete,
            "collapsed rigid domain fixture did not preserve a legal 64-unit fixed array");
        std::vector<Detail::Member> members;
        for(std::size_t row=0;row<7;++row)for(std::size_t column=0;column<8;++column)
        {
            const auto at=transpose(double(column),double(row));members.push_back({1,{at.X,at.Y,0}});
        }
        Detail::NfpPlacementLimits limits;limits.MaxCandidates=1;limits.StopAfterFirstFit=true;
        limits.IncludeContactChannels=false;limits.MaxRigidContactIntersections=0;
        limits.MaxRigidPairQueries=10000;
        limits.CandidatePriority=[](Pose pose){return std::array<double,4>{pose.Y,pose.X,0,0};};
        std::map<std::string,double> measures;
        limits.Trace=[&](const ConstructionTraceEvent& event)
        {
            if(event.Source=="nfp.rigid-placement"&&event.Stage==ConstructionTraceStage::Complete)
                for(const auto& [name,value]:event.Measures)measures[name]=value;
        };
        Detail::ClearanceNfpPlacement generator(p,p.Stocks[0],{},{});
        const auto query=generator.FindRigid(board,members,limits,{});
        const auto expected=transpose(.5,38.5);
        // The pre-fix 2-D Difference erases a collapsed IFP. With contact
        // channels disabled its only events are .5 and 92.5; .5 collides with
        // the carrier, so it returns the remote 92.5 endpoint instead of 38.5.
        require(query.Supported&&query.Fits.size()==1&&query.Fits.front().X==expected.X&&query.Fits.front().Y==expected.Y,
            "collapsed rigid line lost its lowest free interval when contact channels were disabled");
        require(measures.at("contactIntersections")==0&&measures.at("collapsedDomainEvents")>0,
            "collapsed rigid line relied on optional contact intersection work");
        auto completed=board;
        for(std::size_t i=0;i<members.size();++i)
        {
            auto pose=members[i].Transform;pose.X+=query.Fits.front().X;pose.Y+=query.Fits.front().Y;
            completed.Placements.push_back({unit.ID,64+i,pose,2,false});
        }
        completed.MaterialArea+=double(members.size());
        Result result;result.State=Status::Complete;result.Layouts={completed};
        const auto checked=Validate(p,result);
        require(checked.Valid&&checked.Complete,"collapsed rigid line output failed complete original-material validation");
        for(std::size_t i=0;i<p.Parts.size();++i)
            require(p.Parts[i].Quantity==original.Parts[i].Quantity&&p.Parts[i].Geometry.Outer.size()==original.Parts[i].Geometry.Outer.size()&&
                std::equal(p.Parts[i].Geometry.Outer.begin(),p.Parts[i].Geometry.Outer.end(),original.Parts[i].Geometry.Outer.begin(),
                    [](Point a,Point b){return a.X==b.X&&a.Y==b.Y;}),"collapsed rigid line query changed original demand or contours");
    }
    std::cout<<"PASS collapsed full-width and full-height rigid domains retain their low intervals without contact channels\n";
}

static void OrdinaryAndCommonFitsCompeteBeforeTheGlobalSingleFitCap()
{
    Problem p;p.Tolerance=1e-7;p.EdgeMargin=.5;p.PartGap=.2;p.AllowCommonEdges=true;
    p.Stocks={{"board",5,1000000,1,1,"m"}};
    auto fixed=polygon("fixed",{{0,0},{4,0},{4,3},{0,3}}),moving=fixed;moving.ID="moving";
    fixed.Geometry.CommonEdgeRings={false};moving.Geometry.CommonEdgeRings={false};p.Parts={fixed,moving};
    Layout board{"board",0,5,1000000,{{fixed.ID,0,{.5,.5,0},0,false}},12};
    Detail::NfpPlacementLimits limits;limits.MaxCandidates=1;limits.MaxAttempts=20000;
    limits.StopAfterFirstFit=true;limits.CandidatePriority=[](Pose pose){return std::array<double,4>{pose.Y,pose.X,0,0};};
    std::vector<double> streams;
    limits.Trace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source!="nfp.query"||event.Stage!=ConstructionTraceStage::Begin)return;
        for(const auto& [name,value]:event.Measures)if(name=="nominal-common-domain")streams.push_back(value);
    };
    Detail::ClearanceNfpPlacement generator(p,p.Stocks[0],{},{});
    const auto ordinary=generator.Find(board,1,0,limits,{});
    require(ordinary.Fits.size()==1&&ordinary.Fits[0].Y>=3.7&&ordinary.Fits[0].Y<4&&
        !streams.empty()&&streams[0]==0&&ordinary.Attempts<=limits.MaxAttempts,
        "a nominal/common far-end fit occupied MaxCandidates=1 and suppressed the low ordinary-clearance placement");
    requireNominalFit(p,board,1,ordinary.Fits[0]);
    auto common=p;common.Parts[0].Geometry.CommonEdgeRings={true};common.Parts[1].Geometry.CommonEdgeRings={true};
    Detail::ClearanceNfpPlacement commonGenerator(common,common.Stocks[0],{},{});
    const auto seam=commonGenerator.Find(board,1,0,limits,{});
    require(seam.Fits.size()==1&&seam.Fits[0].Y==3.5&&seam.Attempts<=limits.MaxAttempts,
        "ordinary-first placement failed to compare a strictly better authorized common edge through the same caller score");
    requireNominalFit(common,board,1,seam.Fits[0]);
    streams.clear();auto ordinaryOnly=limits;ordinaryOnly.IncludeCommonEdgeProposals=false;
    const auto basic=commonGenerator.Find(board,1,0,ordinaryOnly,{});
    require(basic.Fits.size()==1&&basic.Fits[0].Y>=3.7&&basic.Fits[0].Y<4&&
        !streams.empty()&&std::all_of(streams.begin(),streams.end(),[](double stream){return stream==0;}),
        "deferring common proposals suppressed the ordinary orientation pass or changed source seam permission");
    requireNominalFit(common,board,1,basic.Fits[0]);
    require(common.AllowCommonEdges&&common.Parts[0].Geometry.CommonEdgeRings[0]&&common.Parts[1].Geometry.CommonEdgeRings[0],
        "the common proposal switch modified hard original permissions");
    auto expired=limits;expired.RemainingTimeSeconds=[]{return 0.;};
    const auto localStop=commonGenerator.Find(board,1,0,expired,{});
    require(localStop.Truncated&&localStop.Fits.empty()&&localStop.Attempts==0,
        "query-local time exhaustion was ignored or spent the job's original placement allowance");
    std::cout<<"PASS low ordinary and better common candidates compete with a true one-fit cap\n";
}

static void DynamicCertifiedRanksRetainTheBestOriginalSafeNonFirstFit()
{
    using Rank=std::tuple<double,double,double,double,double,double,std::size_t,std::size_t,std::size_t,std::size_t>;
    for(int fixture=0;fixture<4;++fixture)for(const bool mirrored:{false,true})
    {
        Problem p;p.Tolerance=1e-7;p.PartGap=fixture==2?0:.2;p.AllowCommonEdges=fixture==1||fixture==3;
        p.EdgeMargin=fixture==2||fixture==3?0:.5;p.Stocks={{"board",5,30,1,1,"m"}};
        auto fixed=polygon("fixed",{{0,0},{4,0},{4,3},{0,3}}),moving=fixed;moving.ID="moving";moving.AllowMirror=true;
        Layout board{"board",0,5,30,{{fixed.ID,0,{.5,.5,0},0,false}},12};
        if(fixture==2)
        {
            p.Stocks[0].Width=board.Width=1;p.Stocks[0].Height=board.Height=1;
            fixed.Geometry.Outer={{0,0},{.2,0},{.2,1},{0,1}};
            moving.Geometry.Outer={{0,0},{.1,0},{.1,.5},{0,.5}};
            board.Placements[0].Transform={.1,0,0};board.MaterialArea=.2;
        }
        if(fixture==3)
        {
            // The nominal outer-only common stream can propose a rounded
            // decimal contact, but the exact original edge is not coincident.
            // A real source hole also prevents private-outer equivalence from
            // substituting for the wrapper's full original-material check.
            fixed.Geometry.Holes={{{1,1},{2,1},{2,2},{1,2}}};
            fixed.Geometry.CommonEdgeRings={true,false};moving.Geometry.CommonEdgeRings={true};
            board.Placements[0].Transform={.5,.1,0};board.MaterialArea=11;
        }
        p.Parts={fixed,moving};
        const auto safe=[&](Pose pose)
        {
            if(pose.Mirrored!=mirrored||!Detail::PoseAllowed(moving,pose,p.Tolerance)||
                !Detail::InsideSheet(moving.Geometry,pose,board.Width,board.Height,p.EdgeMargin,p.Tolerance))return false;
            const auto geometry=AxisSafety::Make(moving.Geometry,pose);
            if(AxisSafety::Containment(geometry,board.Width,board.Height,p.EdgeMargin)!=AxisSafety::Verdict::Safe)return false;
            const auto originalFixed=AxisSafety::Make(fixed.Geometry,board.Placements[0].Transform);
            return !Detail::ShapesConflict(moving.Geometry,pose,fixed.Geometry,board.Placements[0].Transform,
                p.PartGap,p.Tolerance,p.AllowCommonEdges)&&AxisSafety::Pair(geometry,originalFixed,p.PartGap,
                p.AllowCommonEdges,p.Tolerance)==AxisSafety::Verdict::Safe;
        };
        const auto rank=[&](Pose pose)->Rank
        {
            const auto a=Detail::Bounds(fixed.Geometry,board.Placements[0].Transform),b=Detail::Bounds(moving.Geometry,pose);
            const double x=std::min(a.X,b.X),y=std::min(a.Y,b.Y);
            const double width=std::max(a.X+a.Width,b.X+b.Width)-x,height=std::max(a.Y+a.Height,b.Y+b.Height)-y;
            const double movingArea=Detail::Area(moving.Geometry),net=Detail::Area(fixed.Geometry)+movingArea;
            const double used=board.Width*std::clamp(y+height+p.EdgeMargin,0.,board.Height);
            // Full greedy final Rank, including its constant material/template
            // terms. Only the four varying terms enter CandidatePriority.
            return {-net/used,-movingArea,width*height,0.,b.Y,b.X,1,0,1,1};
        };
        const auto best=[&](const Detail::NfpPlacementQuery& query)
        {
            require(!query.Fits.empty(),"certified dynamic-rank fixture has no original-safe placement");
            auto value=rank(query.Fits.front());
            for(const auto pose:query.Fits)
            {require(safe(pose),"non-first-fit clearance returned an uncertified original pose");value=std::min(value,rank(pose));}
            return value;
        };
        if(fixture==3)
        {
            const Pose nominal{mirrored?4.5:.5,3.1,0,mirrored};
            require(!safe(nominal),"false-seam fixture accidentally has an authorized exact shared edge");
        }
        Detail::NfpPlacementLimits base;base.MaxCandidates=0;base.MaxAttempts=100000;base.Mirrored=mirrored;
        base.CandidatePriority=[&](Pose pose)
        {const auto key=rank(pose);return std::array<double,4>{std::get<0>(key),std::get<2>(key),std::get<4>(key),std::get<5>(key)};};
        Detail::ClearanceNfpPlacement unscreened(p,p.Stocks[0],{},{});
        const auto reference=unscreened.Find(board,1,0,base,{});const auto referenceBest=best(reference);
        for(const bool dynamic:{false,true})
        {
            Detail::ClearanceNfpPlacement generator(p,p.Stocks[0],{},{});auto limits=base;limits.MaxCandidates=1;
            std::optional<Rank> threshold;std::size_t callbacks=0,unsafeCallbacks=0,rejected=0,retreats=0,ordinary=0,common=0,falseSeams=0;
            std::vector<Rank> witnesses;
            limits.Trace=[&](const ConstructionTraceEvent& event)
            {
                if(event.Source=="nfp.query"&&event.Stage==ConstructionTraceStage::Begin)
                    for(const auto& [name,value]:event.Measures)if(name=="nominal-common-domain")value==0?++ordinary:++common;
                if(event.Source=="nfp.roundoff-retreat"&&event.Stage==ConstructionTraceStage::Generate)++retreats;
                if(event.Reason=="original-clearance-common-gap-witness")++falseSeams;
            };
            if(dynamic)limits.CompetitivePose=[&](Pose pose,std::uint64_t)
            {const bool possible=!threshold||rank(pose)<*threshold;if(!possible)++rejected;return possible;};
            limits.AcceptFit=[&](Pose pose,std::uint64_t)
            {
                ++callbacks;
                if(!safe(pose)){++unsafeCallbacks;return false;}
                const auto key=rank(pose);
                if(dynamic&&threshold&&!(key<*threshold))return false;
                if(dynamic){threshold=key;witnesses.push_back(key);}
                return true;
            };
            const auto actual=generator.Find(board,1,0,limits,{});const auto selected=best(actual);
            require(!limits.StopAfterFirstFit&&callbacks>0&&unsafeCallbacks==0&&selected<=referenceBest&&
                actual.Fits.size()==1&&actual.Attempts<=limits.MaxAttempts&&actual.Blocked.empty(),
                "non-first-fit certified ranking lost a better original-safe pose or exposed an uncertified score to AcceptFit");
            require(ordinary>0&&((fixture==1||fixture==3)?common>0:common==0),
                "dynamic-rank fixture did not exercise its ordinary/common native streams");
            if(dynamic)
            {
                require(threshold&&*threshold==selected&&!witnesses.empty()&&rejected>0,
                    "a certified dynamic threshold lost its actual safe witness through a later stream/fit cap");
                for(std::size_t i=1;i<witnesses.size();++i)
                    require(witnesses[i]<witnesses[i-1],"dynamic original-safe thresholds were not strictly improving complete final ranks");
            }
            if(fixture==2&&!mirrored)require(retreats>0,"dynamic-rank fixture did not retain real final-pose roundoff retreats");
            if(fixture==3)require(falseSeams>0,"non-first-fit gate did not reject the nominal false common-edge contact");
        }
    }
    std::cout<<"PASS non-first-fit AcceptFit certifies all originals before dynamic final ranks, retaining ordinary/common bests, mirrors, retreats and false-seam rejection\n";
}

static void CommonDirectionProofRequiresEveryAuthorizedSourceEdge()
{
    Problem p;p.Tolerance=1e-7;p.EdgeMargin=.5;p.PartGap=.2;p.AllowCommonEdges=true;
    p.Stocks={{"board",30,30,1,1,"m"}};
    auto fixed=polygon("fixed",{{0,0},{4,0},{1,3}}),moving=fixed;moving.ID="moving";
    moving.AllowedAnglesDegrees={23.7};p.Parts={fixed,moving};
    Layout board{"board",0,30,30,{{fixed.ID,0,{10,10,0},0,false}},6};
    Detail::NfpPlacementLimits limits;limits.MaxCandidates=1;limits.StopAfterFirstFit=true;
    limits.CandidatePriority=[](Pose pose){return std::array<double,4>{pose.Y,pose.X,0,0};};
    std::map<std::string,double> summary;std::size_t ordinaryQueries=0,commonQueries=0;
    limits.Trace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source=="nfp.original-clearance"&&event.Reason=="original-clearance-complete")
            for(const auto& [name,value]:event.Measures)summary[name]=value;
        if(event.Source=="nfp.query"&&event.Stage==ConstructionTraceStage::Begin)
            for(const auto& [name,value]:event.Measures)if(name=="nominal-common-domain")
                value==0?++ordinaryQueries:++commonQueries;
    };
    Detail::ClearanceNfpPlacement generator(p,p.Stocks[0],{},{});
    const auto nonparallel=generator.Find(board,1,23.7,limits,{});
    require(!nonparallel.Fits.empty()&&ordinaryQueries==1&&commonQueries==0&&
        summary["commonSourceEdgesComplete"]==1&&summary["commonDirectionScreenComplete"]==1&&
        summary["commonDirectionPairs"]==9&&summary["commonDomainSkippedByDirectionProof"]==1,
        "a complete nonparallel original direction proof failed to preserve the ordinary fit and skip only optional common work");
    for(const auto pose:nonparallel.Fits)requireNominalFit(p,board,1,pose);

    auto rectangles=p;rectangles.Stocks[0].Width=5;rectangles.Stocks[0].Height=1000000;
    rectangles.Parts[0].Geometry.Outer={{0,0},{4,0},{4,3},{0,3}};
    rectangles.Parts[1].Geometry=rectangles.Parts[0].Geometry;rectangles.Parts[1].AllowedAnglesDegrees={0};
    Layout narrow{"board",0,5,1000000,{{fixed.ID,0,{.5,.5,0},0,false}},12};
    Detail::ClearanceNfpPlacement common(rectangles,rectangles.Stocks[0],{},{});
    for(const std::size_t edgeLimit:{std::size_t{256},std::size_t{1}})
    {
        summary.clear();ordinaryQueries=commonQueries=0;auto settings=limits;settings.MaxContactSegments=edgeLimit;
        const auto result=common.Find(narrow,1,0,settings,{});
        require(result.Fits.size()==1&&result.Fits[0].Y==3.5&&ordinaryQueries==1&&commonQueries==1&&
            summary["commonDomainSkippedByDirectionProof"]==0&&summary["commonDirectionScreenComplete"]==0,
            "a possible real common seam or truncated source-edge list was treated as a complete negative direction proof");
        require(summary["commonSourceEdgesComplete"]==(edgeLimit==256?1.:0.),
            "authorized source-edge truncation was not retained in the direction proof");
        requireNominalFit(rectangles,narrow,1,result.Fits[0]);
    }
    summary.clear();ordinaryQueries=commonQueries=0;auto expired=limits;expired.RemainingTimeSeconds=[]{return 0.;};
    const auto stopped=common.Find(narrow,1,0,expired,{});
    require(stopped.Truncated&&stopped.Fits.empty()&&summary["commonDomainSkippedByDirectionProof"]==0&&
        summary["commonDirectionScreenComplete"]==0,
        "an exhausted query reported a complete negative common-direction proof");
    std::cout<<"PASS complete authorized direction proofs preserve true seams, truncated edges and ordinary fits\n";
}

static void CandidatePoolLimitReturnsAnOriginalSafePrefixBeforeTheFullOracle()
{
    Problem problem;problem.PartGap=0;problem.EdgeMargin=.5;problem.Tolerance=1e-7;
    problem.Stocks={{"prefix-pool-sheet",20,12,1,1,"m"}};
    auto fixed=polygon("prefix-fixed",{{0,0},{3,0},{3,4},{0,4}});fixed.Quantity=3;
    auto moving=polygon("prefix-moving",{{0,0},{2,0},{0,2}});
    problem.Parts={fixed,moving};
    Layout board{"prefix-pool-sheet",0,20,12,{
        {fixed.ID,0,{3,2,0,false},1,false},{fixed.ID,1,{9,4,0,false},2,false},
        {fixed.ID,2,{14,2,0,false},3,false}},36};
    Detail::NfpPlacementGenerator generator(problem);
    Detail::NfpPlacementLimits limits;limits.MaxCandidates=8;limits.MaxRawCandidates=4096;
    limits.MaxAttempts=100000;limits.CandidatePriority=[](Pose pose)
        {return std::array<double,4>{pose.Y,pose.X,0,0};};
    require(!limits.StopAfterCandidateLimit,"candidate-pool limit unexpectedly changed the complete default query");
    const auto oracle=generator.Find(board,1,0,limits);
    auto prefixLimits=limits;prefixLimits.StopAfterCandidateLimit=true;prefixLimits.IndexedMaterialProof=true;
    bool recordedPrefixStop=false;
    prefixLimits.Trace=[&](const ConstructionTraceEvent& event)
    {if(event.Source=="nfp.query"&&event.Stage==ConstructionTraceStage::Complete&&event.Reason=="candidate-fit-limit")
        recordedPrefixStop=true;};
    const auto prefix=generator.Find(board,1,0,prefixLimits);
    require(generator.Ready()&&oracle.Supported&&prefix.Supported&&oracle.Fits.size()==8&&prefix.Fits.size()==8&&
        prefix.Truncated&&recordedPrefixStop&&prefix.Attempts<oracle.Attempts,
        "bounded legal NFP pool failed to stop before validating the full default proposal oracle");
    for(const auto pose:oracle.Fits)requireNominalFit(problem,board,1,pose);
    for(const auto pose:prefix.Fits)
    {
        require(pose.AngleDegrees==0&&!pose.Mirrored,"candidate-pool prefix changed the requested original orientation");
        requireNominalFit(problem,board,1,pose);
    }
    // Production queries pass a positive original gap through the clearance
    // wrapper. A caller priority must retain the new finite cap, and the
    // wrapper's complete original gate must run before that cap can stop it.
    auto clearanceProblem=problem;clearanceProblem.PartGap=.2;
    Detail::ClearanceNfpPlacement clearance(clearanceProblem,clearanceProblem.Stocks.front(),{},{});
    auto clearanceLimits=limits;
    require(bool(clearanceLimits.CandidatePriority)&&!clearanceLimits.AcceptFit,
        "clearance prefix fixture accidentally installed an extra caller fit guard");
    const auto clearanceOracle=clearance.Find(board,1,0,clearanceLimits,{});
    clearanceLimits.StopAfterCandidateLimit=true;clearanceLimits.IndexedMaterialProof=true;
    std::size_t certifiedOriginals=0,certifiedBeforePrefixStop=0,nativePrefixQueries=0;
    bool keptEightCandidateCap=true,originalGuardInsideQuery=true,clearancePrefixStopped=false;
    clearanceLimits.Trace=[&](const ConstructionTraceEvent& event)
    {
        if(event.Source=="nfp.original-clearance"&&event.Reason=="original-clearance-fit"&&event.Accepted)
            ++certifiedOriginals;
        if(event.Source!="nfp.query")return;
        if(event.Stage==ConstructionTraceStage::Begin)
        {
            ++nativePrefixQueries;bool cap=false,guard=false,enabled=false;
            for(const auto& [name,value]:event.Measures)
            {
                if(name=="max-candidates")cap=value==8;
                if(name=="additional-fit-guard")guard=value==1;
                if(name=="stop-after-candidate-limit")enabled=value==1;
            }
            keptEightCandidateCap&=cap&&enabled;originalGuardInsideQuery&=guard;
        }
        if(event.Stage==ConstructionTraceStage::Complete&&event.Reason=="candidate-fit-limit")
        {clearancePrefixStopped=true;certifiedBeforePrefixStop=std::max(certifiedBeforePrefixStop,certifiedOriginals);}
    };
    const auto clearancePrefix=clearance.Find(board,1,0,clearanceLimits,{});
    require(clearance.Ready()&&clearanceOracle.Supported&&clearancePrefix.Supported&&
        clearanceOracle.Fits.size()==8&&clearancePrefix.Fits.size()==8&&clearancePrefix.Truncated&&
        nativePrefixQueries>0&&keptEightCandidateCap&&originalGuardInsideQuery&&clearancePrefixStopped&&
        certifiedBeforePrefixStop>=8&&clearancePrefix.Attempts<clearanceOracle.Attempts,
        "positive-gap clearance wrapper erased the priority pool cap or stopped before certifying eight actual original fits");
    for(const auto pose:clearanceOracle.Fits)requireNominalFit(clearanceProblem,board,1,pose);
    for(const auto pose:clearancePrefix.Fits)requireNominalFit(clearanceProblem,board,1,pose);
    // The opt-in flag retains the established zero-means-unlimited contract.
    auto unlimitedLimits=limits;unlimitedLimits.MaxCandidates=0;unlimitedLimits.IndexedMaterialProof=true;
    const auto unlimitedOracle=generator.Find(board,1,0,unlimitedLimits);
    unlimitedLimits.StopAfterCandidateLimit=true;
    const auto unlimited=generator.Find(board,1,0,unlimitedLimits);
    require(unlimited.Supported&&unlimited.Fits.size()>8&&unlimited.Fits.size()==unlimitedOracle.Fits.size()&&
        unlimited.Attempts==unlimitedOracle.Attempts,
        "candidate-pool stopping flag turned zero unlimited candidates into a finite prefix");
    for(std::size_t i=0;i<unlimited.Fits.size();++i)
        require(unlimited.Fits[i].X==unlimitedOracle.Fits[i].X&&unlimited.Fits[i].Y==unlimitedOracle.Fits[i].Y&&
            unlimited.Fits[i].AngleDegrees==unlimitedOracle.Fits[i].AngleDegrees&&
            unlimited.Fits[i].Mirrored==unlimitedOracle.Fits[i].Mirrored,
            "unlimited candidate-pool flag changed the complete ordered original fits");
    std::cout<<"PASS bounded candidate pool retains original-safe fits with fewer oracle attempts\n";
}
int main()try
{
    CandidatePoolLimitReturnsAnOriginalSafePrefixBeforeTheFullOracle();
    GeneratorOwnsItsImmutableProblemSnapshot();
    BoundaryVerticesRetainTheCompleteContactFallback();
    VerifiedFreeDomainRetreatUsesTheOriginalSafetyGate();
    ReusedWorldGeometryMatchesFreshQueries();
    QuickPlacementControls();
    CallerPriorityOrdersCompleteProposals();
    AdditionalFitGuardContinuesThroughOriginalSafety();
    NecessaryFitOnlyRejectsStoredPosesAndRetainsFiniteRetries();
    CompetitivePosePrecedesGeometryAndMaterialNecessaryFit();
    LazyMovingMaterialRetainsCompleteOriginalCertification();
    ImplicitMaterialProofMatchesExplicitOrderedProposals();
    VerificationGapCertifiesTheCompleteOriginalMaterial();
    MirroredOriginalsRetainTheirOwnDomainsCertificatesAndCaches();
    AFullPhasePoolCanEvictOnlyByTheCallersScore();
    TranslationWindowsBoundRawDomainsBeforeTheirCaps();
    RigidOriginalMembersUseTheirJointTranslationDomain();
    RigidNecessaryFitOnlyRejectsFinalDeltasBeforeMaterialWork();
    RigidFixedGeometryReusePreservesExactQueries();
    RigidLoopAndMovingEdgeReusePreservesEveryBoundedQuery();
    CollapsedRigidDomainsRetainTheirLowIntervalWithoutContactChannels();
    OrdinaryAndCommonFitsCompeteBeforeTheGlobalSingleFitCap();
    DynamicCertifiedRanksRetainTheBestOriginalSafeNonFirstFit();
    CommonDirectionProofRequiresEveryAuthorizedSourceEdge();
    Problem p;p.Tolerance=1e-7;p.Stocks.push_back({"board",14,8,1,1,"m"});
    p.Parts.push_back(polygon("U",{{0,3},{2,3},{2,6},{12,6},{12,3},{14,3},{14,8},{0,8}}));
    p.Parts.push_back(polygon("bottom",{{9,0},{9,.5},{3,.5},{3,0}}));
    p.Parts.push_back(polygon("top",{{11,3.8},{11,4},{4,4},{4,3.8}}));
    p.Parts.push_back(polygon("fill",{{10,0},{10,3},{0,3},{0,0}}));
    Layout layout{"board",0,14,8,{{"U",0,{0,0,0},0,false},{"bottom",0,{0,0,0},0,false},{"top",0,{0,0,0},0,false}},0};
    auto cache=std::make_shared<NfpCache>();Detail::NfpPlacementGenerator generator(p,cache);require(generator.Ready(),"simple input unsupported");
    Detail::NfpPlacementLimits limits;limits.MaxCandidates=0;auto query=generator.Find(layout,3,0,limits);
    bool interval=false;
    for(const auto pose:query.Fits)
    {
        requireNominalFit(p,layout,3,pose);
        require(Detail::InsideSheet(p.Parts[3].Geometry,pose,14,8,0,p.Tolerance),"outside stock");
        for(std::size_t i=0;i<3;++i)require(!Detail::ShapesConflict(p.Parts[i].Geometry,{},p.Parts[3].Geometry,pose,0,p.Tolerance),"source-material collision");
        if(std::abs(pose.X-2)<1e-6&&pose.Y>.5001&&pose.Y<.7999)interval=true;
    }
    require(interval,"constrained touch-only interval lost");
    const auto builds=cache->Statistics().RegionBuilds;const auto again=generator.Find(layout,3,0,limits);
    require(builds==cache->Statistics().RegionBuilds&&again.Fits.size()==query.Fits.size(),"repeated node rebuilt pair geometry");
    auto firstLimits=limits;firstLimits.StopAfterFirstFit=true;
    const auto first=generator.Find(layout,3,0,firstLimits);
    require(first.Supported&&first.Fits.size()==1&&first.Truncated&&first.Attempts<=again.Attempts,"early-fit did not stop safely");
    require(first.DomainMeasured&&first.FreeTranslationArea>=0,"early-fit domain measure missing");
    for(std::size_t i=0;i<3;++i)require(!Detail::ShapesConflict(p.Parts[i].Geometry,{},p.Parts[3].Geometry,first.Fits.front(),0,p.Tolerance),"early-fit returned a collision");
    limits.MaxAttempts=1;query=generator.Find(layout,3,0,limits);require(query.Attempts<=1&&query.Truncated,"attempt bound ignored");
    query=generator.Find(layout,3,0,{},[]{return true;});require(query.Fits.empty()&&query.Truncated,"cancel ignored");
    require(generator.Find(layout,3,90).Fits.empty(),"disallowed angle proposed");
    p.Parts[0].FreeRotation=true;Detail::NfpPlacementGenerator freeScope(p);require(freeScope.Ready(),"free-rotation source was rejected before its angle query");
    Problem line;line.Tolerance=1e-7;line.Stocks.push_back({"board",10,14,1,1,"m"});
    line.Parts={polygon("lower",{{2,0},{8,0},{8,5},{2,5}}),polygon("upper",{{2,13.3},{8,13.3},{8,14},{2,14}}),
        polygon("target",{{0,8},{10,0},{10,10},{0,9}})};
    Detail::NfpPlacementGenerator lineGenerator(line);Layout lineLayout{"board",0,10,14,{{"lower",0,{},0,false},{"upper",0,{},0,false}},0};
    limits={};limits.MaxCandidates=0;query=lineGenerator.Find(lineLayout,2,0,limits);bool feasible=false;
    for(const auto pose:query.Fits)
    {
        requireNominalFit(line,lineLayout,2,pose);
        if(std::abs(pose.X)<line.Tolerance&&pose.Y>=3.4-line.Tolerance&&pose.Y<=3.5+line.Tolerance)feasible=true;
    }
    require(feasible,"one-dimensional board domain lost");
    require(query.DomainMeasured&&query.FreeTranslationArea==0,"degenerate domain reported nonzero area");
    Problem displaced;displaced.Tolerance=1e-7;displaced.Stocks.push_back({"board",10,10,1,1,"m"});
    displaced.Parts={polygon("obstacle",{{0,0},{4,0},{4,10},{0,10}}),polygon("moving",{{0,0},{6,0},{6,10},{0,10}})};
    Layout displacedLayout{"board",0,10,10,{{"obstacle",0,{3,0,0},0,false}},40};
    Detail::NfpPlacementGenerator displacedGenerator(displaced);limits={};limits.MaxBlockers=1;
    query=displacedGenerator.Find(displacedLayout,1,0,limits);
    require(query.Fits.empty()&&!query.Blocked.empty(),"bounded blocker proposals missing");
    for(const auto& candidate:query.Blocked)
    {
        require(candidate.Blockers==std::vector<std::size_t>{0},"blocker index mismatch");
        require(candidate.OverlapArea>0,"blocker material area missing");
        require(Detail::InsideSheet(displaced.Parts[1].Geometry,candidate.Transform,10,10,0,displaced.Tolerance),"blocked proposal outside stock");
        require(Detail::ShapesConflict(displaced.Parts[0].Geometry,{3,0,0},displaced.Parts[1].Geometry,candidate.Transform,0,displaced.Tolerance),"false original-material blocker");
    }
    Problem crossing;crossing.Tolerance=1e-7;crossing.Stocks.push_back({"board",10,10,1,1,"m"});
    crossing.Parts={polygon("left",{{0,0},{8,0},{0,8}}),polygon("right",{{2,7},{10,3},{10,10}}),
        polygon("blocker",{{3.2,5.2},{3.8,5.2},{3.8,5.8},{3.2,5.8}}),polygon("moving",{{0,0},{1,0},{1,1},{0,1}})};
    Layout crossingBoard{"board",0,10,10,{{"left",0,{},0,false},{"right",0,{},0,false},{"blocker",0,{},0,false}},0};
    Result crossingBefore;crossingBefore.State=Status::Partial;crossingBefore.Layouts={crossingBoard};crossingBefore.Remaining={{"moving",1}};
    require(Validate(crossing,crossingBefore).Valid,"crossing-event incumbent is not legal");
    Detail::NfpPlacementGenerator crossingGenerator(crossing);limits={};limits.MaxBlockers=1;limits.MaxRawCandidates=8192;limits.MaxBlockedCandidates=0;
    query=crossingGenerator.Find(crossingBoard,3,0,limits);bool crossed=false;
    for(const auto& candidate:query.Blocked)
        if(std::abs(candidate.Transform.X-3)<1e-7&&std::abs(candidate.Transform.Y-5)<1e-7&&candidate.Blockers==std::vector<std::size_t>{2})
        {require(std::abs(candidate.OverlapArea-.36)<1e-8,"original-material overlap area incorrect");crossed=true;}
    require(crossed,"individual NFP edge intersection lost inside forbidden union");
    // Original Albano chevrons: precision-8 area alone manufactured blockers
    // on source-edge-separated oblique contacts. Exercise both native scale
    // and million-unit world coordinates without changing the source contour.
    for(double scale:{1.,500.})
    {
        Problem contact;contact.Tolerance=1e-7;contact.Stocks.push_back({"board",10381.591826489177*scale,4900*scale,1,1,"m"});
        auto chevron=polygon("chevron",{{-796,-152.5},{0,-33.5},{796,-152.5},{870,-27.5},{0,152.5},{-870,-27.5}});
        chevron.Quantity=3;chevron.AllowedAnglesDegrees={0,180};for(auto& v:chevron.Geometry.Outer){v.X*=scale;v.Y*=scale;}contact.Parts={chevron};
        Layout board{"board",0,contact.Stocks[0].Width,contact.Stocks[0].Height,
            {{"chevron",1,{8194.253306791186*scale,4416.542280271649*scale,180},0,false},
             {"chevron",2,{8707.346318542957*scale,4103.903516538441*scale,180},0,false}},0};
        Detail::NfpPlacementGenerator contacts(contact);limits={};limits.MaxCandidates=0;limits.MaxRawCandidates=65536;limits.MaxContactSegments=4096;limits.MaxBlockers=2;limits.MaxBlockedCandidates=0;
        const auto events=contacts.Find(board,0,0,limits);require(events.Supported&&!events.Truncated,"original contact regression was truncated");
        for(const auto& a:events.Blocked) requireCompleteBlockers(contact,board,0,a);
        for(const auto pose:events.Fits) requireNominalFit(contact,board,0,pose);
        for(const auto pose:events.Fits)for(const auto& other:board.Placements)
            require(!Detail::ShapesConflict(chevron.Geometry,other.Transform,chevron.Geometry,pose,0,contact.Tolerance),"contact correction accepted a true overlap");
        auto bounded=limits;bounded.MaxAttempts=1;const auto one=contacts.Find(board,0,0,bounded);
        require(one.Attempts<=1&&one.Truncated,"contact correction escaped attempt budget");
        const auto cancelled=contacts.Find(board,0,0,limits,[]{return true;});require(cancelled.Truncated&&cancelled.Fits.empty(),"blocked contact cancellation ignored");
        contact.PartGap=.01;Detail::NfpPlacementGenerator gapUnsupported(contact);require(!gapUnsupported.Ready(),"positive-gap input silently used zero-gap blocker logic");
    }
    Problem thin;thin.Tolerance=1e-7;thin.Stocks.push_back({"board",10,.01,1,1,"m"});
    thin.Parts={polygon("fixed",{{0,0},{5,0},{5,.01},{0,.01}}),polygon("thin",{{0,0},{5.000001,0},{5.000001,.01},{0,.01}})};
    Layout thinBoard{"board",0,10,.01,{{"fixed",0,{},0,false}},0};Detail::NfpPlacementGenerator thinGenerator(thin);limits={};limits.MaxBlockers=1;limits.MaxCandidates=0;limits.MaxBlockedCandidates=0;
    const auto thinEvents=thinGenerator.Find(thinBoard,1,0,limits);require(thinEvents.Fits.empty(),"true thin material intersection was erased");bool thinFound=false;
    for(const auto& a:thinEvents.Blocked)if(a.OverlapArea>0&&a.OverlapArea<1e-6)
    {thinFound=true;require(a.Blockers==std::vector<std::size_t>{0}&&Detail::ShapesConflict(thin.Parts[0].Geometry,{},thin.Parts[1].Geometry,a.Transform,0,thin.Tolerance),"thin overlap lost its real blocker");}
    require(thinFound,"thin-overlap regression did not exercise near-contact fallback");
    // The exact source endpoint is binary64(.1)+binary64(.2), which lies
    // strictly to the right of binary64(.3). Native tolerance also accepts the
    // nearby precision-8 event coordinates, checked from the actual proposals.
    Problem rounded;rounded.Tolerance=1e-7;rounded.Stocks.push_back({"board",1,1,1,1,"m"});
    rounded.Parts={polygon("fixed",{{0,0},{.2,0},{.2,1},{0,1}}),
        polygon("moving",{{0,0},{.1,0},{.1,.5},{0,.5}})};
    Layout roundedBoard{"board",0,1,1,{{"fixed",0,{.1,0,0},0,false}},.2};
    const Pose legacy{.3,0,0};
    require(Detail::InsideSheet(rounded.Parts[1].Geometry,legacy,1,1,0,rounded.Tolerance)&&
        !Detail::ShapesConflict(rounded.Parts[0].Geometry,{.1,0,0},rounded.Parts[1].Geometry,legacy,0,rounded.Tolerance),
        "legacy contact no longer demonstrates the native tolerance acceptance");
    require(AxisSafety::Pair(AxisSafety::Make(rounded.Parts[0].Geometry,{.1,0,0}),
        AxisSafety::Make(rounded.Parts[1].Geometry,legacy),0,false,rounded.Tolerance)==AxisSafety::Verdict::Blocked,
        "legacy contact does not penetrate exact original material");
    Detail::NfpPlacementGenerator roundedGenerator(rounded);
    limits={};limits.MaxCandidates=0;
    const auto corrected=roundedGenerator.Find(roundedBoard,1,0,limits);
    bool retreated=false;
    for(const auto pose:corrected.Fits)
    {
        requireNominalFit(rounded,roundedBoard,1,pose);
        require(pose.X!=legacy.X||pose.Y!=legacy.Y,"original penetrating pose entered Fits");
        if(pose.X>.3&&pose.X<.300002&&pose.Y==0) retreated=true;
    }
    require(retreated&&corrected.Attempts>corrected.Generated,"rounded contact was discarded without a counted safe retreat");
    limits.MaxBlockers=1;limits.MaxBlockedCandidates=0;
    const auto correctedBlockers=roundedGenerator.Find(roundedBoard,1,0,limits);
    std::size_t nominalBlockers=0;
    for(const auto pose:correctedBlockers.Fits)
    {
        requireNominalFit(rounded,roundedBoard,1,pose);
        require(pose.X!=legacy.X||pose.Y!=legacy.Y,"original penetrating pose became a zero-blocker fit");
    }
    for(const auto& candidate:correctedBlockers.Blocked)
        if(requireCompleteBlockers(rounded,roundedBoard,1,candidate))
        {
            require(candidate.Blockers==std::vector<std::size_t>{0},"nominal-only material blocker was omitted");
            for(const auto pose:correctedBlockers.Fits)
                require(pose.X!=candidate.Transform.X||pose.Y!=candidate.Transform.Y,
                    "actual penetrating event entered zero-blocker Fits");
            ++nominalBlockers;
        }
    require(nominalBlockers>0,"regression did not exercise a native-safe nominal material blocker");
    {
        const auto rounding=std::fegetround();
        struct RestoreRounding {int Mode;~RestoreRounding(){std::fesetround(Mode);}} restore{rounding};
        require(std::fesetround(FE_UPWARD)==0,"could not exercise uncertain arithmetic");
        const auto uncertain=roundedGenerator.Find(roundedBoard,1,0,limits);
        require(uncertain.Fits.empty()&&uncertain.Blocked.empty()&&uncertain.Truncated,
            "uncertain original geometry entered the placement sets");
    }
    Problem allBlockers;allBlockers.Tolerance=1e-7;allBlockers.Stocks.push_back({"board",1,1,1,1,"m"});
    allBlockers.Parts={polygon("left",{{0,0},{.2,0},{.2,1},{0,1}}),
        polygon("right",{{.8,0},{1,0},{1,1},{.8,1}}),polygon("whole",{{0,0},{1,0},{1,1},{0,1}})};
    Layout allBlockersBoard{"board",0,1,1,{{"left",0,{},0,false},{"right",0,{},0,false}},.4};
    Detail::NfpPlacementGenerator allBlockersGenerator(allBlockers);
    limits={};limits.MaxBlockers=2;limits.MaxBlockedCandidates=0;
    const auto completeBlockers=allBlockersGenerator.Find(allBlockersBoard,2,0,limits);
    require(completeBlockers.Fits.empty()&&!completeBlockers.Blocked.empty(),"complete blocker regression produced no proposals");
    for(const auto& candidate:completeBlockers.Blocked)
    {
        requireCompleteBlockers(allBlockers,allBlockersBoard,2,candidate);
        require(candidate.Blockers==std::vector<std::size_t>({0,1}),"a removable material blocker was omitted");
    }
    Problem angled;angled.Tolerance=1e-7;angled.EdgeMargin=.25;angled.Stocks.push_back({"board",60,50,1,1,"m"});
    angled.Parts={polygon("fixed",{{0,0},{10,0},{10,8},{0,8}}),polygon("free",{{-2,-1},{5,-1},{5,2},{-2,2}})};
    angled.Parts[0].AllowedAnglesDegrees={23};angled.Parts[1].FreeRotation=true;angled.Parts[1].AllowedAnglesDegrees.clear();
    Layout angledBoard{"board",0,60,50,{{"fixed",0,{15,13,23},0,false}},80};
    auto angleCache=std::make_shared<NfpCache>();Detail::NfpPlacementGenerator angledGenerator(angled,angleCache);
    limits={};limits.MaxCandidates=0;
    const auto freeFits=angledGenerator.Find(angledBoard,1,37,limits);
    require(angledGenerator.Ready()&&freeFits.Supported&&!freeFits.Fits.empty(),"free 37-degree placement against another stationary angle failed");
    for(const auto pose:freeFits.Fits)
    {require(pose.AngleDegrees==37,"free query changed the requested angle");requireNominalFit(angled,angledBoard,1,pose);}
    const auto angleBuilds=angleCache->Statistics().RegionBuilds;
    require(angledGenerator.Find(angledBoard,1,37,limits).Fits.size()==freeFits.Fits.size()&&
        angleCache->Statistics().RegionBuilds==angleBuilds,"repeated free-angle query rebuilt immutable pair geometry");
    auto finite=angled;finite.Parts[1].FreeRotation=false;finite.Parts[1].AllowedAnglesDegrees={0,90};
    Detail::NfpPlacementGenerator finiteGenerator(finite);
    require(finiteGenerator.Ready()&&finiteGenerator.Find(angledBoard,1,37,limits).Fits.empty(),"free support widened a finite original angle domain");
    Problem high;high.Tolerance=1e-7;high.Stocks.push_back({"board",60,40,1,1,"m"});
    auto many=polygon("many",{});many.Quantity=2;
    for(std::size_t i=0;i<192;++i)
    {
        const double angle=2*Detail::Pi*double(i)/192;
        const Point point{10*std::cos(angle),10*std::sin(angle)};
        many.Geometry.Outer.push_back(point);many.Geometry.Outer.push_back(point);
    }
    many.Geometry.Outer.push_back(many.Geometry.Outer.front());high.Parts={many};
    Layout highBoard{"board",0,60,40,{{"many",0,{15,20,0},0,false}},0};
    Detail::NfpPlacementGenerator highGenerator(high);limits={};limits.StopAfterFirstFit=true;
    limits.MaxRawCandidates=512;limits.MaxContactSegments=32;limits.MaxAttempts=5000;
    const auto highFits=highGenerator.Find(highBoard,0,0,limits);
    require(highGenerator.Ready()&&highFits.Supported&&highFits.Truncated&&!highFits.Fits.empty(),
        "bounded incomplete high-vertex contact loci discarded all legal original placements");
    for(const auto pose:highFits.Fits)requireNominalFit(high,highBoard,0,pose);
    auto noAttempts=limits;noAttempts.MaxAttempts=0;
    require(highGenerator.Find(highBoard,0,0,noAttempts).Fits.empty(),"high-vertex query ignored its attempt cap");
    const auto stopped=highGenerator.Find(highBoard,0,0,limits,[]{return true;});
    require(stopped.Truncated&&stopped.Fits.empty(),"high-vertex contact preparation ignored cancellation");
    for(const std::size_t placed:{129u,161u})
    {
        Problem inventory;inventory.Tolerance=1e-7;inventory.Stocks.push_back({"board",400,2,1,1,"m"});
        auto small=polygon("small",{{0,0},{1,0},{1,1},{0,1}});small.Quantity=placed+1;inventory.Parts={small};
        Layout prefix{"board",0,400,2,{},double(placed)};
        for(std::size_t i=0;i<placed;++i)prefix.Placements.push_back({"small",i,{double(i)*2,0,0},0,false});
        Detail::NfpPlacementGenerator inventoryGenerator(inventory);limits={};limits.StopAfterFirstFit=true;
        const auto next=inventoryGenerator.Find(prefix,0,0,limits);
        require(next.Supported&&!next.Fits.empty(),"original inventory beyond 128 placements was rejected");
        requireNominalFit(inventory,prefix,0,next.Fits.front());
        Result completed;completed.State=Status::Complete;prefix.Placements.push_back({"small",placed,next.Fits.front(),0,false});
        completed.Layouts={prefix};
        require(Validate(inventory,completed).Valid&&Validate(inventory,completed).Complete,
            "later original placement changed quantity or source geometry");
    }
    std::cout<<"NFP placement material safety, channels, reuse and budgets passed\n";
}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
