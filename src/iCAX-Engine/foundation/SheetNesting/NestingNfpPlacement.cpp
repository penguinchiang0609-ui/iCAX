#include "NestingNfpPlacement.h"
#include "NestingInternal.h"
#include "NestingAxisSafety.h"
#include "SheetNestingNfp.h"
#include "SheetNestingConstructionTrace.h"
#include <clipper2/clipper.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace iCAX::SheetNesting::Detail
{
namespace
{
    using namespace Clipper2Lib;
    constexpr std::size_t PairByteBudget=32*1024*1024;
    bool ValidTranslationWindow(const Rect& window)
    {
        return std::isfinite(window.X)&&std::isfinite(window.Y)&&std::isfinite(window.Width)&&
            std::isfinite(window.Height)&&window.Width>=0&&window.Height>=0&&
            std::isfinite(window.X+window.Width)&&std::isfinite(window.Y+window.Height);
    }
    struct Segment { Point A, B; std::size_t Owner = 0; };
    struct MaterialBoundaryIndex
    {
        std::vector<Point> Vertices;
        std::vector<AxisSafety::EdgeBounds> Edges;
        std::unique_ptr<AxisSafety::EdgeIndex> Index;
        double Magnitude=0;
    };
    // EdgeIndex points at its vector object. Own both in a stable heap object;
    // moving a cached PairGeometry must never invalidate those pointers.
    struct MaterialContactIndex
    {
        MaterialBoundaryIndex Fixed,Moving;
        std::size_t Bytes=0;
    };
    struct PairGeometry
    {
        PathsD Forbidden;
        std::vector<Segment> Contacts;
        // All vertex/edge material-contact loci, including nonparallel edges.
        // They guard the fast strict-interior rejection: a regularized NFP can
        // fill touch-only channels or isolated legal poses in its interior.
        std::vector<Segment> MaterialContacts;
        std::shared_ptr<const MaterialContactIndex> MaterialIndex;
        // This optional rejection certificate is independent of proposal
        // completeness. Missing proof always yields to the original gate.
        bool MaterialContactsComplete=false;
        // Missing candidate contact channels report search truncation.
        bool Incomplete=false;
        std::size_t Bytes=0;
    };
    struct PairKey
    {
        std::size_t Stationary, Moving;
        double StationaryAngle, MovingAngle;
        bool FullContacts=true;
        bool StationaryMirrored=false,MovingMirrored=false;
        bool ImplicitMaterialProof=false;
        bool operator<(const PairKey& b) const
        { return std::tie(Stationary, Moving, StationaryAngle, MovingAngle,FullContacts,StationaryMirrored,MovingMirrored,ImplicitMaterialProof)
              < std::tie(b.Stationary, b.Moving, b.StationaryAngle, b.MovingAngle,b.FullContacts,b.StationaryMirrored,b.MovingMirrored,b.ImplicitMaterialProof); }
    };
    std::size_t PairPayloadBytes(const PairGeometry& geometry)
    {
        std::size_t bytes=sizeof(PairGeometry)+sizeof(PairKey)+128+
            geometry.Forbidden.capacity()*sizeof(PathD)+
            (geometry.Contacts.capacity()+geometry.MaterialContacts.capacity())*sizeof(Segment);
        for(const auto& path:geometry.Forbidden) bytes+=path.capacity()*sizeof(PointD);
        if(geometry.MaterialIndex)bytes+=geometry.MaterialIndex->Bytes;
        return bytes;
    }
    bool Clip(Segment& s, double lx, double ly, double rx, double ty)
    {
        const double dx=s.B.X-s.A.X,dy=s.B.Y-s.A.Y;double lower=0,upper=1;
        const auto cut=[&](double a,double b)
        {
            if(a==0)return b>=0;
            const auto t=b/a;
            if(a<0)lower=std::max(lower,t);else upper=std::min(upper,t);
            return lower<=upper;
        };
        if(!cut(-dx,s.A.X-lx)||!cut(dx,rx-s.A.X)||!cut(-dy,s.A.Y-ly)||!cut(dy,ty-s.A.Y))return false;
        const auto old=s.A;s.A={old.X+lower*dx,old.Y+lower*dy};s.B={old.X+upper*dx,old.Y+upper*dy};return true;
    }
    bool BoxesOverlap(Rect a,Rect b,double tolerance)
    {return a.X+a.Width>b.X+tolerance&&b.X+b.Width>a.X+tolerance&&a.Y+a.Height>b.Y+tolerance&&b.Y+b.Height>a.Y+tolerance;}
    bool Near(Rect a,Rect b,double clearance)
    {
        return a.X+a.Width+clearance>=b.X&&b.X+b.Width+clearance>=a.X&&
            a.Y+a.Height+clearance>=b.Y&&b.Y+b.Height+clearance>=a.Y;
    }
    double ContactLinearUncertainty(Rect a,Rect b)
    {
        // Precision-8 clipping rounds source vertices and intersection events.
        const double magnitude=std::max({1.,std::abs(a.X),std::abs(a.Y),std::abs(a.X+a.Width),std::abs(a.Y+a.Height),
            std::abs(b.X),std::abs(b.Y),std::abs(b.X+b.Width),std::abs(b.Y+b.Height)});
        return 8*(1e-8+std::numeric_limits<double>::epsilon()*magnitude);
    }
    double ContactAreaUncertainty(double perimeter,Rect a,Rect b)
    {
        // The rounding sliver scales with the real source perimeter.
        const double linear=ContactLinearUncertainty(a,b);
        return linear*perimeter+16*linear*linear;
    }
    bool OnSegment(Point q,Point a,Point b,double tolerance)
    {
        if(q.X<std::min(a.X,b.X)-tolerance||q.X>std::max(a.X,b.X)+tolerance||q.Y<std::min(a.Y,b.Y)-tolerance||q.Y>std::max(a.Y,b.Y)+tolerance)return false;
        const double dx=b.X-a.X,dy=b.Y-a.Y;
        return std::abs((q.X-a.X)*dy-(q.Y-a.Y)*dx)<=tolerance*std::hypot(dx,dy);
    }
    bool ImplicitMaterialContact(const MaterialContactIndex& proof,Point q,double tolerance)
    {
        if(!AxisSafety::ArithmeticSupported()||!std::isfinite(tolerance)||tolerance<0)return true;
        const double magnitude=AxisSafety::SumBound(proof.Fixed.Magnitude,proof.Moving.Magnitude,true);
        const double expanded=AxisSafety::SumBound(magnitude,tolerance,true);
        const auto rounding=AxisSafety::ProductBounds(std::numeric_limits<double>::epsilon(),expanded);
        if(!rounding.Valid())return true;
        // Under FE_TONEAREST, eps (twice unit roundoff) encloses both
        // fl(vertex-vertex) and the old fl(endpoint +/- tolerance). A true old
        // OnSegment must pass that bounding box. Exact q+r / p-q intervals and
        // this outward radius therefore cannot omit its original edge.
        const double radius=AxisSafety::SumBound(tolerance,
            AxisSafety::SumBound(rounding.High,rounding.High,true),true);
        if(!std::isfinite(radius))return true;
        const auto around=[&](AxisSafety::Number x,AxisSafety::Number y)
        {return AxisSafety::EdgeBounds{AxisSafety::CoordinateBounds(x),AxisSafety::CoordinateBounds(y),true};};
        for(const auto r:proof.Moving.Vertices)
        {
            const auto box=around(AxisSafety::Number(q.X)+AxisSafety::Number(r.X),
                AxisSafety::Number(q.Y)+AxisSafety::Number(r.Y));
            if(!box.X.Valid()||!box.Y.Valid())return true;
            for(const auto i:proof.Fixed.Index->Query(box,radius))
            {
                const auto p=proof.Fixed.Vertices[i],end=proof.Fixed.Vertices[(i+1)%proof.Fixed.Vertices.size()];
                // Preserve the old binary64 endpoint subtraction and predicate.
                if(OnSegment(q,{p.X-r.X,p.Y-r.Y},{end.X-r.X,end.Y-r.Y},tolerance))return true;
            }
        }
        for(const auto p:proof.Fixed.Vertices)
        {
            const auto box=around(AxisSafety::Number(p.X)-AxisSafety::Number(q.X),
                AxisSafety::Number(p.Y)-AxisSafety::Number(q.Y));
            if(!box.X.Valid()||!box.Y.Valid())return true;
            for(const auto j:proof.Moving.Index->Query(box,radius))
            {
                const auto r=proof.Moving.Vertices[j],s=proof.Moving.Vertices[(j+1)%proof.Moving.Vertices.size()];
                if(OnSegment(q,{p.X-r.X,p.Y-r.Y},{p.X-s.X,p.Y-s.Y},tolerance))return true;
            }
        }
        return false;
    }
    bool StrictlyForbidden(const PairGeometry& pair,Point q,double tolerance)
    {
        if(pair.Incomplete||!pair.MaterialContactsComplete)return false;
        int winding=0;
        for(const auto& loop:pair.Forbidden)for(std::size_t i=0;i<loop.size();++i)
        {
            const auto a=loop[i],b=loop[(i+1)%loop.size()];
            if(OnSegment(q,{a.x,a.y},{b.x,b.y},tolerance))return false;
            const double side=(b.x-a.x)*(q.Y-a.y)-(b.y-a.y)*(q.X-a.x);
            if(a.y<=q.Y){if(b.y>q.Y&&side>0)++winding;}
            else if(b.y<=q.Y&&side<0)--winding;
        }
        if(winding==0)return false;
        // A legal point swallowed by regularization must still have a contact
        // between the original material boundaries. Never reject that point
        // using the regularized region; the exact material check decides it.
        if(pair.MaterialIndex)
        {
            if(ImplicitMaterialContact(*pair.MaterialIndex,q,tolerance))return false;
        }
        else for(const auto& segment:pair.MaterialContacts)
            if(OnSegment(q,segment.A,segment.B,tolerance))return false;
        return true;
    }
}
NfpPlacementQuery FindRigidNfpPlacements(const Problem& original,const Layout& board,const std::vector<Member>& members,
    const NfpPlacementLimits& limits,const RigidNfpRegionLookup& lookup,const std::function<bool()>& stop,
    const std::set<std::string>& zeroGapObstacles)
{
    using Clock=std::chrono::steady_clock;
    const bool phaseDiagnostics=bool(limits.Trace);
    const auto queryStarted=phaseDiagnostics?Clock::now():Clock::time_point{};
    struct PhaseTiming
    {
        double Seconds=0;std::size_t Calls=0;bool Active=false;Clock::time_point Started{};
        void Stop()noexcept
        {if(Active){Seconds+=std::chrono::duration<double>(Clock::now()-Started).count();Active=false;}}
    };
    struct PhaseTimer
    {
        PhaseTiming& Target;
        PhaseTimer(PhaseTiming& target,bool enabled):Target(target)
        {if(enabled){++Target.Calls;Target.Active=true;Target.Started=Clock::now();}}
        ~PhaseTimer(){Target.Stop();}
    };
    PhaseTiming lookupTime,loopConversionTime,booleanTime,contactGenerationTime,contactPartitionTime,
        certificationTime,priorityTime;
    NfpPlacementQuery result;result.Supported=true;
    const char* reason="complete-bounded-rigid-events";
    std::size_t pairQueries=0,regionVertices=0,contactIntersections=0,collapsedDomainEvents=0;
    std::size_t necessaryFitCalls=0,necessaryFitRejected=0,necessaryFitUnknown=0,movingGeometryPreparations=0;
    std::size_t competitivePoseCalls=0,competitivePoseRejected=0,competitivePoseUnknown=0;
    std::size_t movingEdgesPreparations=0,movingEdgesBytes=0;
    std::uint64_t nextID=0;
    const auto stopped=[&]
    {if(stop&&stop()){result.Truncated=true;reason="rigid-query-stopped";return true;}return false;};
    const auto observe=[&](ConstructionTraceStage stage,std::uint64_t id,Point delta,bool accepted,const char* why)noexcept
    {
        if(!limits.Trace)return;
        if(limits.TraceHealth&&limits.TraceHealth->Failed.load()){++limits.TraceHealth->Omitted;return;}
        try
        {
            ConstructionTraceEvent event;event.Stage=stage;event.Source="nfp.rigid-placement";
            event.CandidateID=id;event.Accepted=accepted;event.Reason=why;event.Attempts=result.Attempts;
            event.SearchTruncated=result.Truncated;event.StockInstance=board.StockInstance;
            event.BoardWidth=board.Width;event.BoardHeight=board.Height;
            event.Measures={{"members",double(members.size())},{"fixedOriginals",double(board.Placements.size())},
                {"pairRegionQueries",double(pairQueries)},{"regionVertices",double(regionVertices)},
                {"contactIntersections",double(contactIntersections)},{"generated",double(result.Generated)},
                {"collapsedDomainEvents",double(collapsedDomainEvents)},
                {"necessaryFitCalls",double(necessaryFitCalls)},{"necessaryFitRejected",double(necessaryFitRejected)},
                {"necessaryFitUnknown",double(necessaryFitUnknown)},{"movingGeometryPreparations",double(movingGeometryPreparations)},
                {"competitivePoseCalls",double(competitivePoseCalls)},{"competitivePoseRejected",double(competitivePoseRejected)},
                {"competitivePoseUnknown",double(competitivePoseUnknown)},
                {"supported",result.Supported?1.:0.},{"translationX",delta.X},{"translationY",delta.Y}};
            if(stage==ConstructionTraceStage::Complete)event.Measures.insert(event.Measures.end(),{
                {"movingEdgesPreparations",double(movingEdgesPreparations)},{"movingEdgesBytes",double(movingEdgesBytes)},
                {"phaseRigidQuerySeconds",std::chrono::duration<double>(Clock::now()-queryStarted).count()},
                {"phaseRigidLookupSeconds",lookupTime.Seconds},{"phaseRigidLookupCalls",double(lookupTime.Calls)},
                {"phaseRigidLoopConversionSeconds",loopConversionTime.Seconds},{"phaseRigidLoopConversionCalls",double(loopConversionTime.Calls)},
                {"phaseRigidBooleanSeconds",booleanTime.Seconds},{"phaseRigidBooleanCalls",double(booleanTime.Calls)},
                {"phaseRigidContactGenerationSeconds",contactGenerationTime.Seconds},{"phaseRigidContactGenerationCalls",double(contactGenerationTime.Calls)},
                {"phaseRigidContactPartitionSeconds",contactPartitionTime.Seconds},{"phaseRigidContactPartitionCalls",double(contactPartitionTime.Calls)},
                {"phaseRigidCertificationSeconds",certificationTime.Seconds},{"phaseRigidCertificationCalls",double(certificationTime.Calls)},
                {"phaseRigidPrioritySeconds",priorityTime.Seconds},{"phaseRigidPriorityCalls",double(priorityTime.Calls)},
                {"phaseTimesNested",1.}});
            if(id)for(const auto& member:members)
            {auto pose=member.Transform;pose.X+=delta.X;pose.Y+=delta.Y;
                event.Members.push_back({member.PartIndex,std::numeric_limits<std::size_t>::max(),pose,0});}
            limits.Trace(event);
        }
        catch(...){if(limits.TraceHealth){limits.TraceHealth->Failed.store(true);++limits.TraceHealth->Omitted;}}
    };
    const auto finish=[&]()
    {
        for(auto* phase:{&lookupTime,&loopConversionTime,&booleanTime,&contactGenerationTime,&contactPartitionTime,&certificationTime,&priorityTime})phase->Stop();
        observe(ConstructionTraceStage::Complete,0,{},!result.Fits.empty(),reason);return result;
    };
    const auto unsupported=[&](const char* why)
    {result.Supported=false;result.Truncated=true;reason=why;return finish();};
    observe(ConstructionTraceStage::Begin,0,{},true,"rigid-original-member-query");
    if(stopped())return finish();
    if(members.empty()||!lookup||limits.MaxBlockers||limits.Mirrored||
        !std::isfinite(board.Width)||!std::isfinite(board.Height)||board.Width<=0||board.Height<=0)
        return unsupported("invalid-rigid-query");
    if(members.size()>limits.MaxRigidMembers||board.Placements.size()>limits.MaxFixedPlacements)
        return unsupported("rigid-member-or-fixed-limit");
    if(limits.TranslationWindow&&!ValidTranslationWindow(*limits.TranslationWindow))
        return unsupported("invalid-rigid-translation-window");
    const auto verificationGap=limits.VerificationGap.value_or(original.PartGap);
    if(!std::isfinite(verificationGap)||verificationGap<original.PartGap)return unsupported("invalid-rigid-verification-gap");
    if(!limits.MaxAttempts||!limits.MaxRawCandidates)
    {result.Truncated=true;reason="zero-rigid-attempt-or-raw-budget";return finish();}
    const auto stock=std::find_if(original.Stocks.begin(),original.Stocks.end(),[&](const Stock& s){return s.ID==board.StockID;});
    if(stock==original.Stocks.end())return unsupported("unknown-rigid-stock");
    std::map<std::string,std::size_t> indices;
    for(std::size_t i=0;i<original.Parts.size();++i)indices.emplace(original.Parts[i].ID,i);
    std::map<std::size_t,std::size_t> usedDemand;
    struct FixedOriginal{std::size_t Index;Pose Transform;std::shared_ptr<const AxisSafety::Geometry> Geometry;};
    std::vector<FixedOriginal> fixed;fixed.reserve(board.Placements.size());
    for(const auto& placed:board.Placements)
    {
        if(stopped())return finish();
        const auto found=indices.find(placed.PartID);
        if(found==indices.end()||!PoseAllowed(original.Parts[found->second],placed.Transform,original.Tolerance))
            return unsupported("unknown-or-disallowed-rigid-fixed-original");
        const auto& source=original.Parts[found->second];
        if(zeroGapObstacles.contains(source.ID))
        {if(source.Quantity)return unsupported("nonzero-demand-rigid-domain-obstacle");}
        else if(++usedDemand[found->second]>source.Quantity)return unsupported("rigid-fixed-demand-exceeded");
        if(source.Geometry.Radius>0||!source.Geometry.Holes.empty())result.Truncated=true;
        std::shared_ptr<const AxisSafety::Geometry> geometry;
        if(limits.RigidFixedGeometry)try{geometry=limits.RigidFixedGeometry(found->second,placed.Transform);}catch(...){}
        if(!geometry)geometry=std::make_shared<const AxisSafety::Geometry>(AxisSafety::Make(source.Geometry,placed.Transform));
        fixed.push_back({found->second,placed.Transform,std::move(geometry)});
    }
    double lx=-std::numeric_limits<double>::infinity(),ly=lx,ux=-lx,uy=-lx;
    std::map<std::size_t,std::size_t> demand;
    for(const auto& member:members)
    {
        if(stopped())return finish();
        if(member.PartIndex>=original.Parts.size())return unsupported("unknown-rigid-moving-original");
        const auto& part=original.Parts[member.PartIndex];
        if(!part.Quantity||part.Material!=stock->Material||!PoseAllowed(part,member.Transform,original.Tolerance)||
            !std::isfinite(member.Transform.X)||!std::isfinite(member.Transform.Y)||
            ++demand[member.PartIndex]>part.Quantity-usedDemand[member.PartIndex])
            return unsupported("disallowed-rigid-member-or-demand");
        const auto box=Bounds(part.Geometry,member.Transform);
        lx=std::max(lx,original.EdgeMargin-box.X);ly=std::max(ly,original.EdgeMargin-box.Y);
        ux=std::min(ux,board.Width-original.EdgeMargin-box.X-box.Width);
        uy=std::min(uy,board.Height-original.EdgeMargin-box.Y-box.Height);
        // The clearance service has conservative proposal contours for these
        // source types. Safe fits remain valid, but completeness is not claimed.
        if(part.Geometry.Radius>0||!part.Geometry.Holes.empty())result.Truncated=true;
    }
    if(!std::isfinite(lx)||!std::isfinite(ly)||!std::isfinite(ux)||!std::isfinite(uy))
        return unsupported("nonfinite-rigid-member-domain");
    if(ux<lx-original.Tolerance||uy<ly-original.Tolerance)
    {result.DomainMeasured=true;reason="empty-rigid-member-sheet-intersection";return finish();}
    ux=std::max(lx,ux);uy=std::max(ly,uy);
    if(limits.TranslationWindow)
    {
        const auto& window=*limits.TranslationWindow;
        lx=std::max(lx,window.X);ly=std::max(ly,window.Y);
        ux=std::min(ux,window.X+window.Width);uy=std::min(uy,window.Y+window.Height);
        if(ux<lx||uy<ly)
        {result.DomainMeasured=true;reason="empty-rigid-translation-window-intersection";return finish();}
    }
    std::vector<Point> origins;std::set<std::pair<double,double>> seen;
    const auto emit=[&](Point p)
    {
        if(!std::isfinite(p.X)||!std::isfinite(p.Y)||p.X<lx-original.Tolerance||p.Y<ly-original.Tolerance||
            p.X>ux+original.Tolerance||p.Y>uy+original.Tolerance)return;
        p.X=std::clamp(p.X,lx,ux);p.Y=std::clamp(p.Y,ly,uy);
        if(!seen.emplace(p.X,p.Y).second)return;
        if(origins.size()>=limits.MaxRawCandidates){result.Truncated=true;return;}
        origins.push_back(p);
    };
    std::vector<Segment> boundaries{{{lx,ly},{ux,ly},0},{{ux,ly},{ux,uy},0},{{ux,uy},{lx,uy},0},{{lx,uy},{lx,ly},0}};
    std::vector<PathsD> freeDomains;
    const std::size_t streams=original.AllowCommonEdges?2:1;
    for(std::size_t stream=0;stream<streams;++stream)
    {
        PathsD forbidden;std::vector<Segment> streamBoundaries;
        const auto buildStream=[&]
        {
            for(const auto& obstacle:fixed)for(const auto& member:members)
            {
                if(stopped())return false;
                if(pairQueries>=limits.MaxRigidPairQueries){reason="rigid-pair-region-budget";return false;}
                ++pairQueries;NfpLookup pair;
                {PhaseTimer timing{lookupTime,phaseDiagnostics};
                    pair=lookup(obstacle.Index,obstacle.Transform,member.PartIndex,member.Transform,stream!=0);}
                if(pair.State!=NfpStatus::Ready||!pair.Region){reason="rigid-pair-region-unavailable";return false;}
                PhaseTimer conversionTiming{loopConversionTime,phaseDiagnostics};
                std::shared_ptr<const std::vector<std::vector<Point>>> converted;
                if(limits.RigidRegionLoops&&AxisSafety::ArithmeticSupported())
                {
                    auto remaining=limits.MaxRigidRegionVertices-std::min(regionVertices,limits.MaxRigidRegionVertices);
                    bool applicable=true;
                    for(const auto& ring:pair.Region->Loops)
                    {if(ring.size()>remaining){applicable=false;break;}remaining-=ring.size();}
                    // A complete-loop provider cannot spend work beyond a
                    // query which would stop inside this region's vertex cap.
                    if(applicable)try{converted=limits.RigidRegionLoops(pair);}catch(...){}
                    if(converted)
                    {
                        if(converted->size()!=pair.Region->Loops.size())converted.reset();
                        else for(std::size_t i=0;i<converted->size();++i)
                            if((*converted)[i].size()!=pair.Region->Loops[i].size()){converted.reset();break;}
                    }
                }
                for(std::size_t ringIndex=0;ringIndex<pair.Region->Loops.size();++ringIndex)
                {
                    const auto& ring=pair.Region->Loops[ringIndex];
                    if(stopped())return false;
                    if(ring.size()>limits.MaxRigidRegionVertices-std::min(regionVertices,limits.MaxRigidRegionVertices))
                    {reason="rigid-region-vertex-budget";return false;}
                    regionVertices+=ring.size();auto& path=forbidden.emplace_back();path.reserve(ring.size());
                    for(std::size_t pointIndex=0;pointIndex<ring.size();++pointIndex)
                    {
                        const auto relative=converted?(*converted)[ringIndex][pointIndex]:Transform(ring[pointIndex],pair.Transform);
                        path.emplace_back(relative.X+obstacle.Transform.X-member.Transform.X,
                            relative.Y+obstacle.Transform.Y-member.Transform.Y);
                    }
                    for(std::size_t i=0;i<path.size();++i)
                    {Segment edge{{path[i].x,path[i].y},{path[(i+1)%path.size()].x,path[(i+1)%path.size()].y},0};
                        if(Clip(edge,lx,ly,ux,uy))streamBoundaries.push_back(edge);}
                }
            }
            return true;
        };
        if(!buildStream())
        {if(stopped())return finish();result.Truncated=true;continue;}
        if(stopped())return finish();
        try
        {
            PhaseTimer timing{booleanTime,phaseDiagnostics};
            PathsD domain{{{lx,ly},{ux,ly},{ux,uy},{lx,uy}}};
            auto free=forbidden.empty()?domain:Difference(domain,Union(forbidden,FillRule::NonZero,8),FillRule::NonZero,8);
            if(!stream){result.FreeTranslationArea=std::abs(Area(free));result.DomainMeasured=!original.AllowCommonEdges;}
            freeDomains.push_back(std::move(free));
            boundaries.insert(boundaries.end(),streamBoundaries.begin(),streamBoundaries.end());
        }
        catch(...){result.Truncated=true;reason="rigid-domain-boolean-failed";}
    }
    // A failed optional stream cannot discard a previously completed one.
    // Partially built forbidden unions are never used as safe free domains.
    if(freeDomains.empty())return unsupported(reason);
    // Corners also retain collapsed line/point IFPs. The non-collapsed domains
    // above are complete unions of pair regions, not intersected single fits.
    emit({lx,ly});emit({ux,ly});emit({lx,uy});emit({ux,uy});
    std::vector<std::vector<Point>> domainEvents(freeDomains.size());
    for(std::size_t s=0;s<freeDomains.size();++s)for(const auto& path:freeDomains[s])
        for(std::size_t i=0;i<path.size();++i)
        {
            if(stopped())return finish();
            const auto a=path[i],b=path[(i+1)%path.size()];domainEvents[s].push_back({a.x,a.y});
            if(!limits.BoundaryVerticesOnly)domainEvents[s].push_back({(a.x+b.x)*.5,(a.y+b.y)*.5});
        }
    for(std::size_t i=0;;++i)
    {bool any=false;for(const auto& events:domainEvents)if(i<events.size()){emit(events[i]);any=true;}if(!any)break;}
    // Real source contact loci retain seams and collapsed channels swallowed
    // by regularized polygon subtraction. Split each locus at every NFP edge.
    struct SourceEdge{Point A,B;};
    std::vector<std::optional<std::vector<SourceEdge>>> preparedMovingEdges(members.size());
    const auto sourceEdges=[&](const Shape& shape,Pose pose,bool authorizedOnly)
    {
        std::vector<SourceEdge> edges;
        const auto append=[&](const std::vector<Point>& ring,std::size_t index)
        {
            if(authorizedOnly&&!shape.CommonEdgeRings.empty()&&(index>=shape.CommonEdgeRings.size()||!shape.CommonEdgeRings[index]))return;
            for(std::size_t i=0;i<ring.size();++i)
                edges.push_back({Transform(ring[i],pose),Transform(ring[(i+1)%ring.size()],pose)});
        };
        if(shape.Radius<=0){append(shape.Outer,0);for(std::size_t i=0;i<shape.Holes.size();++i)append(shape.Holes[i],i+1);}
        return edges;
    };
    std::vector<Segment> contacts;
    const auto addContact=[&](Segment segment)
    {
        if(!Clip(segment,lx,ly,ux,uy))return;
        if(contacts.size()>=limits.MaxContactSegments){result.Truncated=true;return;}
        contacts.push_back(segment);
    };
    if(limits.IncludeContactChannels&&(original.PartGap==0||original.AllowCommonEdges))
    {
        PhaseTimer timing{contactGenerationTime,phaseDiagnostics};
        bool contactBound=false;
        for(const auto& obstacle:fixed)
        {
            const auto fixedEdges=sourceEdges(original.Parts[obstacle.Index].Geometry,obstacle.Transform,original.PartGap>0);
            for(std::size_t memberIndex=0;memberIndex<members.size();++memberIndex)
            {
                const auto& member=members[memberIndex];
                auto& prepared=preparedMovingEdges[memberIndex];std::vector<SourceEdge> uncached;
                const bool reuse=limits.ReuseRigidMovingEdges&&AxisSafety::ArithmeticSupported();
                if(!reuse||!prepared)
                {
                    ++movingEdgesPreparations;
                    uncached=sourceEdges(original.Parts[member.PartIndex].Geometry,member.Transform,original.PartGap>0);
                    constexpr std::size_t byteLimit=32*1024*1024;
                    if(reuse&&uncached.capacity()<=(byteLimit-movingEdgesBytes)/sizeof(SourceEdge))
                    {movingEdgesBytes+=uncached.capacity()*sizeof(SourceEdge);prepared=std::move(uncached);}
                }
                const auto& movingEdges=reuse&&prepared?*prepared:uncached;
                for(const auto& a:fixedEdges)for(const auto& b:movingEdges)
                {
                    if(stopped())return finish();
                    if(contacts.size()>=limits.MaxContactSegments){contactBound=true;break;}
                    if(original.PartGap>0)
                    {
                        const auto ax=a.B.X-a.A.X,ay=a.B.Y-a.A.Y,bx=b.B.X-b.A.X,by=b.B.Y-b.A.Y;
                        if(std::abs(ax*by-ay*bx)>original.Tolerance*std::max(std::hypot(ax,ay),std::hypot(bx,by)))continue;
                        const std::array<Point,4> ends{{{a.A.X-b.A.X,a.A.Y-b.A.Y},{a.A.X-b.B.X,a.A.Y-b.B.Y},
                            {a.B.X-b.A.X,a.B.Y-b.A.Y},{a.B.X-b.B.X,a.B.Y-b.B.Y}}};
                        const auto projection=[&](Point p){return p.X*ax+p.Y*ay;};
                        const auto range=std::minmax_element(ends.begin(),ends.end(),[&](Point x,Point y){return projection(x)<projection(y);});
                        addContact({*range.first,*range.second,0});
                    }
                    else
                    {
                        addContact({{a.A.X-b.A.X,a.A.Y-b.A.Y},{a.A.X-b.B.X,a.A.Y-b.B.Y},0});
                        addContact({{a.B.X-b.A.X,a.B.Y-b.A.Y},{a.B.X-b.B.X,a.B.Y-b.B.Y},0});
                        addContact({{a.A.X-b.A.X,a.A.Y-b.A.Y},{a.B.X-b.A.X,a.B.Y-b.A.Y},0});
                        addContact({{a.A.X-b.B.X,a.A.Y-b.B.Y},{a.B.X-b.B.X,a.B.Y-b.B.Y},0});
                    }
                }
                if(contactBound)break;
            }
            if(contactBound)break;
        }
        if(contactBound)result.Truncated=true;
    }
    for(const auto& channel:contacts)
    {
        PhaseTimer timing{contactPartitionTime,phaseDiagnostics};
        if(stopped())return finish();
        std::vector<double> cuts{0,1};bool bounded=false;
        const long double rx=channel.B.X-channel.A.X,ry=channel.B.Y-channel.A.Y;
        for(const auto& edge:boundaries)
        {
            if(stopped())return finish();
            if(contactIntersections>=limits.MaxRigidContactIntersections){result.Truncated=true;bounded=true;break;}
            ++contactIntersections;
            const long double sx=edge.B.X-edge.A.X,sy=edge.B.Y-edge.A.Y;
            const long double qx=edge.A.X-channel.A.X,qy=edge.A.Y-channel.A.Y,den=rx*sy-ry*sx;
            if(den!=0)
            {
                const auto t=(qx*sy-qy*sx)/den,u=(qx*ry-qy*rx)/den;
                if(t>=0&&t<=1&&u>=0&&u<=1)cuts.push_back(static_cast<double>(t));
            }
            else if(qx*ry-qy*rx==0)
            {
                const auto length=rx*rx+ry*ry;
                if(length>0)for(const auto p:{edge.A,edge.B})
                {const auto t=((p.X-channel.A.X)*rx+(p.Y-channel.A.Y)*ry)/length;if(t>=0&&t<=1)cuts.push_back(static_cast<double>(t));}
            }
        }
        if(bounded)break; // Never advertise a partially partitioned channel.
        std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
        const auto at=[&](double t){return Point{channel.A.X+double(rx)*t,channel.A.Y+double(ry)*t};};
        for(std::size_t i=0;i<cuts.size();++i)
        {emit(at(cuts[i]));if(i&&!limits.BoundaryVerticesOnly)emit(at((cuts[i-1]+cuts[i])*.5));}
    }
    if(lx==ux||ly==uy)
    {
        // A full-width/height rigid carrier has a one-dimensional sheet
        // domain. Polygon subtraction cannot retain its free intervals.
        // Every completed pair stream has already clipped its NFP edges to
        // this very line: their endpoints partition the remaining axis, so
        // use them directly instead of depending on optional source-contact
        // channels and their separate intersection budget. These are only
        // proposals; the unchanged original-member gate certifies each pose.
        const bool vertical=lx==ux;
        std::vector<double> cuts{vertical?ly:lx,vertical?uy:ux};
        for(const auto& edge:boundaries)
        {
            if(stopped())return finish();
            cuts.push_back(vertical?edge.A.Y:edge.A.X);
            cuts.push_back(vertical?edge.B.Y:edge.B.X);
        }
        std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
        const auto at=[&](double coordinate){return vertical?Point{lx,coordinate}:Point{coordinate,ly};};
        const auto before=origins.size();
        for(std::size_t i=0;i<cuts.size();++i)
        {
            if(stopped())return finish();
            emit(at(cuts[i]));
            if(i&&!limits.BoundaryVerticesOnly)emit(at(cuts[i-1]+(cuts[i]-cuts[i-1])*.5));
        }
        collapsedDomainEvents=origins.size()-before;
    }
    result.Generated=origins.size();
    observe(ConstructionTraceStage::Analysis,0,{},true,"member-ifp-intersection-minus-original-pair-nfp-union");
    if(limits.CandidatePriority)
    {
        PhaseTimer timing{priorityTime,phaseDiagnostics};
        struct Ordered{Point Delta;std::array<double,4> Key;};std::vector<Ordered> ordered;
        for(const auto p:origins)
        {
            if(stopped())return finish();
            const auto key=limits.CandidatePriority({p.X,p.Y,0,false});
            if(std::any_of(key.begin(),key.end(),[](double x){return !std::isfinite(x);}))return unsupported("nonfinite-rigid-caller-priority");
            ordered.push_back({p,key});
        }
        std::stable_sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b)
        {return std::tie(a.Key,a.Delta.Y,a.Delta.X)<std::tie(b.Key,b.Delta.Y,b.Delta.X);});
        for(std::size_t i=0;i<ordered.size();++i)origins[i]=ordered[i].Delta;
    }
    else std::stable_sort(origins.begin(),origins.end(),[](Point a,Point b){return std::tie(a.Y,a.X)<std::tie(b.Y,b.X);});
    for(const auto delta:origins)
    {
        if(stopped())return finish();
        if(result.Attempts>=limits.MaxAttempts){result.Truncated=true;reason="rigid-attempt-budget";break;}
        if(limits.MaxCandidates&&result.Fits.size()>=limits.MaxCandidates){result.Truncated=true;reason="rigid-fit-budget";break;}
        const auto id=++nextID;observe(ConstructionTraceStage::Generate,id,delta,true,"rigid-domain-event");
        if(limits.CompetitivePose)
        {
            bool possible=true;++competitivePoseCalls;
            try{possible=limits.CompetitivePose({delta.X,delta.Y,0,false},id);}
            catch(...)
            {++competitivePoseUnknown;observe(ConstructionTraceStage::Analysis,id,delta,true,"competitive-pose-guard-unproved");}
            if(!possible)
            {++competitivePoseRejected;observe(ConstructionTraceStage::Filter,id,delta,false,"competitive-pose-guard-rejected");continue;}
        }
        PhaseTimer timing{certificationTime,phaseDiagnostics};
        ++result.Attempts;
        if(limits.NecessaryFit)
        {
            // This is the final stored delta, not a local envelope estimate.
            // A passing or unproved screen certifies nothing: every original
            // member still passes the unchanged world-material gates below.
            bool possible=true;++necessaryFitCalls;
            try{possible=limits.NecessaryFit({delta.X,delta.Y,0,false},id);}
            catch(...)
            {++necessaryFitUnknown;observe(ConstructionTraceStage::Analysis,id,delta,true,"necessary-fit-guard-unproved");}
            if(!possible)
            {++necessaryFitRejected;observe(ConstructionTraceStage::Filter,id,delta,false,"necessary-fit-guard-rejected");continue;}
        }
        std::vector<AxisSafety::Geometry> geometry;std::vector<Pose> poses;bool safe=true;
        const auto clear=[&](const Shape& a,Pose ap,const AxisSafety::Geometry& ag,const Shape& b,Pose bp,
            const AxisSafety::Geometry& bg,double gap)
        {
            if(AxisSafety::EnvelopesSeparated(ag,bg,AxisSafety::Clearance(ag,bg,gap)))return true;
            if(ShapesConflict(a,ap,b,bp,gap,original.Tolerance,original.AllowCommonEdges))return false;
            const auto verdict=AxisSafety::Pair(ag,bg,gap,original.AllowCommonEdges,original.Tolerance);
            if(verdict==AxisSafety::Verdict::Uncertain)result.Truncated=true;
            return verdict==AxisSafety::Verdict::Safe;
        };
        try
        {
            for(std::size_t i=0;i<members.size()&&safe;++i)
            {
                if(stopped())return finish();
                const auto& member=members[i];const auto& part=original.Parts[member.PartIndex];auto pose=member.Transform;
                pose.X+=delta.X;pose.Y+=delta.Y;++movingGeometryPreparations;auto actual=AxisSafety::Make(part.Geometry,pose);
                const auto containment=AxisSafety::Containment(actual,board.Width,board.Height,original.EdgeMargin);
                if(containment==AxisSafety::Verdict::Uncertain)result.Truncated=true;
                safe=PoseAllowed(part,pose,original.Tolerance)&&InsideSheet(part.Geometry,pose,board.Width,board.Height,original.EdgeMargin,original.Tolerance)&&
                    containment==AxisSafety::Verdict::Safe;
                for(const auto& obstacle:fixed)
                {
                    if(!safe)break;if(stopped())return finish();const auto& other=original.Parts[obstacle.Index];
                    safe=clear(part.Geometry,pose,actual,other.Geometry,obstacle.Transform,*obstacle.Geometry,
                        zeroGapObstacles.contains(other.ID)?0:verificationGap);
                }
                for(std::size_t j=0;j<i&&safe;++j)
                {if(stopped())return finish();safe=clear(part.Geometry,pose,actual,original.Parts[members[j].PartIndex].Geometry,
                    poses[j],geometry[j],verificationGap);}
                poses.push_back(pose);geometry.push_back(std::move(actual));
            }
            if(safe&&limits.AcceptFit)safe=limits.AcceptFit({delta.X,delta.Y,0,false},id);
        }
        catch(...){safe=false;result.Truncated=true;}
        observe(ConstructionTraceStage::Filter,id,delta,safe,safe?"rigid-all-originals-certified":"rigid-original-material-rejected");
        if(!safe)continue;
        result.Fits.push_back({delta.X,delta.Y,0,false});if(limits.Trace)result.FitsIDs.push_back(id);
        if(limits.StopAfterFirstFit){result.Truncated=true;reason="rigid-first-certified-fit";break;}
    }
    if(result.Fits.empty())
    {
        // Finite quantized boundary/contact events do not exhaust the real
        // translation domain. In particular no rounded boundary retreat was
        // certified here, so an empty sampled set is not infeasibility.
        result.Truncated=true;
        if(reason==std::string_view("complete-bounded-rigid-events"))reason="finite-rigid-events-no-certified-fit";
    }
    return finish();
}

