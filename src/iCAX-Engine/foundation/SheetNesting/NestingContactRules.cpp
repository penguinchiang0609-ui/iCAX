#include "NestingInternal.h"
#include "NestingNfpCombinations.h"
#include "SheetNestingBoundedContact.h"
#include "SheetNestingNfp.h"
#include "NestingAxisSafety.h"
#include <clipper2/clipper.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <iomanip>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>

namespace iCAX::SheetNesting::Detail
{
namespace
{
    struct Edge { Point A, B; double Length; bool CommonEdge = true; };
    struct Disc { Point Center; double Radius; };
    struct ContactBin { std::size_t StockIndex; Layout Output; };
    struct PatternInfo
    {
        Pattern Value;
        std::vector<std::pair<std::size_t, std::size_t>> Demand;
        double AnchorArea = 0;
        double CarrierFill = 0;
        double CoordinateScale = 1;
        std::string Material;
        std::string Signature;
        std::vector<Edge> LocalEdges;
        std::vector<Disc> LocalDiscs;
    };
    struct Fit
    {
        std::size_t PatternIndex = 0, BinIndex = 0, Feasible = 0;
        Point Origin;
        double Area = 0, Top = 0, Right = 0;
        double EdgeRemnant = 0;
        double Contact = 0;
        std::vector<Pose> MemberPoses;
    };
    struct WorldGeometry
    {
        AxisSafety::Geometry Material;
        Rect Box;
    };
    struct Obstacle
    {
        std::size_t PartIndex;
        Pose Transform;
        std::shared_ptr<const WorldGeometry> Geometry;
    };
    bool Near(Rect a,Rect b,double distance)
    {
        return a.X+a.Width+distance>=b.X&&b.X+b.Width+distance>=a.X&&
            a.Y+a.Height+distance>=b.Y&&b.Y+b.Height+distance>=a.Y;
    }
    std::vector<Edge> Edges(const Shape& shape, Pose pose)
    {
        std::vector<Edge> edges;
        const auto append = [&](const auto& ring, std::size_t ringIndex)
        {
            const bool commonEdge = shape.CommonEdgeRings.empty() ||
                (ringIndex < shape.CommonEdgeRings.size() && shape.CommonEdgeRings[ringIndex]);
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                const auto a = Transform(ring[i], pose), b = Transform(ring[(i + 1) % ring.size()], pose);
                const double length = std::hypot(b.X - a.X, b.Y - a.Y);
                if (length > 0) edges.push_back({a, b, length, commonEdge});
            }
        };
        append(shape.Outer, 0);
        for (std::size_t i = 0; i < shape.Holes.size(); ++i) append(shape.Holes[i], i + 1);
        if (edges.size() > 64)
        {
            std::stable_sort(edges.begin(), edges.end(), [](auto a, auto b) { return a.Length > b.Length; });
            edges.resize(64);
        }
        return edges;
    }
    double SharedLength(Edge a,Edge b,double tolerance)
    {
        if (!a.CommonEdge || !b.CommonEdge) return 0;
        const double dx=a.B.X-a.A.X,dy=a.B.Y-a.A.Y,len=a.Length;
        if(len<=tolerance)return 0;
        if(std::abs(dx*(b.A.Y-a.A.Y)-dy*(b.A.X-a.A.X))>tolerance*len||
            std::abs(dx*(b.B.Y-a.A.Y)-dy*(b.B.X-a.A.X))>tolerance*len)return 0;
        const double q0=((b.A.X-a.A.X)*dx+(b.A.Y-a.A.Y)*dy)/len;
        const double q1=((b.B.X-a.A.X)*dx+(b.B.Y-a.A.Y)*dy)/len;
        return std::max(0.0,std::min(len,std::max(q0,q1))-std::max(0.0,std::min(q0,q1)));
    }
    double MaterialHullArea(const Problem& p,const Layout& layout,const std::unordered_map<std::string,std::size_t>& byID)
    {
        std::vector<Point> points;
        for(const auto& placed:layout.Placements)
        {
            const auto& shape=p.Parts[byID.at(placed.PartID)].Geometry;
            if(shape.Radius>0)
            {
                const auto center=Transform(shape.Center,placed.Transform);
                for(int i=0;i<16;++i){const auto angle=2*Pi*i/16;points.push_back({center.X+shape.Radius*std::cos(angle),center.Y+shape.Radius*std::sin(angle)});}
            }
            else for(const auto point:shape.Outer)points.push_back(Transform(point,placed.Transform));
        }
        if(points.size()<3)return 0;
        std::sort(points.begin(),points.end(),[](Point a,Point b){return std::tie(a.X,a.Y)<std::tie(b.X,b.Y);});
        const auto cross=[](Point a,Point b,Point c){return (static_cast<long double>(b.X)-a.X)*(c.Y-a.Y)-(static_cast<long double>(b.Y)-a.Y)*(c.X-a.X);};
        std::vector<Point> hull;hull.reserve(points.size()*2);
        for(const auto point:points){while(hull.size()>1&&cross(hull[hull.size()-2],hull.back(),point)<=0)hull.pop_back();hull.push_back(point);}
        const auto lower=hull.size();
        for(auto it=points.rbegin()+1;it!=points.rend();++it){while(hull.size()>lower&&cross(hull[hull.size()-2],hull.back(),*it)<=0)hull.pop_back();hull.push_back(*it);}
        if(hull.size()<4)return 0;
        long double area=0;for(std::size_t i=1;i<hull.size();++i)area+=static_cast<long double>(hull[i-1].X)*hull[i].Y-static_cast<long double>(hull[i].X)*hull[i-1].Y;
        return static_cast<double>(std::abs(area)/2);
    }
    template<class Add>
    void DiscIntersections(Disc a, Disc b, double tolerance, Add add)
    {
        const long double dx = static_cast<long double>(b.Center.X) - a.Center.X;
        const long double dy = static_cast<long double>(b.Center.Y) - a.Center.Y;
        const long double d = std::hypot(dx, dy), ra = a.Radius, rb = b.Radius;
        if (d <= tolerance || d > ra + rb + tolerance || d < std::abs(ra - rb) - tolerance) return;
        const long double along = (d + (ra - rb) * (ra + rb) / d) / 2;
        const long double h2 = ra * ra - along * along;
        if (h2 < -static_cast<long double>(tolerance) * (2 * ra + tolerance)) return;
        const auto h = std::sqrt(std::max(0.0L, h2));
        const auto x = a.Center.X + along * dx / d, y = a.Center.Y + along * dy / d;
        add(Point{static_cast<double>(x - h * dy / d), static_cast<double>(y + h * dx / d)});
        add(Point{static_cast<double>(x + h * dy / d), static_cast<double>(y - h * dx / d)});
    }
}

