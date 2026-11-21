#pragma once
#include "NestingNfpPlacement.h"
#include "NestingAxisSafety.h"
#include "SheetNestingNfp.h"
#include "SheetNestingConstructionTrace.h"
#include <clipper2/clipper.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <exception>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <tuple>

namespace iCAX::SheetNesting::Detail
{
// Private conservative proposal contours never replace original geometry.
// Fixed obstacles carry clearance; original material gates every returned pose.
class ClearanceNfpPlacement
{
    const Problem& Original;
    Problem Scene;
    Problem CommonScene;
    std::vector<std::size_t> Moving,Fixed;
    std::map<std::string,std::string> FixedIDs;
    std::map<std::string,std::size_t> OriginalIndices;
    std::set<std::string> ZeroGapObstacles;
    std::shared_ptr<NfpCache> Cache;
    std::vector<NfpPreparedShape> Prepared;
    std::vector<NfpPreparedShape> CommonPrepared;
    std::vector<NfpPreparedShape> MirroredPrepared,CommonMirroredPrepared;
    using RigidFixedKey=std::tuple<std::size_t,std::uint64_t,std::uint64_t,std::uint64_t,bool>;
    std::map<RigidFixedKey,std::shared_ptr<const AxisSafety::Geometry>> RigidFixedGeometryCache;
    std::size_t RigidFixedGeometryVertices=0;
    using RegionLoops=std::vector<std::vector<Point>>;
    struct RigidLoopKey
    {
        std::shared_ptr<const NfpRegion> Region;
        std::uint64_t X,Y,Angle;
        bool Mirrored;
        bool operator<(const RigidLoopKey& b)const
        {
            const std::owner_less<std::shared_ptr<const NfpRegion>> less;
            if(less(Region,b.Region))return true;
            if(less(b.Region,Region))return false;
            return std::make_tuple(Region.get(),X,Y,Angle,Mirrored)<std::make_tuple(b.Region.get(),b.X,b.Y,b.Angle,b.Mirrored);
        }
    };
    struct RigidLoopEntry{std::shared_ptr<const RegionLoops> Loops;std::size_t Bytes;};
    static constexpr std::size_t RigidLoopByteLimit=32*1024*1024,RigidLoopEntryLimit=256;
    std::map<RigidLoopKey,RigidLoopEntry> RigidLoopCache;
    std::size_t RigidLoopBytes=0,RigidLoopEvictions=0;
    std::unique_ptr<NfpPlacementGenerator> Placement;
    std::unique_ptr<NfpPlacementGenerator> CommonPlacement;
    bool InitializationFinished=false,InitializationInterrupted=false;
    std::shared_ptr<const AxisSafety::Geometry> RigidFixedGeometry(std::size_t index,Pose pose,bool& hit)
    {
        hit=false;
        if(index>=Original.Parts.size())return {};
        const bool cacheable=AxisSafety::ArithmeticSupported()&&std::isfinite(pose.X)&&
            std::isfinite(pose.Y)&&std::isfinite(pose.AngleDegrees);
        const RigidFixedKey key{index,std::bit_cast<std::uint64_t>(pose.X),std::bit_cast<std::uint64_t>(pose.Y),
            std::bit_cast<std::uint64_t>(pose.AngleDegrees),pose.Mirrored};
        if(cacheable)
            if(const auto found=RigidFixedGeometryCache.find(key);found!=RigidFixedGeometryCache.end())
            {hit=true;return found->second;}
        auto geometry=std::make_shared<const AxisSafety::Geometry>(AxisSafety::Make(Original.Parts[index].Geometry,pose));
        std::size_t vertices=std::max<std::size_t>(1,geometry->Hull.size());
        for(const auto& ring:geometry->Rings)
        {
            if(ring.size()>65536-std::min<std::size_t>(vertices,65536)){vertices=65537;break;}
            vertices+=ring.size();
        }
        // Only immutable original geometry is retained. Full stored-pose bit
        // keys preserve arbitrary-angle rounding, parity and signed zero;
        // capacity limits discard cached work, never a placement candidate.
        if(cacheable&&geometry->Valid&&RigidFixedGeometryCache.size()<4096&&vertices<=65536-RigidFixedGeometryVertices)
        {
            RigidFixedGeometryCache.emplace(key,geometry);
            RigidFixedGeometryVertices+=vertices;
        }
        return geometry;
    }
    static std::optional<std::size_t> RigidLoopPayloadBytes(const NfpRegion& region,const RegionLoops& loops)
    {
        // Charge the retained Region even when another cache shares ownership.
        // Counting it per entry is conservative when several frames share it.
        std::size_t bytes=sizeof(RigidLoopKey)+sizeof(RigidLoopEntry)+sizeof(NfpRegion)+sizeof(RegionLoops)+512;
        const auto add=[&](std::size_t count,std::size_t unit)
        {
            if(count>(RigidLoopByteLimit-bytes)/unit)return false;
            bytes+=count*unit;return true;
        };
        if(!add(region.Loops.capacity(),sizeof(std::vector<Point>))||!add(loops.capacity(),sizeof(std::vector<Point>)))return {};
        for(const auto& ring:region.Loops)if(!add(ring.capacity(),sizeof(Point)))return {};
        for(const auto& ring:loops)if(!add(ring.capacity(),sizeof(Point)))return {};
        return bytes;
    }
    std::shared_ptr<const RegionLoops> RigidTransformedLoops(const NfpLookup& lookup,const std::function<bool()>& stop,bool& hit)
    {
        hit=false;
        if(lookup.State!=NfpStatus::Ready||!lookup.Region||!AxisSafety::ArithmeticSupported()||
            !std::isfinite(lookup.Transform.X)||!std::isfinite(lookup.Transform.Y)||
            !std::isfinite(lookup.Transform.AngleDegrees)||(stop&&stop()))return {};
        const auto pose=lookup.Transform;
        const RigidLoopKey key{lookup.Region,std::bit_cast<std::uint64_t>(pose.X),std::bit_cast<std::uint64_t>(pose.Y),
            std::bit_cast<std::uint64_t>(pose.AngleDegrees),pose.Mirrored};
        if(const auto found=RigidLoopCache.find(key);found!=RigidLoopCache.end())
        {hit=true;return found->second.Loops;}
        if(!RigidLoopPayloadBytes(*lookup.Region,{}))return {};
        auto loops=std::make_shared<RegionLoops>();loops->reserve(lookup.Region->Loops.size());
        std::size_t vertices=0;
        for(const auto& ring:lookup.Region->Loops)
        {
            if((stop&&stop())||ring.size()>(RigidLoopByteLimit/sizeof(Point))-std::min(vertices,RigidLoopByteLimit/sizeof(Point)))return {};
            vertices+=ring.size();auto& transformed=loops->emplace_back();transformed.reserve(ring.size());
            for(std::size_t i=0;i<ring.size();++i)
            {
                if((i&63)==63&&stop&&stop())return {};
                const auto point=Transform(ring[i],pose);
                if(!std::isfinite(point.X)||!std::isfinite(point.Y))return {};
                transformed.push_back(point);
            }
        }
        const auto bytes=RigidLoopPayloadBytes(*lookup.Region,*loops);
        if(!bytes||(stop&&stop()))return {};
        // In-flight immutable loop handles survive eviction. The key owns its
        // Region, so allocator address reuse cannot masquerade as a cache hit.
        if(RigidLoopCache.size()>=RigidLoopEntryLimit||*bytes>RigidLoopByteLimit-RigidLoopBytes)
        {RigidLoopEvictions+=RigidLoopCache.size();RigidLoopCache.clear();RigidLoopBytes=0;}
        const auto [entry,inserted]=RigidLoopCache.emplace(key,RigidLoopEntry{loops,*bytes});
        if(inserted)RigidLoopBytes+=*bytes;
        return entry->second.Loops;
    }
public:
    ClearanceNfpPlacement(const Problem& source,const Stock& stock,std::shared_ptr<NfpCache> cache,
        const std::function<bool()>& stop,const std::set<std::string>& zeroGapObstacles={}):Original(source),
        ZeroGapObstacles(zeroGapObstacles),
        Cache(cache?std::move(cache):std::make_shared<NfpCache>(NfpCacheSettings{512,4096,32*1024*1024}))
    {
        // A deliberately unsupported ordinary scene may still have a usable
        // nominal/common scene. Only cancellation makes preparation retryable.
        struct InitializationGuard
        {
            bool& Finished;bool& Interrupted;
            ~InitializationGuard(){Finished=!Interrupted;}
        } initializationGuard{InitializationFinished,InitializationInterrupted};
        const auto interrupted=[&]
        {if(stop&&stop()){InitializationInterrupted=true;return true;}return false;};
        Scene=source;Scene.Stocks={stock};Scene.Parts.clear();Scene.PartGap=0;Scene.AllowCommonEdges=false;
        Moving.resize(source.Parts.size(),std::numeric_limits<std::size_t>::max());
        Fixed=Moving;
        std::set<std::string> reserved;
        for(std::size_t i=0;i<source.Parts.size();++i)
        {reserved.insert(source.Parts[i].ID);OriginalIndices.emplace(source.Parts[i].ID,i);}
        for(std::size_t i=0;i<source.Parts.size();++i)
        {
            if(interrupted())return;
            const auto& part=source.Parts[i];if(part.Material!=stock.Material)continue;
            Fixed[i]=Moving[i]=Scene.Parts.size();Scene.Parts.push_back(part);FixedIDs.emplace(part.ID,part.ID);
            auto& proposal=Scene.Parts.back().Geometry;
            if(proposal.Radius>0)
            {
                // Tangent sides at the four cardinal directions retain the
                // exact analytic bounding box. Between them this polygon is
                // outside the circle, so it is only a conservative proposal.
                constexpr std::size_t sides=64;
                const auto center=proposal.Center;const double radius=proposal.Radius/std::cos(Pi/sides);
                proposal={};
                for(std::size_t vertex=0;vertex<sides;++vertex)
                {
                    const double angle=(vertex+.5)*2*Pi/sides;
                    proposal.Outer.push_back({center.X+radius*std::cos(angle),center.Y+radius*std::sin(angle)});
                }
            }
            else
            {
                // General hole candidates use the real inner-domain service.
                // This private outer-only proposal retains only the exterior's
                // seam permission; Original still owns every material ring.
                proposal.Holes.clear();
                if(!proposal.CommonEdgeRings.empty())proposal.CommonEdgeRings.resize(1);
            }
        }
        if(source.AllowCommonEdges)
        {
            // The additional nominal domain proposes actual straight-edge
            // contacts. Every returned pose still passes Original.PartGap and
            // Original.AllowCommonEdges below; ordinary near pairs stay apart.
            CommonScene=Scene;CommonScene.AllowCommonEdges=true;
            CommonPrepared.resize(CommonScene.Parts.size());
            CommonMirroredPrepared.resize(CommonScene.Parts.size());
            try{CommonPlacement=std::make_unique<NfpPlacementGenerator>(CommonScene,Cache,interrupted);}
            catch(const std::exception&){CommonPlacement.reset();}
        }
        if(source.PartGap>0)
        {
            const auto count=Scene.Parts.size();
            if(std::count_if(Scene.Parts.begin(),Scene.Parts.end(),[&](const Part& part)
                {return !ZeroGapObstacles.contains(part.ID);})>512)return;
            // Offset rounding is conservative; original nominal clearance is
            // independently checked below before returning any actual pose.
            const double clearance=source.PartGap+std::max(1e-7,4*source.Tolerance);
            for(std::size_t i=0;i<count;++i)
            {
                if(interrupted())return;
                auto obstacle=Scene.Parts[i];
                // The typed domain caller has already eroded these immutable
                // zero-demand obstacles by the stock margin. They prohibit
                // entering the real domain exterior, not another part's gap.
                if(ZeroGapObstacles.contains(obstacle.ID))
                {if(obstacle.Quantity!=0)return;continue;}
                if(obstacle.Geometry.Radius>0||!obstacle.Geometry.Holes.empty())return;
                Clipper2Lib::PathD ring;for(const auto q:obstacle.Geometry.Outer)ring.emplace_back(q.X,q.Y);
                Clipper2Lib::PathsD paths;
                try
                {
                    const auto material=Clipper2Lib::Union(Clipper2Lib::PathsD{ring},Clipper2Lib::FillRule::EvenOdd,8);
                    paths=Clipper2Lib::InflatePaths(material,clearance,
                        Clipper2Lib::JoinType::Miter,Clipper2Lib::EndType::Polygon,2.,8);
                }
                catch(const std::exception&){return;}
                if(paths.size()!=1)return;
                obstacle.Geometry.Outer.clear();for(const auto q:paths.front())obstacle.Geometry.Outer.push_back({q.x,q.y});
                const auto originalID=obstacle.ID;obstacle.ID="__plane_clearance_"+std::to_string(i);
                while(reserved.contains(obstacle.ID))obstacle.ID+='_';
                reserved.insert(obstacle.ID);FixedIDs[originalID]=obstacle.ID;
                Fixed[OriginalIndices.at(originalID)]=Scene.Parts.size();
                obstacle.Quantity=0;Scene.Parts.push_back(std::move(obstacle));
            }
        }
        Prepared.resize(Scene.Parts.size());
        MirroredPrepared.resize(Scene.Parts.size());
        try{Placement=std::make_unique<NfpPlacementGenerator>(Scene,Cache,interrupted);}
        catch(const std::exception&){Placement.reset();}
    }
    bool Ready()const {return (Placement&&Placement->Ready())||(CommonPlacement&&CommonPlacement->Ready());}
    bool InitializationComplete()const {return InitializationFinished;}
    bool WasInterrupted()const {return InitializationInterrupted;}
    // Same relative-translation convention as NfpCache::Get. Only the fixed
    // private contour carries the gap; returned contacts remain proposals and
    // must be checked against the caller's unchanged original material.
    NfpLookup ContactRegion(std::size_t stationaryOriginalIndex,double stationaryAngle,
        std::size_t movingOriginalIndex,double movingAngle,const NfpBuildOptions& options={},bool commonEdge=false,
        bool stationaryMirrored=false,bool movingMirrored=false)
    {
        if(!Ready()||stationaryOriginalIndex>=Fixed.size()||movingOriginalIndex>=Moving.size())return {};
        if(!PoseAllowed(Original.Parts[stationaryOriginalIndex],{0,0,stationaryAngle,stationaryMirrored},Original.Tolerance)||
            !PoseAllowed(Original.Parts[movingOriginalIndex],{0,0,movingAngle,movingMirrored},Original.Tolerance))return {};
        if(commonEdge)
        {
            if(!CommonPlacement||!CommonPlacement->Ready())return {};
            const auto fixed=Moving[stationaryOriginalIndex],moving=Moving[movingOriginalIndex];
            if(fixed>=CommonScene.Parts.size()||moving>=CommonScene.Parts.size())return {};
            auto& a=stationaryMirrored?CommonMirroredPrepared[fixed]:CommonPrepared[fixed];
            auto& b=movingMirrored?CommonMirroredPrepared[moving]:CommonPrepared[moving];
            if(!a)a=Cache->Prepare(CommonScene.Parts[fixed].Geometry,options,stationaryMirrored);
            if(!b)b=Cache->Prepare(CommonScene.Parts[moving].Geometry,options,movingMirrored);
            return Cache->Get(a,stationaryAngle,b,movingAngle,options);
        }
        if(!Placement||!Placement->Ready())return {};
        const auto fixed=Fixed[stationaryOriginalIndex],moving=Moving[movingOriginalIndex];
        if(fixed>=Scene.Parts.size()||moving>=Scene.Parts.size())return {};
        auto& a=stationaryMirrored?MirroredPrepared[fixed]:Prepared[fixed];
        auto& b=movingMirrored?MirroredPrepared[moving]:Prepared[moving];
        if(!a)a=Cache->Prepare(Scene.Parts[fixed].Geometry,options,stationaryMirrored);
        if(!b)b=Cache->Prepare(Scene.Parts[moving].Geometry,options,movingMirrored);
        return Cache->Get(a,stationaryAngle,b,movingAngle,options);
    }
    NfpPlacementQuery FindRigid(const Layout& board,const std::vector<Member>& members,
        const NfpPlacementLimits& limits,const std::function<bool()>& stop)
    {
        if(!Ready()){NfpPlacementQuery unavailable;unavailable.Truncated=true;return unavailable;}
        auto settings=limits;
        std::size_t fixedGeometryHits=0,fixedGeometryMisses=0,regionLoopHits=0,regionLoopMisses=0;
        if(!settings.RigidFixedGeometry)
        {
            settings.RigidFixedGeometry=[&](std::size_t index,Pose pose)
            {
                bool hit=false;auto geometry=RigidFixedGeometry(index,pose,hit);
                if(hit)++fixedGeometryHits;else ++fixedGeometryMisses;
                return geometry;
            };
        }
        if(!settings.RigidRegionLoops)settings.RigidRegionLoops=[&](const NfpLookup& lookup)
        {
            bool hit=false;auto loops=RigidTransformedLoops(lookup,stop,hit);
            if(hit)++regionLoopHits;else ++regionLoopMisses;
            return loops;
        };
        if(limits.Trace)settings.Trace=[&](const ConstructionTraceEvent& raw)
        {
                if(raw.Stage==ConstructionTraceStage::Complete&&raw.Source=="nfp.rigid-placement")
                {
                    auto event=raw;
                    event.Measures.insert(event.Measures.end(),{{"fixedGeometryCacheHits",double(fixedGeometryHits)},
                        {"fixedGeometryCacheMisses",double(fixedGeometryMisses)},
                        {"fixedGeometryCacheEntries",double(RigidFixedGeometryCache.size())},
                        {"fixedGeometryCacheVertices",double(RigidFixedGeometryVertices)},
                        {"regionLoopCacheHits",double(regionLoopHits)},{"regionLoopCacheMisses",double(regionLoopMisses)},
                        {"regionLoopCacheEntries",double(RigidLoopCache.size())},{"regionLoopCacheBytes",double(RigidLoopBytes)},
                        {"regionLoopCacheEvictions",double(RigidLoopEvictions)}});
                    limits.Trace(event);
                }
                else limits.Trace(raw);
        };
        return FindRigidNfpPlacements(Original,board,members,settings,
            [&](std::size_t fixed,Pose fixedPose,std::size_t moving,Pose movingPose,bool common)
            {
                NfpBuildOptions build;build.Cancel=stop;
                return ContactRegion(fixed,fixedPose.AngleDegrees,moving,movingPose.AngleDegrees,build,common,
                    fixedPose.Mirrored,movingPose.Mirrored);
            },stop,ZeroGapObstacles);
    }
    NfpPlacementQuery Find(const Layout& board,std::size_t originalIndex,double angle,
        const NfpPlacementLimits& limits,const std::function<bool()>& stop)
    {
        using Clock=std::chrono::steady_clock;
        const auto wrapperStarted=Clock::now();
        double queryWindow=std::numeric_limits<double>::infinity();
        if(limits.RemainingTimeSeconds)try{queryWindow=std::max(0.,limits.RemainingTimeSeconds());}catch(...){queryWindow=0;}
        const bool timedQuery=std::isfinite(queryWindow);
        const auto windowExpired=[&](double fraction)
        {return timedQuery&&std::chrono::duration<double>(Clock::now()-wrapperStarted).count()>=queryWindow*fraction;};
        const std::function<bool()> extensionStop=[&]{return (stop&&stop())||windowExpired(1.);};
        std::function<bool()> activeStop=extensionStop;
        double directSourceSeconds=0,commonGeneratorSeconds=0,ordinaryGeneratorSeconds=0,originalGuardSeconds=0;
        std::size_t originalGuardCalls=0,directSourceAttempts=0;
        std::map<std::string,double> nativeQueryPhases;
        std::size_t nativePhaseQueries=0;
        double commonGapScreenSeconds=0;
        std::size_t commonGapScreenCalls=0,commonGapScreenPairs=0,commonGapScreenRejected=0,commonGapScreenUnknown=0;
        std::size_t guardStopped=0,guardCaller=0,guardSheet=0,guardContainment=0,guardUnknownID=0,
            guardMaterial=0,guardPair=0,guardAccepted=0;
        std::size_t nativeOriginalVerificationQueries=0,nativeOriginalVerificationCalls=0,nativeOriginalVerifiedFits=0;
        double commonDirectionScreenSeconds=0;
        std::size_t commonDirectionPairs=0,commonDirectionPotential=0,commonDirectionUnknown=0;
        bool commonDirectionScreened=false,commonDirectionComplete=false,commonSourceEdgesComplete=false,
            commonDomainSkippedByDirectionProof=false;
        struct GuardTimer
        {
            double& Total;Clock::time_point Started=Clock::now();
            ~GuardTimer(){Total+=std::chrono::duration<double>(Clock::now()-Started).count();}
        };
        bool underlyingTruncated=false;
        std::size_t activeStream=0;
        bool activeNativeOriginalVerification=false;
        const auto observe=[&](std::uint64_t id,Pose pose,bool accepted,const char* reason,
            std::size_t fixed=std::numeric_limits<std::size_t>::max(),double gap=0,
            ConstructionTraceStage stage=ConstructionTraceStage::Filter)noexcept
        {
            if(!limits.Trace)return;
            if(limits.TraceHealth&&limits.TraceHealth->Failed.load())
            {++limits.TraceHealth->Omitted;return;}
            try
            {
                ConstructionTraceEvent event;event.Stage=stage;
                event.Source="nfp.original-clearance";event.CandidateID=id;event.PartIndex=originalIndex;
                event.StockInstance=board.StockInstance;event.BoardWidth=board.Width;event.BoardHeight=board.Height;
                event.Accepted=accepted;event.Reason=reason;
                event.SearchTruncated=underlyingTruncated;
                if(id)event.Members.push_back({originalIndex,std::numeric_limits<std::size_t>::max(),pose,0});
                event.Measures={{"nominal-gap",Original.PartGap},{"edge-margin",Original.EdgeMargin},
                    {"tolerance",Original.Tolerance},{"pair-gap",gap},{"nominal-common-domain",double(activeStream)},
                    {"proposal-gap",0.},{"verification-gap",Original.PartGap},
                    {"native-original-verification",activeNativeOriginalVerification?1.:0.}};
                if(fixed!=std::numeric_limits<std::size_t>::max())event.Measures.emplace_back("fixed-original-index",double(fixed));
                limits.Trace(event);
            }
            catch(...)
            {
                if(limits.TraceHealth){limits.TraceHealth->Failed.store(true);++limits.TraceHealth->Omitted;}
                /* Observation cannot change the original-material gate. */
            }
        };
        // Blocked/overlap evidence must use original-material semantics before
        // a displacement solver can consume it. This service exposes safe fits.
        if(limits.MaxBlockers||!Ready()||originalIndex>=Moving.size()||Moving[originalIndex]==std::numeric_limits<std::size_t>::max())
        {observe(0,{},false,"original-clearance-query-unsupported");return {};}
        const auto& part=Original.Parts[originalIndex];
        if(!PoseAllowed(part,{0,0,angle,limits.Mirrored},Original.Tolerance))
        {observe(0,{},false,"original-clearance-disallowed-moving-orientation");return {};}
        for(const auto& fixed:board.Placements)
        {
            const auto found=OriginalIndices.find(fixed.PartID);
            if(found==OriginalIndices.end()||!PoseAllowed(Original.Parts[found->second],fixed.Transform,Original.Tolerance))
            {observe(0,{},false,"original-clearance-disallowed-fixed-orientation");return {};}
        }
        const auto identicalCommonOriginals=[&]
        {
            // The common scene may propose zero-gap contacts, but only a
            // byte-identical original polygon scene can publish its stronger
            // native gap certificate in place of this wrapper's material gate.
            if(!ZeroGapObstacles.empty()||limits.VerificationGap||!Original.AllowCommonEdges||
                !CommonScene.AllowCommonEdges||CommonScene.PartGap!=0||
                !std::isfinite(Original.PartGap)||Original.PartGap<=0||CommonScene.Stocks.size()!=1)return false;
            const auto sameNumber=[](double a,double b)
            {return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);};
            if(!sameNumber(CommonScene.EdgeMargin,Original.EdgeMargin)||!sameNumber(CommonScene.Tolerance,Original.Tolerance))return false;
            const auto& stock=CommonScene.Stocks.front();
            const auto originalStock=std::find_if(Original.Stocks.begin(),Original.Stocks.end(),[&](const auto& candidate)
                {return candidate.ID==board.StockID;});
            if(originalStock==Original.Stocks.end()||stock.ID!=board.StockID||stock.Material!=originalStock->Material||
                stock.Quantity!=originalStock->Quantity||!sameNumber(stock.Cost,originalStock->Cost)||
                !sameNumber(stock.Width,originalStock->Width)||!sameNumber(stock.Height,originalStock->Height)||
                !sameNumber(stock.Width,board.Width)||!sameNumber(stock.Height,board.Height))return false;
            const auto samePart=[&](std::size_t index)
            {
                if(index>=Moving.size()||Moving[index]>=CommonScene.Parts.size())return false;
                const auto& original=Original.Parts[index];const auto& common=CommonScene.Parts[Moving[index]];
                const auto& a=original.Geometry;const auto& b=common.Geometry;
                if(a.Radius!=0||b.Radius!=0||!a.Holes.empty()||!b.Holes.empty()||a.Outer.size()!=b.Outer.size()||
                    a.CommonEdgeRings!=b.CommonEdgeRings||!sameNumber(a.Radius,b.Radius)||
                    !sameNumber(a.Center.X,b.Center.X)||!sameNumber(a.Center.Y,b.Center.Y)||
                    original.ID!=common.ID||original.Material!=common.Material||original.Material!=stock.Material||
                    original.Quantity!=common.Quantity||original.FreeRotation!=common.FreeRotation||original.AllowMirror!=common.AllowMirror||
                    original.AllowedAnglesDegrees.size()!=common.AllowedAnglesDegrees.size())return false;
                for(std::size_t i=0;i<a.Outer.size();++i)
                    if(!sameNumber(a.Outer[i].X,b.Outer[i].X)||!sameNumber(a.Outer[i].Y,b.Outer[i].Y))return false;
                for(std::size_t i=0;i<original.AllowedAnglesDegrees.size();++i)
                    if(!sameNumber(original.AllowedAnglesDegrees[i],common.AllowedAnglesDegrees[i]))return false;
                return true;
            };
            if(!samePart(originalIndex))return false;
            for(const auto& fixed:board.Placements)
            {
                const auto found=OriginalIndices.find(fixed.PartID);
                if(found==OriginalIndices.end()||!samePart(found->second))return false;
            }
            return true;
        };
        struct SourceEdge{Point A,B,LocalA,LocalB;int MaterialSide=0;};
        struct SourceEdges{std::vector<SourceEdge> Edges;bool Complete=true;};
        std::map<std::size_t,SourceEdges> localCommonEdges;
        const auto commonEdges=[&](std::size_t index,Pose pose)
        {
            auto prepared=localCommonEdges.find(index);
            if(prepared==localCommonEdges.end())
            {
                SourceEdges preparedEdges;auto& edges=preparedEdges.Edges;
                const auto& shape=Original.Parts[index].Geometry;
                const auto append=[&](const std::vector<Point>& source,std::size_t ringIndex)
                {
                    if(!shape.CommonEdgeRings.empty()&&(ringIndex>=shape.CommonEdgeRings.size()||!shape.CommonEdgeRings[ringIndex]))return;
                    const auto ring=MaterialRing(source);int side=0;
                    if(ring.size()>=3&&AxisSafety::ArithmeticSupported())try
                    {
                        AxisSafety::Number area;
                        const AxisSafety::Vector origin{AxisSafety::Number(ring[0].X),AxisSafety::Number(ring[0].Y)};
                        for(std::size_t i=1;i+1<ring.size();++i)
                        {
                            const AxisSafety::Vector a{AxisSafety::Number(ring[i].X),AxisSafety::Number(ring[i].Y)},
                                b{AxisSafety::Number(ring[i+1].X),AxisSafety::Number(ring[i+1].Y)};
                            const auto u=a-origin,v=b-origin;area=area+u.X*v.Y-u.Y*v.X;
                        }
                        if(area.Valid)side=area.Sign()*(ringIndex? -1:1);
                    }
                    catch(const AxisSafety::ArithmeticUncertainty&){}
                    if(ring.size()>limits.MaxContactSegments-std::min(limits.MaxContactSegments,edges.size()))
                        preparedEdges.Complete=false;
                    for(std::size_t i=0;i<ring.size()&&edges.size()<limits.MaxContactSegments;++i)
                        edges.push_back({ring[i],ring[(i+1)%ring.size()],ring[i],ring[(i+1)%ring.size()],side});
                };
                append(shape.Outer,0);for(std::size_t i=0;i<shape.Holes.size();++i)append(shape.Holes[i],i+1);
                prepared=localCommonEdges.emplace(index,std::move(preparedEdges)).first;
            }
            auto edges=prepared->second.Edges;
            for(auto& edge:edges)
            {edge.A=Transform(edge.A,pose);edge.B=Transform(edge.B,pose);if(pose.Mirrored)edge.MaterialSide=-edge.MaterialSide;}
            return edges;
        };
        const auto sameMaterialSide=[&](const SourceEdge& a,const SourceEdge& b)
        {
            if(!a.MaterialSide||!b.MaterialSide||!AxisSafety::ArithmeticSupported())return false;
            try
            {
                const auto ax=AxisSafety::Number(a.B.X)-AxisSafety::Number(a.A.X),ay=AxisSafety::Number(a.B.Y)-AxisSafety::Number(a.A.Y),
                    bx=AxisSafety::Number(b.B.X)-AxisSafety::Number(b.A.X),by=AxisSafety::Number(b.B.Y)-AxisSafety::Number(b.A.Y);
                const auto dot=ax*bx+ay*by;
                return dot.Valid&&dot.Sign()*a.MaterialSide*b.MaterialSide>0;
            }
            catch(const AxisSafety::ArithmeticUncertainty&){return false;}
        };
        NfpPlacementQuery query;
        std::size_t beforeClearance=0;
        std::map<std::pair<std::size_t,std::uint64_t>,std::uint64_t> traceIDs;
        std::uint64_t nextTraceID=0;
        const auto traceID=[&](std::size_t stream,std::uint64_t local)noexcept
        {
            if(!limits.Trace||!local)return std::uint64_t{};
            try
            {const auto [found,inserted]=traceIDs.emplace(std::make_pair(stream,local),nextTraceID+1);
                if(inserted)++nextTraceID;return found->second;}
            catch(...)
            {if(limits.TraceHealth){limits.TraceHealth->Failed.store(true);++limits.TraceHealth->Omitted;}return std::uint64_t{};}
        };
        // Every stream checks the same current fixed originals. Preparing them
        // per candidate repeated exact hull construction even for remote parts.
        // This view is local to Find: edits, board changes and blocker ordering
        // never survive into a different query.
        struct OriginalFixed
        {
            std::size_t Index=std::numeric_limits<std::size_t>::max();
            std::optional<AxisSafety::Geometry> Geometry;
            std::optional<AxisSafety::Geometry> Boundary;
            std::vector<std::vector<AxisSafety::EdgeBounds>> BoundaryBoxes;
            std::vector<AxisSafety::EdgeIndex> BoundaryIndices;
        };
        std::vector<OriginalFixed> originalFixed(board.Placements.size());
        for(std::size_t i=0;i<board.Placements.size();++i)
        {
            const auto found=OriginalIndices.find(board.Placements[i].PartID);
            if(found!=OriginalIndices.end())originalFixed[i].Index=found->second;
        }
        std::optional<AxisSafety::Geometry> movingQuarter;
        const auto originalGeometry=[&](Pose pose)
        {
            const auto normalized=NormalizeAngle(pose.AngleDegrees);
            if(AxisSafety::ArithmeticSupported()&&part.Geometry.Outer.size()>32&&
                (normalized==0||normalized==90||normalized==180||normalized==270)&&
                AxisSafety::CoordinateBounds(AxisSafety::Number(pose.X)).Valid()&&
                AxisSafety::CoordinateBounds(AxisSafety::Number(pose.Y)).Valid())
            {
                if(!movingQuarter)movingQuarter=AxisSafety::Make(part.Geometry,{0,0,normalized,pose.Mirrored});
                if(movingQuarter->Valid&&movingQuarter->Envelope.X.Valid()&&movingQuarter->Envelope.Y.Valid())
                    return AxisSafety::TranslateQuarterTurn(*movingQuarter,{pose.X,pose.Y});
            }
            return AxisSafety::Make(part.Geometry,pose);
        };
        const auto commonGapBlocked=[&](Pose pose,std::size_t& blockedBy)
        {
            GuardTimer screenTime{commonGapScreenSeconds};++commonGapScreenCalls;
            if(!AxisSafety::ArithmeticSupported()||part.Geometry.Radius>0)
            {++commonGapScreenUnknown;return false;}
            const auto moving=AxisSafety::MakeBoundary(part.Geometry,pose);
            if(!moving.Valid||moving.Rings.empty())
            {++commonGapScreenUnknown;return false;}
            const auto movingBoxes=AxisSafety::BoundsForEdges(moving);
            bool uncertain=false;
            for(std::size_t oi=0;oi<board.Placements.size();++oi)
            {
                auto& prepared=originalFixed[oi];
                if(prepared.Index==std::numeric_limits<std::size_t>::max())
                {uncertain=true;continue;}
                const auto& obstacle=Original.Parts[prepared.Index];
                const double gap=ZeroGapObstacles.contains(obstacle.ID)?0:Original.PartGap;
                if(!(gap>0))continue;
                ++commonGapScreenPairs;
                if(obstacle.Geometry.Radius>0||!std::isfinite(gap))
                {uncertain=true;continue;}
                if(!prepared.Boundary)
                {
                    prepared.Boundary=AxisSafety::MakeBoundary(obstacle.Geometry,board.Placements[oi].Transform);
                    prepared.BoundaryBoxes=AxisSafety::BoundsForEdges(*prepared.Boundary);
                    prepared.BoundaryIndices.reserve(prepared.BoundaryBoxes.size());
                    for(const auto& boxes:prepared.BoundaryBoxes)prepared.BoundaryIndices.emplace_back(boxes);
                }
                const auto& fixed=*prepared.Boundary;
                if(!fixed.Valid||fixed.Rings.empty())
                {uncertain=true;continue;}
                try
                {
                    const AxisSafety::Number required(gap);
                    if(AxisSafety::EnvelopesSeparated(moving,fixed,required))continue;
                    bool belowGap=false;
                    for(std::size_t r=0;r<moving.Rings.size()&&!belowGap;++r)
                    {
                        const auto& ar=moving.Rings[r];
                        for(std::size_t i=0;i<ar.size()&&!belowGap;++i)
                            for(std::size_t s=0;s<fixed.Rings.size()&&!belowGap;++s)
                            {
                                const auto& br=fixed.Rings[s];
                                const auto candidates=prepared.BoundaryIndices[s].Query(movingBoxes[r][i],gap);
                                for(const auto j:candidates)
                                {
                                    if(AxisSafety::BoundsClear(movingBoxes[r][i],prepared.BoundaryBoxes[s][j],gap))continue;
                                    const auto& p=ar[i];const auto& q=ar[(i+1)%ar.size()];
                                    const auto& u=br[j];const auto& v=br[(j+1)%br.size()];
                                    if(!AxisSafety::PointSegmentClear(p,u,v,required)||
                                        !AxisSafety::PointSegmentClear(q,u,v,required)||
                                        !AxisSafety::PointSegmentClear(u,p,q,required)||
                                        !AxisSafety::PointSegmentClear(v,p,q,required))
                                    {belowGap=true;break;}
                                }
                            }
                    }
                    if(!belowGap)continue;
                    // The complete original guard can waive this pair's gap
                    // only for an authorized exact shared edge. Use that same
                    // predicate on every original material ring, without the
                    // direct-proposal segment cap. This is a rejection proof
                    // for this stored pose only; nearby retries are untouched.
                    if(AxisSafety::CommonEdge(moving,fixed,Original.Tolerance,true))continue;
                    blockedBy=prepared.Index;++commonGapScreenRejected;return true;
                }
                catch(const AxisSafety::ArithmeticUncertainty&){uncertain=true;}
            }
            if(uncertain)++commonGapScreenUnknown;
            return false;
        };
        const auto rejected=[&](Pose pose,std::uint64_t id)
        {
            GuardTimer guardTime{originalGuardSeconds};++originalGuardCalls;
            if(activeStop&&activeStop()){++guardStopped;observe(id,pose,false,"stopped-before-original-clearance");return true;}
            if(pose.Mirrored!=limits.Mirrored||!PoseAllowed(part,pose,Original.Tolerance))
            {++guardCaller;observe(id,pose,false,"original-clearance-disallowed-returned-orientation");return true;}
            if(!InsideSheet(part.Geometry,pose,board.Width,board.Height,Original.EdgeMargin,Original.Tolerance))
            {++guardSheet;observe(id,pose,false,"original-clearance-outside-sheet");return true;}
            if(activeStream==1&&Original.AllowCommonEdges&&Original.PartGap>0)
            {
                std::size_t blockedBy=std::numeric_limits<std::size_t>::max();
                if(commonGapBlocked(pose,blockedBy))
                {observe(id,pose,false,"original-clearance-common-gap-witness",blockedBy,Original.PartGap);return true;}
            }
            const auto geometry=originalGeometry(pose);
            const auto containment=AxisSafety::Containment(geometry,board.Width,board.Height,Original.EdgeMargin);
            if(containment!=AxisSafety::Verdict::Safe)
            {++guardContainment;observe(id,pose,false,containment==AxisSafety::Verdict::Uncertain?
                "original-clearance-containment-uncertain":"original-clearance-containment-blocked");return true;}
            for(std::size_t i=0;i<board.Placements.size();++i)
            {
                const auto& fixed=board.Placements[i];auto& prepared=originalFixed[i];
                if(prepared.Index==std::numeric_limits<std::size_t>::max())
                {++guardUnknownID;observe(id,pose,false,"original-clearance-unknown-original-id");return true;}
                const auto& obstacle=Original.Parts[prepared.Index];
                if(!prepared.Geometry)prepared.Geometry=AxisSafety::Make(obstacle.Geometry,fixed.Transform);
                const double gap=ZeroGapObstacles.contains(obstacle.ID)?0:Original.PartGap;
                if(AxisSafety::EnvelopesSeparated(geometry,*prepared.Geometry,
                    AxisSafety::Clearance(geometry,*prepared.Geometry,gap)))continue;
                if(ShapesConflict(part.Geometry,pose,obstacle.Geometry,fixed.Transform,gap,Original.Tolerance,Original.AllowCommonEdges))
                {++guardMaterial;observe(id,pose,false,"original-clearance-material-conflict",prepared.Index,gap);return true;}
                const auto verdict=AxisSafety::Pair(geometry,*prepared.Geometry,gap,Original.AllowCommonEdges,Original.Tolerance);
                if(verdict!=AxisSafety::Verdict::Safe)
                {++guardPair;observe(id,pose,false,verdict==AxisSafety::Verdict::Uncertain?
                    "original-clearance-pair-uncertain":"original-clearance-pair-blocked",prepared.Index,gap);return true;}
            }
            // An additional caller gate may retain a certified score as a
            // later rejection threshold. Never invoke it on an original pose
            // which this wrapper has not yet completely certified.
            if(limits.AcceptFit&&!limits.AcceptFit(pose,id))
            {++guardCaller;observe(id,pose,false,"caller-additional-fit-guard-rejected");return true;}
            ++guardAccepted;
            observe(id,pose,true,"original-clearance-fit");
            return false;
        };
        std::set<std::tuple<double,double,double,bool>> retained;
        const auto run=[&](NfpPlacementGenerator& generator,const Layout& environment,NfpPlacementLimits settings,std::size_t stream,
            const std::function<bool()>& stageStop)
        {
            activeStream=stream;
            activeStop=stageStop;
            // A stream-local geometric fit cap must not discard a pose before
            // the wrapper compares the caller's final stored-coordinate score.
            if(settings.CandidatePriority&&!settings.StopAfterFirstFit&&!settings.StopAfterCandidateLimit)settings.MaxCandidates=0;
            const bool nativeOriginalVerification=stream==1&& &generator==CommonPlacement.get()&&
                &environment==&board&&identicalCommonOriginals();
            activeNativeOriginalVerification=nativeOriginalVerification;
            if(nativeOriginalVerification)
            {settings.VerificationGap=Original.PartGap;++nativeOriginalVerificationQueries;}
            if(nativeOriginalVerification&&Original.AllowCommonEdges&&Original.PartGap>0)
            {
                // A necessary gap witness can reject before the native deep
                // gate, never approve a fit. Unproved poses retain that gate;
                // the query keeps all established finite retreat proposals.
                settings.NecessaryFit=[&,stream](Pose pose,std::uint64_t nativeID)
                {
                    const auto id=traceID(stream,nativeID);
                    GuardTimer guardTime{originalGuardSeconds};
                    if(limits.NecessaryFit&&!limits.NecessaryFit(pose,id))
                    {
                        ++originalGuardCalls;++guardCaller;
                        observe(id,pose,false,"caller-necessary-fit-guard-rejected");return false;
                    }
                    std::size_t blockedBy=std::numeric_limits<std::size_t>::max();
                    bool blocked=false;
                    try{blocked=commonGapBlocked(pose,blockedBy);}
                    catch(...){++commonGapScreenUnknown;return true;}
                    if(blocked)
                    {
                        ++originalGuardCalls;
                        observe(id,pose,false,"original-clearance-common-gap-witness",blockedBy,Original.PartGap);return false;
                    }
                    return true;
                };
            }
            const bool originalGuardInQuery=nativeOriginalVerification||settings.StopAfterCandidateLimit||bool(settings.AcceptFit)||
                (settings.StopAfterFirstFit&&bool(settings.CandidatePriority));
            if(originalGuardInQuery)
            {
                // A private fit is insufficient for either a first-fit stop
                // or a caller's dynamic certified-score threshold. Complete
                // the unchanged original-material gate inside the stream.
                settings.AcceptFit=[&,stream,nativeOriginalVerification](Pose pose,std::uint64_t nativeID)
                {
                    const auto id=traceID(stream,nativeID);
                    if(nativeOriginalVerification)
                    {
                        // Native verification used these same original rings,
                        // fixed poses, stock, margin, tolerance and common-edge
                        // permissions with Original.PartGap. Keep only the
                        // caller's additional gate; do not rebuild and recheck
                        // the identical material a second time.
                        GuardTimer guardTime{originalGuardSeconds};++originalGuardCalls;++nativeOriginalVerificationCalls;
                        if(activeStop&&activeStop()){++guardStopped;observe(id,pose,false,"stopped-before-original-clearance");return false;}
                        if(pose.Mirrored!=limits.Mirrored||!PoseAllowed(part,pose,Original.Tolerance))
                        {++guardCaller;observe(id,pose,false,"original-clearance-disallowed-returned-orientation");return false;}
                        if(limits.AcceptFit&&!limits.AcceptFit(pose,id))
                        {++guardCaller;observe(id,pose,false,"caller-additional-fit-guard-rejected");return false;}
                        ++guardAccepted;++nativeOriginalVerifiedFits;
                        observe(id,pose,true,"original-clearance-fit",std::numeric_limits<std::size_t>::max(),Original.PartGap);return true;
                    }
                    return !rejected(pose,id);
                };
            }
            else settings.AcceptFit={}; // The same complete wrapper gate runs below.
            // A nominal common-edge stream must retain its representable seam
            // retreat order. Prefer an inward domain proposal only for the
            // ordinary conservative-clearance stream.
            if(stream)settings.PrioritizeFreeDomainRetreat=false;
            if(limits.Trace)settings.Trace=[&,stream](const ConstructionTraceEvent& native)
            {
                // Accumulate before forwarding: a report may sample the many
                // inner query events but retain this wrapper's final summary.
                if(native.Source=="nfp.query"&&native.Stage==ConstructionTraceStage::Complete)
                {
                    ++nativePhaseQueries;
                    for(const auto& [name,value]:native.Measures)
                        if(name.starts_with("phase")&&name!="phaseTimesNested")nativeQueryPhases[name]+=value;
                }
                auto event=native;event.CandidateID=traceID(stream,native.CandidateID);
                event.PartIndex=originalIndex;
                for(auto& member:event.Members)member.PartIndex=originalIndex;
                for(auto& measure:event.Measures)
                    if((measure.first=="duplicate-of"||measure.first=="parent-candidate-id")&&measure.second>0)
                        measure.second=double(traceID(stream,static_cast<std::uint64_t>(measure.second)));
                event.Measures.emplace_back("nominal-common-domain",double(stream));
                limits.Trace(event);
            };
            const auto generatorStarted=Clock::now();
            auto proposed=generator.Find(environment,Moving[originalIndex],angle,settings,stageStop);
            (stream?commonGeneratorSeconds:ordinaryGeneratorSeconds)+=
                std::chrono::duration<double>(Clock::now()-generatorStarted).count();
            query.Attempts+=proposed.Attempts;query.Generated+=proposed.Generated;
            query.Supported=query.Supported||proposed.Supported;query.Truncated=query.Truncated||proposed.Truncated;
            underlyingTruncated=query.Truncated;beforeClearance+=proposed.Fits.size();
            if(!stream){query.FreeTranslationArea=proposed.FreeTranslationArea;query.DomainMeasured=proposed.DomainMeasured;}
            const bool haveIDs=proposed.FitsIDs.size()==proposed.Fits.size();
            for(std::size_t i=0;i<proposed.Fits.size();++i)
            {
                const auto pose=proposed.Fits[i];const auto id=haveIDs?traceID(stream,proposed.FitsIDs[i]):0;
                if(!originalGuardInQuery&&rejected(pose,id))continue;
                if(!retained.emplace(pose.X,pose.Y,pose.AngleDegrees,pose.Mirrored).second)
                {observe(id,pose,false,"original-clearance-duplicate-fit");continue;}
                query.Fits.push_back(pose);if(limits.Trace)query.FitsIDs.push_back(id);
            }
        };
        // Spend only part of a bounded query on nominal contacts, keeping a
        // normal-clearance stream available when no shared edge can be made.
        const bool common=limits.IncludeCommonEdgeProposals&&Original.AllowCommonEdges&&part.Geometry.Radius<=0&&CommonPlacement&&CommonPlacement->Ready()&&
            limits.MaxAttempts>1&&limits.MaxRawCandidates>1&&std::any_of(board.Placements.begin(),board.Placements.end(),[&](const auto& fixed)
            {const auto found=OriginalIndices.find(fixed.PartID);return found!=OriginalIndices.end()&&Original.Parts[found->second].Geometry.Radius<=0;});
        // The ordinary contour domain is the basic placement opportunity.
        // Every stream has its own candidate allowance; only the final merged
        // caller-scored pool applies MaxCandidates.
        if(Placement&&Placement->Ready())
        {
            auto environment=board;
            for(auto& member:environment.Placements)
            {const auto found=FixedIDs.find(member.PartID);if(found==FixedIDs.end())
                {observe(0,{},false,"original-clearance-unknown-fixed-id");return {};}member.PartID=found->second;}
            auto settings=limits;
            if(common)
            {settings.MaxAttempts=limits.MaxAttempts/2+limits.MaxAttempts%2;
                settings.MaxRawCandidates=limits.MaxRawCandidates/2+limits.MaxRawCandidates%2;}
            const std::function<bool()> basicStop=[&]{return (stop&&stop())||windowExpired(common?.75:1.);};
            run(*Placement,environment,std::move(settings),0,basicStop);
        }
        activeStop=extensionStop;
        if(common&&Original.PartGap>0&&!extensionStop())
        {
            const auto screenStarted=Clock::now();commonDirectionScreened=true;
            const auto noPossibleCommonDirection=[&]
            {
                if(!AxisSafety::ArithmeticSupported()) {++commonDirectionUnknown;return false;}
                const Pose movingPose{0,0,angle,limits.Mirrored};
                const auto movingEdges=commonEdges(originalIndex,movingPose);
                bool edgesComplete=localCommonEdges.at(originalIndex).Complete;
                // A capped direct-proposal edge list cannot certify absence.
                // Prepare and check every fixed source before using any list.
                for(const auto& fixed:board.Placements)
                {
                    if(extensionStop()){++commonDirectionUnknown;return false;}
                    const auto found=OriginalIndices.find(fixed.PartID);
                    if(found==OriginalIndices.end()){++commonDirectionUnknown;return false;}
                    if(Original.Parts[found->second].Geometry.Radius>0)continue;
                    commonEdges(found->second,fixed.Transform);
                    edgesComplete=edgesComplete&&localCommonEdges.at(found->second).Complete;
                }
                commonSourceEdgesComplete=edgesComplete;
                if(!commonSourceEdgesComplete){++commonDirectionUnknown;return false;}
                struct Direction{AxisSafety::Interval X,Y;int MaterialSide=0;};
                const auto direction=[&](const SourceEdge& edge,Pose pose,bool moving)
                {
                    const auto normalized=NormalizeAngle(pose.AngleDegrees);
                    const bool quarter=normalized==0||normalized==90||normalized==180||normalized==270;
                    // AxisSafety keeps quarter-turn additions exact. Its
                    // direction therefore cancels translation before rounding.
                    const auto a=quarter?Transform(edge.LocalA,{0,0,pose.AngleDegrees,pose.Mirrored}):edge.A;
                    const auto b=quarter?Transform(edge.LocalB,{0,0,pose.AngleDegrees,pose.Mirrored}):edge.B;
                    Direction result{AxisSafety::CoordinateBounds(AxisSafety::Number(b.X)-AxisSafety::Number(a.X)),
                        AxisSafety::CoordinateBounds(AxisSafety::Number(b.Y)-AxisSafety::Number(a.Y)),
                        quarter?edge.MaterialSide:0};
                    if(moving&&!quarter)
                    {
                        // The final translated binary64 endpoints can differ
                        // from the zero-translation direction. Enclose that
                        // rounding, including cancellation at source origins;
                        // this interval never relaxes the final material gate.
                        const double scale=std::max({1.,std::abs(board.Width),std::abs(board.Height),
                            std::abs(edge.LocalA.X),std::abs(edge.LocalA.Y),std::abs(edge.LocalB.X),std::abs(edge.LocalB.Y)});
                        const double error=std::nextafter(128*std::numeric_limits<double>::epsilon()*scale,
                            std::numeric_limits<double>::infinity());
                        result.X=result.X+AxisSafety::Interval{-error,error};
                        result.Y=result.Y+AxisSafety::Interval{-error,error};
                    }
                    return result;
                };
                std::vector<Direction> movingDirections;movingDirections.reserve(movingEdges.size());
                for(const auto& edge:movingEdges)movingDirections.push_back(direction(edge,movingPose,true));
                for(const auto& fixed:board.Placements)
                {
                    if(extensionStop()){++commonDirectionUnknown;return false;}
                    const auto index=OriginalIndices.at(fixed.PartID);
                    if(Original.Parts[index].Geometry.Radius>0)continue;
                    for(const auto& edge:commonEdges(index,fixed.Transform))
                    {
                        const auto stationary=direction(edge,fixed.Transform,false);
                        for(const auto& moving:movingDirections)
                        {
                            if(extensionStop()){++commonDirectionUnknown;return false;}
                            ++commonDirectionPairs;
                            const auto cross=AxisSafety::IntervalSign(stationary.X*moving.Y-stationary.Y*moving.X);
                            if(cross&&*cross!=0)continue;
                            if(stationary.MaterialSide&&moving.MaterialSide)
                            {
                                const auto dot=AxisSafety::IntervalSign(stationary.X*moving.X+stationary.Y*moving.Y);
                                if(dot&&*dot*stationary.MaterialSide*moving.MaterialSide>0)continue;
                            }
                            // Parallel, possibly parallel, unknown side and
                            // unsupported arithmetic all retain the full stream.
                            ++commonDirectionPotential;return false;
                        }
                    }
                }
                commonDirectionComplete=true;return true;
            };
            try{commonDomainSkippedByDirectionProof=noPossibleCommonDirection();}
            catch(...){++commonDirectionUnknown;}
            commonDirectionScreenSeconds=std::chrono::duration<double>(Clock::now()-screenStarted).count();
            if(limits.Trace&&(!limits.TraceHealth||!limits.TraceHealth->Failed.load()))try
            {
                ConstructionTraceEvent event;event.Stage=ConstructionTraceStage::Analysis;
                event.Source="nfp.original-clearance";event.PartIndex=originalIndex;
                event.StockInstance=board.StockInstance;event.BoardWidth=board.Width;event.BoardHeight=board.Height;
                event.Accepted=commonDomainSkippedByDirectionProof;
                event.Reason=commonDomainSkippedByDirectionProof?"common-domain-no-authorized-opposite-direction-proof":
                    "common-domain-direction-possible-or-unproved";
                event.Measures={{"source-edges-complete",commonSourceEdgesComplete?1.:0.},
                    {"direction-screen-complete",commonDirectionComplete?1.:0.},{"direction-pairs",double(commonDirectionPairs)},
                    {"potential-direction-pairs",double(commonDirectionPotential)},{"unknown",double(commonDirectionUnknown)}};
                limits.Trace(event);
            }
            catch(...){if(limits.TraceHealth){limits.TraceHealth->Failed.store(true);++limits.TraceHealth->Omitted;}}
        }
        if(common&&!commonDomainSkippedByDirectionProof&&!extensionStop())
        {
            const auto directStarted=Clock::now();
            // Work directly with source vertices as well as the quantized NFP
            // domain. A clipper-grid translation need not preserve a genuinely
            // collinear seam at an arbitrary allowed angle.
            activeStream=2;
            activeNativeOriginalVerification=false;
            const auto directBeginAttempts=query.Attempts;
            const auto directAttemptEnd=query.Attempts+std::min(std::max<std::size_t>(1,limits.MaxAttempts/4),
                limits.MaxAttempts-std::min(limits.MaxAttempts,query.Attempts));
            const auto directRawEnd=query.Generated+std::min(std::max<std::size_t>(1,limits.MaxRawCandidates/4),
                limits.MaxRawCandidates-std::min(limits.MaxRawCandidates,query.Generated));
            const auto movingEdges=commonEdges(originalIndex,{0,0,angle,limits.Mirrored});
            std::set<std::tuple<double,double,bool>> rawSeen;std::uint64_t rawID=0;
            std::size_t referencePairs=0,shortlistedPairs=0,sameSidePairs=0;
            const auto done=[&]
            {return query.Attempts>=directAttemptEnd||query.Generated>=directRawEnd||extensionStop();};
            for(const auto& fixed:board.Placements)
            {
                if(done())break;
                const auto found=OriginalIndices.find(fixed.PartID);if(found==OriginalIndices.end())continue;
                const auto& shape=Original.Parts[found->second].Geometry;if(shape.Radius>0)continue;
                const auto fixedEdges=commonEdges(found->second,fixed.Transform);
                // The unchanged parallel test permits sin(delta) <= tolerance /
                // min(lengths). A conservative direction bucket width and two
                // neighbours on either side retain every possible match. Tiny
                // or unsupported directions retain the complete original scan.
                double shortest=std::numeric_limits<double>::infinity();bool finiteDirections=true;
                for(const auto* edges:{&movingEdges,&fixedEdges})for(const auto& e:*edges)
                {
                    const auto length=std::hypot(e.B.X-e.A.X,e.B.Y-e.A.Y);
                    finiteDirections=finiteDirections&&std::isfinite(length);
                    if(length>Original.Tolerance)shortest=std::min(shortest,length);
                }
                const bool indexed=AxisSafety::ArithmeticSupported()&&finiteDirections&&std::isfinite(shortest)&&
                    Original.Tolerance/shortest<.25;
                const double binWidth=indexed?std::max(1e-8,2*std::asin(Original.Tolerance/shortest)+64*std::numeric_limits<double>::epsilon()):Pi;
                const auto bins=std::max<long long>(1,static_cast<long long>(std::floor(Pi/binWidth)));
                const auto bucket=[&](const SourceEdge& e)
                {
                    double direction=std::fmod(std::atan2(e.B.Y-e.A.Y,e.B.X-e.A.X)+Pi,Pi);
                    if(direction<0)direction+=Pi;
                    return std::min(bins-1,static_cast<long long>(direction/Pi*bins));
                };
                std::map<long long,std::vector<std::size_t>> directions;
                if(indexed)for(std::size_t i=0;i<movingEdges.size();++i)directions[bucket(movingEdges[i])].push_back(i);
                referencePairs+=fixedEdges.size()*movingEdges.size();
                for(const auto& edge:fixedEdges)
                {
                    if(done())break;
                    const auto ax=edge.B.X-edge.A.X,ay=edge.B.Y-edge.A.Y,al=std::hypot(ax,ay);
                    if(al<=Original.Tolerance)continue;
                    std::vector<std::size_t> matching;
                    if(indexed)
                    {
                        const auto bin=bucket(edge);
                        for(long long offset=-2;offset<=2;++offset)
                        {
                            const auto key=((bin+offset)%bins+bins)%bins;
                            if(const auto fit=directions.find(key);fit!=directions.end())matching.insert(matching.end(),fit->second.begin(),fit->second.end());
                        }
                        std::sort(matching.begin(),matching.end());matching.erase(std::unique(matching.begin(),matching.end()),matching.end());
                    }
                    else{matching.resize(movingEdges.size());std::iota(matching.begin(),matching.end(),0);}
                    for(const auto movingIndex:matching)
                    {
                        if(done())break;
                        ++shortlistedPairs;const auto& moving=movingEdges[movingIndex];
                        const auto bx=moving.B.X-moving.A.X,by=moving.B.Y-moving.A.Y,bl=std::hypot(bx,by);
                        if(bl<=Original.Tolerance||std::abs(ax*by-ay*bx)>Original.Tolerance*std::max(al,bl))continue;
                        if(sameMaterialSide(edge,moving)){++sameSidePairs;continue;}
                        const Point anchors[]{edge.A,edge.B,{(edge.A.X+edge.B.X)/2,(edge.A.Y+edge.B.Y)/2}};
                        const Point offsets[]{moving.A,moving.B,{(moving.A.X+moving.B.X)/2,(moving.A.Y+moving.B.Y)/2}};
                        for(const auto anchor:anchors)for(const auto offset:offsets)
                        {
                            if(done())break;
                            const Pose pose{anchor.X-offset.X,anchor.Y-offset.Y,angle,limits.Mirrored};
                            ++rawID;
                            if(!rawSeen.emplace(pose.X,pose.Y,pose.Mirrored).second)
                            {observe(0,pose,false,"duplicate-source-edge-proposal",found->second,Original.PartGap,ConstructionTraceStage::Analysis);continue;}
                            const auto id=traceID(2,rawID);
                            observe(id,pose,true,"source-edge-alignment-proposal",found->second,Original.PartGap,ConstructionTraceStage::Generate);
                            ++query.Generated;++query.Attempts;++beforeClearance;query.Supported=true;
                            if(rejected(pose,id))continue;
                            if(!retained.emplace(pose.X,pose.Y,pose.AngleDegrees,pose.Mirrored).second)
                            {observe(id,pose,false,"original-clearance-duplicate-fit");continue;}
                            query.Fits.push_back(pose);if(limits.Trace)query.FitsIDs.push_back(id);
                        }
                    }
                }
            }
            if(query.Attempts>=directAttemptEnd||query.Generated>=directRawEnd||extensionStop())
                query.Truncated=true;
            if(limits.Trace)try
            {
                ConstructionTraceEvent event;event.Stage=ConstructionTraceStage::Analysis;event.Source="nfp.source-edge-summary";
                event.PartIndex=originalIndex;event.StockInstance=board.StockInstance;event.Accepted=true;
                event.Reason="authorized-rings-conservative-direction-and-exact-material-side";
                event.Measures={{"reference-edge-pairs",double(referencePairs)},{"shortlisted-edge-pairs",double(shortlistedPairs)},
                    {"same-material-side-pairs-rejected",double(sameSidePairs)},{"direct-raw-proposals",double(rawID)},
                    {"direct-unique-proposals",double(rawSeen.size())},{"direct-duplicate-proposals",double(rawID-rawSeen.size())}};
                limits.Trace(event);
            }
            catch(...){if(limits.TraceHealth){limits.TraceHealth->Failed.store(true);++limits.TraceHealth->Omitted;}}
            directSourceSeconds=std::chrono::duration<double>(Clock::now()-directStarted).count();
            directSourceAttempts=query.Attempts-directBeginAttempts;
            auto settings=limits;
            const auto attempts=limits.MaxAttempts-std::min(limits.MaxAttempts,query.Attempts);
            const auto raw=limits.MaxRawCandidates-std::min(limits.MaxRawCandidates,query.Generated);
            settings.MaxAttempts=attempts;settings.MaxRawCandidates=raw;
            if(settings.MaxAttempts&&settings.MaxRawCandidates&&!extensionStop())
                run(*CommonPlacement,board,std::move(settings),1,extensionStop);
        }
        else if(common&&!commonDomainSkippedByDirectionProof)query.Truncated=true;
        // Merge competing original-safe streams before applying the global fit
        // cap. A nominal fit can never suppress the ordinary contour query.
        if(limits.CandidatePriority&&!query.Fits.empty())
        {
            std::vector<std::size_t> order(query.Fits.size());std::iota(order.begin(),order.end(),0);
            std::vector<std::array<double,4>> scores;
            for(const auto pose:query.Fits)
            {
                const auto score=limits.CandidatePriority(pose);
                if(std::any_of(score.begin(),score.end(),[](double value){return !std::isfinite(value);}))
                {
                    query.Supported=false;query.Truncated=true;query.Fits.clear();query.FitsIDs.clear();
                    observe(0,pose,false,"nonfinite-merged-caller-priority");return query;
                }
                scores.push_back(score);
            }
            std::stable_sort(order.begin(),order.end(),[&](auto a,auto b)
            {return std::tie(scores[a],query.Fits[a].X,query.Fits[a].Y)<std::tie(scores[b],query.Fits[b].X,query.Fits[b].Y);});
            const auto fits=query.Fits;const auto ids=query.FitsIDs;const bool haveIDs=ids.size()==fits.size();
            for(std::size_t i=0;i<order.size();++i){query.Fits[i]=fits[order[i]];if(haveIDs)query.FitsIDs[i]=ids[order[i]];}
        }
        if(limits.MaxCandidates&&query.Fits.size()>limits.MaxCandidates)
        {query.Truncated=true;query.Fits.resize(limits.MaxCandidates);if(!query.FitsIDs.empty())query.FitsIDs.resize(limits.MaxCandidates);}
        if(limits.IncludeCommonEdgeProposals&&board.Placements.empty()&&Original.AllowCommonEdges&&part.Quantity>1&&part.Geometry.Radius<=0&&
            std::remainder(angle,90.)!=0&&!query.Fits.empty()&&limits.MaxContactSegments)
        {
            // Before committing the first copy, test nearby representable
            // translations for an actual same-source common-edge continuation.
            // Translating a rotated polygon can change the final F64 seam;
            // no committed part, angle, clearance or certificate is changed.
            activeStream=3;
            activeNativeOriginalVerification=false;
            const auto attemptEnd=query.Attempts+std::min<std::size_t>(512,
                (limits.MaxAttempts-std::min(limits.MaxAttempts,query.Attempts))/16);
            const auto rawEnd=query.Generated+std::min<std::size_t>(512,
                (limits.MaxRawCandidates-std::min(limits.MaxRawCandidates,query.Generated))/16);
            const auto exhausted=[&]
            {return query.Attempts>=attemptEnd||query.Generated>=rawEnd||extensionStop();};
            const auto edges=[&](Pose pose)
            {return commonEdges(originalIndex,pose);};
            const auto movingEdges=edges({0,0,angle,limits.Mirrored});
            const auto initialCount=std::min<std::size_t>(4,query.Fits.size());
            const std::vector<Pose> originalSeeds(query.Fits.begin(),query.Fits.begin()+initialCount);
            std::uint64_t rawID=0;
            std::set<std::tuple<double,double,double,bool>> phaseSeen;
            for(const auto seed:originalSeeds)
            {
                if(exhausted())break;
                // Move toward the existing low/left seed rule first. ULP
                // counts are representation probes, not geometric tolerances.
                for(const bool xAxis:{false,true})for(const int sign:{-1,1})for(const int steps:{1,2,4})
                {
                    if(exhausted())break;
                    auto phase=seed;
                    auto& coordinate=xAxis?phase.X:phase.Y;
                    for(int i=0;i<steps;++i)coordinate=std::nextafter(coordinate,
                        sign<0?-std::numeric_limits<double>::infinity():std::numeric_limits<double>::infinity());
                    if(!std::isfinite(phase.X)||!std::isfinite(phase.Y)||
                        !phaseSeen.emplace(phase.X,phase.Y,phase.AngleDegrees,phase.Mirrored).second)continue;
                    const auto fixedEdges=edges(phase);
                    std::vector<std::pair<std::size_t,std::size_t>> possible;
                    for(std::size_t fi=0;fi<fixedEdges.size()&&!exhausted();++fi)
                    {
                        const auto& fixed=fixedEdges[fi];
                        const auto ax=fixed.B.X-fixed.A.X,ay=fixed.B.Y-fixed.A.Y,al=std::hypot(ax,ay);
                        if(al<=Original.Tolerance)continue;
                        for(std::size_t mi=0;mi<movingEdges.size()&&!exhausted();++mi)
                        {
                            const auto& moving=movingEdges[mi];
                            const auto bx=moving.B.X-moving.A.X,by=moving.B.Y-moving.A.Y,bl=std::hypot(bx,by);
                            if(bl<=Original.Tolerance||std::abs(ax*by-ay*bx)>Original.Tolerance*std::max(al,bl)||
                                sameMaterialSide(fixed,moving))continue;
                            possible.emplace_back(fi,mi);
                        }
                    }
                    if(exhausted())break;
                    if(possible.empty())
                    {
                        observe(0,phase,false,"first-seed-no-authorized-opposite-edge-pair",originalIndex,
                            Original.PartGap,ConstructionTraceStage::Analysis);
                        continue;
                    }
                    const auto id=traceID(3,++rawID);
                    ++query.Generated;++query.Attempts;++beforeClearance;
                    observe(id,phase,true,"first-seed-representable-phase-proposal",originalIndex,Original.PartGap,ConstructionTraceStage::Generate);
                    if(rejected(phase,id))continue;
                    const auto fixedGeometry=AxisSafety::Make(part.Geometry,phase);
                    std::set<std::tuple<double,double,bool>> continuationSeen;
                    bool certified=false;
                    for(const auto [fi,mi]:possible)
                    {
                        if(exhausted()||certified)break;
                        const auto& fixed=fixedEdges[fi];const auto& moving=movingEdges[mi];
                            const Point anchors[]{fixed.A,fixed.B,{(fixed.A.X+fixed.B.X)/2,(fixed.A.Y+fixed.B.Y)/2}};
                            const Point offsets[]{moving.A,moving.B,{(moving.A.X+moving.B.X)/2,(moving.A.Y+moving.B.Y)/2}};
                            for(const auto anchor:anchors)for(const auto offset:offsets)
                            {
                                if(exhausted()||certified)break;
                                const Pose next{anchor.X-offset.X,anchor.Y-offset.Y,angle,limits.Mirrored};
                                if(!continuationSeen.emplace(next.X,next.Y,next.Mirrored).second)continue;
                                ++query.Generated;++query.Attempts;
                                const auto nextID=traceID(3,++rawID);
                                bool safe=InsideSheet(part.Geometry,next,board.Width,board.Height,Original.EdgeMargin,Original.Tolerance);
                                if(safe)try
                                {
                                    const auto geometry=originalGeometry(next);
                                    safe=AxisSafety::Containment(geometry,board.Width,board.Height,Original.EdgeMargin)==AxisSafety::Verdict::Safe&&
                                        AxisSafety::CommonEdge(fixedGeometry,geometry,Original.Tolerance,true)&&
                                        !ShapesConflict(part.Geometry,phase,part.Geometry,next,Original.PartGap,Original.Tolerance,true)&&
                                        AxisSafety::Pair(fixedGeometry,geometry,Original.PartGap,true,Original.Tolerance)==AxisSafety::Verdict::Safe;
                                }
                                catch(...){safe=false;}
                                observe(nextID,next,safe,safe?"hypothetical-same-source-common-continuation-certified":
                                    "hypothetical-same-source-common-continuation-rejected",originalIndex,Original.PartGap,ConstructionTraceStage::Analysis);
                                certified=safe;
                            }
                    }
                    if(!certified)
                    {observe(id,phase,false,"first-seed-no-certified-common-continuation");continue;}
                    if(!retained.emplace(phase.X,phase.Y,phase.AngleDegrees,phase.Mirrored).second)continue;
                    if(!limits.MaxCandidates||query.Fits.size()<limits.MaxCandidates)
                    {query.Fits.push_back(phase);if(limits.Trace)query.FitsIDs.push_back(id);}
                    else
                    {
                        // A full pool can evict only through the caller's
                        // actual finite score. Continuation certification is
                        // evidence, never a quality bonus of its own.
                        query.Truncated=true;
                        if(!limits.CandidatePriority)
                        {
                            retained.erase({phase.X,phase.Y,phase.AngleDegrees,phase.Mirrored});
                            observe(id,phase,false,"first-seed-fit-cap-no-caller-priority");continue;
                        }
                        const auto phaseKey=limits.CandidatePriority(phase);
                        const auto finite=[](const auto& key)
                        {return std::all_of(key.begin(),key.end(),[](double value){return std::isfinite(value);});};
                        std::size_t replace=query.Fits.size();
                        std::optional<std::array<double,4>> displacedKey;
                        if(finite(phaseKey))for(std::size_t i=0;i<query.Fits.size();++i)
                        {
                            const auto previous=query.Fits[i];
                            if(previous.AngleDegrees!=phase.AngleDegrees||previous.Mirrored!=phase.Mirrored)continue;
                            const auto previousKey=limits.CandidatePriority(previous);
                            if(!finite(previousKey))continue;
                            const bool dominates=phase.X<=previous.X&&phase.Y<=previous.Y&&
                                (phase.X<previous.X||phase.Y<previous.Y);
                            if(!(phaseKey<previousKey||(phaseKey==previousKey&&dominates)))continue;
                            if(!displacedKey||*displacedKey<previousKey){replace=i;displacedKey=previousKey;}
                        }
                        if(replace==query.Fits.size())
                        {
                            retained.erase({phase.X,phase.Y,phase.AngleDegrees,phase.Mirrored});
                            observe(id,phase,false,"first-seed-fit-cap-not-better-by-caller-priority");continue;
                        }
                        const auto previous=query.Fits[replace];
                        const bool haveIDs=query.FitsIDs.size()==query.Fits.size();
                        observe(haveIDs?query.FitsIDs[replace]:0,previous,false,"first-seed-fit-cap-replaced-by-caller-priority",
                            std::numeric_limits<std::size_t>::max(),Original.PartGap,ConstructionTraceStage::Filter);
                        retained.erase({previous.X,previous.Y,previous.AngleDegrees,previous.Mirrored});
                        query.Fits[replace]=phase;if(haveIDs)query.FitsIDs[replace]=id;
                    }
                    observe(id,phase,true,"first-seed-certified-common-continuation");
                }
            }
            if(exhausted())query.Truncated=true;
        }
        // The normal stream's area does not measure the union with isolated
        // exact common-edge contacts. Stream-local observations keep that area.
        if(common){query.DomainMeasured=false;query.FreeTranslationArea=0;}
        if(limits.Trace&&limits.TraceHealth&&limits.TraceHealth->Failed.load())++limits.TraceHealth->Omitted;
        else if(limits.Trace)try
        {
            ConstructionTraceEvent event;event.Stage=ConstructionTraceStage::Complete;
            event.Source="nfp.original-clearance";event.PartIndex=originalIndex;
            event.StockInstance=board.StockInstance;event.BoardWidth=board.Width;event.BoardHeight=board.Height;
            event.Attempts=query.Attempts;event.Accepted=query.Supported;event.Reason="original-clearance-complete";
            event.SearchTruncated=query.Truncated;
            event.Measures={{"fits-before-clearance",double(beforeClearance)},{"fits-after-clearance",double(query.Fits.size())},
                {"supported",query.Supported?1.:0.},{"truncated",query.Truncated?1.:0.},
                {"wrapperWallSeconds",std::chrono::duration<double>(Clock::now()-wrapperStarted).count()},
                {"directSourceEdgeSeconds",directSourceSeconds},{"directSourceEdgeAttempts",double(directSourceAttempts)},
                {"commonGeneratorSeconds",commonGeneratorSeconds},{"ordinaryGeneratorSeconds",ordinaryGeneratorSeconds},
                {"commonDirectionScreenSeconds",commonDirectionScreenSeconds},{"commonDirectionScreened",commonDirectionScreened?1.:0.},
                {"commonSourceEdgesComplete",commonSourceEdgesComplete?1.:0.},{"commonDirectionScreenComplete",commonDirectionComplete?1.:0.},
                {"commonDirectionPairs",double(commonDirectionPairs)},{"commonDirectionPotential",double(commonDirectionPotential)},
                {"commonDirectionUnknown",double(commonDirectionUnknown)},
                {"commonDomainSkippedByDirectionProof",commonDomainSkippedByDirectionProof?1.:0.},
                {"queryWindowSeconds",timedQuery?queryWindow:-1.},{"ordinaryFirst",1.},
                {"originalGuardSeconds",originalGuardSeconds},{"originalGuardCalls",double(originalGuardCalls)},
                {"commonGapScreenSeconds",commonGapScreenSeconds},{"commonGapScreenCalls",double(commonGapScreenCalls)},
                {"commonGapScreenPairs",double(commonGapScreenPairs)},{"commonGapScreenRejected",double(commonGapScreenRejected)},
                {"commonGapScreenUnknown",double(commonGapScreenUnknown)},
                {"originalGuardStopped",double(guardStopped)},{"originalGuardRejectedCaller",double(guardCaller)},
                {"originalGuardRejectedSheet",double(guardSheet)},{"originalGuardRejectedContainment",double(guardContainment)},
                {"originalGuardRejectedUnknownID",double(guardUnknownID)},{"originalGuardRejectedMaterial",double(guardMaterial)},
                {"originalGuardRejectedPair",double(guardPair)},{"originalGuardAccepted",double(guardAccepted)},
                {"nativeOriginalVerificationQueries",double(nativeOriginalVerificationQueries)},
                {"nativeOriginalVerificationCalls",double(nativeOriginalVerificationCalls)},
                {"nativeOriginalVerifiedFits",double(nativeOriginalVerifiedFits)},
                {"phaseQueryCount",double(nativePhaseQueries)},{"phaseTimesNested",1.},
                // Original gates are nested in direct/common/ordinary phases;
                // these wall durations must not be added together.
                {"originalGuardTimeNestedInOtherPhases",1.}};
            for(const auto& measure:nativeQueryPhases)event.Measures.push_back(measure);
            limits.Trace(event);
        }
        catch(...)
        {
            if(limits.TraceHealth){limits.TraceHealth->Failed.store(true);++limits.TraceHealth->Omitted;}
            /* The returned fits and their IDs are already final. */
        }
        return query;
    }
};
}