struct NfpPlacementGenerator::Impl
{
    // Every prepared shape, pair key and world-pose key belongs to this owned
    // immutable input snapshot. External edits cannot mix new source material
    // with an earlier NFP/geometry preparation, nor dangle the input reference.
    const Problem P;
    std::shared_ptr<NfpCache> Cache;
    std::vector<NfpPreparedShape> Prepared;
    std::vector<NfpPreparedShape> MirroredPrepared;
    std::vector<double> Perimeters;
    std::unordered_map<std::string,std::size_t> Indices;
    std::map<PairKey,std::shared_ptr<const PairGeometry>> Pairs;
    std::size_t PairBytes=0;
    struct FixedWorldGeometry
    {
        Rect Box;
        AxisSafety::Geometry Nominal;
    };
    using FixedWorldKey=std::tuple<std::size_t,double,double,double,bool>;
    std::map<FixedWorldKey,std::shared_ptr<const FixedWorldGeometry>> FixedWorld;
    std::size_t FixedWorldVertices=0;
    bool IsReady=false;
    std::shared_ptr<const FixedWorldGeometry> FixedGeometry(std::size_t index,Pose pose)
    {
        const bool cacheable=AxisSafety::ArithmeticSupported()&&std::isfinite(pose.X)&&
            std::isfinite(pose.Y)&&std::isfinite(pose.AngleDegrees);
        const FixedWorldKey key{index,pose.X,pose.Y,pose.AngleDegrees,pose.Mirrored};
        if(cacheable)
            if(const auto found=FixedWorld.find(key);found!=FixedWorld.end())return found->second;
        auto world=std::make_shared<FixedWorldGeometry>();
        const auto& shape=P.Parts[index].Geometry;
        world->Box=Bounds(shape,pose);
        world->Nominal=AxisSafety::Make(shape,pose);
        std::size_t vertices=std::max<std::size_t>(1,world->Nominal.Hull.size());
        for(const auto& ring:world->Nominal.Rings)
        {
            if(ring.size()>65536-std::min<std::size_t>(vertices,65536)){vertices=65537;break;}
            vertices+=ring.size();
        }
        // Only immutable original world geometry is retained. Layout membership,
        // blocker indices and board dimensions remain local to every query.
        // In-flight obstacle handles survive the bounded cache reaching its cap.
        if(cacheable&&FixedWorld.size()<4096&&vertices<=65536-FixedWorldVertices)
        {FixedWorld.emplace(key,world);FixedWorldVertices+=vertices;}
        return world;
    }
    Impl(const Problem& p,std::shared_ptr<NfpCache> cache,const std::function<bool()>& stop):P(p),Cache(std::move(cache))
    {
        if(!Cache)Cache=std::make_shared<NfpCache>(NfpCacheSettings{512,4096,32*1024*1024});
        if(p.Stocks.size()!=1||p.Stocks[0].Quantity==0||p.PartGap!=0||p.Tolerance<1e-7||p.Parts.size()>1024)return;
        if(stop&&stop())return;if(!CheckProblem(p).empty())return;
        Prepared.reserve(p.Parts.size());
        MirroredPrepared.resize(p.Parts.size());
        for(std::size_t i=0;i<p.Parts.size();++i)
        {
            if(stop&&stop())return;
            const auto& part=p.Parts[i];
            if(part.Geometry.Radius>0||!part.Geometry.Holes.empty()||part.Material!=p.Stocks[0].Material)return;
            NfpBuildOptions build;build.TimeLimitSeconds=5;build.Cancel=stop;
            Prepared.push_back(Cache->Prepare(part.Geometry,build));if(!Prepared.back())return;
            double perimeter=0;for(std::size_t j=0;j<part.Geometry.Outer.size();++j)
            {const auto a=part.Geometry.Outer[j],b=part.Geometry.Outer[(j+1)%part.Geometry.Outer.size()];perimeter+=std::hypot(a.X-b.X,a.Y-b.Y);}
            Perimeters.push_back(perimeter);
            Indices.emplace(part.ID,i);
        }
        IsReady=true;
    }
    std::shared_ptr<const PairGeometry> Get(std::size_t stationary,double stationaryAngle,std::size_t moving,double movingAngle,
        const std::function<bool()>& stop,bool fullContacts=true,bool stationaryMirrored=false,bool movingMirrored=false,
        bool implicitMaterialProof=false)
    {
        const PairKey key{stationary,moving,NormalizeAngle(stationaryAngle),NormalizeAngle(movingAngle),fullContacts,
            stationaryMirrored,movingMirrored,implicitMaterialProof};
        if(const auto it=Pairs.find(key);it!=Pairs.end())return it->second;
        if(stop&&stop())return nullptr;
        NfpBuildOptions build;build.TimeLimitSeconds=5;build.Cancel=stop;
        const auto prepared=[&](std::size_t index,bool mirrored)->const NfpPreparedShape&
        {
            auto& value=mirrored?MirroredPrepared[index]:Prepared[index];
            if(!value)value=Cache->Prepare(P.Parts[index].Geometry,build,mirrored);
            return value;
        };
        const auto& fixedPrepared=prepared(stationary,stationaryMirrored);
        const auto& movingPrepared=prepared(moving,movingMirrored);
        const auto lookup=Cache->Get(fixedPrepared,key.StationaryAngle,movingPrepared,key.MovingAngle,build);
        if(lookup.State!=NfpStatus::Ready||!lookup.Region)return nullptr;
        PairGeometry geometry;
        for(const auto& ring:lookup.Region->Loops)
        {
            if(stop&&stop())return nullptr;
            const auto bytes=PairPayloadBytes(geometry);
            if(bytes>=PairByteBudget||ring.size()>(PairByteBudget-bytes)/sizeof(PointD))return nullptr;
            auto& path=geometry.Forbidden.emplace_back();path.reserve(ring.size());
            for(const auto point:ring){const auto q=Transform(point,lookup.Transform);path.emplace_back(q.X,q.Y);}
        }
        if(!fullContacts)geometry.Incomplete=true;
        else
        {
        // Actual parallel source edges retain touch-only channels discarded by
        // regularized polygon unions. Coalesce collinear overlapping intervals
        // once per immutable pair rather than at every search node.
        struct LineInterval
        {
            std::tuple<long long,long long,long long> Key;
            Point Direction;
            double Offset,Low,High;
            std::size_t Order;
        };
        // Bound both optional contact pools before their quadratic enumeration.
        constexpr std::size_t maxIntervals=PairByteBudget/(16*sizeof(LineInterval));
        constexpr std::size_t maxMaterialContacts=PairByteBudget/(16*sizeof(Segment));
        std::vector<LineInterval> intervals;
        const auto a=MaterialRing(P.Parts[stationary].Geometry.Outer);
        const auto b=MaterialRing(P.Parts[moving].Geometry.Outer);
        if(a.size()<3||b.size()<3)return nullptr;
        const auto intervalCapacity=a.size()>maxIntervals/b.size()?maxIntervals:a.size()*b.size();
        intervals.reserve(intervalCapacity);
        const bool completeMaterial=!implicitMaterialProof&&a.size()<=maxMaterialContacts/2/b.size();
        if(completeMaterial)geometry.MaterialContacts.reserve(2*a.size()*b.size());
        // Only the optional strict-interior certificate is omitted. Every
        // candidate contact channel below is still enumerated in source order.
        struct SourceEdge
        {
            Point A,B;
            double X,Y,Length;
        };
        const auto prepareEdges=[&](const std::vector<Point>& ring,Pose pose)
        {
            std::vector<Point> vertices;vertices.reserve(ring.size());
            for(std::size_t i=0;i<ring.size();++i)
            {
                if((i&63)==63&&stop&&stop())return std::vector<SourceEdge>{};
                vertices.push_back(Transform(ring[i],pose));
            }
            std::vector<SourceEdge> edges;edges.reserve(vertices.size());
            for(std::size_t i=0;i<vertices.size();++i)
            {
                const auto p=vertices[i],q=vertices[(i+1)%vertices.size()];
                const double x=q.X-p.X,y=q.Y-p.Y;
                edges.push_back({p,q,x,y,std::hypot(x,y)});
            }
            return edges;
        };
        const auto fixedEdges=prepareEdges(a,{0,0,key.StationaryAngle,key.StationaryMirrored});
        const auto movingEdges=prepareEdges(b,{0,0,key.MovingAngle,key.MovingMirrored});
        if(fixedEdges.size()!=a.size()||movingEdges.size()!=b.size())return nullptr;
        const double unit=std::max(1e-9,P.Tolerance*.1);
        bool intervalLimit=false;
        for(std::size_t i=0;i<a.size()&&!intervalLimit;++i)
        {
            if(stop&&stop())return nullptr;
            const auto& fixed=fixedEdges[i];const auto p=fixed.A,q=fixed.B;
            const double ax=fixed.X,ay=fixed.Y,al=fixed.Length;if(al==0)continue;
            for(std::size_t j=0;j<b.size();++j)
            {
                if((j&63)==63&&stop&&stop())return nullptr;
                const auto& movingEdge=movingEdges[j];const auto r=movingEdge.A,s=movingEdge.B;
                if(completeMaterial)
                {
                    geometry.MaterialContacts.push_back({{p.X-r.X,p.Y-r.Y},{q.X-r.X,q.Y-r.Y},0});
                    geometry.MaterialContacts.push_back({{p.X-r.X,p.Y-r.Y},{p.X-s.X,p.Y-s.Y},0});
                }
                const double bx=movingEdge.X,by=movingEdge.Y,bl=movingEdge.Length;
                if(bl==0||std::abs(ax*by-ay*bx)>1e-10*al*bl)continue;
                Point d{ax/al,ay/al};if(d.X< -1e-12||(std::abs(d.X)<=1e-12&&d.Y<0)){d.X=-d.X;d.Y=-d.Y;}
                const Point endpoints[]={{p.X-r.X,p.Y-r.Y},{p.X-s.X,p.Y-s.Y},{q.X-r.X,q.Y-r.Y},{q.X-s.X,q.Y-s.Y}};
                double lo=std::numeric_limits<double>::infinity(),hi=-lo;
                for(const auto x:endpoints){const auto t=x.X*d.X+x.Y*d.Y;lo=std::min(lo,t);hi=std::max(hi,t);}
                const double offset=-d.Y*endpoints[0].X+d.X*endpoints[0].Y;
                const auto lk=std::tuple(std::llround(d.X*1e10),std::llround(d.Y*1e10),std::llround(offset/unit));
                if(intervals.size()==maxIntervals){geometry.Incomplete=true;intervalLimit=true;break;}
                intervals.push_back({lk,d,offset,lo,hi,intervals.size()});
            }
        }
        geometry.MaterialContactsComplete=completeMaterial&&!intervalLimit&&
            geometry.MaterialContacts.size()==2*a.size()*b.size();
        std::sort(intervals.begin(),intervals.end(),[](const LineInterval& first,const LineInterval& second)
        {return std::tie(first.Key,first.Order)<std::tie(second.Key,second.Order);});
        geometry.Contacts.reserve(intervals.size());
        for(std::size_t first=0;first<intervals.size();)
        {
            if(stop&&stop())return nullptr;
            auto last=first+1;
            while(last<intervals.size()&&intervals[last].Key==intervals[first].Key)++last;
            const auto direction=intervals[first].Direction;
            const double offset=intervals[first].Offset;
            std::sort(intervals.begin()+first,intervals.begin()+last,[](const LineInterval& left,const LineInterval& right)
            {return std::tie(left.Low,left.High)<std::tie(right.Low,right.High);});
            double lo=intervals[first].Low,hi=intervals[first].High;
            const auto emit=[&](double left,double right)
            {
                const auto d=direction;const auto c=offset;
                geometry.Contacts.push_back({{d.X*left-d.Y*c,d.Y*left+d.X*c},{d.X*right-d.Y*c,d.Y*right+d.X*c},0});
            };
            for(std::size_t i=first+1;i<last;++i)
                if(intervals[i].Low<=hi+unit)hi=std::max(hi,intervals[i].High);
                else{emit(lo,hi);lo=intervals[i].Low;hi=intervals[i].High;}
            emit(lo,hi);
            first=last;
        }
        if(implicitMaterialProof&&!geometry.Incomplete&&AxisSafety::ArithmeticSupported())
        {
            // The two source edge BVHs replace only the quadratic rejection
            // payload. The parallel candidate intervals above are unchanged.
            const auto used=PairPayloadBytes(geometry);
            constexpr std::size_t nodeBytes=sizeof(AxisSafety::EdgeBounds)+4*sizeof(std::size_t);
            constexpr std::size_t edgeBytes=sizeof(Point)+sizeof(AxisSafety::EdgeBounds)+sizeof(std::size_t)+2*nodeBytes;
            constexpr std::size_t overhead=sizeof(MaterialContactIndex)+2*sizeof(AxisSafety::EdgeIndex)+512;
            if(used<PairByteBudget&&overhead<=PairByteBudget-used&&
                fixedEdges.size()<=(PairByteBudget-used-overhead)/edgeBytes&&
                movingEdges.size()<=(PairByteBudget-used-overhead)/edgeBytes-fixedEdges.size())
            {
                auto proof=std::make_shared<MaterialContactIndex>();
                const auto prepareBoundary=[&](const auto& source,MaterialBoundaryIndex& boundary)
                {
                    boundary.Vertices.reserve(source.size());boundary.Edges.reserve(source.size());
                    for(std::size_t i=0;i<source.size();++i)
                    {
                        if((i&63)==63&&stop&&stop())return false;
                        const auto p=source[i].A,q=source[i].B;
                        if(!AxisSafety::CoordinateBounds(AxisSafety::Number(p.X)).Valid()||
                            !AxisSafety::CoordinateBounds(AxisSafety::Number(p.Y)).Valid()||
                            !AxisSafety::CoordinateBounds(AxisSafety::Number(q.X)).Valid()||
                            !AxisSafety::CoordinateBounds(AxisSafety::Number(q.Y)).Valid())return false;
                        boundary.Vertices.push_back(p);
                        boundary.Edges.push_back({{std::min(p.X,q.X),std::max(p.X,q.X)},
                            {std::min(p.Y,q.Y),std::max(p.Y,q.Y)},true});
                        boundary.Magnitude=std::max({boundary.Magnitude,std::abs(p.X),std::abs(p.Y)});
                    }
                    // Query also supports <=16 edges through its original
                    // complete linear fallback; Ready()==false is not empty.
                    boundary.Index=std::make_unique<AxisSafety::EdgeIndex>(boundary.Edges);
                    return true;
                };
                if(prepareBoundary(fixedEdges,proof->Fixed)&&prepareBoundary(movingEdges,proof->Moving))
                {
                    const auto bytes=[&](const MaterialBoundaryIndex& boundary)
                    {
                        // EdgeIndex reserves n order entries and 2*n Nodes.
                        // Include its private node fields and allocation slack.
                        return boundary.Vertices.capacity()*sizeof(Point)+boundary.Edges.capacity()*sizeof(AxisSafety::EdgeBounds)+
                            sizeof(AxisSafety::EdgeIndex)+boundary.Edges.size()*(sizeof(std::size_t)+2*nodeBytes)+256;
                    };
                    proof->Bytes=sizeof(MaterialContactIndex)+bytes(proof->Fixed)+bytes(proof->Moving);
                    if(proof->Bytes<=PairByteBudget-used)
                    {geometry.MaterialIndex=std::move(proof);geometry.MaterialContactsComplete=true;}
                }
                if(stop&&stop())return nullptr;
            }
        }
        }
        // Bound retained transformed pairs/contact loci independently of the
        // prepared NFP cache. In-flight query handles survive cache eviction.
        const auto bytes=PairPayloadBytes(geometry);geometry.Bytes=bytes;
        auto retained=std::make_shared<const PairGeometry>(std::move(geometry));
        if(bytes>PairByteBudget)return nullptr;
        if(PairBytes>PairByteBudget-bytes||Pairs.size()>=1024){Pairs.clear();PairBytes=0;}
        PairBytes+=bytes;Pairs.emplace(key,retained);return retained;
    }
    NfpPlacementQuery Find(const Layout& layout,std::size_t index,double angle,const NfpPlacementLimits& limits,
        const std::function<bool()>& stop)
    {
        using Clock=std::chrono::steady_clock;
        const bool phaseDiagnostics=bool(limits.Trace);
        const auto queryStarted=phaseDiagnostics?Clock::now():Clock::time_point{};
        struct PhaseTiming{double Seconds=0;std::size_t Calls=0;};
        struct PhaseTimer
        {
            PhaseTiming* Target=nullptr;Clock::time_point Started{};
            PhaseTimer(PhaseTiming& target,bool enabled)
            {if(enabled){Target=&target;++Target->Calls;Started=Clock::now();}}
            void Stop()noexcept
            {if(Target){Target->Seconds+=std::chrono::duration<double>(Clock::now()-Started).count();Target=nullptr;}}
            ~PhaseTimer(){Stop();}
        };
        PhaseTiming pairPreparationTime,pairGetTime,freeBooleanTime,contactPartitionTime,priorityTime,sortTime,
            poseValidationTime,strictForbiddenTime,competitivePoseTime,necessaryFitTime,originalMaterialTime,
            boundaryGeometryTime,fullGeometryTime,boundaryContainmentTime,fullContainmentTime;
        std::size_t priorityCalls=0,strictForbiddenRejected=0,necessaryFitRejected=0,necessaryFitUnknown=0;
        std::size_t competitivePoseRejected=0,competitivePoseUnknown=0;
        NfpPlacementQuery result;result.Supported=IsReady;
        const double verificationGap=limits.VerificationGap.value_or(P.PartGap);
        const bool strongerVerification=limits.VerificationGap.has_value()&&verificationGap>P.PartGap;
        // The bounded source-edge index supplies the same strict rejection
        // proof without materializing Cartesian loci. Complete construction
        // may reuse it too; the default explicit mode remains an oracle.
        const bool implicitMaterialProof=limits.IndexedMaterialProof||
            (limits.StopAfterFirstFit&&bool(limits.CandidatePriority));
        bool tracing=static_cast<bool>(limits.Trace);
        std::uint64_t nextTraceID=0;
        std::size_t rawEmissions=0,rawKept=0,preCapFits=0;
        using ProposalKey=std::tuple<long long,long long,bool>;
        std::map<ProposalKey,std::uint64_t> traceIDs;
        std::set<std::uint64_t> traceVisited;
        const char* exitReason="complete";
        const auto disableTrace=[&]()noexcept
        {
            tracing=false;result.FitsIDs.clear();traceIDs.clear();traceVisited.clear();
            if(limits.TraceHealth){limits.TraceHealth->Failed.store(true);++limits.TraceHealth->Omitted;}
        };
        const auto observe=[&](ConstructionTraceStage stage,std::uint64_t id,const char* source,Pose pose,
            bool accepted,const char* reason,std::initializer_list<std::pair<const char*,double>> measures={})noexcept
        {
            if(!tracing)return;
            if(limits.TraceHealth&&limits.TraceHealth->Failed.load())
            {++limits.TraceHealth->Omitted;return;}
            try
            {
                ConstructionTraceEvent event;event.Stage=stage;event.Source=source;event.CandidateID=id;
                event.PartIndex=index;event.StockInstance=layout.StockInstance;
                event.BoardWidth=layout.Width;event.BoardHeight=layout.Height;
                event.Attempts=result.Attempts;event.Accepted=accepted;event.Reason=reason;
                event.SearchTruncated=result.Truncated;
                if(id&&std::isfinite(pose.X)&&std::isfinite(pose.Y)&&std::isfinite(pose.AngleDegrees))
                    event.Members.push_back({index,std::numeric_limits<std::size_t>::max(),pose,0});
                for(const auto& measure:measures)event.Measures.emplace_back(measure.first,measure.second);
                limits.Trace(event);
            }
            catch(...)
            {
                if(limits.TraceHealth){limits.TraceHealth->Failed.store(true);++limits.TraceHealth->Omitted;}
                /* Observation cannot reject or add a placement. */
            }
        };
        const auto finish=[&]()
        {
            if(tracing&&rawKept>traceVisited.size())
                observe(ConstructionTraceStage::Budget,0,"nfp.query",{},false,exitReason,
                    {{"raw-kept",double(rawKept)},{"raw-visited",double(traceVisited.size())},
                     {"raw-unvisited",double(rawKept-traceVisited.size())}});
            observe(ConstructionTraceStage::Complete,0,"nfp.query",{},result.Supported,exitReason,
                {{"supported",result.Supported?1.:0.},{"truncated",result.Truncated?1.:0.},
                 {"raw-emissions",double(rawEmissions)},{"raw-kept",double(rawKept)},
                 {"raw-visited",double(traceVisited.size())},{"generated",double(result.Generated)},
                 {"fits-before-cap",double(preCapFits)},{"fits-returned",double(result.Fits.size())},
                 {"blocked-returned",double(result.Blocked.size())},{"domain-measured",result.DomainMeasured?1.:0.},
                 {"free-translation-area",result.FreeTranslationArea},
                 {"phaseQuerySeconds",phaseDiagnostics?std::chrono::duration<double>(Clock::now()-queryStarted).count():0.},
                 {"phasePairPreparationSeconds",pairPreparationTime.Seconds},{"phasePairPreparationCalls",double(pairPreparationTime.Calls)},
                 {"phasePairGetSeconds",pairGetTime.Seconds},{"phasePairGetCalls",double(pairGetTime.Calls)},
                 {"phaseFreeDomainBooleanSeconds",freeBooleanTime.Seconds},{"phaseFreeDomainBooleanCalls",double(freeBooleanTime.Calls)},
                 {"phaseContactPartitionSeconds",contactPartitionTime.Seconds},{"phaseContactPartitionCalls",double(contactPartitionTime.Calls)},
                 {"phaseCandidatePrioritySeconds",priorityTime.Seconds},{"phaseCandidatePriorityCalls",double(priorityCalls)},
                 {"phaseCandidatePriorityPasses",double(priorityTime.Calls)},
                 {"phaseCandidateSortSeconds",sortTime.Seconds},{"phaseCandidateSortCalls",double(sortTime.Calls)},
                 {"phasePoseValidationSeconds",poseValidationTime.Seconds},{"phasePoseValidationCalls",double(poseValidationTime.Calls)},
                 {"phaseStrictForbiddenSeconds",strictForbiddenTime.Seconds},{"phaseStrictForbiddenCalls",double(strictForbiddenTime.Calls)},
                 {"phaseStrictForbiddenRejected",double(strictForbiddenRejected)},
                  {"phaseCompetitivePoseSeconds",competitivePoseTime.Seconds},{"phaseCompetitivePoseCalls",double(competitivePoseTime.Calls)},
                  {"phaseCompetitivePoseRejected",double(competitivePoseRejected)},
                  {"phaseCompetitivePoseUnknown",double(competitivePoseUnknown)},
                 {"phaseNecessaryFitSeconds",necessaryFitTime.Seconds},{"phaseNecessaryFitCalls",double(necessaryFitTime.Calls)},
                 {"phaseNecessaryFitRejected",double(necessaryFitRejected)},
                 {"phaseNecessaryFitUnknown",double(necessaryFitUnknown)},
                 {"phaseBoundaryGeometrySeconds",boundaryGeometryTime.Seconds},{"phaseBoundaryGeometryCalls",double(boundaryGeometryTime.Calls)},
                 {"phaseFullGeometrySeconds",fullGeometryTime.Seconds},{"phaseFullGeometryCalls",double(fullGeometryTime.Calls)},
                 {"phaseBoundaryContainmentSeconds",boundaryContainmentTime.Seconds},{"phaseBoundaryContainmentCalls",double(boundaryContainmentTime.Calls)},
                 {"phaseFullContainmentSeconds",fullContainmentTime.Seconds},{"phaseFullContainmentCalls",double(fullContainmentTime.Calls)},
                 {"phaseOriginalMaterialGateSeconds",originalMaterialTime.Seconds},{"phaseOriginalMaterialGateCalls",double(originalMaterialTime.Calls)},
                 // Get is inside pair preparation. Strict/material gates and
                 // caller AcceptFit are inside pose validation, not additive.
                 {"phaseTimesNested",1.}});
            return std::move(result);
        };
        observe(ConstructionTraceStage::Begin,0,"nfp.query",{},true,"query-begin",
            {{"angle",angle},{"fixed-placements",double(layout.Placements.size())},
             {"moving-mirrored",limits.Mirrored?1.:0.},
             {"max-candidates",double(limits.MaxCandidates)},{"max-raw-candidates",double(limits.MaxRawCandidates)},
             {"max-contact-segments",double(limits.MaxContactSegments)},{"max-attempts",double(limits.MaxAttempts)},
             {"max-fixed-placements",double(limits.MaxFixedPlacements)},
             {"include-contact-channels",limits.IncludeContactChannels?1.:0.},
             {"stop-after-first-fit",limits.StopAfterFirstFit?1.:0.},
             {"stop-after-candidate-limit",limits.StopAfterCandidateLimit?1.:0.},{"fast-adjacency",limits.FastAdjacency?1.:0.},
             {"boundary-vertices-only",limits.BoundaryVerticesOnly?1.:0.},
             {"prioritize-free-domain-retreat",limits.PrioritizeFreeDomainRetreat?1.:0.},
             {"caller-priority",limits.CandidatePriority?1.:0.},{"additional-fit-guard",limits.AcceptFit?1.:0.},
             {"necessary-fit-guard",limits.NecessaryFit?1.:0.},
              {"competitive-pose-guard",limits.CompetitivePose?1.:0.},
             {"material-contact-proof-requested",1.},{"material-contact-proof-implicit",implicitMaterialProof?1.:0.},
             {"proposal-gap",P.PartGap},{"verification-gap",verificationGap},
             {"verification-gap-override",limits.VerificationGap.has_value()?1.:0.},
             {"verification-gap-rejection-retreat",strongerVerification?1.:0.},
             {"max-blockers",double(limits.MaxBlockers)},{"max-blocked-candidates",double(limits.MaxBlockedCandidates)}});
        if(limits.VerificationGap&&(!std::isfinite(verificationGap)||verificationGap<P.PartGap||limits.MaxBlockers))
        {result.Supported=false;exitReason="invalid-verification-gap";return finish();}
        if(limits.TranslationWindow&&!ValidTranslationWindow(*limits.TranslationWindow))
        {result.Supported=false;result.Truncated=true;exitReason="invalid-translation-window";return finish();}
        if(layout.Placements.size()>limits.MaxFixedPlacements||!std::isfinite(layout.Width)||!std::isfinite(layout.Height)||layout.Width<=0||layout.Height<=0)
        {result.Supported=false;exitReason="invalid-layout-or-fixed-placement-limit";return finish();}
        if(!IsReady||index>=P.Parts.size()||limits.MaxBlockers>8)
        {exitReason="unsupported-query-or-angle";return finish();}
        if(!PoseAllowed(P.Parts[index],{0,0,angle,limits.Mirrored},P.Tolerance))
        {result.Supported=false;exitReason="disallowed-moving-orientation";return finish();}
        bool cancelled=false;
        const auto stopped=[&]{if(!cancelled&&stop&&stop())cancelled=true;
            if(cancelled){result.Truncated=true;exitReason="stopped";}return cancelled;};
        if(stopped())return finish();
        if(limits.FastAdjacency&&limits.StopAfterFirstFit&&(limits.MaxRawCandidates==0||limits.MaxAttempts==0))
        {result.Truncated=true;exitReason="zero-raw-or-attempt-limit";return finish();}
        const auto& part=P.Parts[index];const auto box=Bounds(part.Geometry,{0,0,angle,limits.Mirrored});
        double lx=P.EdgeMargin-box.X,ly=P.EdgeMargin-box.Y;
        const double rx=layout.Width-P.EdgeMargin-box.X-box.Width,ty=layout.Height-P.EdgeMargin-box.Y-box.Height;
        if(rx<lx-P.Tolerance||ty<ly-P.Tolerance){exitReason="empty-rectangle-translation-domain";return finish();}
        double ux=std::max(lx,rx),uy=std::max(ly,ty);
        if(limits.TranslationWindow)
        {
            const auto& window=*limits.TranslationWindow;
            lx=std::max(lx,window.X);ly=std::max(ly,window.Y);
            ux=std::min(ux,window.X+window.Width);uy=std::min(uy,window.Y+window.Height);
            if(ux<lx||uy<ly)
            {result.DomainMeasured=true;exitReason="empty-translation-window-intersection";return finish();}
        }
        const double unit=std::max(1e-8,P.Tolerance*.25);
        std::vector<Point> origins;std::set<ProposalKey> seen;
        std::map<ProposalKey,std::vector<Point>> inwardDirections;
        std::vector<std::vector<AxisSafety::Vector>> freeRings;
        const auto emit=[&](Point q,const char* source="nfp.raw",std::size_t owner=0,std::size_t detail=0)
        {
            const auto id=tracing?++nextTraceID:0;
            if(tracing)++rawEmissions;
            const auto original=q;
            if(!std::isfinite(q.X)||!std::isfinite(q.Y)||q.X<lx-P.Tolerance||q.X>ux+P.Tolerance||q.Y<ly-P.Tolerance||q.Y>uy+P.Tolerance)
            {observe(ConstructionTraceStage::Filter,id,source,{q.X,q.Y,angle,limits.Mirrored},false,"raw-nonfinite-or-outside-domain",
                {{"owner",double(owner)},{"source-index",double(detail)},
                 {"finite-x",std::isfinite(original.X)?1.:0.},{"finite-y",std::isfinite(original.Y)?1.:0.}});return;}
            q.X=std::clamp(q.X,lx,ux);q.Y=std::clamp(q.Y,ly,uy);
            if(origins.size()>=limits.MaxRawCandidates)
            {result.Truncated=true;observe(ConstructionTraceStage::Filter,id,source,{q.X,q.Y,angle,limits.Mirrored},false,"raw-candidate-limit",
                {{"owner",double(owner)},{"source-index",double(detail)},{"raw-x",original.X},{"raw-y",original.Y}});return;}
            const ProposalKey key{std::llround(q.X/unit),std::llround(q.Y/unit),limits.Mirrored};
            if(seen.emplace(key).second)
            {
                origins.push_back(q);
                if(tracing)
                {
                    ++rawKept;
                    try{traceIDs.emplace(key,id);}catch(...){disableTrace();}
                }
                observe(ConstructionTraceStage::Generate,id,source,{q.X,q.Y,angle,limits.Mirrored},true,"raw-candidate",
                    {{"owner",double(owner)},{"source-index",double(detail)},
                     {"raw-x",original.X},{"raw-y",original.Y},{"raw-index",double(origins.size()-1)}});
            }
            else
            {
                const auto prior=tracing?traceIDs.find(key):traceIDs.end();
                observe(ConstructionTraceStage::Filter,id,source,{q.X,q.Y,angle,limits.Mirrored},false,"raw-duplicate",
                    {{"owner",double(owner)},{"source-index",double(detail)},
                     {"raw-x",original.X},{"raw-y",original.Y},
                     {"duplicate-of",prior==traceIDs.end()?0.:double(prior->second)}});
            }
        };
        const auto traceID=[&](Point q)
        {
            if(!tracing)return std::uint64_t{};
            const auto found=traceIDs.find({std::llround(q.X/unit),std::llround(q.Y/unit),limits.Mirrored});
            return found==traceIDs.end()?std::uint64_t{}:found->second;
        };
        struct Obstacle
        {
            std::size_t Index;
            Pose Transform;
            Rect Box;
            std::shared_ptr<const PairGeometry> Pair;
            PathD Material;
            std::shared_ptr<const FixedWorldGeometry> World;
        };
        std::vector<Obstacle> obstacles;obstacles.reserve(layout.Placements.size());
        double obstacleRoundoff=0;
        for(const auto& placed:layout.Placements)
        {
            if(stopped())return finish();
            const auto it=Indices.find(placed.PartID);if(it==Indices.end()){result.Supported=false;exitReason="unknown-fixed-part";return finish();}
            if(!PoseAllowed(P.Parts[it->second],placed.Transform,P.Tolerance))
            {result.Supported=false;exitReason="disallowed-fixed-orientation";return finish();}
            PathD material;if(limits.MaxBlockers)for(const auto point:P.Parts[it->second].Geometry.Outer)
            {const auto q=Transform(point,placed.Transform);material.emplace_back(q.X,q.Y);}
            auto world=FixedGeometry(it->second,placed.Transform);
            obstacleRoundoff=std::max(obstacleRoundoff,world->Nominal.Roundoff);
            obstacles.push_back({it->second,placed.Transform,world->Box,
                {},std::move(material),std::move(world)});
        }
        PathD movingWorld;
        if(limits.MaxBlockers) movingWorld.resize(part.Geometry.Outer.size());
        std::optional<AxisSafety::Geometry> movingQuarter;
        const auto movingGeometry=[&](Pose pose)
        {
            PhaseTimer geometryTime{fullGeometryTime,phaseDiagnostics};
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
            // Arbitrary angles retain their original rounded world vertices and
            // pose-dependent roundoff. Unsupported arithmetic never uses a cache.
            return AxisSafety::Make(part.Geometry,pose);
        };
        const auto movingBoundary=[&](Pose pose)
        {
            PhaseTimer geometryTime{boundaryGeometryTime,phaseDiagnostics};
            return AxisSafety::MakeBoundary(part.Geometry,pose);
        };
        const auto tryPose=[&](Pose pose,bool retainBlocked,bool& retryable,std::uint64_t id)
        {
            PhaseTimer poseTime{poseValidationTime,phaseDiagnostics};
            retryable=false;
            if(result.Attempts>=limits.MaxAttempts)
            {result.Truncated=true;exitReason="attempt-limit";observe(ConstructionTraceStage::Budget,id,"nfp.validation",pose,false,"attempt-limit");return false;}
            if(stopped())
            {observe(ConstructionTraceStage::Budget,id,"nfp.validation",pose,false,"stopped-before-validation");return false;}
            if(limits.CompetitivePose)
            {
                // Only the cheap final-rank screen belongs before numerical
                // geometry. Material/gap NecessaryFit follows strict NFP
                // rejection, so blocked interiors never pay for that screen.
                bool possible=true;
                {PhaseTimer competitiveTime{competitivePoseTime,phaseDiagnostics};
                    try{possible=limits.CompetitivePose(pose,id);}
                    catch(...)
                    {if(phaseDiagnostics)++competitivePoseUnknown;
                        observe(ConstructionTraceStage::Analysis,id,"nfp.validation",pose,true,"competitive-pose-guard-unproved");}}
                if(!possible)
                {
                    retryable=true;if(phaseDiagnostics)++competitivePoseRejected;
                    observe(ConstructionTraceStage::Filter,id,"nfp.validation",pose,false,"competitive-pose-guard-rejected");
                    return false;
                }
            }
            // A score-only rejection is bounded by the raw event stream and
            // finite retreat levels, and has its own work counters. It must
            // not consume the later constructors' material-attempt allowance.
            ++result.Attempts;
            if(!InsideSheet(part.Geometry,pose,layout.Width,layout.Height,P.EdgeMargin,P.Tolerance))
            {observe(ConstructionTraceStage::Filter,id,"nfp.validation",pose,false,"original-outside-sheet");return false;}
            const bool boundaryFirst=bool(limits.NecessaryFit);
            auto nominal=boundaryFirst?movingBoundary(pose):movingGeometry(pose);
            AxisSafety::Verdict containment;
            {PhaseTimer containmentTime{boundaryFirst?boundaryContainmentTime:fullContainmentTime,phaseDiagnostics};
                containment=AxisSafety::Containment(nominal,layout.Width,layout.Height,P.EdgeMargin);}
            if(containment==AxisSafety::Verdict::Uncertain)
            {result.Truncated=true;observe(ConstructionTraceStage::Filter,id,"nfp.validation",pose,false,"nominal-containment-uncertain");return false;}
            if(containment==AxisSafety::Verdict::Blocked)
            {retryable=true;observe(ConstructionTraceStage::Filter,id,"nfp.validation",pose,false,"nominal-containment-blocked");return false;}
            const Rect moving=Bounds(part.Geometry,pose);
            if(limits.MaxBlockers==0)for(const auto& obstacle:obstacles)
                if(obstacle.Pair&&BoxesOverlap(moving,obstacle.Box,P.Tolerance))
                {
                    bool forbidden;
                    {PhaseTimer strictTime{strictForbiddenTime,phaseDiagnostics};
                        forbidden=StrictlyForbidden(*obstacle.Pair,
                            {pose.X-obstacle.Transform.X,pose.Y-obstacle.Transform.Y},P.Tolerance*.5);}
                    if(forbidden)
                    {if(phaseDiagnostics)++strictForbiddenRejected;
                        observe(ConstructionTraceStage::Filter,id,"nfp.validation",pose,false,"strict-nfp-interior",
                            {{"fixed-part-index",double(obstacle.Index)}});return false;}
                }
            if(limits.NecessaryFit)
            {
                bool possible=true;
                {PhaseTimer necessaryTime{necessaryFitTime,phaseDiagnostics};
                    try{possible=limits.NecessaryFit(pose,id);}
                    catch(...)
                    {if(phaseDiagnostics)++necessaryFitUnknown;
                        observe(ConstructionTraceStage::Analysis,id,"nfp.validation",pose,true,"necessary-fit-guard-unproved");}}
                if(!possible)
                {
                    retryable=true;if(phaseDiagnostics)++necessaryFitRejected;
                    observe(ConstructionTraceStage::Filter,id,"nfp.validation",pose,false,"necessary-fit-guard-rejected");
                    return false;
                }
            }
            if(boundaryFirst)
            {
                // Boundary preparation retained the same represented rings,
                // roundoff and sheet rule, but cannot certify full material.
                // Only surviving poses pay for the original convex/hull work.
                nominal=movingGeometry(pose);
                {PhaseTimer containmentTime{fullContainmentTime,phaseDiagnostics};
                    containment=AxisSafety::Containment(nominal,layout.Width,layout.Height,P.EdgeMargin);}
                if(containment==AxisSafety::Verdict::Uncertain)
                {result.Truncated=true;observe(ConstructionTraceStage::Filter,id,"nfp.validation",pose,false,"nominal-containment-uncertain");return false;}
                if(containment==AxisSafety::Verdict::Blocked)
                {retryable=true;observe(ConstructionTraceStage::Filter,id,"nfp.validation",pose,false,"nominal-containment-blocked");return false;}
            }
            PhaseTimer materialTime{originalMaterialTime,phaseDiagnostics};
            std::vector<std::size_t> blockers;double overlapArea=0;
            bool nativeBlocked=false,nominalBlocked=false;
            bool verificationRejected=false,verificationUncertain=false,verificationNativeBlocked=false;
            if(strongerVerification)for(std::size_t oi=0;oi<obstacles.size();++oi)
            {
                if(stopped())
                {observe(ConstructionTraceStage::Budget,id,"nfp.validation",pose,false,"stopped-during-validation");return false;}
                const auto& obstacle=obstacles[oi];
                const auto& fixed=obstacle.World->Nominal;
                // The zero-gap proposal broad phase cannot certify a positive
                // clearance. Use the original-material gate's exact envelope
                // certificate, including both represented-world roundoffs.
                if(AxisSafety::EnvelopesSeparated(nominal,fixed,
                    AxisSafety::Clearance(nominal,fixed,verificationGap)))continue;
                const bool conflict=ShapesConflict(part.Geometry,pose,P.Parts[obstacle.Index].Geometry,
                    obstacle.Transform,verificationGap,P.Tolerance,P.AllowCommonEdges);
                const auto verdict=conflict?AxisSafety::Verdict::Blocked:
                    AxisSafety::Pair(nominal,fixed,verificationGap,P.AllowCommonEdges,P.Tolerance);
                if(!conflict&&verdict==AxisSafety::Verdict::Safe)continue;
                verificationRejected=true;
                verificationUncertain=verdict==AxisSafety::Verdict::Uncertain;
                verificationNativeBlocked=conflict;
                if(verificationUncertain)result.Truncated=true;
                observe(ConstructionTraceStage::Filter,id,"nfp.validation",pose,false,
                    conflict?"verification-gap-material-blocked":verificationUncertain?
                    "verification-gap-pair-uncertain":"verification-gap-pair-blocked",
                    {{"fixed-index",double(oi)},{"fixed-part-index",double(obstacle.Index)},
                     {"proposal-gap",P.PartGap},{"verification-gap",verificationGap}});
                break;
            }
            if(limits.MaxBlockers)for(std::size_t i=0;i<movingWorld.size();++i)
            {const auto world=Transform(part.Geometry.Outer[i],pose);movingWorld[i]={world.X,world.Y};}
            // A stronger query uses one authoritative material gate, on both
            // success and failure. Its rejected/uncertain poses may use the
            // same bounded actual retreat probes without repeating a zero-gap
            // diagnosis. The default query retains its established gate below.
            if(!strongerVerification)for(std::size_t oi=0;oi<obstacles.size();++oi)
            {
                if(stopped())
                {observe(ConstructionTraceStage::Budget,id,"nfp.validation",pose,false,"stopped-during-validation");return false;}
                const auto& obstacle=obstacles[oi];
                if(!Near(moving,obstacle.Box,P.Tolerance+nominal.Roundoff+obstacle.World->Nominal.Roundoff)) continue;
                double overlap=0;bool conflict=false;
                if(limits.MaxBlockers&&BoxesOverlap(moving,obstacle.Box,P.Tolerance))
                {
                    // The same original-ring, EvenOdd, precision-8 material
                    // intersection used by zero-gap ShapesConflict. Keep the
                    // area rather than transforming the same rings a second
                    // time for a proposal-ranking metric.
                    try{overlap=std::abs(Clipper2Lib::Area(Intersect(PathsD{movingWorld},PathsD{obstacle.Material},FillRule::EvenOdd,8)));}
                    catch(...){overlap=std::numeric_limits<double>::infinity();}
                    conflict=overlap>P.Tolerance*P.Tolerance;
                    // ShapesConflict additionally proves separation on source
                    // edges. Do not turn a clipping-created sliver into a fake
                    // blocker. Deep overlaps keep the cached clipping fast path.
                    if(conflict&&overlap<=ContactAreaUncertainty(Perimeters[index]+Perimeters[obstacle.Index],moving,obstacle.Box))
                        conflict=ShapesConflict(P.Parts[obstacle.Index].Geometry,obstacle.Transform,part.Geometry,pose,0,P.Tolerance);
                }
                else if(!limits.MaxBlockers)
                    conflict=ShapesConflict(P.Parts[obstacle.Index].Geometry,obstacle.Transform,part.Geometry,pose,0,P.Tolerance);
                const auto verdict=AxisSafety::Pair(nominal,obstacle.World->Nominal,0,P.AllowCommonEdges,P.Tolerance);
                if(verdict==AxisSafety::Verdict::Uncertain)
                {result.Truncated=true;observe(ConstructionTraceStage::Filter,id,"nfp.validation",pose,false,"nominal-pair-uncertain",
                    {{"fixed-index",double(oi)},{"fixed-part-index",double(obstacle.Index)}});return false;}
                nativeBlocked=nativeBlocked||conflict;
                nominalBlocked=nominalBlocked||verdict==AxisSafety::Verdict::Blocked;
                if(conflict||verdict==AxisSafety::Verdict::Blocked)
                {blockers.push_back(oi);overlapArea+=overlap;}
            }
            retryable=verificationRejected||(nominalBlocked&&!nativeBlocked);
            if(blockers.empty()&&!verificationRejected)
            {
                if(limits.AcceptFit&&!limits.AcceptFit(pose,id))
                {
                    retryable=true;
                    observe(ConstructionTraceStage::Filter,id,"nfp.validation",pose,false,"additional-fit-guard-rejected");
                    return false;
                }
                result.Fits.push_back(pose);
                if(tracing)
                {
                    ++preCapFits;
                    try{result.FitsIDs.push_back(id);}catch(...){disableTrace();}
                }
                observe(ConstructionTraceStage::Filter,id,"nfp.validation",pose,true,"original-material-fit",
                    {{"fit-index-before-cap",double(result.Fits.size()-1)},
                     {"proposal-gap",P.PartGap},{"verification-gap",verificationGap},
                     {"verification-gap-override",limits.VerificationGap.has_value()?1.:0.}});
                return true;
            }
            observe(ConstructionTraceStage::Filter,id,"nfp.validation",pose,false,
                verificationRejected?"verification-gap-rejected":"original-or-nominal-pair-blocked",
                {{"blocker-count",double(blockers.size())},{"native-blocked",nativeBlocked?1.:0.},
                 {"nominal-blocked",nominalBlocked?1.:0.},{"overlap-area",overlapArea},
                 {"verification-gap",verificationGap},{"verification-gap-uncertain",verificationUncertain?1.:0.},
                 {"verification-native-blocked",verificationNativeBlocked?1.:0.},
                 {"legacy-native-classification-skipped",verificationRejected?1.:0.},
                 {"roundoff-retryable",retryable?1.:0.}});
            if(!verificationRejected&&retainBlocked&&blockers.size()<=limits.MaxBlockers)
                result.Blocked.push_back({pose,std::move(blockers),overlapArea});
            return false;
        };
        constexpr std::array<Point,8> directions{{{1,0},{0,1},{-1,0},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}}};
        const auto freeLocation=[&](Point point)
        {
            if(freeRings.empty()||!AxisSafety::ArithmeticSupported())return AxisSafety::Location::Uncertain;
            bool inside=false;
            try
            {
                const AxisSafety::Vector q{AxisSafety::Number(point.X),AxisSafety::Number(point.Y)};
                // Boolean output is a set of disjoint nested contours. Parity
                // handles outer rings, holes and islands without guessing their
                // orientation. A boundary/uncertain probe cannot choose a side.
                for(const auto& ring:freeRings)
                {
                    if(stopped())return AxisSafety::Location::Uncertain;
                    const auto location=AxisSafety::InRing(q,ring,true);
                    if(location==AxisSafety::Location::Uncertain||location==AxisSafety::Location::Boundary)return location;
                    if(location==AxisSafety::Location::Inside)inside=!inside;
                }
                return inside?AxisSafety::Location::Inside:AxisSafety::Location::Outside;
            }
            catch(const AxisSafety::ArithmeticUncertainty&){return AxisSafety::Location::Uncertain;}
        };
        const auto checkPose=[&](Point q,bool retainBlocked)
        {
            const Pose proposal{q.X,q.Y,angle,limits.Mirrored};
            const auto id=traceID(q);
            if(tracing)try{traceVisited.insert(id);}catch(...){disableTrace();}
            observe(ConstructionTraceStage::Filter,id,"nfp.validation",proposal,true,"validation-attempt",
                {{"raw-visit-index",double(traceVisited.size())}});
            bool retryable=false;
            bool found=tryPose(proposal,retainBlocked,retryable,id);
            if(!found&&retryable)
            {
                // Retreat uses only Roundoff; its value is identical in the
                // boundary view and does not require convex/hull preparation.
                const auto nominal=limits.CompetitivePose||limits.NecessaryFit?movingBoundary(proposal):movingGeometry(proposal);
                const double retreat=std::max(ContactLinearUncertainty(Bounds(part.Geometry,proposal),
                    {q.X,q.Y,layout.Width,layout.Height}),2*(nominal.Roundoff+obstacleRoundoff));
                if(limits.BoundaryVerticesOnly&&limits.PrioritizeFreeDomainRetreat&&limits.MaxAttempts-result.Attempts>3*directions.size())
                    if(const auto hints=inwardDirections.find({std::llround(q.X/unit),std::llround(q.Y/unit),limits.Mirrored});hints!=inwardDirections.end())
                        for(const auto hint:hints->second)
                        {
                            if(stopped())break;
                            const Point positive{q.X+hint.X*retreat,q.Y+hint.Y*retreat},negative{q.X-hint.X*retreat,q.Y-hint.Y*retreat};
                            const auto pos=freeLocation(positive),neg=freeLocation(negative);
                            Point inward;
                            if(pos==AxisSafety::Location::Inside&&neg==AxisSafety::Location::Outside)inward=positive;
                            else if(neg==AxisSafety::Location::Inside&&pos==AxisSafety::Location::Outside)inward=negative;
                            else continue;
                            const Pose retry{inward.X,inward.Y,angle,limits.Mirrored};bool ignored=false;
                            observe(ConstructionTraceStage::Generate,id,"nfp.free-domain-inward-retreat",retry,true,
                                "strict-domain-probes-select-side-before-original-validation",
                                {{"origin-x",q.X},{"origin-y",q.Y},{"hint-x",hint.X},{"hint-y",hint.Y},{"distance",retreat}});
                            found=tryPose(retry,false,ignored,id);
                            // Keep the complete established 8-direction/3-level
                            // fallback if this domain-derived proposal fails.
                            break;
                        }
                // Retry actual poses after coarse event deduplication. Exact
                // contacts are tested first so collapsed legal channels survive.
                for(unsigned level=0;level<3&&!found;++level)
                {
                    const double distance=std::ldexp(retreat,int(level)*2);
                    for(const auto direction:directions)
                    {
                        if(result.Attempts>=limits.MaxAttempts||stopped()) break;
                        bool ignored=false;
                        const Pose retry{q.X+direction.X*distance,q.Y+direction.Y*distance,angle,limits.Mirrored};
                        observe(ConstructionTraceStage::Generate,id,"nfp.roundoff-retreat",retry,true,"roundoff-retreat",
                            {{"origin-x",q.X},{"origin-y",q.Y},{"level",double(level)},
                             {"direction-x",direction.X},{"direction-y",direction.Y},{"distance",distance}});
                        found=tryPose(retry,false,ignored,id);
                        if(found) break;
                    }
                    if(result.Attempts>=limits.MaxAttempts||stopped()) break;
                }
            }
            return found;
        };
        // Envelopes propose cheap locations only. The same original-material
        // and nominal-geometry gates validate both these and later NFP events.
        if(limits.FastAdjacency&&limits.StopAfterFirstFit&&limits.MaxBlockers==0&&!limits.CandidatePriority)
        {
            emit({lx,ly},"nfp.fast-board-corner");
            for(const auto& obstacle:obstacles)
            {
                const auto& occupied=obstacle.Box;
                const double right=occupied.X+occupied.Width-box.X,top=occupied.Y+occupied.Height-box.Y;
                const auto owner=static_cast<std::size_t>(&obstacle-obstacles.data())+1;
                emit({right,ly},"nfp.fast-adjacency",owner,0);emit({lx,top},"nfp.fast-adjacency",owner,1);
                emit({right,occupied.Y-box.Y},"nfp.fast-adjacency",owner,2);emit({occupied.X-box.X,top},"nfp.fast-adjacency",owner,3);
                emit({right,occupied.Y+occupied.Height-box.Y-box.Height},"nfp.fast-adjacency",owner,4);
                emit({occupied.X+occupied.Width-box.X-box.Width,top},"nfp.fast-adjacency",owner,5);
            }
            std::stable_sort(origins.begin(),origins.end(),[](Point a,Point b)
            {return std::tie(a.Y,a.X)<std::tie(b.Y,b.X);});
            for(const auto q:origins)
            {
                if(result.Attempts>=limits.MaxAttempts||stopped())break;
                if(checkPose(q,false))
                {result.Generated=origins.size();result.Truncated=true;exitReason="first-fit-fast-adjacency";return finish();}
            }
            const auto corners=origins.size();
            emit({ux,ly},"nfp.fast-board-corner",0,1);emit({lx,uy},"nfp.fast-board-corner",0,2);emit({ux,uy},"nfp.fast-board-corner",0,3);
            for(std::size_t i=corners;i<origins.size();++i)
            {
                if(result.Attempts>=limits.MaxAttempts||stopped())break;
                if(checkPose(origins[i],false))
                {result.Generated=origins.size();result.Truncated=true;exitReason="first-fit-fast-board-corner";return finish();}
            }
            if(result.Attempts>=limits.MaxAttempts||stopped())
            {result.Generated=origins.size();result.Truncated=true;if(!cancelled)exitReason="attempt-limit";return finish();}
        }
        emit({lx,ly},"nfp.board-corner",0,0);emit({ux,ly},"nfp.board-corner",0,1);
        emit({lx,uy},"nfp.board-corner",0,2);emit({ux,uy},"nfp.board-corner",0,3);
        // Repeated instances share one immutable handle within this query.
        // Optional contact payload yields to NFP boundaries as boards grow.
        std::map<PairKey,std::shared_ptr<const PairGeometry>> queryPairs;
        const auto pairAllowance=PairByteBudget/std::max<std::size_t>(1,layout.Placements.size());
        PathsD forbidden;std::vector<Segment> edges,contacts;std::vector<Point> pairVertices;
        edges.push_back({{lx,ly},{ux,ly},0});edges.push_back({{ux,ly},{ux,uy},0});
        edges.push_back({{ux,uy},{lx,uy},0});edges.push_back({{lx,uy},{lx,ly},0});
        std::size_t owner=0;
        for(auto& obstacle:obstacles)
        {
            if(stopped())return finish();
            PhaseTimer pairTime{pairPreparationTime,phaseDiagnostics};
            ++owner;
            const PairKey pairKey{obstacle.Index,index,NormalizeAngle(obstacle.Transform.AngleDegrees),NormalizeAngle(angle),
                !limits.BoundaryVerticesOnly,obstacle.Transform.Mirrored,limits.Mirrored,implicitMaterialProof};
            std::shared_ptr<const PairGeometry> pair;
            if(const auto found=queryPairs.find(pairKey);found!=queryPairs.end())pair=found->second;
            else
            {
                {PhaseTimer getTime{pairGetTime,phaseDiagnostics};
                    pair=Get(obstacle.Index,obstacle.Transform.AngleDegrees,index,angle,stop,!limits.BoundaryVerticesOnly,
                        obstacle.Transform.Mirrored,limits.Mirrored,implicitMaterialProof);}
                if(pair&&pair->Bytes>pairAllowance)
                {
                    auto bounded=std::make_shared<PairGeometry>();bounded->Forbidden=pair->Forbidden;
                    bounded->Incomplete=pair->Incomplete;
                    // Dropping only the optional proof leaves the candidate
                    // stream complete and always disables strict rejection.
                    bounded->Bytes=PairPayloadBytes(*bounded);
                    if(bounded->Bytes>pairAllowance){result.Truncated=true;exitReason="pair-byte-limit";pairTime.Stop();return finish();}
                    bounded->Contacts=pair->Contacts;bounded->Bytes=PairPayloadBytes(*bounded);
                    if(bounded->Bytes>pairAllowance)
                    {
                        std::vector<Segment>{}.swap(bounded->Contacts);
                        bounded->Incomplete=true;
                        bounded->Bytes=PairPayloadBytes(*bounded);
                    }
                    pair=std::move(bounded);
                }
                if(pair)queryPairs.emplace(pairKey,pair);
            }
            if(!pair){result.Truncated=true;exitReason="pair-preparation-unavailable";pairTime.Stop();return finish();}
            result.Truncated=result.Truncated||pair->Incomplete;
            obstacle.Pair=pair;
            for(const auto& path:pair->Forbidden)
            {
                auto& world=forbidden.emplace_back();world.reserve(path.size());
                for(const auto q:path){world.emplace_back(q.x+obstacle.Transform.X,q.y+obstacle.Transform.Y);
                    if(!limits.BoundaryVerticesOnly)pairVertices.push_back({world.back().x,world.back().y});}
                if(!limits.BoundaryVerticesOnly)for(std::size_t j=0;j<world.size();++j)
                {
                    Segment edge{{world[j].x,world[j].y},{world[(j+1)%world.size()].x,world[(j+1)%world.size()].y},owner};
                    if(Clip(edge,lx,ly,ux,uy))edges.push_back(edge);
                }
            }
            for(const auto& relative:pair->Contacts)
            {
                Segment contact{{relative.A.X+obstacle.Transform.X,relative.A.Y+obstacle.Transform.Y},
                    {relative.B.X+obstacle.Transform.X,relative.B.Y+obstacle.Transform.Y},owner};
                if(Clip(contact,lx,ly,ux,uy))contacts.push_back(contact);
            }
        }
        if(stopped())return finish();
        if(ux-lx>P.Tolerance&&uy-ly>P.Tolerance)
        {
            PathsD domain{{{lx,ly},{ux,ly},{ux,uy},{lx,uy}}};
            try
            {
                const auto free=[&]
                {PhaseTimer booleanTime{freeBooleanTime,phaseDiagnostics};
                    return forbidden.empty()?domain:Difference(domain,Union(forbidden,FillRule::NonZero,8),FillRule::NonZero,8);}();
                if(limits.BoundaryVerticesOnly&&limits.PrioritizeFreeDomainRetreat&&free.size()<=1024)
                {
                    std::size_t vertices=0;
                    for(const auto& path:free)
                    {if(path.size()>65536-std::min<std::size_t>(vertices,65536)){vertices=65537;break;}vertices+=path.size();}
                    if(vertices<=65536)for(const auto& path:free)
                    {
                        auto& ring=freeRings.emplace_back();ring.reserve(path.size());
                        for(const auto q:path)ring.push_back({AxisSafety::Number(q.x),AxisSafety::Number(q.y)});
                    }
                }
                result.FreeTranslationArea=std::abs(Clipper2Lib::Area(free));result.DomainMeasured=true;
                observe(ConstructionTraceStage::Analysis,0,"nfp.free-domain",{},true,"free-domain-measured",
                    {{"polygon-count",double(free.size())},{"area",result.FreeTranslationArea}});
                std::size_t polygon=0;
                for(const auto& path:free)
                {
                    for(std::size_t j=0;j<path.size();++j)
                    {const auto a=path[j],b=path[(j+1)%path.size()];emit({a.x,a.y},"nfp.free-domain-vertex",polygon,j);
                        if(limits.BoundaryVerticesOnly&&!freeRings.empty()&&path.size()>=3&&AxisSafety::ArithmeticSupported())try
                        {
                            const auto p=path[(j+path.size()-1)%path.size()];
                            const AxisSafety::Vector previous{AxisSafety::Number(p.x),AxisSafety::Number(p.y)},
                                current{AxisSafety::Number(a.x),AxisSafety::Number(a.y)},next{AxisSafety::Number(b.x),AxisSafety::Number(b.y)};
                            if(AxisSafety::OrientationSign(previous,current,next,true)!=0)
                            {
                                const double px=a.x-p.x,py=a.y-p.y,nx=b.x-a.x,ny=b.y-a.y,pl=std::hypot(px,py),nl=std::hypot(nx,ny);
                                if(pl>unit&&nl>unit)
                                {
                                    Point hint{-py/pl-ny/nl,px/pl+nx/nl};const double scale=std::max(std::abs(hint.X),std::abs(hint.Y));
                                    if(std::isfinite(scale)&&scale>1e-8)
                                    {
                                        hint.X/=scale;hint.Y/=scale;
                                        auto& hints=inwardDirections[{std::llround(std::clamp(a.x,lx,ux)/unit),std::llround(std::clamp(a.y,ly,uy)/unit),limits.Mirrored}];
                                        if(hints.size()<4)hints.push_back(hint);
                                    }
                                }
                            }
                        }
                        catch(const AxisSafety::ArithmeticUncertainty&){}
                        if(!limits.BoundaryVerticesOnly)emit({(a.x+b.x)*.5,(a.y+b.y)*.5},"nfp.free-domain-midpoint",polygon,j);}
                    ++polygon;
                }
            }
            catch(...){result.Supported=false;exitReason="free-domain-boolean-failed";return finish();}
        }
        else result.DomainMeasured=true;
        if(limits.MaxBlockers&&origins.size()<limits.MaxRawCandidates)
        {
            // With deliberate displacement, legal proposals may lie inside
            // the forbidden union. Preserve intersections between individual
            // neighbours' NFP edges; their union boundary cannot expose them.
            const auto cap=origins.size()+(limits.MaxRawCandidates-origins.size())/2;
            bool capped=false;
            for(std::size_t i=0;i<edges.size()&&!capped;++i)
            {
                if((i&7)==0&&stopped())return finish();
                const auto& a=edges[i];const double ax=a.B.X-a.A.X,ay=a.B.Y-a.A.Y;
                for(std::size_t j=0;j<i;++j)
                {
                    if((j&255)==0&&stopped())return finish();
                    const auto& b=edges[j];if(a.Owner==b.Owner)continue;
                    if(std::max(a.A.X,a.B.X)<std::min(b.A.X,b.B.X)-P.Tolerance||std::max(b.A.X,b.B.X)<std::min(a.A.X,a.B.X)-P.Tolerance||
                        std::max(a.A.Y,a.B.Y)<std::min(b.A.Y,b.B.Y)-P.Tolerance||std::max(b.A.Y,b.B.Y)<std::min(a.A.Y,a.B.Y)-P.Tolerance)continue;
                    const double bx=b.B.X-b.A.X,by=b.B.Y-b.A.Y,den=ax*by-ay*bx,dx=b.A.X-a.A.X,dy=b.A.Y-a.A.Y;
                    if(std::abs(den)>1e-12*std::max(1.,std::hypot(ax,ay)*std::hypot(bx,by)))
                    {
                        const double t=(dx*by-dy*bx)/den,u=(dx*ay-dy*ax)/den;
                        if(t>=-1e-9&&t<=1+1e-9&&u>=-1e-9&&u<=1+1e-9)emit({a.A.X+t*ax,a.A.Y+t*ay},"nfp.displacement-edge-intersection",a.Owner,j);
                    }
                    else if(std::abs(dx*ay-dy*ax)<=P.Tolerance*std::hypot(ax,ay))
                    {emit(a.A,"nfp.displacement-collinear",a.Owner,i);emit(a.B,"nfp.displacement-collinear",a.Owner,i);
                     emit(b.A,"nfp.displacement-collinear",b.Owner,j);emit(b.B,"nfp.displacement-collinear",b.Owner,j);}
                    if(origins.size()>=cap){result.Truncated=true;capped=true;break;}
                }
            }
        }
        // Even the fast mode retains exact contact-line endpoints/midpoints.
        // Full channel mode partitions each line at all other obstacles' NFP
        // boundaries; this recovers legal intervals swallowed by regularization.
        if(contacts.size()>limits.MaxContactSegments)
        {
            result.Truncated=true;
            if(tracing)
            {
                std::size_t selected=0;
                for(std::size_t j=0;j<contacts.size();++j)
                {
                    const bool keep=selected<limits.MaxContactSegments&&j==selected*contacts.size()/limits.MaxContactSegments;
                    if(keep)++selected;
                    const auto& segment=contacts[j];
                    observe(ConstructionTraceStage::Filter,0,"nfp.contact-segment",{},keep,
                        keep?"contact-segment-retained":"contact-segment-limit",
                        {{"segment-index",double(j)},{"owner",double(segment.Owner)},
                         {"a-x",segment.A.X},{"a-y",segment.A.Y},{"b-x",segment.B.X},{"b-y",segment.B.Y},
                         {"segments-before-cap",double(contacts.size())},{"segment-cap",double(limits.MaxContactSegments)}});
                }
            }
            std::vector<Segment> reduced;reduced.reserve(limits.MaxContactSegments);
            for(std::size_t j=0;j<limits.MaxContactSegments;++j)reduced.push_back(contacts[j*contacts.size()/limits.MaxContactSegments]);
            contacts=std::move(reduced);
        }
        observe(ConstructionTraceStage::Analysis,0,"nfp.contact-segment",{},true,"contact-segments-ready",
            {{"retained-segments",double(contacts.size())}});
        for(std::size_t i=0;i<contacts.size();++i)
        {
            if((i&15)==0&&stopped())return finish();
            PhaseTimer partitionTime{contactPartitionTime,phaseDiagnostics};
            const auto& c=contacts[i];const double dx=c.B.X-c.A.X,dy=c.B.Y-c.A.Y,len2=dx*dx+dy*dy;
            emit(c.A,"nfp.contact-endpoint-a",c.Owner,i);emit(c.B,"nfp.contact-endpoint-b",c.Owner,i);
            emit({(c.A.X+c.B.X)*.5,(c.A.Y+c.B.Y)*.5},"nfp.contact-midpoint",c.Owner,i);
            if(!limits.IncludeContactChannels||len2<=unit*unit)continue;
            std::vector<double> cuts{0,1};
            for(const auto& edge:edges)
            {
                if(edge.Owner==c.Owner)continue;
                const double ex=edge.B.X-edge.A.X,ey=edge.B.Y-edge.A.Y,den=dx*ey-dy*ex;
                const double ax=edge.A.X-c.A.X,ay=edge.A.Y-c.A.Y;
                if(std::abs(den)>1e-12*std::max(1.,std::sqrt(len2)*std::hypot(ex,ey)))
                {
                    const double t=(ax*ey-ay*ex)/den,u=(ax*dy-ay*dx)/den;
                    if(t>=-1e-9&&t<=1+1e-9&&u>=-1e-9&&u<=1+1e-9)cuts.push_back(std::clamp(t,0.,1.));
                }
                else if(std::abs(ax*dy-ay*dx)<=P.Tolerance*std::sqrt(len2))
                    for(const auto q:{edge.A,edge.B})
                    {const auto t=((q.X-c.A.X)*dx+(q.Y-c.A.Y)*dy)/len2;if(t>=-1e-9&&t<=1+1e-9)cuts.push_back(std::clamp(t,0.,1.));}
            }
            std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end(),[](double a,double b){return std::abs(a-b)<1e-10;}),cuts.end());
            for(std::size_t j=0;j<cuts.size();++j)
            {
                emit({c.A.X+cuts[j]*dx,c.A.Y+cuts[j]*dy},"nfp.contact-channel-endpoint",c.Owner,i);
                if(j)emit({c.A.X+(cuts[j-1]+cuts[j])*.5*dx,c.A.Y+(cuts[j-1]+cuts[j])*.5*dy},"nfp.contact-channel-midpoint",c.Owner,i);
            }
        }
        // A board-sized part has a line/point translation domain. Its only
        // feasible contact can be an intersection with an NFP edge.
        if(ux-lx<=P.Tolerance||uy-ly<=P.Tolerance)
        {
            for(const auto& edge:edges){emit(edge.A,"nfp.degenerate-domain-edge",edge.Owner);emit(edge.B,"nfp.degenerate-domain-edge",edge.Owner);}
            std::sort(origins.begin(),origins.end(),[&](Point a,Point b){return ux-lx<=P.Tolerance?a.Y<b.Y:a.X<b.X;});
            const auto copy=origins;for(std::size_t i=1;i<copy.size();++i)
                emit({(copy[i-1].X+copy[i].X)*.5,(copy[i-1].Y+copy[i].Y)*.5},"nfp.degenerate-domain-midpoint",0,i);
        }
        // Critical free-domain and real-contact events use the raw budget
        // before internal vertices of individual forbidden obstacles. The
        // latter cannot crowd out a last part's collapsed legal channel.
        std::size_t pairVertex=0;for(const auto q:pairVertices)emit(q,"nfp.pair-forbidden-vertex",0,pairVertex++);
        result.Generated=origins.size();
        // Check promising left contacts early when a caller supplies a strict
        // attempt budget, but retain all heights and angles in the final cap.
        if(limits.CandidatePriority)
        {
            struct OrderedPose{Point Origin;std::array<double,4> Priority;};
            std::vector<OrderedPose> ordered;ordered.reserve(origins.size());
            PhaseTimer keyTime{priorityTime,phaseDiagnostics};
            for(const auto origin:origins)
            {
                if(stopped()){keyTime.Stop();return finish();}
                if(phaseDiagnostics)++priorityCalls;
                const auto key=limits.CandidatePriority({origin.X,origin.Y,angle,limits.Mirrored});
                if(std::any_of(key.begin(),key.end(),[](double value){return !std::isfinite(value);}))
                {
                    result.Supported=false;result.Truncated=true;exitReason="nonfinite-caller-priority";
                    observe(ConstructionTraceStage::Filter,traceID(origin),"nfp.proposal-order",
                        {origin.X,origin.Y,angle,limits.Mirrored},false,"nonfinite-caller-priority");
                    keyTime.Stop();return finish();
                }
                ordered.push_back({origin,key});
            }
            keyTime.Stop();
            {PhaseTimer prioritySortTime{sortTime,phaseDiagnostics};
                std::stable_sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b)
                {return std::tie(a.Priority,a.Origin.X,a.Origin.Y)<std::tie(b.Priority,b.Origin.X,b.Origin.Y);});}
            for(std::size_t i=0;i<ordered.size();++i)origins[i]=ordered[i].Origin;
            observe(ConstructionTraceStage::Analysis,0,"nfp.proposal-order",{},true,"caller-priority-before-validation",
                {{"ordered-origins",static_cast<double>(ordered.size())},{"keys-evaluated",static_cast<double>(ordered.size())}});
        }
        else
        {PhaseTimer coordinateSortTime{sortTime,phaseDiagnostics};
            std::stable_sort(origins.begin(),origins.end(),[](Point a,Point b){return a.X<b.X||(a.X==b.X&&a.Y<b.Y);});}
        for(const auto q:origins)
        {
            if(result.Attempts>=limits.MaxAttempts) {result.Truncated=true;exitReason="attempt-limit";break;}
            if(stopped()) break;
            const bool found=checkPose(q,true);
            if(found&&(limits.StopAfterFirstFit||(limits.StopAfterCandidateLimit&&limits.MaxCandidates&&
                result.Fits.size()>=limits.MaxCandidates)))
            {
                result.Truncated=true;
                if(limits.MaxBlockedCandidates&&result.Blocked.size()>limits.MaxBlockedCandidates)
                    result.Blocked.resize(limits.MaxBlockedCandidates);
                exitReason=limits.StopAfterFirstFit?"first-fit":"candidate-fit-limit";return finish();
            }
        }
        if(limits.MaxBlockedCandidates&&result.Blocked.size()>limits.MaxBlockedCandidates)
        {
            result.Truncated=true;
            std::stable_sort(result.Blocked.begin(),result.Blocked.end(),[](const NfpBlockedPlacement& a,const NfpBlockedPlacement& b)
            {return std::tuple(a.Blockers.size(),a.OverlapArea,a.Transform.X,a.Transform.Y)<std::tuple(b.Blockers.size(),b.OverlapArea,b.Transform.X,b.Transform.Y);});
            result.Blocked.resize(limits.MaxBlockedCandidates);
        }
        if(limits.MaxCandidates&&result.Fits.size()>limits.MaxCandidates)
        {
            result.Truncated=true;const auto original=result.Fits;std::vector<bool> used(original.size(),false);result.Fits.clear();
            const auto originalIDs=std::move(result.FitsIDs);
            const auto take=[&](std::size_t j,const char* source)
            {
                if(!used[j]&&result.Fits.size()<limits.MaxCandidates)
                {
                    used[j]=true;result.Fits.push_back(original[j]);
                    const auto id=tracing&&j<originalIDs.size()?originalIDs[j]:0;
                    if(tracing)try{result.FitsIDs.push_back(id);}catch(...){disableTrace();}
                    observe(ConstructionTraceStage::Filter,id,source,original[j],true,"fit-retained-after-cap",
                        {{"fit-index-before-cap",double(j)},{"fit-index-after-cap",double(result.Fits.size()-1)}});
                }
            };
            take(0,"nfp.fit-cap-first");
            const std::size_t bands=std::max<std::size_t>(1,limits.MaxCandidates/2);
            // Leftmost contact in each horizontal band, including top and
            // bottom, survives independently of low-left ranking.
            for(std::size_t band=0;band<bands&&result.Fits.size()<limits.MaxCandidates;++band)
            {
                const double y=ly+(uy-ly)*(bands==1?0:double(band)/double(bands-1));std::size_t best=0;double distance=std::numeric_limits<double>::infinity();
                for(std::size_t j=0;j<original.size();++j){const double d=std::abs(original[j].Y-y);if(d<distance){distance=d;best=j;}}
                take(best,"nfp.fit-cap-y-band");
            }
            for(std::size_t band=0;band<bands&&result.Fits.size()<limits.MaxCandidates;++band)
            {
                const double x=lx+(ux-lx)*(bands==1?0:double(band)/double(bands-1));std::size_t best=0;double distance=std::numeric_limits<double>::infinity();
                for(std::size_t j=0;j<original.size();++j){const double d=std::abs(original[j].X-x);if(d<distance){distance=d;best=j;}}
                take(best,"nfp.fit-cap-x-band");
            }
            for(std::size_t j=0;j<original.size()&&result.Fits.size()<limits.MaxCandidates;++j)take(j,"nfp.fit-cap-fill");
            if(tracing)for(std::size_t j=0;j<original.size();++j)if(!used[j])
                observe(ConstructionTraceStage::Filter,j<originalIDs.size()?originalIDs[j]:0,"nfp.fit-cap",original[j],false,
                    "legal-fit-candidate-limit",{{"fit-index-before-cap",double(j)}});
        }
        else if(tracing)for(std::size_t j=0;j<result.Fits.size();++j)
            observe(ConstructionTraceStage::Filter,j<result.FitsIDs.size()?result.FitsIDs[j]:0,"nfp.fit-cap",result.Fits[j],true,
                "fit-retained-without-cap",{{"fit-index-before-cap",double(j)},{"fit-index-after-cap",double(j)}});
        return finish();
    }
};
NfpPlacementGenerator::NfpPlacementGenerator(const Problem& problem,std::shared_ptr<NfpCache> cache,const std::function<bool()>& stop)
    :Data(std::make_unique<Impl>(problem,std::move(cache),stop)){}
NfpPlacementGenerator::~NfpPlacementGenerator()=default;
bool NfpPlacementGenerator::Ready() const{return Data->IsReady;}
NfpPlacementQuery NfpPlacementGenerator::Find(const Layout& layout,std::size_t partIndex,double angle,
    const NfpPlacementLimits& limits,const std::function<bool()>& stop)
{return Data->Find(layout,partIndex,angle,limits,stop);}
}