Result RunContactRulesImpl(const Problem& p, const Options& options, ContactOrder order, bool boundedSearch, const Result* seed = nullptr, int diversity = 0, NfpCache* sharedNfp = nullptr, const std::vector<NfpPreparedShape>* sharedPrepared = nullptr, bool* sharedEmptyRoot = nullptr, const std::vector<Pattern>* sharedMotifs = nullptr, bool preserveComplements = false, NfpCompositePlacement placementPolicy = NfpCompositePlacement::EnvelopeArea)
{
    Result result;
    const auto start = std::chrono::steady_clock::now();
    bool cancelled = false, exhausted = false;
    const auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); };
    const auto stop = [&]
    {
        if (cancelled || exhausted) return true;
        if (options.Cancel && options.Cancel()) { cancelled = true; return true; }
        if (elapsed() >= options.TimeLimitSeconds || result.Stats.PlacementAttempts >= options.MaxPlacementAttempts)
        { exhausted = true; return true; }
        return false;
    };
    std::vector<std::size_t> consumed(p.Parts.size()), stockUsed(p.Stocks.size());
    std::unordered_map<std::string, std::size_t> byID;
    std::vector<double> areas(p.Parts.size());
    for (std::size_t i = 0; i < p.Parts.size(); ++i) { byID.emplace(p.Parts[i].ID, i); areas[i] = Area(p.Parts[i].Geometry); }
    // Reuse only exact quarter-turn material within this immutable job.
    // Other angles are reconstructed at their actual world pose by Make.
    std::map<std::tuple<std::size_t,double,bool>,std::optional<AxisSafety::Geometry>> quarterGeometry;
    const auto materialGeometry=[&](std::size_t index,Pose pose)
    {
        const auto& shape=p.Parts[index].Geometry;
        const double angle=NormalizeAngle(pose.AngleDegrees);
        if (shape.Radius>0||shape.Outer.size()<=32||(angle!=0&&angle!=90&&angle!=180&&angle!=270)||
            !AxisSafety::CoordinateBounds(AxisSafety::Number(pose.X)).Valid()||
            !AxisSafety::CoordinateBounds(AxisSafety::Number(pose.Y)).Valid())
            return AxisSafety::Make(shape,pose);
        const auto key=std::tuple(index,angle,pose.Mirrored);
        auto found=quarterGeometry.find(key);
        if (found==quarterGeometry.end())
        {
            if (quarterGeometry.size()>=128) return AxisSafety::Make(shape,pose);
            auto prepared=AxisSafety::Make(shape,{0,0,angle,pose.Mirrored});
            bool supported=prepared.Valid;
            for (const auto& ring:prepared.Rings) for (const auto& point:ring)
                supported=supported&&AxisSafety::BoundsOf(point).X.Valid()&&AxisSafety::BoundsOf(point).Y.Valid();
            found=quarterGeometry.emplace(key,supported?std::optional(std::move(prepared)):std::nullopt).first;
        }
        if (!found->second) return AxisSafety::Make(shape,pose);
        return AxisSafety::TranslateQuarterTurn(*found->second,{pose.X,pose.Y});
    };
    if(boundedSearch)
    {
        std::size_t total=0;
        for(const auto& part:p.Parts)total+=part.Quantity;
        if(!CheckProblem(p).empty()||p.Stocks.size()!=1||!p.Stocks[0].Quantity||p.Parts.size()>32||total>128)
        {
            for(const auto& part:p.Parts)if(part.Quantity)result.Remaining.push_back({part.ID,part.Quantity});
            result.State=Status::Partial;result.Diagnostics.push_back("Bounded contact search supports one stock, at most 32 specifications and 128 original instances.");
            result.Stats.ElapsedSeconds=elapsed();return result;
        }
    }
    std::vector<PatternInfo> patterns;
    std::vector<double> companionPriority;
    std::size_t separatedPatterns=0,rejectedPatterns=0,numericalRetreats=0;
    const bool cacheContacts=boundedSearch||sharedMotifs!=nullptr;
    double patternDeadline=std::numeric_limits<double>::infinity();
    const auto patternStop=[&] { return stop()||elapsed()>=patternDeadline; };
    const auto safeMember=[&](const Pattern& pattern,std::size_t index,Pose pose)
    {
        if (patternStop()) return false;
        const auto& member=pattern.Members[index];
        const auto& shape=p.Parts[member.PartIndex].Geometry;
        const auto geometry=materialGeometry(member.PartIndex,pose);
        if (!geometry.Valid) return false;
        for (std::size_t j=0;j<index;++j)
        {
            if (patternStop()) return false;
            const auto& other=pattern.Members[j];
            const auto& otherShape=p.Parts[other.PartIndex].Geometry;
            if (ShapesConflict(shape,pose,otherShape,other.Transform,p.PartGap,p.Tolerance,p.AllowCommonEdges)||
                AxisSafety::Pair(geometry,materialGeometry(other.PartIndex,other.Transform),p.PartGap,
                    p.AllowCommonEdges,p.Tolerance)!=AxisSafety::Verdict::Safe) return false;
        }
        return true;
    };
    const auto addPattern = [&](Pattern pattern)
    {
        if (patternStop() || !HasSinglePartType(pattern.Members)) return;
        bool separated=false;
        for (std::size_t i=1;i<pattern.Members.size();++i)
        {
            if (patternStop()) return;
            const auto original=pattern.Members[i].Transform;
            if (safeMember(pattern,i,original)) continue;
            const auto& shape=p.Parts[pattern.Members[i].PartIndex].Geometry;
            const auto box=Bounds(shape);
            const double magnitude=std::max({1.,std::abs(original.X),std::abs(original.Y),
                std::abs(box.X),std::abs(box.Y),std::abs(box.X+box.Width),std::abs(box.Y+box.Height)});
            const double step=std::max(4*p.Tolerance,1024*std::numeric_limits<double>::epsilon()*magnitude);
            bool repaired=false;
            for (const double factor:{1.,4.})
            {
                const double distance=step*factor;
                const std::array<Point,8> offsets{{{distance,0},{0,distance},{-distance,0},{0,-distance},
                    {distance,distance},{distance,-distance},{-distance,distance},{-distance,-distance}}};
                for (const auto offset:offsets)
                {
                    if (patternStop()) return;
                    ++result.Stats.PlacementAttempts;
                    const Pose candidate=ComposePose({offset.X,offset.Y,0},original);
                    if (safeMember(pattern,i,candidate))
                    {
                        pattern.Members[i].Transform=candidate;repaired=separated=true;break;
                    }
                }
                if (repaired) break;
            }
            if (!repaired) { ++rejectedPatterns; return; }
        }
        if (separated)
        {
            NormalizePattern(pattern,p);
            for (std::size_t i=1;i<pattern.Members.size();++i)
                if (!safeMember(pattern,i,pattern.Members[i].Transform)) { ++rejectedPatterns; return; }
            ++separatedPatterns;
        }
        PatternInfo info; info.Value = std::move(pattern);
        std::map<std::size_t, std::size_t> counts;
        for (const auto& m : info.Value.Members)
        {
            ++counts[m.PartIndex];info.AnchorArea=std::max(info.AnchorArea,areas[m.PartIndex]);
            const auto box=Bounds(p.Parts[m.PartIndex].Geometry);
            info.CoordinateScale=std::max({info.CoordinateScale,std::abs(m.Transform.X),std::abs(m.Transform.Y),
                std::abs(box.X)+std::abs(box.Y),std::abs(box.X+box.Width)+std::abs(box.Y+box.Height)});
        }
        for (auto count : counts)
        { info.Demand.push_back(count); info.Signature += std::to_string(count.first) + ":" + std::to_string(count.second) + ";"; }
        if (info.Value.Members.empty()) return;
        if (info.Value.Members.size()==1)
        {
            // A singleton already has its exact rotated bounds and material
            // area. Reclassifying the source for every direction repeats its
            // quadratic topology validation without changing this density.
            const double envelope=info.Value.Width*info.Value.Height;
            info.CarrierFill=envelope>0?info.Value.MaterialArea/envelope:0;
        }
        else
        {
            const auto carrier=AssessNfpCombinationCarrier(p,info.Value);
            info.CarrierFill=carrier.Fill;
            if(carrier.Filled)info.Value.Kind=carrier.Kind;
            else if(info.Value.Kind==ShapeKind::Rectangle||info.Value.Kind==ShapeKind::Parallelogram)
                info.Value.Kind=ShapeKind::Irregular;
        }
        info.Material = p.Parts[info.Value.Members.front().PartIndex].Material;
        if(cacheContacts)
        {
            for(const auto& member:info.Value.Members)
            {
                const auto& shape=p.Parts[member.PartIndex].Geometry;
                auto edges=Edges(shape,member.Transform);info.LocalEdges.insert(info.LocalEdges.end(),edges.begin(),edges.end());
                if(shape.Radius>0)info.LocalDiscs.push_back({Transform(shape.Center,member.Transform),shape.Radius});
            }
        }
        patterns.push_back(std::move(info));
    };
    // Keep every base direction available before spending a bounded preparation
    // slice on optional combinations and their original-material certification.
    for (std::size_t i = 0; i < p.Parts.size() && !stop(); ++i)
        if (p.Parts[i].Quantity)
            for (const auto orientation : CandidateOrientations(p.Parts[i], p.Tolerance))
            {
                const auto box = Bounds(p.Parts[i].Geometry,orientation);
                Pattern pattern;
                pattern.Members.push_back({i,ComposePose({-box.X,-box.Y,0},orientation)});
                pattern.Width = box.Width; pattern.Height = box.Height; pattern.MaterialArea = areas[i];
                addPattern(std::move(pattern));
            }
    const double combinationDeadline=std::min(options.TimeLimitSeconds*.25,elapsed()+1.);
    patternDeadline=combinationDeadline;
    if (options.EnableCombinations && !patternStop())
    {
        auto motifOptions = options;
        // A deterministic work quota avoids changing motifs merely because independent jobs run concurrently.
        motifOptions.MaxPlacementAttempts = std::max<std::size_t>(1, std::min<std::size_t>(2048, options.MaxPlacementAttempts / 10));
        if(boundedSearch||sharedMotifs)motifOptions.MaxPatterns=options.MaxPatterns/2;
        for (auto& pattern : BuildPatterns(p, motifOptions, patternStop))
        {
            if (patternStop()) break;
            if (pattern.Members.size() > 1) addPattern(std::move(pattern));
        }
    }
    patternDeadline=std::numeric_limits<double>::infinity();
    result.Stats.CandidatePatterns = patterns.size();
    result.Stats.PreparationSeconds = elapsed();
    std::vector<ContactBin> bins;
    std::size_t group = 1;
    const auto available = [&](const PatternInfo& pattern, const ContactBin& bin)
    {
        if (pattern.Material != p.Stocks[bin.StockIndex].Material) return false;
        for (auto [i, count] : pattern.Demand) if (count > p.Parts[i].Quantity - consumed[i]) return false;
        return true;
    };
    const auto staticRank = [&](const PatternInfo& pattern)
    {
        double imbalance = 0;
        if (order == ContactOrder::BalancedSpecifications || order == ContactOrder::BalancedRemnant)
        {
            double lo = 1, hi = 0;
            for (std::size_t i = 0; i < p.Parts.size(); ++i)
                if (p.Parts[i].Quantity && p.Parts[i].Material == pattern.Material)
                {
                    std::size_t n = consumed[i];
                    for (auto [index, count] : pattern.Demand) if (index == i) n += count;
                    const double fraction = static_cast<double>(n) / p.Parts[i].Quantity;
                    lo = std::min(lo, fraction); hi = std::max(hi, fraction);
                }
            imbalance = hi - lo;
        }
        const bool filledUnit = pattern.Value.Members.size() > 1 &&
            (pattern.Value.Kind == ShapeKind::Rectangle || pattern.Value.Kind == ShapeKind::Parallelogram);
        // Kind denotes a verified full original-material union. A skew full
        // parallelogram has density one even when its AABB contains empty corners.
        const double fill = pattern.CarrierFill;
        double priority=pattern.AnchorArea;
        if(diversity==1)priority=pattern.Value.Width;
        if(diversity==2)priority=pattern.Value.Height;
        if(diversity==3)priority=std::max(pattern.Value.Width,pattern.Value.Height);
        if(diversity==4)priority=std::min(pattern.Value.Width,pattern.Value.Height);
        if(diversity==5)priority=pattern.Value.MaterialArea;
        const auto index=static_cast<std::size_t>(&pattern-patterns.data());
        if(preserveComplements&&index<companionPriority.size()&&std::isfinite(companionPriority[index]))
            priority=companionPriority[index];
        return std::tuple(imbalance, -priority, filledUnit ? -1 : 0, -std::round(std::clamp(fill, 0.0, 1.0) * 1e8) / 1e8,
            -pattern.Value.MaterialArea, pattern.Signature);
    };
    const auto fitRank = [&](const Fit& fit)
    {
        if(boundedSearch||placementPolicy==NfpCompositePlacement::SharedBoundary)
            return std::tuple(-fit.Contact/std::max(1e-12,std::sqrt(patterns[fit.PatternIndex].Value.MaterialArea)),fit.Top,fit.Right,fit.Area,fit.Origin.Y);
        if(placementPolicy==NfpCompositePlacement::LowThenLeft)
            return std::tuple(fit.Top,fit.Right,fit.Area,fit.Origin.Y,fit.Origin.X);
        if (order == ContactOrder::PreserveRemnant || order == ContactOrder::BalancedRemnant)
            return std::tuple(-fit.EdgeRemnant, fit.Area, fit.Top, fit.Right, fit.Origin.Y);
        return order == ContactOrder::LowThenLeft ? std::tuple(fit.Top, fit.Right, fit.Area, fit.Origin.Y, fit.Origin.X)
            : std::tuple(fit.Area, fit.Top, fit.Right, fit.Origin.Y, fit.Origin.X);
    };
    // Exact poses are the cache key; no tolerance quantisation can substitute
    // one candidate's geometry for another. The cap bounds job-local memory.
    std::map<std::tuple<std::size_t,double,double,double,bool>,std::vector<Edge>> worldEdgeCache;
    std::map<std::tuple<std::size_t,double,double,double,bool>,std::shared_ptr<const WorldGeometry>> worldGeometryCache;
    const auto worldGeometry=[&](std::size_t index,Pose pose)
    {
        const auto key=std::tuple(index,pose.X,pose.Y,pose.AngleDegrees,pose.Mirrored);
        if (const auto found=worldGeometryCache.find(key);found!=worldGeometryCache.end()) return found->second;
        const auto& shape=p.Parts[index].Geometry;
        std::shared_ptr<const WorldGeometry> geometry=std::make_shared<WorldGeometry>(
            WorldGeometry{materialGeometry(index,pose),Bounds(shape,pose)});
        if (worldGeometryCache.size()<4096) worldGeometryCache.emplace(key,geometry);
        return geometry;
    };
    std::unique_ptr<NfpCache> ownedNfp;
    NfpCache* nfpCache=sharedNfp;
    if(boundedSearch&&p.PartGap==0&&!nfpCache)
    {
        NfpCacheSettings settings;settings.MaxRegionEntries=4096;settings.MaxBytes=32*1024*1024;
        ownedNfp=std::make_unique<NfpCache>(settings);nfpCache=ownedNfp.get();
    }
    std::vector<NfpPreparedShape> ownedPrepared;
    const auto* nfpShapes=sharedPrepared;
    if(nfpCache&&!nfpShapes)
    {
        ownedPrepared.resize(p.Parts.size());
        for(std::size_t i=0;i<p.Parts.size()&&!stop();++i)
        {
            const auto& shape=p.Parts[i].Geometry;
            if(shape.Radius>0||!shape.Holes.empty())continue;
            NfpBuildOptions build;build.TimeLimitSeconds=std::max(0.0,options.TimeLimitSeconds-elapsed());build.Cancel=stop;
            ownedPrepared[i]=nfpCache->Prepare(shape,build);
        }
        nfpShapes=&ownedPrepared;
    }
    if(sharedMotifs&&options.EnableCombinations)
    {
        patternDeadline=combinationDeadline;
        std::size_t existing=0;for(const auto& item:patterns)if(item.Value.Members.size()>1)++existing;
        for(const auto& pattern:*sharedMotifs)
        {if(patternStop()||existing>=options.MaxPatterns)break;addPattern(pattern);++existing;}
        patternDeadline=std::numeric_limits<double>::infinity();
        result.Stats.CandidatePatterns=patterns.size();result.Stats.PreparationSeconds=elapsed();
    }
    const auto query = [&](const ContactBin& bin, std::size_t pi, std::size_t bi, std::vector<Fit>* all = nullptr) -> std::optional<Fit>
    {
        const auto& info = patterns[pi]; const auto& pattern = info.Value;
        if (!available(info, bin) || stop()) return {};
        std::vector<Point> origins;
        std::set<std::pair<std::int64_t, std::int64_t>> seen;
        constexpr std::size_t cap = 4096;
        const auto emit = [&](Point o)
        {
            if (origins.size() >= cap || !std::isfinite(o.X) || !std::isfinite(o.Y) || o.X + p.Tolerance < p.EdgeMargin || o.Y + p.Tolerance < p.EdgeMargin ||
                o.X + pattern.Width > bin.Output.Width - p.EdgeMargin + p.Tolerance ||
                o.Y + pattern.Height > bin.Output.Height - p.EdgeMargin + p.Tolerance) return;
            const auto key = std::pair(std::llround(o.X / p.Tolerance), std::llround(o.Y / p.Tolerance));
            if (seen.insert(key).second) origins.push_back(o);
        };
        const double left = p.EdgeMargin, bottom = p.EdgeMargin;
        const double right = bin.Output.Width - p.EdgeMargin - pattern.Width, top = bin.Output.Height - p.EdgeMargin - pattern.Height;
        emit({left, bottom}); emit({right, bottom}); emit({left, top}); emit({right, top});
        if(nfpCache&&!bin.Output.Placements.empty()&&right>left&&top>bottom)
        {
            using namespace Clipper2Lib;
            PathsD forbidden;bool any=false;std::vector<Point> boundaryFallback;
            for(const auto& placed:bin.Output.Placements)
            {
                if(stop())return {};
                const auto stationaryIndex=byID.at(placed.PartID);
                const auto& stationary=p.Parts[stationaryIndex].Geometry;
                for(const auto& member:pattern.Members)
                {
                    if(stop())return {};
                    const auto& moving=p.Parts[member.PartIndex].Geometry;
                    if(stationary.Radius>0||moving.Radius>0||!stationary.Holes.empty()||!moving.Holes.empty()||
                       !nfpShapes||!(*nfpShapes)[stationaryIndex]||!(*nfpShapes)[member.PartIndex])continue;
                    NfpBuildOptions build;
                    build.TimeLimitSeconds=std::max(0.0,options.TimeLimitSeconds-elapsed());build.Cancel=stop;
                    const auto nfp=placed.Transform.Mirrored||member.Transform.Mirrored?
                        nfpCache->Get(stationary,placed.Transform.AngleDegrees,moving,member.Transform.AngleDegrees,
                            build,placed.Transform.Mirrored,member.Transform.Mirrored):
                        nfpCache->Get((*nfpShapes)[stationaryIndex],placed.Transform.AngleDegrees,
                            (*nfpShapes)[member.PartIndex],member.Transform.AngleDegrees,build);
                    if(nfp.State!=NfpStatus::Ready||!nfp.Region)continue;
                    PathsD one;one.reserve(nfp.Region->Loops.size());
                    for(const auto& loop:nfp.Region->Loops)
                    {
                        auto& path=one.emplace_back();path.reserve(loop.size());
                        for(const auto point:loop)
                        {
                            const auto q=Transform(point,nfp.Transform);
                            const Point origin{q.X+placed.Transform.X-member.Transform.X,q.Y+placed.Transform.Y-member.Transform.Y};
                            path.emplace_back(origin.X,origin.Y);
                            // A zero-area feasible pocket is not represented
                            // by polygon difference; keep individual vertices.
                            if(boundaryFallback.size()<1024)boundaryFallback.push_back(origin);
                        }
                    }
                    if(one.empty())continue;
                    for(auto& path:one)forbidden.push_back(std::move(path));any=true;
                }
            }
            if(any&&!stop())
            {
                const PathsD board{{{left,bottom},{right,bottom},{right,top},{left,top}}};
                // Collect all translated original-pair regions, preserving
                // their winding and holes, then union once per placement query.
                const auto united=Union(forbidden,FillRule::NonZero,8);
                const auto free=Difference(board,united,FillRule::NonZero,8);
                for(const auto& loop:free)for(const auto& point:loop)emit({point.x,point.y});
            }
            for(const auto origin:boundaryFallback)emit(origin);
        }
        std::vector<Edge> localScratch, placedEdges;
        std::vector<Disc> localDiscScratch, placedDiscs;
        std::vector<Obstacle> obstacles;
        if(!cacheContacts)for (const auto& member : pattern.Members)
        {
            const auto& shape = p.Parts[member.PartIndex].Geometry;
            auto edges = Edges(shape, member.Transform); localScratch.insert(localScratch.end(), edges.begin(), edges.end());
            if (shape.Radius > 0) localDiscScratch.push_back({Transform(shape.Center, member.Transform), shape.Radius});
        }
        const auto& localEdges=cacheContacts?info.LocalEdges:localScratch;
        const auto& localDiscs=cacheContacts?info.LocalDiscs:localDiscScratch;
        double x0 = bin.Output.Width, y0 = bin.Output.Height, x1 = 0, y1 = 0;
        for (const auto& placed : bin.Output.Placements)
        {
            if (stop()) return {};
            const auto partIndex=byID.at(placed.PartID);
            const auto& shape = p.Parts[partIndex].Geometry;
            const auto fixedGeometry=worldGeometry(partIndex,placed.Transform);
            obstacles.push_back({partIndex,placed.Transform,fixedGeometry});
            const auto box = fixedGeometry->Box;
            x0 = std::min(x0, box.X); y0 = std::min(y0, box.Y); x1 = std::max(x1, box.X + box.Width); y1 = std::max(y1, box.Y + box.Height);
            if(cacheContacts)
            {
                const auto key=std::tuple(partIndex,placed.Transform.X,placed.Transform.Y,placed.Transform.AngleDegrees,placed.Transform.Mirrored);
                auto it=worldEdgeCache.find(key);
                if(it==worldEdgeCache.end()&&worldEdgeCache.size()<4096)it=worldEdgeCache.emplace(key,Edges(shape,placed.Transform)).first;
                if(it!=worldEdgeCache.end())placedEdges.insert(placedEdges.end(),it->second.begin(),it->second.end());
                else {auto edges=Edges(shape,placed.Transform);placedEdges.insert(placedEdges.end(),edges.begin(),edges.end());}
            }
            else {auto edges = Edges(shape, placed.Transform); placedEdges.insert(placedEdges.end(), edges.begin(), edges.end());}
            if (shape.Radius > 0) placedDiscs.push_back({Transform(shape.Center, placed.Transform), shape.Radius});
            for (double x : {box.X, box.X + box.Width + p.PartGap, box.X - pattern.Width - p.PartGap, box.X + box.Width - pattern.Width})
                for (double y : {box.Y, box.Y + box.Height + p.PartGap, box.Y - pattern.Height - p.PartGap, box.Y + box.Height - pattern.Height}) emit({x, y});
        }
        for (const auto& a : placedEdges)
        {
            if (stop() || origins.size() >= cap) break;
            const double ux = a.B.X - a.A.X, uy = a.B.Y - a.A.Y;
            const Point normal{-uy / a.Length, ux / a.Length};
            const Point anchors[] = {a.A, a.B, {(a.A.X + a.B.X) / 2, (a.A.Y + a.B.Y) / 2}};
            for (const auto& b : localEdges)
            {
                if (stop()) return {};
                const double vx = b.B.X - b.A.X, vy = b.B.Y - b.A.Y;
                // A permitted seam needs zero-offset proposals even when the
                // ordinary gap is positive. Endpoint alignment can expose a
                // seam on another incident edge; point-only contacts still
                // fail the original gap and nominal material checks below.
                const bool commonEdgeContact=p.AllowCommonEdges&&p.PartGap>0&&a.CommonEdge&&b.CommonEdge;
                if((cacheContacts&&p.PartGap==0)||commonEdgeContact)
                    for(auto ap:{a.A,a.B})for(auto bp:{b.A,b.B})emit({ap.X-bp.X,ap.Y-bp.Y});
                if (std::abs(ux * vy - uy * vx) > p.Tolerance * std::max(a.Length, b.Length)) continue;
                const Point local[] = {b.A, b.B, {(b.A.X + b.B.X) / 2, (b.A.Y + b.B.Y) / 2}};
                if(commonEdgeContact)
                    for(auto ap:anchors)for(auto bp:local)emit({ap.X-bp.X,ap.Y-bp.Y});
                for (int side : {-1, 1}) for (auto ap : anchors) for (auto bp : local)
                    emit({ap.X - bp.X + side * p.PartGap * normal.X, ap.Y - bp.Y + side * p.PartGap * normal.Y});
            }
            for (auto disc : localDiscs)
                for (int side : {-1, 1}) for (auto anchor : anchors)
                    emit({anchor.X + side * (disc.Radius + p.PartGap) * normal.X - disc.Center.X,
                        anchor.Y + side * (disc.Radius + p.PartGap) * normal.Y - disc.Center.Y});
        }
        for (const auto& disc : placedDiscs)
        {
            if (stop() || origins.size() >= cap) break;
            for (const auto& edge : localEdges)
            {
                const Point normal{-(edge.B.Y - edge.A.Y) / edge.Length, (edge.B.X - edge.A.X) / edge.Length};
                const Point anchors[] = {edge.A, edge.B, {(edge.A.X + edge.B.X) / 2, (edge.A.Y + edge.B.Y) / 2}};
                for (int side : {-1, 1}) for (auto anchor : anchors)
                    emit({disc.Center.X - anchor.X + side * (disc.Radius + p.PartGap) * normal.X,
                        disc.Center.Y - anchor.Y + side * (disc.Radius + p.PartGap) * normal.Y});
            }
        }
        for (const auto& target : localDiscs)
        {
            if (stop()) return {};
            const auto addCenter = [&](Point center) { emit({center.X - target.Center.X, center.Y - target.Center.Y}); };
            const double minX = p.EdgeMargin + target.Radius, maxX = bin.Output.Width - p.EdgeMargin - target.Radius;
            const double minY = p.EdgeMargin + target.Radius, maxY = bin.Output.Height - p.EdgeMargin - target.Radius;
            for (std::size_t i = 0; i < placedDiscs.size() && origins.size() < cap; ++i)
            {
                const Disc a{placedDiscs[i].Center, target.Radius + placedDiscs[i].Radius + p.PartGap};
                for (double x : {minX, maxX})
                {
                    const long double dx = static_cast<long double>(x) - a.Center.X, h2 = static_cast<long double>(a.Radius) * a.Radius - dx * dx;
                    if (h2 >= 0) for (int sign : {-1, 1}) addCenter({x, a.Center.Y + sign * static_cast<double>(std::sqrt(h2))});
                }
                for (double y : {minY, maxY})
                {
                    const long double dy = static_cast<long double>(y) - a.Center.Y, h2 = static_cast<long double>(a.Radius) * a.Radius - dy * dy;
                    if (h2 >= 0) for (int sign : {-1, 1}) addCenter({a.Center.X + sign * static_cast<double>(std::sqrt(h2)), y});
                }
                for (std::size_t j = i + 1; j < placedDiscs.size() && origins.size() < cap; ++j)
                {
                    if (stop()) return {};
                    DiscIntersections(a, {placedDiscs[j].Center, target.Radius + placedDiscs[j].Radius + p.PartGap}, p.Tolerance, addCenter);
                }
            }
        }
        std::vector<Edge> boundaries;
        if((all||placementPolicy==NfpCompositePlacement::SharedBoundary)&&p.PartGap==0)
        {
            boundaries=placedEdges;
            const Point a{p.EdgeMargin,p.EdgeMargin},b{bin.Output.Width-p.EdgeMargin,p.EdgeMargin};
            const Point c{bin.Output.Width-p.EdgeMargin,bin.Output.Height-p.EdgeMargin},d{p.EdgeMargin,bin.Output.Height-p.EdgeMargin};
            boundaries.push_back({a,b,std::hypot(b.X-a.X,b.Y-a.Y)});boundaries.push_back({b,c,std::hypot(c.X-b.X,c.Y-b.Y)});
            boundaries.push_back({c,d,std::hypot(d.X-c.X,d.Y-c.Y)});boundaries.push_back({d,a,std::hypot(a.X-d.X,a.Y-d.Y)});
        }
        std::optional<Fit> best;
        std::size_t feasible = 0;
        const auto probe=[&](Point origin,bool& numericalFailure)->std::optional<Fit>
        {
            numericalFailure=false;
            if (stop()) return {};
            ++result.Stats.PlacementAttempts;
            Fit candidate{pi, bi, 0, origin,
                (std::max(x1, origin.X + pattern.Width) - std::min(x0, origin.X)) *
                (std::max(y1, origin.Y + pattern.Height) - std::min(y0, origin.Y)),
                std::max(y1, origin.Y + pattern.Height), std::max(x1, origin.X + pattern.Width)};
            candidate.EdgeRemnant = std::max(
                (bin.Output.Width - 2 * p.EdgeMargin) * std::max(0.0, bin.Output.Height - p.EdgeMargin - candidate.Top - p.PartGap),
                (bin.Output.Height - 2 * p.EdgeMargin) * std::max(0.0, bin.Output.Width - p.EdgeMargin - candidate.Right - p.PartGap));
            if (bin.Output.Placements.empty()) candidate.Area = pattern.Width * pattern.Height;
            if (!all&&best&&order!=ContactOrder::ScarceCandidates&&placementPolicy!=NfpCompositePlacement::SharedBoundary&&
                !(fitRank(candidate)<fitRank(*best))) return {};
            std::vector<AxisSafety::Geometry> geometry;
            std::vector<Rect> boxes;
            double lowX=std::numeric_limits<double>::infinity(),lowY=lowX,highX=-lowX,highY=-lowX;
            for (std::size_t i=0;i<pattern.Members.size();++i)
            {
                const auto& m = pattern.Members[i]; const auto& part = p.Parts[m.PartIndex];
                const Pose pose=ComposePose({origin.X,origin.Y,0},m.Transform);
                if (!PoseAllowed(part,pose,p.Tolerance)||
                    !InsideSheet(part.Geometry,pose,bin.Output.Width,bin.Output.Height,p.EdgeMargin,p.Tolerance)) return {};
                candidate.MemberPoses.push_back(pose);
                geometry.push_back(materialGeometry(m.PartIndex,pose));
                boxes.push_back(Bounds(part.Geometry,pose));
                if (!AxisSafety::Inside(geometry.back(),bin.Output.Width,bin.Output.Height,p.EdgeMargin))
                { numericalFailure=true;return {}; }
                const auto box=boxes.back();
                lowX=std::min(lowX,box.X);lowY=std::min(lowY,box.Y);
                highX=std::max(highX,box.X+box.Width);highY=std::max(highY,box.Y+box.Height);
                for (const auto& fixed:obstacles)
                {
                    if (stop()) return {};
                    if (!Near(box,fixed.Geometry->Box,p.PartGap+p.Tolerance+geometry.back().Roundoff+
                        fixed.Geometry->Material.Roundoff)) continue;
                    if (ShapesConflict(part.Geometry,pose,p.Parts[fixed.PartIndex].Geometry,fixed.Transform,
                        p.PartGap,p.Tolerance,p.AllowCommonEdges)) return {};
                    if (AxisSafety::Pair(geometry.back(),fixed.Geometry->Material,p.PartGap,p.AllowCommonEdges,
                        p.Tolerance)!=AxisSafety::Verdict::Safe) { numericalFailure=true;return {}; }
                }
                for (std::size_t j=0;j<i;++j)
                {
                    if (stop()) return {};
                    const auto& n = pattern.Members[j];
                    const auto fixed=candidate.MemberPoses[j];
                    if (!Near(box,boxes[j],p.PartGap+p.Tolerance+geometry.back().Roundoff+geometry[j].Roundoff)) continue;
                    if (ShapesConflict(part.Geometry,pose,p.Parts[n.PartIndex].Geometry,fixed,p.PartGap,
                        p.Tolerance,p.AllowCommonEdges)) return {};
                    if (AxisSafety::Pair(geometry.back(),geometry[j],p.PartGap,p.AllowCommonEdges,p.Tolerance)!=
                        AxisSafety::Verdict::Safe) { numericalFailure=true;return {}; }
                }
            }
            candidate.Area=(std::max(x1,highX)-std::min(x0,lowX))*(std::max(y1,highY)-std::min(y0,lowY));
            candidate.Top=std::max(y1,highY);candidate.Right=std::max(x1,highX);
            candidate.EdgeRemnant=std::max(
                (bin.Output.Width-2*p.EdgeMargin)*std::max(0.,bin.Output.Height-p.EdgeMargin-candidate.Top-p.PartGap),
                (bin.Output.Height-2*p.EdgeMargin)*std::max(0.,bin.Output.Width-p.EdgeMargin-candidate.Right-p.PartGap));
            if (bin.Output.Placements.empty()) candidate.Area=(highX-lowX)*(highY-lowY);
            if ((all||placementPolicy==NfpCompositePlacement::SharedBoundary)&&p.PartGap==0)
            {
                for (std::size_t i=0;i<pattern.Members.size();++i)
                {
                    for (const auto edge:Edges(p.Parts[pattern.Members[i].PartIndex].Geometry,candidate.MemberPoses[i]))
                        for(const auto other:boundaries)candidate.Contact+=SharedLength(edge,other,p.Tolerance);
                }
            }
            return candidate;
        };
        for (const auto origin:origins)
        {
            if (stop()) break;
            bool numericalFailure=false;
            auto candidate=probe(origin,numericalFailure);
            if (!candidate&&numericalFailure)
            {
                // Keep numerical retreats outside the tolerance-quantized
                // origin set. Every actual trial consumes the common budget.
                const double scale=info.CoordinateScale+std::max({1.,std::abs(origin.X),std::abs(origin.Y),
                    bin.Output.Width,bin.Output.Height});
                const double step=32*std::numeric_limits<double>::epsilon()*scale;
                for (const double distance:{step,std::max(step*16,2e-8)})
                {
                    const std::array<Point,8> offsets{{{distance,0},{0,distance},{-distance,0},{0,-distance},
                        {distance,distance},{distance,-distance},{-distance,distance},{-distance,-distance}}};
                    for (const auto offset:offsets)
                    {
                        if (stop()) break;
                        bool ignored=false;
                        candidate=probe({origin.X+offset.X,origin.Y+offset.Y},ignored);
                        if (candidate) { ++numericalRetreats;break; }
                    }
                    if (candidate||stop()) break;
                }
            }
            if (!candidate) continue;
            ++feasible;
            if (all) all->push_back(*candidate);
            if (!best||fitRank(*candidate)<fitRank(*best)) best=std::move(candidate);
        }
        if (best) best->Feasible = feasible;
        return best;
    };
    const auto choose = [&](const ContactBin& bin, std::size_t bi) -> std::optional<Fit>
    {
        std::vector<std::size_t> indices;
        for (std::size_t i = 0; i < patterns.size(); ++i) if (available(patterns[i], bin)) indices.push_back(i);
        if(preserveComplements)
        {
            std::vector<std::size_t> remaining(p.Parts.size());
            for(std::size_t i=0;i<remaining.size();++i)remaining[i]=p.Parts[i].Quantity-consumed[i];
            std::vector<double> intrinsic(p.Parts.size(),1e-6);
            for(const auto& info:patterns)if(info.Value.Members.size()==1)
            {
                const auto i=info.Value.Members.front().PartIndex;const auto& shape=p.Parts[i].Geometry;
                const auto kind=Classify(shape,p.Tolerance).Kind;
                const double density=shape.Holes.empty()&&(kind==ShapeKind::Rectangle||kind==ShapeKind::Parallelogram)
                    ?1.:std::clamp(info.CarrierFill,1e-6,1.);
                intrinsic[i]=std::max(intrinsic[i],density);
            }
            const auto potential=[&](const std::vector<std::size_t>& demand)
            {
                auto density=intrinsic;
                for(const auto& info:patterns)
                {
                    if(info.Value.Members.size()<2||info.Material!=p.Stocks[bin.StockIndex].Material||
                        info.Value.Width>bin.Output.Width-2*p.EdgeMargin+p.Tolerance||
                        info.Value.Height>bin.Output.Height-2*p.EdgeMargin+p.Tolerance)continue;
                    bool usable=true;for(auto [i,count]:info.Demand)if(count>demand[i]){usable=false;break;}
                    if(!usable)continue;
                    for(auto [i,count]:info.Demand){(void)count;density[i]=std::max(density[i],std::clamp(info.CarrierFill,1e-6,1.));}
                }
                return density;
            };
            const auto before=potential(remaining);
            companionPriority.assign(patterns.size(),std::numeric_limits<double>::quiet_NaN());
            for(auto index:indices)
            {
                if(stop())break;auto afterDemand=remaining;
                for(auto [i,count]:patterns[index].Demand)afterDemand[i]-=count;
                const auto after=potential(afterDemand);double lostArea=0,priority=0;
                // Estimate the extra envelope needed by originals whose dense
                // partners would be exhausted. This is inventory lookahead,
                // never a claim that an available motif will fit spatially.
                for(std::size_t i=0;i<p.Parts.size();++i)
                    lostArea+=afterDemand[i]*areas[i]*std::max(0.,1./after[i]-1./before[i]);
                for(auto [i,count]:patterns[index].Demand)
                {
                    const double footprint=areas[i]/before[i];
                    if(diversity==5)priority+=count*footprint;else priority=std::max(priority,footprint);
                }
                companionPriority[index]=priority-lostArea;
            }
        }
        std::stable_sort(indices.begin(), indices.end(), [&](auto a, auto b) { return staticRank(patterns[a]) < staticRank(patterns[b]); });
        std::optional<Fit> best;
        std::string selectedDemand;
        const bool compareOrientations = order == ContactOrder::PreserveRemnant || order == ContactOrder::BalancedRemnant || placementPolicy!=NfpCompositePlacement::EnvelopeArea;
        std::set<std::string> inspected;
        std::size_t alternatives = 0;
        for (auto index : indices)
        {
            if (stop()) break;
            if (compareOrientations && !selectedDemand.empty() && patterns[index].Signature != selectedDemand) continue;
            auto fit = query(bin, index, bi);
            if (!fit) continue;
            if (!compareOrientations&&(order == ContactOrder::LargeFirst || order == ContactOrder::BalancedSpecifications)) return fit;
            if (compareOrientations) selectedDemand = patterns[index].Signature;
            if (!best || (order == ContactOrder::ScarceCandidates && fit->Feasible != best->Feasible
                ? fit->Feasible < best->Feasible : fitRank(*fit) < fitRank(*best))) best = fit;
            inspected.insert(patterns[index].Signature);
            if (!compareOrientations && ++alternatives >= 24 && !inspected.empty()) break;
        }
        return best;
    };
    const auto commit = [&](ContactBin& bin, const Fit& fit)
    {
        const auto& pattern = patterns[fit.PatternIndex].Value;
        for (std::size_t i=0;i<pattern.Members.size();++i)
        {
            const auto& m=pattern.Members[i];
            const auto& part = p.Parts[m.PartIndex];
            bin.Output.Placements.push_back({part.ID, consumed[m.PartIndex]++,
                fit.MemberPoses[i], group, false});
            bin.Output.MaterialArea += areas[m.PartIndex];
        }
        if (pattern.Members.size() > 1) ++result.Stats.CombinationGroups;
        ++group;
    };
    std::vector<std::size_t> stockOrder(p.Stocks.size());
    std::iota(stockOrder.begin(), stockOrder.end(), 0);
    std::stable_sort(stockOrder.begin(), stockOrder.end(), [&](auto a, auto b)
    {
        // Larger stock first gives a distinct one-board-oriented candidate alongside the cost-density jobs.
        const auto& x = p.Stocks[a]; const auto& y = p.Stocks[b];
        return x.Width * x.Height != y.Width * y.Height ? x.Width * x.Height > y.Width * y.Height : x.ID < y.ID;
    });
    if(boundedSearch)
    {
        // This independent search is deliberately one-stock and bounded. It
        // retains the best legal partial if its candidate or time budget ends.
        const auto& stock=p.Stocks[0];ContactBin bin{0,{stock.ID,0,stock.Width,stock.Height,{},0}},bestBin=bin;
        std::vector<std::size_t> bestConsumed=consumed;std::size_t bestCount=0,bestCombinations=0;double bestMaterial=0;
        Layout initial=bin.Output;
        if(seed&&seed->Layouts.size()==1&&Validate(p,*seed).Valid)
        {
            initial=seed->Layouts.front();bestBin.Output=initial;
            for(const auto& placement:initial.Placements)++bestConsumed[byID.at(placement.PartID)];
            bestCount=initial.Placements.size();bestMaterial=initial.MaterialArea;
            bestCombinations=seed->Stats.CombinationGroups;
        }
        double localDeadline=options.TimeLimitSeconds;
        std::size_t localAttempts=options.MaxPlacementAttempts;
        std::size_t searchNodes=0,maxAdded=0,prefixCount=0,searchQueries=0,searchChoices=0;
        const auto branchStop=[&]{return stop()||elapsed()>=localDeadline||result.Stats.PlacementAttempts>=localAttempts;};
        const auto choices=[&]()->std::vector<Fit>
        {
            std::vector<std::size_t> indices;
            for(std::size_t i=0;i<patterns.size();++i)
                if(available(patterns[i],bin)&&patterns[i].Value.Width<=stock.Width-2*p.EdgeMargin+p.Tolerance&&
                    patterns[i].Value.Height<=stock.Height-2*p.EdgeMargin+p.Tolerance&&
                    (patterns[i].Value.Members.size()==1||patterns[i].Value.MaterialArea>=.85*patterns[i].Value.Width*patterns[i].Value.Height))indices.push_back(i);
            std::stable_sort(indices.begin(),indices.end(),[&](auto a,auto b)
            {
                const bool singleA=patterns[a].Value.Members.size()==1,singleB=patterns[b].Value.Members.size()==1;
                if(singleA!=singleB)return singleA;
                if(!singleA)
                {
                    const auto& x=patterns[a].Value;const auto& y=patterns[b].Value;
                    const double dx=x.MaterialArea/(x.Width*x.Height),dy=y.MaterialArea/(y.Width*y.Height);
                    if(dx!=dy)return dx>dy;
                }
                return staticRank(patterns[a])<staticRank(patterns[b]);
            });
            struct DemandFits {std::size_t Feasible=0,Members=0;double Area=0,Fill=0;std::vector<Fit> Choices;};
            std::map<std::string,DemandFits> grouped;
            constexpr std::size_t singleLimit=48,comboLimit=12,fitLimit=4,totalLimit=24;
            std::size_t singles=0,combos=0;
            std::map<std::string,std::size_t> comboVersions;
            for(auto index:indices)
            {
                if(branchStop())break;
                const bool single=patterns[index].Value.Members.size()==1;
                if((single&&singles>=singleLimit)||(!single&&combos>=comboLimit))continue;
                if(!single&&comboVersions[patterns[index].Signature]>=3)continue;
                if(single)++singles;else ++combos;
                if(!single)++comboVersions[patterns[index].Signature];
                std::vector<Fit> fits;query(bin,index,0,&fits);++searchQueries;
                if(fits.empty())continue;
                auto& demand=grouped[patterns[index].Signature];demand.Feasible+=fits.size();demand.Area=patterns[index].Value.MaterialArea;
                demand.Members=patterns[index].Value.Members.size();
                demand.Fill=patterns[index].Value.MaterialArea/(patterns[index].Value.Width*patterns[index].Value.Height);
                std::stable_sort(fits.begin(),fits.end(),[&](const auto& a,const auto& b){return fitRank(a)<fitRank(b);});
                if(fits.size()>fitLimit)
                {
                    // Keep the best-ranked contact and spatially separate
                    // alternatives; adjacent vertices on the same contact
                    // should not consume every branch slot.
                    std::vector<Fit> diverse{fits.front()};std::vector<bool> selected(fits.size());selected[0]=true;
                    while(diverse.size()<fitLimit)
                    {
                        std::size_t best=fits.size();double bestDistance=-1;
                        for(std::size_t fi=0;fi<fits.size();++fi)if(!selected[fi])
                        {
                            double distance=std::numeric_limits<double>::infinity();
                            for(const auto& prior:diverse)
                            {
                                const double dx=(fits[fi].Origin.X-prior.Origin.X)/stock.Width;
                                const double dy=(fits[fi].Origin.Y-prior.Origin.Y)/stock.Height;
                                distance=std::min(distance,dx*dx+dy*dy);
                            }
                            if(distance>bestDistance){bestDistance=distance;best=fi;}
                        }
                        if(best==fits.size())break;selected[best]=true;diverse.push_back(fits[best]);
                    }
                    fits=std::move(diverse);
                }
                demand.Choices.insert(demand.Choices.end(),fits.begin(),fits.end());
            }
            std::vector<DemandFits*> singleRanked,comboRanked;
            for(auto& [signature,groupedFits]:grouped)(groupedFits.Members==1?singleRanked:comboRanked).push_back(&groupedFits);
            std::stable_sort(singleRanked.begin(),singleRanked.end(),[](const auto* a,const auto* b)
            {return a->Feasible!=b->Feasible?a->Feasible<b->Feasible:a->Area>b->Area;});
            std::stable_sort(comboRanked.begin(),comboRanked.end(),[](const auto* a,const auto* b)
            {return a->Fill!=b->Fill?a->Fill>b->Fill:a->Feasible<b->Feasible;});
            std::vector<Fit> optionsHere;
            const auto addGroup=[&](DemandFits* demandGroup)
            {
                std::stable_sort(demandGroup->Choices.begin(),demandGroup->Choices.end(),[&](const auto& a,const auto& b){return fitRank(a)<fitRank(b);});
                const auto take=std::min(fitLimit,demandGroup->Choices.size());
                optionsHere.insert(optionsHere.end(),demandGroup->Choices.begin(),demandGroup->Choices.begin()+take);
            };
            for(std::size_t i=0;optionsHere.size()<totalLimit&&(i<singleRanked.size()||i<comboRanked.size());++i)
            {
                if(i<singleRanked.size())addGroup(singleRanked[i]);
                if(optionsHere.size()<totalLimit&&i<comboRanked.size())addGroup(comboRanked[i]);
            }
            searchChoices+=optionsHere.size();
            return optionsHere;
        };
        const auto recurse=[&](auto&& self)->bool
        {
            const auto count=std::accumulate(consumed.begin(),consumed.end(),std::size_t{0});
            ++searchNodes;maxAdded=std::max(maxAdded,count>=prefixCount?count-prefixCount:0);
            if(count>bestCount||(count==bestCount&&bin.Output.MaterialArea>bestMaterial))
            {bestCount=count;bestMaterial=bin.Output.MaterialArea;bestBin=bin;bestConsumed=consumed;bestCombinations=result.Stats.CombinationGroups;}
            if(branchStop())return false;
            bool complete=true;for(std::size_t i=0;i<p.Parts.size();++i)complete&=consumed[i]==p.Parts[i].Quantity;
            if(complete)return true;
            const auto optionsHere=choices();
            for(const auto& fit:optionsHere)
            {
                if(branchStop())return false;
                const auto oldCount=bin.Output.Placements.size();const double oldArea=bin.Output.MaterialArea;
                const auto oldConsumed=consumed;const auto oldGroup=group;const auto oldCombinations=result.Stats.CombinationGroups;
                commit(bin,fit);
                if(self(self))return true;
                bin.Output.Placements.resize(oldCount);bin.Output.MaterialArea=oldArea;
                consumed=oldConsumed;group=oldGroup;result.Stats.CombinationGroups=oldCombinations;
            }
            return false;
        };
        const auto beamSearch=[&]()->bool
        {
            struct State
            {
                ContactBin Bin;
                std::vector<std::size_t> Consumed;
                std::size_t Group=1,Combinations=0,Placed=0;
                double Score=0;
            };
            std::vector<State> beam{{bin,consumed,group,result.Stats.CombinationGroups,
                std::accumulate(consumed.begin(),consumed.end(),std::size_t{0}),0}};
            constexpr std::size_t beamWidth=24,perDemandState=4,parentBranches=24;
            while(!beam.empty()&&!branchStop())
            {
                std::vector<State> next;
                for(const auto& state:beam)
                {
                    if(branchStop())break;
                    bin=state.Bin;consumed=state.Consumed;group=state.Group;result.Stats.CombinationGroups=state.Combinations;
                    const auto count=std::accumulate(consumed.begin(),consumed.end(),std::size_t{0});
                    ++searchNodes;maxAdded=std::max(maxAdded,count);
                    if(count>bestCount||(count==bestCount&&bin.Output.MaterialArea>bestMaterial))
                    {bestCount=count;bestMaterial=bin.Output.MaterialArea;bestBin=bin;bestConsumed=consumed;bestCombinations=result.Stats.CombinationGroups;}
                    bool complete=true;for(std::size_t i=0;i<p.Parts.size();++i)complete&=consumed[i]==p.Parts[i].Quantity;
                    if(complete)return true;
                    auto candidates=choices();
                    if(candidates.size()>parentBranches)candidates.resize(parentBranches);
                    for(const auto& fit:candidates)
                    {
                        if(branchStop())break;
                        bin=state.Bin;consumed=state.Consumed;group=state.Group;result.Stats.CombinationGroups=state.Combinations;
                        commit(bin,fit);
                        const auto placed=std::accumulate(consumed.begin(),consumed.end(),std::size_t{0});
                        if(placed>bestCount||(placed==bestCount&&bin.Output.MaterialArea>bestMaterial))
                        {bestCount=placed;bestMaterial=bin.Output.MaterialArea;bestBin=bin;bestConsumed=consumed;bestCombinations=result.Stats.CombinationGroups;}
                        bool done=true;for(std::size_t i=0;i<p.Parts.size();++i)done&=consumed[i]==p.Parts[i].Quantity;
                        if(done)return true;
                        const double hull=MaterialHullArea(p,bin.Output,byID);
                        const double density=hull>0?std::min(1.0,bin.Output.MaterialArea/hull):0;
                        const double contact=fit.Contact/std::max(1e-12,std::sqrt(bin.Output.MaterialArea));
                        next.push_back({bin,consumed,group,result.Stats.CombinationGroups,placed,density+.02*contact});
                    }
                }
                std::stable_sort(next.begin(),next.end(),[](const State& a,const State& b){return a.Placed!=b.Placed?a.Placed>b.Placed:a.Score>b.Score;});
                std::map<std::vector<std::size_t>,std::size_t> counts;beam.clear();
                for(auto& state:next)
                {
                    auto& used=counts[state.Consumed];if(used>=perDemandState)continue;
                    ++used;beam.push_back(std::move(state));if(beam.size()>=beamWidth)break;
                }
            }
            return false;
        };
        const std::size_t n=initial.Placements.size();
        const std::vector<std::size_t> releases=n?std::vector<std::size_t>{2,4,8,12,16}:std::vector<std::size_t>{0};
        bool localEmptyRoot=false;auto* emptyRoot=sharedEmptyRoot?sharedEmptyRoot:&localEmptyRoot;
        for(std::size_t ri=0;ri<releases.size()&&!stop();++ri)
        {
            const auto keep=n-std::min(n,releases[ri]);
            // Each empty prefix has the same consumed state and deterministic
            // candidate order, irrespective of the seed it came from.
            if(!keep&&*emptyRoot)continue;
            prefixCount=keep;
            bin.Output=initial;bin.Output.Placements.resize(keep);bin.Output.MaterialArea=0;
            std::fill(consumed.begin(),consumed.end(),0);group=1;
            for(auto& placement:bin.Output.Placements)
            {
                const auto index=byID.at(placement.PartID);placement.Instance=consumed[index]++;
                bin.Output.MaterialArea+=areas[index];group=std::max(group,placement.Group+1);
            }
            const auto remainingRounds=releases.size()-ri;
            localDeadline=elapsed()+std::max(0.0,options.TimeLimitSeconds-elapsed())/remainingRounds;
            localAttempts=result.Stats.PlacementAttempts+(options.MaxPlacementAttempts-result.Stats.PlacementAttempts)/remainingRounds;
            const bool solved=!seed&&!keep?beamSearch():recurse(recurse);
            if(!keep&&!solved&&!branchStop())*emptyRoot=true;
            if(solved)break;
        }
        consumed=std::move(bestConsumed);
        result.Stats.CombinationGroups=bestCombinations;
        result.Diagnostics.push_back("Bounded search nodes="+std::to_string(searchNodes)+", max extra placements="+std::to_string(maxAdded)+", queries="+std::to_string(searchQueries)+", branch choices="+std::to_string(searchChoices));
        if(!bestBin.Output.Placements.empty())bins.push_back(std::move(bestBin));
    }
    else while (!stop())
    {
        bool complete = true;
        for (std::size_t i = 0; i < p.Parts.size(); ++i) complete &= consumed[i] == p.Parts[i].Quantity;
        if (complete) break;
        std::optional<Fit> best;
        for (std::size_t bi = 0; bi < bins.size() && !stop(); ++bi)
            if (auto fit = choose(bins[bi], bi); fit && (!best || fitRank(*fit) < fitRank(*best))) best = fit;
        if (best) { commit(bins[best->BinIndex], *best); continue; }
        bool opened = false;
        for (auto si : stockOrder)
        {
            if (stop()) break;
            const auto& stock = p.Stocks[si];
            if (stockUsed[si] >= stock.Quantity) continue;
            ContactBin bin{si, {stock.ID, stockUsed[si], stock.Width, stock.Height, {}, 0}};
            if (auto fit = choose(bin, bins.size()))
            { commit(bin, *fit); ++stockUsed[si]; bins.push_back(std::move(bin)); opened = true; break; }
        }
        if (!opened) break;
    }
    for (auto& bin : bins) result.Layouts.push_back(std::move(bin.Output));
    for (std::size_t i = 0; i < p.Parts.size(); ++i)
        if (consumed[i] < p.Parts[i].Quantity) result.Remaining.push_back({p.Parts[i].ID, p.Parts[i].Quantity - consumed[i]});
    result.State = cancelled ? Status::Cancelled : result.Remaining.empty() ? Status::Complete : Status::Partial;
    if (cancelled) result.Diagnostics.push_back("Unified rule-contact job cancelled; completed placements retained.");
    if (exhausted) result.Diagnostics.push_back("Unified rule-contact time or attempt budget exhausted.");
    if (!cancelled && !exhausted && !result.Remaining.empty()) result.Diagnostics.push_back("Finite rule-contact candidates or stock exhausted; no infeasibility proof.");
    if(nfpCache)
    {
        const auto stats=nfpCache->Statistics();
        result.Diagnostics.push_back("NFP cache cumulative hits="+std::to_string(stats.RegionHits)+", misses="+std::to_string(stats.RegionMisses)+", evictions="+std::to_string(stats.Evictions));
    }
    result.Diagnostics.push_back("contact.nominal separated-patterns="+std::to_string(separatedPatterns)+
        ", rejected-patterns="+std::to_string(rejectedPatterns)+", origin-retreats="+std::to_string(numericalRetreats));
    result.Stats.ElapsedSeconds = elapsed();
    result.Stats.PlacementSeconds = std::max(0.0, result.Stats.ElapsedSeconds - result.Stats.PreparationSeconds);
    return result;
}

