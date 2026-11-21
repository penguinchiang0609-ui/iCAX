#include "NestingPlaneHoles.h"
#include "NestingInternal.h"
#include "NestingAxisSafety.h"
#include "NestingPlaneDomainGeometry.h"
#include "SheetNestingNfp.h"
#include "SheetNestingConstructionTrace.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace iCAX::SheetNesting::Detail
{
namespace
{
using namespace Clipper2Lib;

Shape ProposalShape(const Shape& original)
{
    Shape result;
    if(original.Radius<=0)
    {
        // Filling a moving hole is conservative for containment/collision
        // proposals. The original holes remain present in final validation.
        result.Outer=MaterialRing(original.Outer);return result;
    }
    constexpr std::size_t count=32;
    const double radius=original.Radius/std::cos(Pi/static_cast<double>(count));
    for(std::size_t i=0;i<count;++i)
    {
        const auto a=2*Pi*static_cast<double>(i)/static_cast<double>(count);
        result.Outer.push_back({original.Center.X+radius*std::cos(a),original.Center.Y+radius*std::sin(a)});
    }
    return result;
}

PathsD OccupiedMaterial(const Shape& original,Pose pose)
{
    if(original.Radius>0)return PlaneDomain::Material(ProposalShape(original),pose);
    return PlaneDomain::Material(original,pose);
}

bool Near(const Rect& a,const Rect& b,double gap)
{
    return a.X+a.Width+gap>=b.X&&b.X+b.Width+gap>=a.X&&
        a.Y+a.Height+gap>=b.Y&&b.Y+b.Height+gap>=a.Y;
}
}

NfpPlacementQuery FindPlaneHolePlacements(const Problem& problem,const Layout& board,
    std::size_t partIndex,double angle,const NfpPlacementLimits& limits,const std::function<bool()>& stop)
{
    NfpPlacementQuery result;
    bool tracing=static_cast<bool>(limits.Trace);
    std::uint64_t nextTraceID=0;
    std::size_t hostIndex=0,holeIndex=0;
    const char* exitReason="complete";
    struct RetainedTrace {std::uint64_t ID;std::size_t Host,Hole;};
    std::map<std::tuple<double,double,bool>,RetainedTrace> retainedTraceIDs;
    const auto disableTrace=[&]()noexcept
    {
        tracing=false;result.FitsIDs.clear();
        if(limits.TraceHealth){limits.TraceHealth->Failed.store(true);++limits.TraceHealth->Omitted;}
    };
    const auto observe=[&](ConstructionTraceStage stage,std::uint64_t id,const char* source,Pose pose,
        bool accepted,const char* reason,std::initializer_list<std::pair<const char*,double>> measures={})noexcept
    {
        if(!tracing)return;
        if(limits.TraceHealth&&limits.TraceHealth->Failed.load()){++limits.TraceHealth->Omitted;return;}
        try
        {
            ConstructionTraceEvent event;event.Stage=stage;event.Source=source;event.CandidateID=id;
            event.PartIndex=partIndex;event.StockInstance=board.StockInstance;event.BoardWidth=board.Width;event.BoardHeight=board.Height;
            event.Attempts=result.Attempts;event.Accepted=accepted;event.Reason=reason;
            event.SearchTruncated=result.Truncated||(limits.MaxCandidates&&result.Fits.size()>=limits.MaxCandidates);
            if(id)event.Members.push_back({partIndex,std::numeric_limits<std::size_t>::max(),pose,0});
            event.Measures={{"host-index",double(hostIndex)},{"hole-index",double(holeIndex)}};
            for(const auto& measure:measures)event.Measures.emplace_back(measure.first,measure.second);
            limits.Trace(event);
        }
        catch(...){if(limits.TraceHealth){limits.TraceHealth->Failed.store(true);++limits.TraceHealth->Omitted;}}
    };
    const auto finish=[&]()
    {
        observe(ConstructionTraceStage::Complete,0,"plane.hole.query",{},result.Supported,exitReason,
            {{"supported",result.Supported?1.:0.},{"truncated",result.Truncated?1.:0.},
             {"generated",double(result.Generated)},{"fits-returned",double(result.Fits.size())},
             {"fit-candidate-cap-reached",limits.MaxCandidates&&result.Fits.size()>=limits.MaxCandidates?1.:0.}});
        return std::move(result);
    };
    observe(ConstructionTraceStage::Begin,0,"plane.hole.query",{},true,"hole-query-begin",
        {{"angle",angle},{"mirrored",limits.Mirrored?1.:0.},{"max-candidates",double(limits.MaxCandidates)},{"max-attempts",double(limits.MaxAttempts)},
         {"max-raw-candidates",double(limits.MaxRawCandidates)},{"max-contact-segments",double(limits.MaxContactSegments)},
         {"max-fixed-placements",double(limits.MaxFixedPlacements)},
         {"include-contact-channels",limits.IncludeContactChannels?1.:0.},
         {"stop-after-first-fit",limits.StopAfterFirstFit?1.:0.},{"fast-adjacency",limits.FastAdjacency?1.:0.},
         {"max-blockers",double(limits.MaxBlockers)},{"max-blocked-candidates",double(limits.MaxBlockedCandidates)}});
    if(partIndex>=problem.Parts.size()||!std::isfinite(angle)||!std::isfinite(problem.PartGap)||problem.PartGap<0||
        !std::isfinite(problem.EdgeMargin)||problem.EdgeMargin<0||!std::isfinite(problem.Tolerance)||problem.Tolerance<0||
        !std::isfinite(board.Width)||board.Width<=0||!std::isfinite(board.Height)||board.Height<=0||limits.MaxBlockers)
        {exitReason="invalid-hole-query";return finish();}
    const auto& part=problem.Parts[partIndex];
    if(!PoseAllowed(part,{0,0,angle,limits.Mirrored},problem.Tolerance)){exitReason="hole-orientation-not-allowed";return finish();}
    std::string error;
    if(!ShapeValid(part.Geometry,problem.Tolerance,error)){exitReason="invalid-moving-original";return finish();}
    const auto stock=std::find_if(problem.Stocks.begin(),problem.Stocks.end(),[&](const Stock& value){return value.ID==board.StockID;});
    if(stock==problem.Stocks.end()||stock->Width!=board.Width||stock->Height!=board.Height||
        stock->Material!=part.Material||board.StockInstance>=stock->Quantity)
        {exitReason="hole-stock-or-material-mismatch";return finish();}
    bool interrupted=false;
    const auto stopped=[&]()
    {
        if(interrupted)return true;
        try{if(stop&&stop()){interrupted=true;result.Truncated=true;exitReason="stopped";}}
        catch(...){interrupted=true;result.Truncated=true;exitReason="stop-callback-failed";}
        return interrupted;
    };
    if(stopped())return finish();
    if(!limits.MaxAttempts||!limits.MaxRawCandidates)
    {result.Supported=true;result.Truncated=true;exitReason="zero-hole-budget";return finish();}

    struct Fixed
    {
        const Part* Original;
        const Placement* Placed;
        Rect Box;
        AxisSafety::Geometry Nominal;
    };
    std::map<std::string,const Part*> originals;
    for(const auto& value:problem.Parts)
    {
        if(stopped())return finish();
        if(!originals.emplace(value.ID,&value).second){exitReason="duplicate-original-id";return finish();}
    }
    std::vector<Fixed> fixed;fixed.reserve(board.Placements.size());
    std::size_t localUse=0;
    for(const auto& placed:board.Placements)
    {
        if(stopped())return finish();
        const auto found=originals.find(placed.PartID);
        if(found==originals.end()||placed.Instance>=found->second->Quantity||
            !PoseAllowed(*found->second,placed.Transform,problem.Tolerance))
        {exitReason="invalid-fixed-original-instance";return finish();}
        if(placed.PartID==part.ID)++localUse;
        fixed.push_back({found->second,&placed,Bounds(found->second->Geometry,placed.Transform),
            AxisSafety::Make(found->second->Geometry,placed.Transform)});
    }
    result.Supported=true;
    if(localUse>=part.Quantity){exitReason="hole-local-demand-exhausted";return finish();}
    if(stopped())return finish();
    const auto enough=[&]{return limits.MaxCandidates&&result.Fits.size()>=limits.MaxCandidates;};
    const auto rotated=Bounds(part.Geometry,{0,0,angle,limits.Mirrored});
    const auto proposal=ProposalShape(part.Geometry);
    const double retreat=std::max(1e-7,4*problem.Tolerance);
    const double clearance=problem.PartGap>0?problem.PartGap+retreat:0;
    // Nominal hole-boundary contacts are proposals only. The actual host and
    // every other original below retain the requested pair-gap/common-edge gate.
    const double domainGap=problem.AllowCommonEdges?0:problem.PartGap;
    auto cache=std::make_shared<NfpCache>(NfpCacheSettings{128,256,8*1024*1024});
    std::set<std::tuple<double,double,bool>> retained;

    for(const auto& host:fixed)
    {
        hostIndex=static_cast<std::size_t>(&host-fixed.data());holeIndex=0;
        if(stopped()||enough())
        {if(!interrupted)exitReason="hole-fit-candidate-limit";
            observe(ConstructionTraceStage::Budget,0,"plane.hole.host",{},false,exitReason);break;}
        if(host.Original->Material!=part.Material)
        {observe(ConstructionTraceStage::Filter,0,"plane.hole.host",{},false,"host-material-mismatch");continue;}
        for(const auto& sourceHole:host.Original->Geometry.Holes)
        {
            holeIndex=static_cast<std::size_t>(&sourceHole-host.Original->Geometry.Holes.data());
            if(stopped()||enough())
            {if(!interrupted)exitReason="hole-fit-candidate-limit";
                observe(ConstructionTraceStage::Budget,0,"plane.hole.domain",{},false,exitReason);break;}
            if(result.Attempts>=limits.MaxAttempts||result.Generated>=limits.MaxRawCandidates)
            {result.Truncated=true;exitReason="hole-attempt-or-raw-limit";return finish();}
            Shape hole;
            for(const auto point:sourceHole)hole.Outer.push_back(Transform(point,host.Placed->Transform));
            hole.Outer=MaterialRing(hole.Outer);
            if(!ShapeValid(hole,problem.Tolerance,error))
            {result.Truncated=true;observe(ConstructionTraceStage::Filter,0,"plane.hole.domain",{},false,"invalid-host-hole");continue;}
            const auto domainBox=Bounds(hole);
            observe(ConstructionTraceStage::Analysis,0,"plane.hole.domain",{},true,"actual-host-hole-domain",
                {{"x",domainBox.X},{"y",domainBox.Y},{"width",domainBox.Width},{"height",domainBox.Height},
                 {"vertices",double(hole.Outer.size())},{"host-original-index",double(host.Original-problem.Parts.data())},
                 {"host-instance",double(host.Placed->Instance)}});
            if(domainBox.Width+problem.Tolerance<rotated.Width+2*domainGap||
                domainBox.Height+problem.Tolerance<rotated.Height+2*domainGap)
            {observe(ConstructionTraceStage::Filter,0,"plane.hole.domain",{},false,"hole-envelope-necessary-fit-failed");continue;}
            const double pad=std::max({1.,domainBox.Width,domainBox.Height});
            Shape wall;wall.Outer={{domainBox.X-pad,domainBox.Y-pad},
                {domainBox.X+domainBox.Width+pad,domainBox.Y-pad},
                {domainBox.X+domainBox.Width+pad,domainBox.Y+domainBox.Height+pad},
                {domainBox.X-pad,domainBox.Y+domainBox.Height+pad}};
            wall.Holes={hole.Outer};
            const auto nominalWall=AxisSafety::Make(wall,{});
            std::set<std::tuple<double,double,bool>> tried;
            const auto verify=[&](Pose pose,bool charged,std::uint64_t id)
            {
                if(stopped())
                {observe(ConstructionTraceStage::Budget,id,"plane.hole.original-check",pose,false,"stopped-before-hole-verification");return false;}
                if(!PoseAllowed(part,pose,problem.Tolerance))
                {observe(ConstructionTraceStage::Filter,id,"plane.hole.original-check",pose,false,"hole-orientation-not-allowed");return false;}
                if(!tried.emplace(pose.X,pose.Y,pose.Mirrored).second)
                {observe(ConstructionTraceStage::Filter,id,"plane.hole.original-check",pose,false,"hole-local-duplicate");return false;}
                if(!charged)
                {
                    if(result.Attempts>=limits.MaxAttempts||result.Generated>=limits.MaxRawCandidates)
                    {result.Truncated=true;observe(ConstructionTraceStage::Budget,id,"plane.hole.original-check",pose,false,"hole-attempt-or-raw-limit");return false;}
                    ++result.Attempts;++result.Generated;
                }
                const auto box=Bounds(part.Geometry,pose);
                if(box.X<domainBox.X-problem.Tolerance||box.Y<domainBox.Y-problem.Tolerance||
                    box.X+box.Width>domainBox.X+domainBox.Width+problem.Tolerance||
                    box.Y+box.Height>domainBox.Y+domainBox.Height+problem.Tolerance||
                    !InsideSheet(part.Geometry,pose,board.Width,board.Height,problem.EdgeMargin,problem.Tolerance))
                {observe(ConstructionTraceStage::Filter,id,"plane.hole.original-check",pose,false,"hole-or-sheet-bounds-rejected");return false;}
                const auto nominal=AxisSafety::Make(part.Geometry,pose);
                const auto containment=AxisSafety::Containment(nominal,board.Width,board.Height,problem.EdgeMargin);
                if(containment!=AxisSafety::Verdict::Safe)
                {observe(ConstructionTraceStage::Filter,id,"plane.hole.original-check",pose,false,
                    containment==AxisSafety::Verdict::Uncertain?"hole-sheet-containment-uncertain":"hole-sheet-containment-blocked");return false;}
                if(ShapesConflict(part.Geometry,pose,wall,{},domainGap,problem.Tolerance,problem.AllowCommonEdges))
                {observe(ConstructionTraceStage::Filter,id,"plane.hole.original-check",pose,false,"actual-hole-wall-material-conflict");return false;}
                const auto wallVerdict=AxisSafety::Pair(nominal,nominalWall,domainGap,problem.AllowCommonEdges,problem.Tolerance);
                if(wallVerdict!=AxisSafety::Verdict::Safe)
                {observe(ConstructionTraceStage::Filter,id,"plane.hole.original-check",pose,false,
                    wallVerdict==AxisSafety::Verdict::Uncertain?"hole-wall-nominal-uncertain":"hole-wall-nominal-blocked");return false;}
                for(const auto& occupied:fixed)
                {
                    if(stopped())
                    {observe(ConstructionTraceStage::Budget,id,"plane.hole.original-check",pose,false,"stopped-during-hole-verification");return false;}
                    if(!Near(box,occupied.Box,problem.PartGap+problem.Tolerance+nominal.Roundoff+occupied.Nominal.Roundoff))continue;
                    if(ShapesConflict(part.Geometry,pose,occupied.Original->Geometry,occupied.Placed->Transform,
                        problem.PartGap,problem.Tolerance,problem.AllowCommonEdges))
                    {observe(ConstructionTraceStage::Filter,id,"plane.hole.original-check",pose,false,"hole-occupied-original-material-conflict",
                        {{"fixed-original-index",double(occupied.Original-problem.Parts.data())},{"fixed-instance",double(occupied.Placed->Instance)}});return false;}
                    const auto verdict=AxisSafety::Pair(nominal,occupied.Nominal,problem.PartGap,problem.AllowCommonEdges,problem.Tolerance);
                    if(verdict!=AxisSafety::Verdict::Safe)
                    {observe(ConstructionTraceStage::Filter,id,"plane.hole.original-check",pose,false,
                        verdict==AxisSafety::Verdict::Uncertain?"hole-occupied-nominal-uncertain":"hole-occupied-nominal-blocked",
                        {{"fixed-original-index",double(occupied.Original-problem.Parts.data())},{"fixed-instance",double(occupied.Placed->Instance)}});return false;}
                }
                if(retained.emplace(pose.X,pose.Y,pose.Mirrored).second)
                {
                    result.Fits.push_back(pose);
                    if(tracing)try
                    {result.FitsIDs.push_back(id);retainedTraceIDs.emplace(std::make_tuple(pose.X,pose.Y,pose.Mirrored),RetainedTrace{id,hostIndex,holeIndex});}
                    catch(...){disableTrace();}
                    observe(ConstructionTraceStage::Filter,id,"plane.hole.original-check",pose,true,"actual-original-hole-fit",
                        {{"already-charged",charged?1.:0.},{"fit-index",double(result.Fits.size()-1)}});
                }
                else observe(ConstructionTraceStage::Filter,id,"plane.hole.original-check",pose,false,"hole-fit-already-retained");
                return true;
            };

            // Cheap exact-original seeds make small budgets useful and retain
            // legal collapsed channels that an inward clipping operation can
            // regularize away. Every seed still receives the full wall check.
            std::vector<Point> seeds;
            const auto seedRange=[&](double gap)
            {
                const double lx=domainBox.X+gap-rotated.X,ly=domainBox.Y+gap-rotated.Y;
                const double rx=domainBox.X+domainBox.Width-gap-rotated.X-rotated.Width;
                const double ty=domainBox.Y+domainBox.Height-gap-rotated.Y-rotated.Height;
                const std::array<Point,9> range{{{lx,ly},{rx,ly},{lx,ty},{rx,ty},{(lx+rx)/2,(ly+ty)/2},
                    {lx,(ly+ty)/2},{rx,(ly+ty)/2},{(lx+rx)/2,ly},{(lx+rx)/2,ty}}};
                seeds.insert(seeds.end(),range.begin(),range.end());
            };
            seedRange(domainGap);if(domainGap!=problem.PartGap)seedRange(problem.PartGap);
            std::size_t seedIndex=0;
            for(const auto seed:seeds)
            {
                if(stopped()||enough()||result.Attempts>=limits.MaxAttempts||result.Generated>=limits.MaxRawCandidates)
                {observe(ConstructionTraceStage::Budget,0,"plane.hole.seed",{},false,
                    interrupted?"stopped-before-hole-seed":enough()?"hole-fit-candidate-limit":"hole-attempt-or-raw-limit",
                    {{"unvisited-seeds",double(seeds.size()-seedIndex)}});break;}
                const auto id=tracing?++nextTraceID:0;
                observe(ConstructionTraceStage::Generate,id,"plane.hole.seed",{seed.X,seed.Y,angle,limits.Mirrored},true,"actual-hole-seed",
                    {{"seed-index",double(seedIndex)}});
                ++seedIndex;
                if(verify({seed.X,seed.Y,angle,limits.Mirrored},false,id)&&limits.StopAfterFirstFit)
                {exitReason="first-hole-fit-seed";return finish();}
            }
            if(stopped()||enough())
            {if(!interrupted)exitReason="hole-fit-candidate-limit";break;}
            if(result.Attempts>=limits.MaxAttempts||result.Generated>=limits.MaxRawCandidates)
            {result.Truncated=true;exitReason="hole-attempt-or-raw-limit";return finish();}
            if(hole.Outer.size()>std::max<std::size_t>(3,limits.MaxContactSegments))
            {result.Truncated=true;observe(ConstructionTraceStage::Filter,0,"plane.hole.domain",{},false,"hole-contact-vertex-limit");continue;}
            std::vector<double> domainClearances{clearance};
            if(problem.AllowCommonEdges&&clearance>0)domainClearances.insert(domainClearances.begin(),0.);
            for(const double proposalClearance:domainClearances)
            {
            if(stopped()||enough())break;
            if(result.Attempts>=limits.MaxAttempts||result.Generated>=limits.MaxRawCandidates)
            {result.Truncated=true;exitReason="hole-attempt-or-raw-limit";return finish();}
            try
            {
                // Build the actual free material domain. Inflating fixed
                // material retains their holes; it does not substitute their
                // envelopes and does not consume or modify source geometry.
                auto available=PlaneDomain::Allowed(hole,{},proposalClearance);
                for(const auto& occupied:fixed)
                {
                    if(stopped())break;
                    if(!Near(domainBox,occupied.Box,proposalClearance))continue;
                    auto material=OccupiedMaterial(occupied.Original->Geometry,occupied.Placed->Transform);
                    if(proposalClearance>0)material=InflatePaths(material,proposalClearance,JoinType::Miter,EndType::Polygon,2.,8);
                    available=Difference(available,material,FillRule::EvenOdd,8);
                    if(available.empty())break;
                }
                if(stopped())break;
                observe(ConstructionTraceStage::Analysis,0,"plane.hole.available",{},!available.empty(),
                    available.empty()?"actual-hole-free-domain-empty":"actual-hole-free-domain-built",
                    {{"path-count",double(available.size())},{"clearance",proposalClearance}});
                if(available.empty())continue;
                std::size_t vertices=0;for(auto& path:available)
                {
                    vertices+=path.size();for(auto& point:path){point.x-=domainBox.X;point.y-=domainBox.Y;}
                }
                if(vertices>std::max<std::size_t>(64,std::min<std::size_t>(4096,limits.MaxRawCandidates)))
                {result.Truncated=true;observe(ConstructionTraceStage::Filter,0,"plane.hole.available",{},false,"hole-domain-vertex-limit",
                    {{"vertices",double(vertices)}});continue;}
                const auto pieces=PlaneDomain::Decompose(available,domainBox.Width,domainBox.Height,
                    std::max(1e-7,problem.Tolerance));
                observe(ConstructionTraceStage::Analysis,0,"plane.hole.decomposition",{},true,"actual-hole-domain-decomposed",
                    {{"available-vertices",double(vertices)},{"blocker-count",double(pieces.Blockers.size())},
                     {"domain-width",domainBox.Width},{"domain-height",domainBox.Height}});
                if(pieces.Blockers.size()>127)
                {result.Truncated=true;observe(ConstructionTraceStage::Filter,0,"plane.hole.decomposition",{},false,"hole-decomposition-blocker-limit");continue;}
                Problem scene;scene.Tolerance=std::max(1e-7,problem.Tolerance);scene.AllowCommonEdges=problem.AllowCommonEdges;
                scene.Stocks={{"hole-local-stock",domainBox.Width,domainBox.Height,1,1,part.Material}};
                Part candidate=part;candidate.ID="hole-moving-original";candidate.Quantity=1;candidate.Geometry=proposal;
                scene.Parts.push_back(std::move(candidate));
                Layout environment{scene.Stocks[0].ID,0,domainBox.Width,domainBox.Height,{},0};
                for(const auto& geometry:pieces.Blockers)
                {
                    Part obstacle;obstacle.ID="hole-outside-"+std::to_string(scene.Parts.size());obstacle.Geometry=geometry;
                    obstacle.Material=part.Material;obstacle.Quantity=0;obstacle.FreeRotation=false;obstacle.AllowedAnglesDegrees={0};
                    environment.Placements.push_back({obstacle.ID,0,{},0,false});scene.Parts.push_back(std::move(obstacle));
                }
                NfpPlacementGenerator generator(scene,cache,stopped);
                if(!generator.Ready())
                {result.Truncated=true;observe(ConstructionTraceStage::Filter,0,"plane.hole.inner-nfp",{},false,"hole-inner-generator-unavailable");continue;}
                auto remaining=limits;remaining.MaxCandidates=0;remaining.MaxBlockers=0;remaining.StopAfterFirstFit=false;
                remaining.MaxAttempts=limits.MaxAttempts-result.Attempts;
                remaining.MaxRawCandidates=limits.MaxRawCandidates-result.Generated;
                if(proposalClearance==0&&clearance>0)
                {
                    remaining.MaxAttempts=remaining.MaxAttempts/2+remaining.MaxAttempts%2;
                    remaining.MaxRawCandidates=remaining.MaxRawCandidates/2+remaining.MaxRawCandidates%2;
                }
                std::map<std::uint64_t,std::uint64_t> innerTraceIDs;
                if(tracing)try
                {
                    remaining.Trace=[&](const ConstructionTraceEvent& observed)noexcept
                    {
                        if(!tracing)return;
                        if(limits.TraceHealth&&limits.TraceHealth->Failed.load()){++limits.TraceHealth->Omitted;return;}
                        try
                        {
                            auto event=observed;
                            const auto globalID=[&](std::uint64_t local)
                            {
                                if(!local)return std::uint64_t{};
                                auto found=innerTraceIDs.find(local);
                                if(found==innerTraceIDs.end())found=innerTraceIDs.emplace(local,++nextTraceID).first;
                                return found->second;
                            };
                            event.CandidateID=globalID(event.CandidateID);
                            for(auto& measure:event.Measures)
                                if((measure.first=="duplicate-of"||measure.first=="parent-candidate-id")&&measure.second>0)
                                    measure.second=double(globalID(static_cast<std::uint64_t>(measure.second)));
                            event.Source="plane.hole.inner-nfp/"+event.Source;
                            event.PartIndex=partIndex;event.StockInstance=board.StockInstance;
                            event.Measures.emplace_back("inner-candidate-id",double(observed.CandidateID));
                            event.Measures.emplace_back("proposal-clearance",proposalClearance);
                            event.Measures.emplace_back("host-index",double(hostIndex));event.Measures.emplace_back("hole-index",double(holeIndex));
                            event.Measures.emplace_back("domain-origin-x",domainBox.X);event.Measures.emplace_back("domain-origin-y",domainBox.Y);
                            event.Measures.emplace_back("inner-board-width",event.BoardWidth);event.Measures.emplace_back("inner-board-height",event.BoardHeight);
                            event.BoardWidth=board.Width;event.BoardHeight=board.Height;
                            for(auto& member:event.Members)
                            {member.PartIndex=partIndex;member.Transform=ComposePose({domainBox.X,domainBox.Y,0},member.Transform);}
                            limits.Trace(event);
                        }
                        catch(...){if(limits.TraceHealth){limits.TraceHealth->Failed.store(true);++limits.TraceHealth->Omitted;}}
                    };
                }
                catch(...){disableTrace();remaining.Trace={};}
                else remaining.Trace={};
                const auto query=generator.Find(environment,0,angle,remaining,stopped);
                result.Attempts+=query.Attempts;result.Generated+=query.Generated;result.Truncated=result.Truncated||query.Truncated;
                observe(ConstructionTraceStage::Analysis,0,"plane.hole.inner-nfp",{},query.Supported,"actual-hole-inner-query-complete",
                    {{"supported",query.Supported?1.:0.},{"truncated",query.Truncated?1.:0.},
                     {"inner-generated",double(query.Generated)},{"inner-attempts",double(query.Attempts)},{"inner-fits",double(query.Fits.size())}});
                for(std::size_t fitIndex=0;fitIndex<query.Fits.size();++fitIndex)
                {
                    auto pose=query.Fits[fitIndex];
                    if(stopped()||enough())
                    {observe(ConstructionTraceStage::Budget,0,"plane.hole.inner-nfp",{},false,
                        interrupted?"stopped-before-hole-original-check":"hole-fit-candidate-limit",
                        {{"unvisited-inner-fits",double(query.Fits.size()-fitIndex)}});break;}
                    pose=ComposePose({domainBox.X,domainBox.Y,0},pose);
                    std::uint64_t id=0;
                    if(tracing)try
                    {
                        const auto localID=fitIndex<query.FitsIDs.size()?query.FitsIDs[fitIndex]:0;
                        auto found=innerTraceIDs.find(localID);
                        if(localID&&found!=innerTraceIDs.end())id=found->second;
                        else
                        {id=++nextTraceID;if(localID)innerTraceIDs.emplace(localID,id);}
                    }
                    catch(...){disableTrace();}
                    // This pose already cost one proposal attempt in Find;
                    // rechecking unchanged originals is the acceptance gate.
                    if(verify(pose,true,id)&&limits.StopAfterFirstFit)
                    {exitReason="first-hole-fit-inner-nfp";return finish();}
                }
            }
            catch(...){result.Truncated=true;observe(ConstructionTraceStage::Filter,0,"plane.hole.domain",{},false,"hole-domain-or-proposal-exception");}
            }
        }
    }
    std::stable_sort(result.Fits.begin(),result.Fits.end(),[](Pose a,Pose b){return std::tie(a.Y,a.X)<std::tie(b.Y,b.X);});
    const auto endingHost=hostIndex,endingHole=holeIndex;
    if(tracing&&result.FitsIDs.size()==result.Fits.size())for(std::size_t i=0;i<result.Fits.size();++i)
    {
        const auto found=retainedTraceIDs.find({result.Fits[i].X,result.Fits[i].Y,result.Fits[i].Mirrored});
        if(found==retainedTraceIDs.end()){disableTrace();break;}
        result.FitsIDs[i]=found->second.ID;hostIndex=found->second.Host;holeIndex=found->second.Hole;
        observe(ConstructionTraceStage::Filter,result.FitsIDs[i],"plane.hole.output-order",result.Fits[i],true,"hole-fit-final-order",
            {{"fit-index",double(i)}});
    }
    hostIndex=endingHost;holeIndex=endingHole;
    return finish();
}
}