Result RunContactRules(const Problem& p,const Options& options,ContactOrder order)
{return RunContactRulesImpl(p,options,order,false);}
}

namespace iCAX::SheetNesting
{
namespace
{
    class BoundedContact final:public IStrategy
    {
    public:
        explicit BoundedContact(std::shared_ptr<NfpCache> cache):Cache(std::move(cache)){}
        Result Run(const Problem& p,const Options& o) override
        {
            const auto start=std::chrono::steady_clock::now();
            const auto elapsed=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();};
            Result best;best.State=Status::Partial;for(const auto& x:p.Parts)if(x.Quantity)best.Remaining.push_back({x.ID,x.Quantity});
            std::size_t usedAttempts=0,bestPlaced=0;bool cancelled=false;
            std::vector<std::pair<std::size_t,Result>> seeds;std::set<std::string> signatures;
            const std::vector<std::pair<Detail::ContactOrder,int>> recipes{
                {Detail::ContactOrder::LargeFirst,0},{Detail::ContactOrder::LowThenLeft,0},
                {Detail::ContactOrder::BalancedRemnant,0},{Detail::ContactOrder::LargeFirst,1},
                {Detail::ContactOrder::LargeFirst,2},{Detail::ContactOrder::LargeFirst,3},
                {Detail::ContactOrder::LargeFirst,4},{Detail::ContactOrder::LargeFirst,5},
                {Detail::ContactOrder::BalancedSpecifications,1},{Detail::ContactOrder::BalancedSpecifications,2}};
            for(const auto [order,diversity]:recipes)
            {
                if((o.Cancel&&o.Cancel())){cancelled=true;break;}
                const double remaining=o.TimeLimitSeconds-elapsed();if(remaining<=0||usedAttempts>=o.MaxPlacementAttempts)break;
                auto q=o;q.TimeLimitSeconds=std::min(remaining,std::min(.4,o.TimeLimitSeconds/16));
                q.MaxPlacementAttempts=std::min(o.MaxPlacementAttempts-usedAttempts,std::max<std::size_t>(1,o.MaxPlacementAttempts/16));
                auto candidate=Detail::RunContactRulesImpl(p,q,order,false,nullptr,diversity);usedAttempts+=candidate.Stats.PlacementAttempts;
                const auto check=Validate(p,candidate);
                const bool childCancelled=candidate.State==Status::Cancelled;
                if(check.Valid&&!candidate.Layouts.empty())
                {
                    std::ostringstream key;key<<std::setprecision(12);
                    for(const auto& layout:candidate.Layouts)for(const auto& pose:layout.Placements)
                        key<<pose.PartID<<':'<<pose.Instance<<':'<<pose.Transform.X<<':'<<pose.Transform.Y<<':'<<pose.Transform.AngleDegrees<<':'<<pose.Transform.Mirrored<<';';
                    if(signatures.insert(key.str()).second)seeds.emplace_back(check.PlacedCount,candidate);
                }
                if(check.Valid&&check.PlacedCount>bestPlaced){bestPlaced=check.PlacedCount;best=std::move(candidate);}
                if(childCancelled){cancelled=true;break;}
                if(check.Valid&&check.Complete)break;
            }
            std::stable_sort(seeds.begin(),seeds.end(),[](const auto& a,const auto& b){return a.first>b.first;});
            if(seeds.size()>4)seeds.resize(4);
            std::shared_ptr<NfpCache> sharedNfp;
            if(p.PartGap==0&&!seeds.empty())
            {
                sharedNfp=Cache;
                if(!sharedNfp)
                {
                    NfpCacheSettings settings;settings.MaxRegionEntries=4096;settings.MaxBytes=32*1024*1024;
                    sharedNfp=std::make_shared<NfpCache>(settings);
                }
            }
            std::vector<NfpPreparedShape> prepared;
            bool emptyRootSearched=false;
            std::vector<std::string> branchDiagnostics;
            if(sharedNfp)
            {
                prepared.resize(p.Parts.size());
                for(std::size_t i=0;i<p.Parts.size();++i)
                {
                    if((o.Cancel&&o.Cancel())){cancelled=true;break;}
                    const auto remaining=o.TimeLimitSeconds-elapsed();if(remaining<=0)break;
                    const auto& shape=p.Parts[i].Geometry;if(shape.Radius>0||!shape.Holes.empty())continue;
                    NfpBuildOptions build;build.TimeLimitSeconds=remaining;build.Cancel=o.Cancel;
                    prepared[i]=sharedNfp->Prepare(shape,build);
                }
            }
            std::vector<Detail::Pattern> motifs;
            std::size_t motifAttempts=0;
            const auto currentBeforeMotifs=Validate(p,best);
            if(sharedNfp&&!cancelled&&currentBeforeMotifs.Valid&&!currentBeforeMotifs.Complete&&
                o.EnableCombinations&&o.MaxPatternMembers>=2&&o.MaxPatterns>0&&usedAttempts<o.MaxPlacementAttempts)
            {
                const double remaining=o.TimeLimitSeconds-elapsed();
                if(remaining>0)
                {
                    const double deadline=elapsed()+std::min(1.0,remaining*.15);
                    Detail::NfpCombinationOptions cfg;
                    cfg.MaxMembers=std::min<std::size_t>(4,o.MaxPatternMembers);
                    cfg.MaxResults=std::min<std::size_t>(64,o.MaxPatterns);
                    cfg.MaxAttempts=std::min<std::size_t>(12000,o.MaxPlacementAttempts-usedAttempts);
                    cfg.Stop=[&]{return elapsed()>=deadline||(o.Cancel&&o.Cancel());};
                    motifs=Detail::BuildNfpCombinationPatterns(p,*sharedNfp,prepared,cfg,motifAttempts);
                    usedAttempts+=motifAttempts;
                    if(o.Cancel&&o.Cancel())cancelled=true;
                }
            }
            std::size_t pairs=0,triples=0,quadruples=0;
            for(const auto& motif:motifs)
            {
                if(motif.Members.size()==2)++pairs;
                else if(motif.Members.size()==3)++triples;
                else if(motif.Members.size()==4)++quadruples;
            }
            branchDiagnostics.push_back("NFP dense combinations 2/3/4="+std::to_string(pairs)+"/"+
                std::to_string(triples)+"/"+std::to_string(quadruples)+", attempts="+std::to_string(motifAttempts));
            std::map<std::string,std::size_t> pureMotifs;
            for(const auto& motif:motifs)if(!motif.Members.empty())
            {
                const auto index=motif.Members.front().PartIndex;bool pure=true;
                for(const auto& m:motif.Members)if(m.PartIndex!=index){pure=false;break;}
                if(pure)++pureMotifs[p.Parts[index].ID+":"+std::to_string(motif.Members.size())];
            }
            if(!pureMotifs.empty())
            {
                std::string diagnostic="NFP pure combinations";
                for(const auto& [id,count]:pureMotifs)diagnostic+=" "+id+"="+std::to_string(count);
                branchDiagnostics.push_back(std::move(diagnostic));
            }
            // For small mixed problems, spend a substantial bounded slice
            // exploring from an empty sheet as well as repairing greedy
            // prefixes; a wrong early placement cannot be fixed by a tail.
            std::size_t totalDemand=0;for(const auto& item:p.Parts)totalDemand+=item.Quantity;
            if(!cancelled&&sharedNfp&&p.Parts.size()<=12&&totalDemand<=32)
            {
                const auto current=Validate(p,best);const double remaining=o.TimeLimitSeconds-elapsed();
                if(current.Valid&&!current.Complete&&remaining>0&&usedAttempts<o.MaxPlacementAttempts)
                {
                    auto q=o;q.TimeLimitSeconds=remaining*.6;
                    const auto attemptsLeft=o.MaxPlacementAttempts-usedAttempts;
                    q.MaxPlacementAttempts=std::max<std::size_t>(1,(attemptsLeft/5)*3+((attemptsLeft%5)*3)/5);
                    auto candidate=Detail::RunContactRulesImpl(p,q,Detail::ContactOrder::LowThenLeft,true,nullptr,0,
                        sharedNfp.get(),&prepared,&emptyRootSearched,&motifs);
                    usedAttempts+=candidate.Stats.PlacementAttempts;const auto next=Validate(p,candidate);
                    for(const auto& diagnostic:candidate.Diagnostics)
                        if(diagnostic.rfind("Bounded search nodes=",0)==0)branchDiagnostics.push_back("From empty: "+diagnostic);
                    if(candidate.State==Status::Cancelled)cancelled=true;
                    if(next.Valid&&next.PlacedCount>current.PlacedCount)best=std::move(candidate);
                }
            }
            for(std::size_t i=0;i<seeds.size()&&!cancelled;++i)
            {
                const auto check=Validate(p,best);const auto remaining=o.TimeLimitSeconds-elapsed();
                if(!check.Valid||check.Complete||remaining<=0||usedAttempts>=o.MaxPlacementAttempts)break;
                auto q=o;const auto rounds=seeds.size()-i;
                q.TimeLimitSeconds=remaining/rounds;q.MaxPlacementAttempts=(o.MaxPlacementAttempts-usedAttempts)/rounds;
                if(!q.MaxPlacementAttempts)break;
                auto candidate=Detail::RunContactRulesImpl(p,q,Detail::ContactOrder::LowThenLeft,true,&seeds[i].second,0,sharedNfp.get(),sharedNfp?&prepared:nullptr,&emptyRootSearched,&motifs);
                usedAttempts+=candidate.Stats.PlacementAttempts;const auto next=Validate(p,candidate);
                for(const auto& diagnostic:candidate.Diagnostics)
                    if(diagnostic.rfind("Bounded search nodes=",0)==0)branchDiagnostics.push_back(diagnostic);
                if(candidate.State==Status::Cancelled)cancelled=true;
                if(next.Valid&&next.PlacedCount>check.PlacedCount)best=std::move(candidate);
            }
            if(cancelled)best.State=Status::Cancelled;
            best.Stats.PlacementAttempts=usedAttempts;best.Stats.ElapsedSeconds=elapsed();
            best.Diagnostics.insert(best.Diagnostics.end(),branchDiagnostics.begin(),branchDiagnostics.end());
            if(sharedNfp)
            {
                const auto stats=sharedNfp->Statistics();
                best.Diagnostics.push_back("NFP cache cumulative region hits="+std::to_string(stats.RegionHits)+", misses="+std::to_string(stats.RegionMisses)+", evictions="+std::to_string(stats.Evictions)+", resident bytes="+std::to_string(stats.ResidentBytes));
            }
            best.Diagnostics.push_back("Bounded contact search retains the best validated seed or backtracking result; it is not an infeasibility proof.");
            return best;
        }
    private:
        std::shared_ptr<NfpCache> Cache;
    };
    class NfpCompositeContact final:public IStrategy
    {
        NfpCompositeSettings Settings;
        std::shared_ptr<NfpCache> SharedCache;
    public:
        NfpCompositeContact(NfpCompositeSettings settings,std::shared_ptr<NfpCache> cache)
            :Settings(settings),SharedCache(std::move(cache)){}
        Result Run(const Problem& p,const Options& o)override
        {
            const auto start=std::chrono::steady_clock::now();std::size_t usedAttempts=0;bool cancelled=false;
            const auto elapsed=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();};
            const auto pollCancel=[&]{if(!cancelled&&o.Cancel&&o.Cancel())cancelled=true;return cancelled;};
            const auto stop=[&]{return pollCancel()||elapsed()>=o.TimeLimitSeconds||usedAttempts>=o.MaxPlacementAttempts;};
            const auto empty=[&](std::string note)
            {
                Result result;result.State=cancelled?Status::Cancelled:Status::Partial;
                for(const auto& original:p.Parts)if(original.Quantity)result.Remaining.push_back({original.ID,original.Quantity});
                result.Diagnostics.push_back(std::move(note));result.Stats.PlacementAttempts=usedAttempts;
                result.Stats.ElapsedSeconds=elapsed();return result;
            };
            if(const auto errors=Detail::CheckProblem(p);!errors.empty())
            {auto result=empty("Invalid original composite-contact problem.");result.State=Status::InvalidInput;result.Diagnostics=errors;return result;}
            if(p.Stocks.size()!=1||p.Stocks[0].Quantity==0||p.Parts.size()>128||p.PartGap!=0||!std::isfinite(o.TimeLimitSeconds))
                return empty("Composite contact requires one zero-gap stock and at most 128 original specifications with a finite time budget.");
            if(stop())return empty("Composite contact stopped before construction.");
            Detail::ContactOrder order=Detail::ContactOrder::LargeFirst;int diversity=0;
            switch(Settings.Order)
            {
                case NfpCompositeOrder::MaterialArea:diversity=5;break;
                case NfpCompositeOrder::AnchorArea:break;
                case NfpCompositeOrder::BalancedSpecifications:order=Detail::ContactOrder::BalancedSpecifications;break;
                case NfpCompositeOrder::PreserveRemnant:order=Detail::ContactOrder::PreserveRemnant;diversity=5;break;
                default:return empty("Unknown composite-contact ordering policy.");
            }
            auto cache=SharedCache;
            if(!cache)cache=std::make_shared<NfpCache>(NfpCacheSettings{512,4096,32*1024*1024});
            std::vector<NfpPreparedShape> prepared(p.Parts.size());
            const double preparationDeadline=std::min(o.TimeLimitSeconds*.25,elapsed()+1.);
            const auto preparationStop=[&] { return stop()||elapsed()>=preparationDeadline; };
            for(std::size_t i=0;i<p.Parts.size()&&!preparationStop();++i)
            {
                const auto& shape=p.Parts[i].Geometry;
                if(!p.Parts[i].Quantity||shape.Radius>0||!shape.Holes.empty())continue;
                NfpBuildOptions build;build.TimeLimitSeconds=std::max(0.,preparationDeadline-elapsed());build.Cancel=preparationStop;
                prepared[i]=cache->Prepare(shape,build);
            }
            std::vector<Detail::Pattern> motifs;
            if(!stop()&&o.EnableCombinations&&o.MaxPatternMembers>=2&&o.MaxPatterns&&Settings.MaxCombinationAttempts)
            {
                const auto deadline=elapsed()+std::min(1.,std::max(0.,o.TimeLimitSeconds-elapsed())*.15);
                Detail::NfpCombinationOptions config;config.MaxMembers=std::min<std::size_t>(4,o.MaxPatternMembers);
                config.MaxResults=std::min<std::size_t>(64,o.MaxPatterns);
                config.MaxAttempts=std::min(Settings.MaxCombinationAttempts,o.MaxPlacementAttempts-usedAttempts);
                config.Stop=[&]{return stop()||elapsed()>=deadline;};
                std::size_t attempts=0;motifs=Detail::BuildNfpCombinationPatterns(p,*cache,prepared,config,attempts);
                usedAttempts+=attempts;
            }
            const auto preparation=elapsed();
            if(stop())return empty("Composite contact stopped during geometry preparation.");
            auto remaining=o;remaining.TimeLimitSeconds=std::max(0.,o.TimeLimitSeconds-preparation);
            remaining.MaxPlacementAttempts=o.MaxPlacementAttempts-usedAttempts;remaining.Cancel=pollCancel;
            // Every motif is present before the first placement. The same
            // original-member query is used for small and large quantities.
            auto result=Detail::RunContactRulesImpl(p,remaining,order,false,nullptr,diversity,
                cache.get(),&prepared,nullptr,&motifs,Settings.PreserveComplementaryInventory,Settings.Placement);
            usedAttempts+=result.Stats.PlacementAttempts;
            if(result.State==Status::Cancelled)cancelled=true;
            const auto checkedAt=std::chrono::steady_clock::now();const auto checked=Validate(p,result);
            const auto verification=std::chrono::duration<double>(std::chrono::steady_clock::now()-checkedAt).count();
            if(!checked.Valid)return empty("Composite construction failed original-material validation; no invalid layout retained.");
            result.GeometryValidated=true;
            result.Stats.PreparationSeconds=std::max(0.,result.Stats.PreparationSeconds)+preparation;
            result.Stats.VerificationSeconds=std::max(0.,result.Stats.VerificationSeconds)+verification;
            result.Stats.PlacementAttempts=usedAttempts;result.Stats.ElapsedSeconds=elapsed();
            if(pollCancel())result.State=Status::Cancelled;
            result.Diagnostics.push_back("NFP composite contact constructed from empty; motifs="+std::to_string(motifs.size())+
                "; original specifications="+std::to_string(p.Parts.size())+
                "; preserve complementary inventory="+(Settings.PreserveComplementaryInventory?"1":"0")+
                "; placement policy="+std::to_string(static_cast<int>(Settings.Placement)));
            return result;
        }
    };
}
StrategyFactory MakeBoundedContactFactory(std::shared_ptr<NfpCache> cache){return [cache=std::move(cache)]{return std::make_unique<BoundedContact>(cache);};}
StrategyFactory MakeNfpCompositeContactFactory(NfpCompositeSettings settings,std::shared_ptr<NfpCache> cache)
{return [settings,cache=std::move(cache)]{return std::make_unique<NfpCompositeContact>(settings,cache);};}
}
