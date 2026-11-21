#include "SheetNestingPlaneStrategies.h"
#include "SheetNestingPlaneEvaluation.h"
#include "NestingInternal.h"
#include "NestingExecution.h"
#include "NestingAxisSafety.h"
#include "NestingPoseBounds.h"
#include "NestingNfpPlacement.h"
#include "NestingClearancePlacement.h"
#include "SheetNestingAxisCompaction.h"
#include "SheetNestingRotationCompaction.h"
#include "SheetNestingPlaneCompaction.h"
#include "NestingNfpCombinations.h"
#include "NestingSheetTemplateReuse.h"
#include "NestingPlaneHoles.h"
#include "NestingTrace.h"
#include "SheetNestingNfp.h"
#include <clipper2/clipper.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

#include "NestingPlaneConstruction.h"

namespace iCAX::SheetNesting::Detail::PlaneConstruction
{
using Kind=PlaneStrategyKind;
struct CommitHistory
{
    std::shared_ptr<const CommitHistory> Parent;
    ConstructionTraceEvent Record;
    Layout Header,Base;
    std::vector<Placement> Added;
};
Rect Unite(Rect a,Rect b)
{
    const double x=std::min(a.X,b.X),y=std::min(a.Y,b.Y);
    return {x,y,std::max(a.X+a.Width,b.X+b.Width)-x,std::max(a.Y+a.Height,b.Y+b.Height)-y};
}
double BoxArea(Rect r){return r.Width*r.Height;}
bool Within(Rect a,Rect b,double t)
{return a.X>=b.X-t&&a.Y>=b.Y-t&&a.X+a.Width<=b.X+b.Width+t&&a.Y+a.Height<=b.Y+b.Height+t;}
Clipper2Lib::PathsD Paths(const Shape& s,Pose pose)
{
    using namespace Clipper2Lib;
    PathsD result;
    const auto ring=[&](const std::vector<Point>& points)
    {PathD path;for(auto p:points){auto q=Transform(p,pose);path.emplace_back(q.X,q.Y);}result.push_back(std::move(path));};
    if(s.Radius>0)
    {
        std::vector<Point> points;
        for(int i=0;i<64;++i){double a=2*Pi*i/64;points.push_back({s.Center.X+s.Radius*std::cos(a),s.Center.Y+s.Radius*std::sin(a)});}ring(points);
    }
    else {ring(s.Outer);for(const auto& h:s.Holes)ring(h);}
    return Union(result,FillRule::EvenOdd,8);
}
double Perimeter(const Clipper2Lib::PathsD& paths)
{
    double total=0;
    for(const auto& path:paths)for(std::size_t i=0;i<path.size();++i)
    {const auto& a=path[i];const auto& b=path[(i+1)%path.size()];total+=std::hypot(a.x-b.x,a.y-b.y);}
    return total;
}
double FirstCornerDistance(const Shape& shape,Pose pose,Rect box)
{
    if(shape.Outer.size()<3)return 0;
    const auto origin=shape.Outer.front();long double crossSum=0,cx=0,cy=0;
    for(std::size_t i=0;i<shape.Outer.size();++i)
    {
        const auto a=shape.Outer[i],b=shape.Outer[(i+1)%shape.Outer.size()];
        const long double ax=a.X-origin.X,ay=a.Y-origin.Y,bx=b.X-origin.X,by=b.Y-origin.Y;
        const auto cross=ax*by-ay*bx;crossSum+=cross;cx+=(ax+bx)*cross;cy+=(ay+by)*cross;
    }
    if(crossSum==0)return 0;
    const auto center=Transform({origin.X+static_cast<double>(cx/(3*crossSum)),origin.Y+static_cast<double>(cy/(3*crossSum))},pose);
    return (center.X-box.X)/box.Width+(center.Y-box.Y)/box.Height;
}
bool IsNfp(Kind k){return k==Kind::NfpAllSpecifications||k==Kind::NfpClosest||k==Kind::NfpDirectional||k==Kind::NfpStrip;}
bool ContourArray(Kind k){return k==Kind::NfpArray||k==Kind::NfpMultiArray;}
bool Searching(Kind k){return k==Kind::ArrayFirstSpecificationSearch||k==Kind::ArrayMultiSpecificationSearch||k==Kind::ArrayReduceSearch||k==Kind::RectangleBlockSearch||k==Kind::FourBlockArray;}
bool FirstSpecification(Kind k)
{return k!=Kind::ArrayMultiSpecificationSearch&&k!=Kind::NfpMultiArray&&k!=Kind::RectangleBlockSearch&&k!=Kind::RectangleContact&&k!=Kind::SingleProductArray;}

SheetTemplateReuseReport ReuseLastBoard(const Problem& problem,const PlaneStrategySettings& settings,Budget& budget,
    const Stock& stock,std::size_t& nextStock,std::vector<std::size_t>& used,std::size_t& nextGroup,Result& result)
{
    if(!settings.EnableSheetTemplateReuse||result.Layouts.empty()||nextStock>=stock.Quantity||budget.Stop())return {};
    auto options=budget.Remaining();
    options.TimeLimitSeconds=std::max(0.,std::min(budget.O.TimeLimitSeconds,budget.LocalTimeLimit)-budget.Seconds());
    options.MaxPlacementAttempts=std::min(budget.O.MaxPlacementAttempts,budget.LocalAttemptLimit)-budget.Attempts;
    options.Cancel=[&]{budget.Stop();return budget.Cancelled;};
    if(options.TimeLimitSeconds<=0||!options.MaxPlacementAttempts)return {};
    const auto report=AppendSheetTemplateCopies(problem,result.Layouts.back(),stock,nextStock,used,nextGroup,result,
        options,settings.MinimumSheetReuseUtilization);
    budget.Attempts+=std::min(report.Attempts,options.MaxPlacementAttempts);
    if(report.Cancelled)budget.Cancelled=true;
    return report;
}

class Kernel::Impl
{
    friend class Kernel;
    const Problem& P;
    PlaneStrategySettings S;
    Budget& B;
    std::vector<Pattern> Templates;
    std::vector<std::vector<std::pair<std::size_t,std::size_t>>> TemplateNeeds;
    struct LocalCarrier {std::size_t Template;Pattern Value;bool SwappedAxes=false;};
    std::vector<LocalCarrier> LocalPairTemplates;
    bool LocalPairTemplatesReady=false;
    std::vector<std::vector<Point>> Pitches;
    std::map<std::string,std::size_t> Index;
    std::vector<std::vector<Pose>> Orientations;
    struct IntrinsicPlacementAssessment
    {
        double EnvelopeArea=0,MaterialCornerDistance=0;
    };
    // These two shape-only ties belong to an immutable original orientation.
    // Raw world translations must neither rescan the contour nor change them.
    mutable std::map<std::tuple<std::size_t,double,bool>,IntrinsicPlacementAssessment> IntrinsicPlacementAssessments;
    std::vector<bool> Rectangular;
    std::vector<double> Areas,SourceScales;
    std::shared_ptr<NfpCache> Cache;
    std::map<std::string,std::unique_ptr<ClearanceNfpPlacement>> Generators;
    struct ContactGeometry
    {
        std::size_t Part=0;
        Pose Transform;
        Clipper2Lib::PathsD Material;
        Rect Box;
    };
    // Only the last scored board is retained. Exact original index/pose keys
    // prevent stale geometry when a search branch reuses the same State address.
    std::vector<ContactGeometry> ContactFixed;
    std::map<std::tuple<std::size_t,double,bool>,std::shared_ptr<const Clipper2Lib::PathsD>> ContactExpanded;
    std::size_t ContactExpandedVertices=0;
    using SafetyHandle=std::shared_ptr<const AxisSafety::Geometry>;
    using SafetyKey=std::tuple<std::size_t,double,double,double,bool>;
    std::map<SafetyKey,SafetyHandle> SafetyCache;
    std::size_t SafetyCacheVertices=0;
    std::map<std::tuple<std::size_t,double,bool>,SafetyHandle> QuarterSafetyCache;
    std::size_t QuarterSafetyCacheVertices=0;
    using PairKey=std::array<std::uint64_t,10>;
    struct PairKeyHash
    {
        std::size_t operator()(const PairKey& key)const noexcept
        {
            std::uint64_t hash=0;
            for(const auto value:key)hash^=value+0x9e3779b9u+(hash<<6)+(hash>>2);
            // Integer and half-integer coordinates share many low zero bits.
            // Spread their complete binary64 keys before selecting a slot.
            hash^=hash>>33;hash*=0xff51afd7ed558ccdULL;
            hash^=hash>>33;hash*=0xc4ceb9fe1a85ec53ULL;hash^=hash>>33;
            return static_cast<std::size_t>(hash);
        }
    };
    struct PairCacheEntry
    {
        PairKey Key{};
        AxisSafety::Verdict Value=AxisSafety::Verdict::Uncertain;
        bool Occupied=false;
    };
    // Ordered world-coordinate pairs belong only to this immutable Problem.
    // Neither relative translations nor the reversed call order share proof.
    std::vector<PairCacheEntry> PairCache;
    std::size_t PairCacheHits=0,PairCacheMisses=0;
    struct SafetyGeometry
    {
        std::size_t Part=0;
        Pose Transform;
        SafetyHandle Value;
    };
    // This board-local view holds transformed geometry. Pair verdicts use the
    // separate full world-coordinate key above; branches still match all poses.
    std::vector<SafetyGeometry> SafetyFixed;
    std::size_t NextGroup=1;
    std::size_t NfpTrial=0;
    std::size_t SettlingMoves=0,SettledBoards=0,SettledAngles=0;
    std::size_t ReusedBoards=0,ReusedParts=0;
    std::size_t IndependentPairCandidates=0,HoleCandidates=0,CenterFilledParts=0;
    double PatternSeconds=0,NfpTemplateSeconds=0,NfpCombinationSeconds=0,NfpPitchSeconds=0;
    double SearchSeconds=0,ConstructionSeconds=0,TailRepackSeconds=0,SettlingSeconds=0,ReuseSeconds=0;
    double ArraysSeconds=0;
    mutable double SelectionSeconds=0;
    struct ArrayQueryProfile
    {
        double Started=0,FitsSeconds=0;
        std::size_t FitsCalls=0;
        std::array<std::size_t,2> Queries{},Attempts{},Generated{},Fits{};
        std::array<double,2> QuerySeconds{};
        std::map<std::string,double> PhaseSums;
    };
    // Observation only. Accumulate before the bounded writer sees individual
    // query records, so late construction states still have complete totals.
    ArrayQueryProfile* ActiveArrayQueryProfile=nullptr;
    bool HasHoleGeometry=false;
    std::string TraceContext;
    mutable std::uint64_t TraceStep=0,NextCandidate=0,NextQuery=0,NextState=0;
    std::set<std::uint64_t> ObservedDecisionStates;
    template<class Build> void Observe(ConstructionTraceStage stage,Build&& build)const
    {
        ObserveConstruction(B.O,stage,[&](ConstructionTraceEvent& event)
        {
            event.Step=++TraceStep;event.ElapsedSeconds=B.Seconds();event.Attempts=B.Attempts;event.ExpandedStates=B.Nodes;
            event.Scope=TraceContext;
            event.Measures={{"kind",static_cast<double>(S.Kind)},{"trial",static_cast<double>(NfpTrial)},
                {"orientationProposalPolicy",static_cast<double>(B.O.OrientationProposals)},
                {"continuousConstructionTime",B.O.ContinuousConstructionTime?1.:0.},
                {"xFirst",S.XFirst?1.:0.},{"timeLimitSeconds",std::min(B.O.TimeLimitSeconds,B.LocalTimeLimit)},
                {"attemptLimit",static_cast<double>(std::min(B.O.MaxPlacementAttempts,B.LocalAttemptLimit))}};
            build(event);
        });
    }
    void Identify(Candidate& candidate)const
    {if(B.O.ConstructionTrace&&!candidate.TraceID)candidate.TraceID=++NextCandidate;}
    void ObserveCandidate(ConstructionTraceStage stage,const State& state,const Candidate& candidate,
        const std::string& source,const std::string& reason,bool accepted=true)const
    {
        Observe(stage,[&](ConstructionTraceEvent& event)
        {
            event.CandidateID=candidate.TraceID;event.ProposalID=candidate.TraceProposal;event.StateID=state.TraceID;
            event.PartIndex=candidate.Part;event.StockInstance=state.Board.StockInstance;
            event.BoardWidth=state.Board.Width;event.BoardHeight=state.Board.Height;
            event.Source=source;event.Reason=reason;event.Accepted=accepted;
            event.Width=candidate.Box.Width;event.Height=candidate.Box.Height;event.MaterialArea=candidate.Material;
            event.Measures.insert(event.Measures.end(),{{"x",candidate.Box.X},{"y",candidate.Box.Y},
                {"columns",static_cast<double>(candidate.Columns)},{"rows",static_cast<double>(candidate.Rows)},
                {"template",static_cast<double>(candidate.Template)},{"score",candidate.Score}});
            for(const auto& member:candidate.Members)event.Members.push_back({member.PartIndex,
                std::numeric_limits<std::size_t>::max(),member.Transform,0});
        });
    }
    void FourBlocks()
    {
        const auto singles=Templates;
        std::map<std::size_t,std::vector<std::size_t>> directionsByPart;
        for(std::size_t i=0;i<singles.size();++i)
        {
            if(B.Stop())return;
            if(singles[i].Members.size()==1&&Rectangular[singles[i].Members[0].PartIndex])
                directionsByPart[singles[i].Members[0].PartIndex].push_back(i);
        }
        for(const auto& first:singles)
        {
            if(B.Stop())return;
            if(first.Members.size()!=1)continue;
            const auto pi=first.Members[0].PartIndex;
            const auto directions=directionsByPart.find(pi);
            if(directions==directionsByPart.end())continue;
            for(const auto index:directions->second)
            {
                if(B.Stop())return;
                const auto& turned=singles[index];
                if(std::abs(first.Width-turned.Height)>P.Tolerance||std::abs(first.Height-turned.Width)>P.Tolerance)continue;
                for(std::size_t a=1;a<=std::min<std::size_t>(8,S.MaxArrayAxis)&&!B.Stop();++a)
                for(std::size_t b=1;b<=std::min<std::size_t>(8,S.MaxArrayAxis)&&!B.Stop();++b)
                {
                    if(4*a*b>P.Parts[pi].Quantity)continue;
                    const double w=first.Width+P.PartGap,h=first.Height+P.PartGap,A=a*w,Bb=b*h;
                    Pattern cell;cell.Kind=ShapeKind::Irregular;
                    for(std::size_t x=0;x<a;++x)for(std::size_t y=0;y<b;++y)
                    {
                        auto horizontal=first.Members[0],vertical=turned.Members[0];
                        horizontal.Transform.X+=x*w;horizontal.Transform.Y+=y*h;cell.Members.push_back(horizontal);
                        horizontal.Transform.X+=Bb;horizontal.Transform.Y+=A;cell.Members.push_back(horizontal);
                        vertical.Transform.X+=A+y*h;vertical.Transform.Y+=x*w;cell.Members.push_back(vertical);
                        vertical.Transform.X-=A;vertical.Transform.Y+=Bb;cell.Members.push_back(vertical);
                    }
                    NormalizePattern(cell,P);Templates.push_back(std::move(cell));
                }
            }
        }
    }
    void NfpTemplates()
    {
        StageTime totalTime(B,NfpTemplateSeconds);
        const auto previousTime=B.LocalTimeLimit;
        struct RestorePreparationTime{Budget& Work;double Previous;~RestorePreparationTime(){Work.LocalTimeLimit=Previous;}}restore{B,previousTime};
        // Preparation, including periodic pitch search, is optional work. It
        // cannot spend the ordinary original-placement construction budget.
        const auto preparationStarted=B.Seconds();
        B.LocalTimeLimit=preparationStarted+std::max(0.,std::min(B.O.TimeLimitSeconds,previousTime)-preparationStarted)*.15;
        std::vector<NfpPreparedShape> prepared;
        for(const auto& p:P.Parts)
        {if(B.Stop())return;NfpBuildOptions o;o.Cancel=[&]{return B.Stop();};prepared.push_back(Cache->Prepare(p.Geometry,o));}
        if(S.Kind==Kind::NfpArray||S.Kind==Kind::NfpMultiArray)
            Templates.erase(std::remove_if(Templates.begin(),Templates.end(),[&](const Pattern& p)
                {return p.Members.size()>1&&std::any_of(p.Members.begin(),p.Members.end(),[&](const Member& m){return !prepared[m.PartIndex];});}),Templates.end());
        {
            StageTime combinationTime(B,NfpCombinationSeconds);
            for(const auto& stock:P.Stocks)
            {
                if(!B.O.EnableCombinations||B.Stop())break;
                if(!stock.Quantity||!Generator(stock).Ready())continue;
                auto local=P;local.Stocks={stock};
                for(auto& part:local.Parts)if(part.Material!=stock.Material)part.Quantity=0;
                NfpCombinationOptions o;o.SamePartOnly=true;
                o.MaxMembers=std::min<std::size_t>(4,B.O.MaxPatternMembers);o.MaxResults=std::min<std::size_t>(64,B.O.MaxPatterns);
                // A greedy array consumes its prepared carriers immediately.
                // Larger self assemblies belong to the independent search
                // constructor. Different original types are never assembled.
                o.ExpandAssemblies=S.Kind==Kind::NfpMultiArray||S.Kind==Kind::ArrayMultiSpecificationSearch;
                o.OrientationProposals=B.O.OrientationProposals;
                o.MaxAttempts=std::min(o.MaxAttempts,B.O.MaxPlacementAttempts-B.Attempts);o.Stop=[&]{return B.Stop();};
                o.RemainingPreparationSeconds=[&]{return std::max(0.,std::min(B.O.TimeLimitSeconds,B.LocalTimeLimit)-B.Seconds());};
                o.ConstructionTrace=B.O.ConstructionTrace;o.ConstructionTraceRoute=B.O.ConstructionTraceRoute;
                o.ConstructionTraceScope=TraceContext;o.ConstructionObservation=B.O.ConstructionObservation;
                std::size_t attempts=0;auto extra=BuildNfpCombinationPatterns(local,*Cache,prepared,o,attempts);B.Attempts+=attempts;
                for(auto& pattern:extra)if(pattern.Members.size()>1&&HasSinglePartType(pattern.Members))Templates.push_back(std::move(pattern));
            }
        }
        StageTime pitchTime(B,NfpPitchSeconds);
        Pitches.reserve(Templates.size());
        for(const auto& pattern:Templates)
        {
            const double separation=P.PartGap>0?P.PartGap+std::max(1e-7,4*P.Tolerance):0;
            const Point boxPitch{pattern.Width+separation,pattern.Height+separation};
            std::vector<Point> pitches{boxPitch};
            std::map<std::size_t,std::size_t> demand;
            for(const auto& member:pattern.Members)++demand[member.PartIndex];
            std::size_t copies=std::numeric_limits<std::size_t>::max();
            for(const auto& [part,count]:demand)copies=std::min(copies,P.Parts[part].Quantity/count);
            // A one-off mixed assembly never uses a periodic neighbour. Keep
            // its safe box pitch without spending the construction budget on it.
            const bool independent=S.EnableIndependentPairArrays&&pattern.Members.size()==2&&
                pattern.Members[0].PartIndex==pattern.Members[1].PartIndex&&
                P.Parts[pattern.Members[0].PartIndex].Quantity>=3;
            if((copies<2&&!independent)||B.Stop()){Pitches.push_back(std::move(pitches));continue;}
            const auto stock=std::find_if(P.Stocks.begin(),P.Stocks.end(),[&](const Stock& s)
                {return s.Quantity&&s.Material==P.Parts[pattern.Members.front().PartIndex].Material;});
            if(stock==P.Stocks.end()||!Generator(*stock).Ready()){Pitches.push_back(std::move(pitches));continue;}
            struct Segment{Point A,B;};std::vector<Segment> boundaries;
            std::vector<double> xs{boxPitch.X},ys{boxPitch.Y};
            const auto cut=[&](double u,double v,double a,double b,std::vector<double>& out)
            {
                if(std::abs(u)<=P.Tolerance&&a>P.Tolerance)out.push_back(a);
                if((u<0&&v>0)||(u>0&&v<0))
                {const double t=-u/(v-u),value=a+t*(b-a);if(value>P.Tolerance)out.push_back(value);}
            };
            for(const auto& fixed:pattern.Members)for(const auto& moving:pattern.Members)
            {
                if(B.Stop()||!prepared[fixed.PartIndex]||!prepared[moving.PartIndex])continue;
                NfpBuildOptions o;o.Cancel=[&]{return B.Stop();};
                auto pair=Generator(*stock).ContactRegion(fixed.PartIndex,fixed.Transform.AngleDegrees,moving.PartIndex,
                    moving.Transform.AngleDegrees,o,false,fixed.Transform.Mirrored,moving.Transform.Mirrored);
                if(pair.State!=NfpStatus::Ready||!pair.Region)continue;
                for(const auto& loop:pair.Region->Loops)for(std::size_t i=0;i<loop.size();++i)
                {
                    auto a=Transform(loop[i],pair.Transform),b=Transform(loop[(i+1)%loop.size()],pair.Transform);
                    a.X+=fixed.Transform.X-moving.Transform.X;a.Y+=fixed.Transform.Y-moving.Transform.Y;
                    b.X+=fixed.Transform.X-moving.Transform.X;b.Y+=fixed.Transform.Y-moving.Transform.Y;
                    boundaries.push_back({a,b});
                    cut(a.Y,b.Y,a.X,b.X,xs);cut(a.X,b.X,a.Y,b.Y,ys);
                }
            }
            const auto trim=[&](std::vector<double>& values,double maximum)
            {
                values.erase(std::remove_if(values.begin(),values.end(),[&](double value)
                    {return !std::isfinite(value)||value<=P.Tolerance||value>maximum+P.Tolerance;}),values.end());
                std::sort(values.begin(),values.end());
                values.erase(std::unique(values.begin(),values.end(),[&](double a,double b){return std::abs(a-b)<=P.Tolerance;}),values.end());
                if(values.size()>32)values.resize(32);
                if(values.empty()||std::abs(values.back()-maximum)>P.Tolerance)values.push_back(maximum);
            };
            trim(xs,boxPitch.X);trim(ys,boxPitch.Y);
            const auto periodicSafe=[&](Point pitch)
            {
                if(B.Stop()||pitch.X<=0||pitch.Y<=0)return false;
                const auto columns=std::ceil((pattern.Width+P.PartGap)/pitch.X);
                const auto rows=std::ceil((pattern.Height+P.PartGap)/pitch.Y);
                if(!std::isfinite(columns)||!std::isfinite(rows)||columns>8||rows>8)return false;
                const auto nx=static_cast<int>(columns),ny=static_cast<int>(rows);
                bool safe=true;
                // Inspect enough periodic neighbours for the complete template
                // extents, not only the 2x2 diagonal; very small periods can
                // conflict with a farther copy while the adjacent one fits.
                for(int ix=-nx;ix<=nx&&safe;++ix)
                for(int iy=0;iy<=ny&&safe;++iy)
                {
                    if((iy==0&&ix<=0))continue;
                    for(const auto& a:pattern.Members)for(const auto& b:pattern.Members)
                    {
                        if(B.Stop()){safe=false;break;}++B.Attempts;
                        auto pose=b.Transform;pose.X+=ix*pitch.X;pose.Y+=iy*pitch.Y;
                        const auto& ga=P.Parts[a.PartIndex].Geometry;const auto& gb=P.Parts[b.PartIndex].Geometry;
                        if(ShapesConflict(ga,a.Transform,gb,pose,P.PartGap,P.Tolerance,P.AllowCommonEdges)||
                            AxisSafety::Pair(AxisSafety::Make(ga,a.Transform),AxisSafety::Make(gb,pose),P.PartGap,P.AllowCommonEdges,P.Tolerance)!=AxisSafety::Verdict::Safe)safe=false;
                    }
                }
                return safe;
            };
            const auto retain=[&](Point pitch)
            {
                if(std::none_of(pitches.begin(),pitches.end(),[&](Point old)
                    {return std::abs(old.X-pitch.X)<=P.Tolerance&&std::abs(old.Y-pitch.Y)<=P.Tolerance;}))pitches.push_back(pitch);
            };
            const double retreat=std::max(1e-7,4*P.Tolerance);
            for(bool xFirst:{true,false})
            {
                if(B.Stop())break;
                const auto& axial=xFirst?xs:ys;const auto maximum=xFirst?boxPitch.X:boxPitch.Y;
                std::optional<double> fixed;
                for(const auto value:axial)
                {
                    for(const double proposed:{value,value+retreat})
                    {
                        if(B.Stop())break;if(proposed>maximum+P.Tolerance)continue;
                        const Point pitch=xFirst?Point{proposed,boxPitch.Y}:Point{boxPitch.X,proposed};
                        if(periodicSafe(pitch)){fixed=proposed;break;}
                    }
                    if(fixed||B.Stop())break;
                }
                if(!fixed)continue;
                // Once one axis is fixed, intersect the member-pair forbidden
                // domains at every relevant periodic neighbour of that axis.
                // Independent x/y minima are not a jointly safe lattice.
                auto variable=xFirst?ys:xs;const auto variableMaximum=xFirst?boxPitch.Y:boxPitch.X;
                const auto extent=xFirst?pattern.Width+P.PartGap:pattern.Height+P.PartGap;
                const auto neighbours=static_cast<int>(std::min(8.,std::ceil(extent / *fixed)));
                for(int k=-neighbours;k<=neighbours&&!B.Stop();++k)
                for(const auto& edge:boundaries)
                {
                    if(B.Stop())break;const double line=k * *fixed;
                    if(xFirst)cut(edge.A.X-line,edge.B.X-line,edge.A.Y,edge.B.Y,variable);
                    else cut(edge.A.Y-line,edge.B.Y-line,edge.A.X,edge.B.X,variable);
                }
                trim(variable,variableMaximum);
                for(const auto value:variable)
                {
                    bool accepted=false;
                    for(const double proposed:{value,value+retreat})
                    {
                        if(B.Stop())break;if(proposed>variableMaximum+P.Tolerance)continue;
                        const Point pitch=xFirst?Point{*fixed,proposed}:Point{proposed,*fixed};
                        if(periodicSafe(pitch)){retain(pitch);accepted=true;break;}
                    }
                    if(accepted||B.Stop())break;
                }
            }
            Pitches.push_back(std::move(pitches));
        }
    }
    void AppendOrdinaryUnitOrientations()
    {
        // A tight unit remains one placeable object. Its orientation relative
        // to a free region is not fixed by the orientation in which preparation
        // first discovered it. Keep the original stream, then append legal
        // physical quarter turns/reflections with their corresponding pitches.
        // Preparation may return before building pitches when its own lease
        // expires. Its caller can still have construction work left. Preserve
        // template/pitch alignment with conservative source-unit box periods.
        const double separation=P.PartGap>0?P.PartGap+std::max(1e-7,4*P.Tolerance):0;
        while(Pitches.size()<Templates.size())
        {
            const auto& unit=Templates[Pitches.size()];
            Pitches.push_back({{unit.Width+separation,unit.Height+separation}});
        }
        using MemberKey=std::array<std::uint64_t,5>;
        const auto signature=[](const Pattern& pattern)
        {
            std::vector<MemberKey> key;
            for(const auto& member:pattern.Members)key.push_back({member.PartIndex,
                std::bit_cast<std::uint64_t>(member.Transform.X),
                std::bit_cast<std::uint64_t>(member.Transform.Y),
                std::bit_cast<std::uint64_t>(member.Transform.AngleDegrees),
                std::uint64_t(member.Transform.Mirrored)});
            std::sort(key.begin(),key.end());return key;
        };
        const auto mergePitches=[](std::vector<Point>& target,const std::vector<Point>& source)
        {
            const auto key=[](Point pitch)
            {return std::array<std::uint64_t,2>{std::bit_cast<std::uint64_t>(pitch.X),
                std::bit_cast<std::uint64_t>(pitch.Y)};};
            std::set<std::array<std::uint64_t,2>> seen;
            for(const auto pitch:target)seen.insert(key(pitch));
            for(const auto pitch:source)if(seen.insert(key(pitch)).second)target.push_back(pitch);
        };
        std::map<std::vector<MemberKey>,std::size_t> seen;
        for(std::size_t ti=0;ti<Templates.size();++ti)seen.try_emplace(signature(Templates[ti]),ti);
        const auto originalCount=Templates.size();
        for(std::size_t ti=0;ti<originalCount&&!B.Stop();++ti)
        {
            const auto source=Templates[ti];
            if(source.Members.size()<2||source.Members.size()>4||!HasSinglePartType(source.Members))continue;
            const auto sourcePitches=Pitches[ti];
            for(const bool mirrored:{false,true})for(const double angle:{0.,90.,180.,270.})
            {
                if(B.Stop())return;
                auto unit=source;bool legal=true;
                for(auto& member:unit.Members)
                {
                    member.Transform=ComposePose({0,0,angle,mirrored},member.Transform);
                    legal&=PoseAllowed(P.Parts[member.PartIndex],member.Transform,P.Tolerance)&&
                        (B.O.OrientationProposals!=OrientationProposalPolicy::OriginalOnly||!member.Transform.Mirrored);
                }
                if(!legal)continue;
                NormalizePattern(unit,P);
                auto pitches=sourcePitches;
                if(angle==90||angle==270)for(auto& pitch:pitches)std::swap(pitch.X,pitch.Y);
                auto key=signature(unit);
                if(const auto target=seen.find(key);target!=seen.end())
                {mergePitches(Pitches[target->second],pitches);continue;}
                const auto target=Templates.size();
                Templates.push_back(std::move(unit));Pitches.push_back(std::move(pitches));
                seen.emplace(std::move(key),target);
            }
        }
    }
    bool StrictRectangles()const
    {return S.Kind==Kind::RectangleContact||S.Kind==Kind::SingleColumn||S.Kind==Kind::SingleProductArray||S.Kind==Kind::RectangleBlockSearch||S.Kind==Kind::FourBlockArray;}
    bool ArrayFillConstruction()const
    {return !StrictRectangles()&&!Searching(S.Kind)&&!IsNfp(S.Kind)&&!ContourArray(S.Kind)&&S.Kind!=Kind::Scanline;}
    std::vector<std::size_t> LargeFirstSpecifications()const
    {
        std::vector<std::size_t> order(P.Parts.size());std::iota(order.begin(),order.end(),0);
        std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return Areas[a]>Areas[b];});
        return order;
    }
    SafetyHandle GeometryFor(std::size_t part,Pose pose)
    {
        // Unsupported arithmetic must not reuse a formerly valid geometry or
        // publish its new Invalid geometry into the supported-mode cache.
        if(!AxisSafety::ArithmeticSupported())
            return std::make_shared<const AxisSafety::Geometry>(AxisSafety::Make(P.Parts[part].Geometry,pose));
        const SafetyKey key{part,pose.X,pose.Y,pose.AngleDegrees,pose.Mirrored};
        // Invalid poses still follow Make's existing conservative verdict; do
        // not put nonfinite values into an ordered exact-pose key.
        const bool cacheable=std::isfinite(pose.X)&&std::isfinite(pose.Y)&&std::isfinite(pose.AngleDegrees);
        if(cacheable)
            if(const auto found=SafetyCache.find(key);found!=SafetyCache.end())return found->second;
        SafetyHandle geometry;
        const auto angle=NormalizeAngle(pose.AngleDegrees);
        const auto& shape=P.Parts[part].Geometry;
        if(cacheable&&shape.Outer.size()>32&&(angle==0||angle==90||angle==180||angle==270)&&
            AxisSafety::CoordinateBounds(AxisSafety::Number(pose.X)).Valid()&&
            AxisSafety::CoordinateBounds(AxisSafety::Number(pose.Y)).Valid())
        {
            const auto quarterKey=std::tuple(part,angle,pose.Mirrored);
            auto prepared=QuarterSafetyCache.find(quarterKey);
            if(prepared==QuarterSafetyCache.end()&&QuarterSafetyCache.size()<128)
            {
                auto local=std::make_shared<const AxisSafety::Geometry>(AxisSafety::Make(shape,{0,0,angle,pose.Mirrored}));
                std::size_t size=local->Hull.size();for(const auto& ring:local->Rings)size+=ring.size();
                if(local->Valid&&local->Envelope.X.Valid()&&local->Envelope.Y.Valid()&&size<=65536-QuarterSafetyCacheVertices)
                {QuarterSafetyCacheVertices+=size;prepared=QuarterSafetyCache.emplace(quarterKey,std::move(local)).first;}
            }
            if(prepared!=QuarterSafetyCache.end())
                geometry=std::make_shared<const AxisSafety::Geometry>(AxisSafety::TranslateQuarterTurn(*prepared->second,{pose.X,pose.Y}));
        }
        if(!geometry)geometry=std::make_shared<const AxisSafety::Geometry>(AxisSafety::Make(shape,pose));
        std::size_t vertices=std::max<std::size_t>(1,geometry->Hull.size());
        for(const auto& ring:geometry->Rings)
        {
            if(ring.size()>65536-std::min<std::size_t>(vertices,65536)){vertices=65537;break;}
            vertices+=ring.size();
        }
        // Immutable handles held by a candidate or the current board stay
        // valid even when this bounded per-task cache cannot admit more poses.
        if(cacheable&&SafetyCache.size()<4096&&vertices<=65536-SafetyCacheVertices)
        {
            SafetyCache.emplace(key,geometry);SafetyCacheVertices+=vertices;
        }
        return geometry;
    }
    bool Fits(const State& state,const std::vector<Member>& members,std::string* reason=nullptr)
    {
        struct ProfileTime
        {
            ArrayQueryProfile* Profile;
            Budget& Work;
            double Started;
            ~ProfileTime(){if(Profile)Profile->FitsSeconds+=Work.Seconds()-Started;}
        } profileTime{ActiveArrayQueryProfile,B,ActiveArrayQueryProfile?B.Seconds():0};
        if(ActiveArrayQueryProfile)++ActiveArrayQueryProfile->FitsCalls;
        const auto reject=[&](const char* value){if(reason)*reason=value;return false;};
        std::vector<std::size_t> required(P.Parts.size());
        std::vector<SafetyHandle> memberGeometry;memberGeometry.reserve(members.size());
        SafetyHandle unsupportedFixed;
        bool fixedReady=false;
        const auto fixedGeometry=[&](std::size_t index)->const AxisSafety::Geometry&
        {
            if(!AxisSafety::ArithmeticSupported())
            {
                const auto& placed=state.Board.Placements[index];
                unsupportedFixed=GeometryFor(Index.at(placed.PartID),placed.Transform);
                return *unsupportedFixed;
            }
            if(!fixedReady)
            {
                bool same=SafetyFixed.size()==state.Board.Placements.size();
                for(std::size_t i=0;same&&i<SafetyFixed.size();++i)
                {
                    const auto& placed=state.Board.Placements[i];const auto& cached=SafetyFixed[i];
                    same=cached.Part==Index.at(placed.PartID)&&cached.Transform.X==placed.Transform.X&&
                        cached.Transform.Y==placed.Transform.Y&&cached.Transform.AngleDegrees==placed.Transform.AngleDegrees&&
                        cached.Transform.Mirrored==placed.Transform.Mirrored;
                }
                if(!same)
                {
                    SafetyFixed.clear();SafetyFixed.reserve(state.Board.Placements.size());
                    for(const auto& placed:state.Board.Placements)
                        SafetyFixed.push_back({Index.at(placed.PartID),placed.Transform,{}});
                }
                fixedReady=true;
            }
            auto& cached=SafetyFixed[index];
            if(!cached.Value)cached.Value=GeometryFor(cached.Part,cached.Transform);
            return *cached.Value;
        };
        for(std::size_t i=0;i<members.size();++i)
        {
            if(B.Stop())return reject("budget-before-material-check");++B.Attempts;
            const auto& m=members[i];const auto& part=P.Parts[m.PartIndex];
            if(++required[m.PartIndex]>part.Quantity-state.Used[m.PartIndex])return reject("remaining-quantity");
            if(!PoseAllowed(part,m.Transform,P.Tolerance))return reject("orientation-not-allowed");
            if(!InsideSheet(part.Geometry,m.Transform,state.Board.Width,state.Board.Height,P.EdgeMargin,P.Tolerance))return reject("outside-sheet-margin");
            auto nominal=GeometryFor(m.PartIndex,m.Transform);
            if(AxisSafety::Containment(*nominal,state.Board.Width,state.Board.Height,P.EdgeMargin)!=AxisSafety::Verdict::Safe)return reject("exact-containment-not-safe");
            const auto safe=[&](std::size_t other,Pose pose,const auto& geometry)
            {
                const bool supported=AxisSafety::ArithmeticSupported();
                const bool cacheable=supported&&std::isfinite(m.Transform.X)&&std::isfinite(m.Transform.Y)&&
                    std::isfinite(m.Transform.AngleDegrees)&&std::isfinite(pose.X)&&std::isfinite(pose.Y)&&std::isfinite(pose.AngleDegrees);
                PairKey key{};
                PairCacheEntry* slot=nullptr;
                if(cacheable)
                {
                    key={static_cast<std::uint64_t>(m.PartIndex),std::bit_cast<std::uint64_t>(m.Transform.X),
                        std::bit_cast<std::uint64_t>(m.Transform.Y),std::bit_cast<std::uint64_t>(m.Transform.AngleDegrees),
                        static_cast<std::uint64_t>(m.Transform.Mirrored),
                        static_cast<std::uint64_t>(other),std::bit_cast<std::uint64_t>(pose.X),
                        std::bit_cast<std::uint64_t>(pose.Y),std::bit_cast<std::uint64_t>(pose.AngleDegrees),
                        static_cast<std::uint64_t>(pose.Mirrored)};
                    if(PairCache.empty())PairCache.resize(16384);
                    slot=&PairCache[PairKeyHash{}(key)%PairCache.size()];
                    if(slot->Occupied&&slot->Key==key)
                    {++PairCacheHits;return slot->Value==AxisSafety::Verdict::Safe;}
                }
                ++PairCacheMisses;
                // Preserve the original short circuit and every exact Pair
                // verdict; Uncertain remains a rejection on both hits and misses.
                auto verdict=AxisSafety::Verdict::Blocked;
                // Distant exact world bounds certify both material separation
                // and the nominal gap without repeatedly transforming every
                // source vertex inside ShapesConflict for this immutable pair.
                if(supported&&AxisSafety::EnvelopesSeparated(*nominal,geometry(),
                    AxisSafety::Clearance(*nominal,geometry(),P.PartGap)))verdict=AxisSafety::Verdict::Safe;
                else if(!ShapesConflict(part.Geometry,m.Transform,P.Parts[other].Geometry,pose,P.PartGap,P.Tolerance,P.AllowCommonEdges))
                {
                    if(supported)verdict=AxisSafety::Pair(*nominal,geometry(),P.PartGap,P.AllowCommonEdges,P.Tolerance);
                    else verdict=AxisSafety::Pair(AxisSafety::Make(part.Geometry,m.Transform),
                        AxisSafety::Make(P.Parts[other].Geometry,pose),P.PartGap,P.AllowCommonEdges,P.Tolerance);
                }
                // A collision replaces only cached work. Full-key equality
                // remains mandatory, so eviction cannot change any verdict.
                if(slot)*slot={key,verdict,true};
                return verdict==AxisSafety::Verdict::Safe;
            };
            for(std::size_t j=0;j<state.Board.Placements.size();++j)
            {
                const auto& placed=state.Board.Placements[j];
                if(B.Stop()||!safe(Index.at(placed.PartID),placed.Transform,[&]()->const AxisSafety::Geometry&
                    {return fixedGeometry(j);}))return reject("fixed-material-conflict-or-interrupted");
            }
            for(std::size_t j=0;j<i;++j)
                if(B.Interrupted()||!safe(members[j].PartIndex,members[j].Transform,[&]()->const AxisSafety::Geometry&
                    {return *memberGeometry[j];}))return reject("candidate-members-conflict-or-interrupted");
            memberGeometry.push_back(std::move(nominal));
        }
        return true;
    }
    bool CheckCandidate(const State& state,Candidate& candidate,const std::string& source)
    {
        if(!HasSinglePartType(candidate.Members))
        {
            Identify(candidate);
            ObserveCandidate(ConstructionTraceStage::Filter,state,candidate,source,"cross-type-combination-disabled",false);
            return false;
        }
        Identify(candidate);ObserveCandidate(ConstructionTraceStage::Generate,state,candidate,source,"proposed");
        std::string reason;const bool fits=Fits(state,candidate.Members,B.O.ConstructionTrace?&reason:nullptr);
        ObserveCandidate(ConstructionTraceStage::Filter,state,candidate,source,fits?"original-material-safe":reason,fits);
        return fits;
    }
    std::optional<double> Contact(const State& state,std::size_t part,Pose pose,Rect region,bool directional,
        std::vector<std::pair<std::string,double>>* measures=nullptr) try
    {
        using namespace Clipper2Lib;
        if(B.Stop())return {};
        const auto pathBounds=[](const PathsD& paths)
        {
            double x0=std::numeric_limits<double>::infinity(),y0=x0,x1=-x0,y1=-x0;
            for(const auto& path:paths)for(const auto q:path)
            {x0=std::min(x0,q.x);y0=std::min(y0,q.y);x1=std::max(x1,q.x);y1=std::max(y1,q.y);}
            return std::isfinite(x0)?Rect{x0,y0,x1-x0,y1-y0}:Rect{};
        };
        bool same=ContactFixed.size()==state.Board.Placements.size();
        for(std::size_t i=0;same&&i<ContactFixed.size();++i)
        {
            const auto& old=state.Board.Placements[i];const auto& cached=ContactFixed[i];
            same=cached.Part==Index.at(old.PartID)&&cached.Transform.X==old.Transform.X&&
                cached.Transform.Y==old.Transform.Y&&cached.Transform.AngleDegrees==old.Transform.AngleDegrees&&
                cached.Transform.Mirrored==old.Transform.Mirrored;
        }
        if(!same)
        {
            std::vector<ContactGeometry> fixed;fixed.reserve(state.Board.Placements.size());
            for(const auto& old:state.Board.Placements)
            {
                if(B.Stop())return {};
                ContactGeometry cached;cached.Part=Index.at(old.PartID);cached.Transform=old.Transform;
                cached.Material=Paths(P.Parts[cached.Part].Geometry,old.Transform);cached.Box=pathBounds(cached.Material);
                fixed.push_back(std::move(cached));
            }
            if(B.Stop())return {};
            ContactFixed.swap(fixed);
        }
        PathsD expanded;
        if(IsNfp(S.Kind))
        {
            const auto key=std::tuple(part,NormalizeAngle(pose.AngleDegrees),pose.Mirrored);
            auto prepared=ContactExpanded.find(key);
            std::shared_ptr<const PathsD> local;
            if(prepared!=ContactExpanded.end())local=prepared->second;
            else
            {
                local=std::make_shared<const PathsD>(InflatePaths(Paths(P.Parts[part].Geometry,{0,0,std::get<1>(key),std::get<2>(key)}),
                    S.ContactBand,JoinType::Miter,EndType::Polygon,2.,8));
                std::size_t vertices=0;
                for(const auto& path:*local)
                {if(path.size()>65536-std::min<std::size_t>(vertices,65536)){vertices=65537;break;}vertices+=path.size();}
                if(ContactExpanded.size()<256&&vertices<=65536-ContactExpandedVertices)
                {ContactExpanded.emplace(key,local);ContactExpandedVertices+=vertices;}
            }
            // This is a translation-invariant local precision-8 scoring proxy.
            // Only the proxy is reused: all original-material safety continues
            // to use the actual final world pose and independent exact checks.
            expanded=*local;
            for(auto& path:expanded)for(auto& q:path){q.x+=pose.X;q.y+=pose.Y;}
        }
        else expanded=InflatePaths(Paths(P.Parts[part].Geometry,pose),S.ContactBand,JoinType::Miter,EndType::Polygon,2.,8);
        const auto expandedBox=pathBounds(expanded);
        // Bounds come from the actual clipped/offset paths, including miters,
        // rather than assuming ContactBand is the maximum offset at a corner.
        const double guard=std::max(1e-7,P.Tolerance);
        double neighbors=0;
        for(const auto& old:ContactFixed)
        {
            if(B.Stop())return {};
            if(expandedBox.X+expandedBox.Width<old.Box.X-guard||old.Box.X+old.Box.Width<expandedBox.X-guard||
                expandedBox.Y+expandedBox.Height<old.Box.Y-guard||old.Box.Y+old.Box.Height<expandedBox.Y-guard)continue;
            const auto intersection=Intersect(expanded,old.Material,FillRule::EvenOdd,8);
            neighbors+=directional?Perimeter(intersection)*.5:std::abs(Clipper2Lib::Area(intersection));
        }
        PathsD outside;
        if(!Within(expandedBox,{region.X+guard,region.Y+guard,region.Width-2*guard,region.Height-2*guard},0))
        {
            PathsD rect{{{region.X,region.Y},{region.X+region.Width,region.Y},{region.X+region.Width,region.Y+region.Height},{region.X,region.Y+region.Height}}};
            outside=Difference(expanded,rect,FillRule::EvenOdd,8);
        }
        const auto box=Bounds(P.Parts[part].Geometry,pose);
        const double outsideArea=(!directional||measures)?std::abs(Clipper2Lib::Area(outside)):0;
        const double outsideHalfPerimeter=(directional||measures)?Perimeter(outside)*.5:0;
        const double score=directional?(S.XFirst?box.X:box.Y)-neighbors-.4*outsideHalfPerimeter:
            -neighbors-.9*outsideArea;
        // Contact is a tie-breaker for already certified material. Never
        // publish partial geometry work or a non-finite ranking component.
        if(B.Stop()||!std::isfinite(score)||!std::isfinite(neighbors)||
            !std::isfinite(outsideArea)||!std::isfinite(outsideHalfPerimeter))return {};
        if(measures)*measures={{"neighborContact",neighbors},{"outsideArea",outsideArea},{"outsideHalfPerimeter",outsideHalfPerimeter},
            {"primaryAxis",S.XFirst?box.X:box.Y},{"directional",directional?1.:0.},{"contactBand",S.ContactBand},
            {"localOffsetScoringPrecision",IsNfp(S.Kind)?8.:0.}};
        return score;
    }
    catch(const std::exception&){return {};}
    ClearanceNfpPlacement& Generator(const Stock& stock)
    {
        auto it=Generators.find(stock.ID);
        // A preparation lease may end while building a generator. It must not
        // poison the later ordinary-placement phase with a permanently partial
        // generator. Only the immutable pair cache is shared across phases.
        if(it==Generators.end()||(!it->second->InitializationComplete()&&!B.Stop()))
        {
            auto generator=std::make_unique<ClearanceNfpPlacement>(P,stock,Cache,[&]{return B.Stop();});
            it=Generators.insert_or_assign(stock.ID,std::move(generator)).first;
        }
        return *it->second;
    }
    ClearanceNfpPlacement& Generator(const State& state)
    {
        const auto stock=std::find_if(P.Stocks.begin(),P.Stocks.end(),[&](const Stock& s){return s.ID==state.Board.StockID;});
        return Generator(*stock);
    }
    void Commit(State& state,const Candidate& c,bool filler=false,const std::optional<Rect>& frozenFillBand={},int constructionPhase=0)
    {
        const auto parent=state.TraceID;
        const auto prefixSize=state.Board.Placements.size();
        const auto prefixMaterial=state.Board.MaterialArea;
        const auto group=NextGroup++;
        for(const auto& m:c.Members)
        {
            const auto& part=P.Parts[m.PartIndex];
            state.Board.Placements.push_back({part.ID,state.Used[m.PartIndex]++,m.Transform,group,filler});
            state.Board.MaterialArea+=Areas[m.PartIndex];
            if(filler)++state.Fillers;
        }
        state.Free.Occupy({c.Box.X-P.EdgeMargin,c.Box.Y-P.EdgeMargin,c.Box.Width+P.PartGap,c.Box.Height+P.PartGap});
        if(c.Members.size()>1)++state.Groups;
        state.Product=c.Part;state.ColumnWidth=c.Box.Width;state.ColumnTop=c.Box.Y+c.Box.Height;
        if(B.O.ConstructionTrace)state.TraceID=++NextState;
        Observe(ConstructionTraceStage::Commit,[&](ConstructionTraceEvent& event)
        {
            event.CandidateID=c.TraceID;event.ProposalID=c.TraceProposal;event.StateID=state.TraceID;event.ParentStateID=parent;
            event.Source=filler?"fill.commit":"constructor.commit";event.Reason="original-instances-committed";
            event.Layouts={state.Board};event.StockInstance=state.Board.StockInstance;
            TracePlacementRank(event,c);
            if(constructionPhase)event.Measures.emplace_back("constructionPhase",double(constructionPhase));
            if(c.UnitMembers&&c.Members.size()%c.UnitMembers==0)event.Measures.insert(event.Measures.end(),{
                {"unitMembers",double(c.UnitMembers)},{"arrayCells",double(c.Members.size()/c.UnitMembers)}});
            if(frozenFillBand)event.Measures.insert(event.Measures.end(),{
                {"fillBandX",frozenFillBand->X},{"fillBandY",frozenFillBand->Y},
                {"fillBandWidth",frozenFillBand->Width},{"fillBandHeight",frozenFillBand->Height},
                {"frozenFillTop",frozenFillBand->Y+frozenFillBand->Height}});
            if(c.PlanningInset>0)event.Measures.emplace_back("arrayPlanningInset",c.PlanningInset);
            if(c.PlacementQueries)event.Measures.insert(event.Measures.end(),
                {{"placementConstructionSeconds",c.ConstructionSeconds},{"placementQueries",double(c.PlacementQueries)},
                 {"generatedPositions",double(c.GeneratedPositions)},{"verifiedPositions",double(c.VerifiedPositions)}});
            for(std::size_t i=state.Board.Placements.size()-c.Members.size();i<state.Board.Placements.size();++i)
            {const auto& placed=state.Board.Placements[i];event.Members.push_back({Index.at(placed.PartID),placed.Instance,placed.Transform,placed.Group});}
            auto history=std::make_shared<CommitHistory>();
            history->Parent=state.History;history->Record=event;history->Record.Layouts.clear();
            history->Header={state.Board.StockID,state.Board.StockInstance,state.Board.Width,state.Board.Height,{},state.Board.MaterialArea};
            history->Added.assign(state.Board.Placements.begin()+prefixSize,state.Board.Placements.end());
            if(!history->Parent)
            {
                history->Base=history->Header;history->Base.MaterialArea=prefixMaterial;
                history->Base.Placements.assign(state.Board.Placements.begin(),state.Board.Placements.begin()+prefixSize);
            }
            state.History=std::move(history);
        });
    }
    void SelectedPath(const State& state)const
    {
        if(!B.O.ConstructionTrace)return;
        std::vector<const CommitHistory*> chain;
        for(auto node=state.History.get();node;node=node->Parent.get())chain.push_back(node);
        std::reverse(chain.begin(),chain.end());
        Layout prefix=chain.empty()?state.Board:chain.front()->Base;
        for(const auto* node:chain)
        {
            prefix.StockID=node->Header.StockID;prefix.StockInstance=node->Header.StockInstance;
            prefix.Width=node->Header.Width;prefix.Height=node->Header.Height;prefix.MaterialArea=node->Header.MaterialArea;
            prefix.Placements.insert(prefix.Placements.end(),node->Added.begin(),node->Added.end());
            Observe(ConstructionTraceStage::Selected,[&](ConstructionTraceEvent& event)
            {
                const auto step=event.Step;const auto elapsed=event.ElapsedSeconds;
                event=node->Record;event.Stage=ConstructionTraceStage::Selected;event.Step=step;event.ElapsedSeconds=elapsed;
                event.Source="construction.path.commit";event.Reason="retained-original-commit:"+node->Record.Source;
                event.Measures.emplace_back("originalCommitStep",double(node->Record.Step));
                event.Measures.emplace_back("originalCommitElapsedSeconds",node->Record.ElapsedSeconds);
                event.Layouts={prefix};
            });
        }
        Observe(ConstructionTraceStage::Complete,[&](ConstructionTraceEvent& event)
        {
            event.Source="construction.path.raw";event.StateID=state.TraceID;event.Layouts={state.Board};
            event.Reason="actual-selected-board-before-optional-refinement";
            event.Measures={{"retainedCommitSteps",double(chain.size())}};
        });
    }
    struct UtilizationContext
    {
        std::optional<Rect> Occupied;
        double Material=0;
        double BoardWidth=0,BoardHeight=0;
    };
    UtilizationContext UtilizationBefore(const State& state)const
    {
        UtilizationContext context;
        context.BoardWidth=state.Board.Width;context.BoardHeight=state.Board.Height;
        for(const auto& item:state.Board.Placements)
        {
            const auto pi=Index.at(item.PartID);const auto box=Bounds(P.Parts[pi].Geometry,item.Transform);
            context.Occupied=context.Occupied?Unite(*context.Occupied,box):box;
            context.Material+=Areas[pi];
        }
        return context;
    }
    double UsedSectionArea(const UtilizationContext& context,Rect occupied)const
    {
        // A legal fixed-width board consumes its full width from the board
        // origin through the highest planned material, including top margin.
        // Cropping side/bottom blanks out of a group AABB would reward a tall
        // narrow column even though that same whole stock section was used.
        const auto length=std::clamp(occupied.Y+occupied.Height+P.EdgeMargin,0.,context.BoardHeight);
        return context.BoardWidth*length;
    }
    void ScoreUtilization(const UtilizationContext& context,Candidate& candidate)const
    {
        candidate.ContactRank=0;
        candidate.Space={};candidate.SpaceEvaluationSeconds=0;candidate.SpaceCandidatesEvaluated=0;
        // Only original net material contributes to the numerator. Planning
        // gaps, template voids, holes and boundary inset remain occupied/waste
        // area; none is a bonus for carrying another member or a host hole.
        // This is board-placement utilization. A combination's own Fill is
        // separately measured by its net originals / local carrier AABB.
        candidate.Material=0;
        for(const auto& member:candidate.Members)candidate.Material+=Areas[member.PartIndex];
        candidate.OccupiedMaterial=context.Material+candidate.Material;
        const auto occupied=context.Occupied?Unite(*context.Occupied,candidate.Box):candidate.Box;
        candidate.OccupiedPlanningArea=BoxArea(occupied);
        candidate.UsedSectionArea=UsedSectionArea(context,occupied);
        candidate.Utilization=candidate.UsedSectionArea>0&&std::isfinite(candidate.UsedSectionArea)?
            candidate.OccupiedMaterial/candidate.UsedSectionArea:-std::numeric_limits<double>::infinity();
        const auto ties=candidate.Members.size()==1?SinglePlacementTies(context,candidate.Members[0].PartIndex,
            candidate.Members[0].Transform,candidate.OccupiedPlanningArea):std::pair(candidate.OccupiedPlanningArea,0.);
        candidate.TieArea=ties.first;candidate.TieCorner=ties.second;
        candidate.PrimaryAxis=!context.Occupied||S.XFirst?candidate.Box.X:candidate.Box.Y;
        candidate.SecondaryAxis=!context.Occupied||S.XFirst?candidate.Box.Y:candidate.Box.X;
    }
    std::pair<double,double> SinglePlacementTies(const UtilizationContext& context,std::size_t part,Pose pose,
        double occupiedPlanningArea)const
    {
        if(context.Occupied)return {occupiedPlanningArea,0};
        const auto key=std::tuple(part,NormalizeAngle(pose.AngleDegrees),pose.Mirrored);
        auto found=IntrinsicPlacementAssessments.find(key);
        if(found==IntrinsicPlacementAssessments.end())
        {
            const Pose orientation{0,0,std::get<1>(key),pose.Mirrored};
            const auto box=Bounds(P.Parts[part].Geometry,orientation);
            found=IntrinsicPlacementAssessments.emplace(key,IntrinsicPlacementAssessment{
                BoxArea(box),FirstCornerDistance(P.Parts[part].Geometry,orientation,box)}).first;
        }
        return {found->second.EnvelopeArea,found->second.MaterialCornerDistance};
    }
    auto Rank(const Candidate& c)const
    {
        // Progress and stable placement order only resolve exactly equal
        // utilization. The number of originals is never an objective proxy.
        return std::tuple(-c.Utilization,-c.Material,c.TieArea,c.TieCorner,c.PrimaryAxis,c.SecondaryAxis,
            c.Part,c.Template,c.Columns,c.Rows);
    }
    auto LocalPlacementRank(const Candidate& c)const
    {
        // Strategy-specific contact and the shape of the space left by this
        // current placement only resolve equal utilization/net material.
        // No future original is trial-placed to produce these terms.
        return std::tuple(-c.Utilization,-c.Material,c.ContactRank,
            c.Space.Valid?-c.Space.LargestFreeRegularArea:0.,
            c.Space.Valid?c.Space.FragmentFreeArea:0.,
            c.Space.Valid?c.Space.FreePerimeter:0.,
            c.Space.Valid?-c.Space.Attachment:0.,
            c.TieArea,c.TieCorner,c.PrimaryAxis,c.SecondaryAxis,c.Part,c.Template,c.Columns,c.Rows);
    }
    bool SourceEvaluation(Candidate& candidate,const Pattern& pattern,Rect region)
    {
        if(S.UseSourceArrayEvaluation)
            Observe(ConstructionTraceStage::Score,[&](ConstructionTraceEvent& event)
            {
                event.Source="array.source-43";event.CandidateID=candidate.TraceID;
                event.Reason="original-material-utilization-replaces-count-weight";
                event.Accepted=std::isfinite(candidate.Utilization);
                event.Measures.insert(event.Measures.end(),{{"localUtilization",candidate.Utilization},
                    {"candidateNetMaterialArea",candidate.Material},{"occupiedNetMaterialArea",candidate.OccupiedMaterial},
                    {"occupiedPlanningArea",candidate.OccupiedPlanningArea},{"usedSectionArea",candidate.UsedSectionArea},
                    {"usedSectionUtilization",candidate.Utilization},{"regionWidth",region.Width},{"regionHeight",region.Height},
                    {"templateMembers",double(pattern.Members.size())}});
            });
        candidate.Score=candidate.Utilization;
        return std::isfinite(candidate.Utilization);
    }
    void TracePlacementRank(ConstructionTraceEvent& event,const Candidate& candidate)const
    {
        TraceTuple(event,LocalPlacementRank(candidate),{"negativeUsedSectionUtilization","negativeNetMaterial",
            "strategyContact","negativeLargeRegularFreeSpace","fragmentFreeArea","freeBoundaryPerimeter","negativeAttachment",
            "orientationOrOccupiedArea","intrinsicFirstMaterialCornerDistance","primaryAxis",
            "secondaryAxis","part","template","columns","rows"});
        event.Measures.emplace_back("strategyContact",candidate.ContactRank);
        event.Measures.emplace_back("spaceScoreValid",candidate.Space.Valid?1.:0.);
        if(candidate.Space.Valid)
        {
            event.Measures.insert(event.Measures.end(),{
                {"largestFreeArea",candidate.Space.LargestFreeArea},
                {"largeRegularFreeSpace",candidate.Space.LargestFreeRegularArea},
                {"fragmentFreeArea",candidate.Space.FragmentFreeArea},{"freeBoundaryPerimeter",candidate.Space.FreePerimeter},
                {"attachment",candidate.Space.Attachment},{"freeArea",candidate.Space.FreeArea},
                {"freeComponents",double(candidate.Space.FreeComponents)},{"spaceCircleProxy",candidate.Space.CircleProxyUsed?1.:0.},
                {"spaceEvaluationSeconds",candidate.SpaceEvaluationSeconds},
                {"spaceCandidatesEvaluated",double(candidate.SpaceCandidatesEvaluated)}});
        }
    }
    Candidate WithTail(const State& state,Candidate main,Rect region,const UtilizationContext& context)
    {
        ScoreUtilization(context,main);
        Candidate best=main;
        const auto mainNeed=static_cast<std::size_t>(std::count_if(main.Members.begin(),main.Members.end(),[&](const Member& m){return m.PartIndex==main.Part;}));
        const auto available=P.Parts[main.Part].Quantity-state.Used[main.Part];
        if(available<=mainNeed)return main;
        const double gap=P.PartGap>0?P.PartGap+std::max(1e-7,4*P.Tolerance):0;
        const double right=main.Box.X+main.Box.Width+gap,top=main.Box.Y+main.Box.Height+gap;
        const Rect rightFull{right,region.Y,region.X+region.Width-right,region.Height};
        const Rect topFull{region.X,top,region.Width,region.Y+region.Height-top};
        const Rect topLeft{region.X,top,main.Box.Width,region.Y+region.Height-top};
        const Rect rightBottom{right,region.Y,region.X+region.Width-right,main.Box.Height};
        const auto grow=[&](const Candidate& base,Rect space)
        {
            Candidate grown=base;
            const auto used=static_cast<std::size_t>(std::count_if(base.Members.begin(),base.Members.end(),[&](const Member& m){return m.PartIndex==base.Part;}));
            const auto remain=available>used?available-used:0;
            if(!remain||space.Width<=0||space.Height<=0)return grown;
            for(const auto& t:Templates)
            {
                if(B.Stop())break;
                if(t.Members.size()!=1||t.Members[0].PartIndex!=base.Part)continue;
                const auto nx=std::min<std::size_t>(S.MaxArrayAxis,static_cast<std::size_t>(std::max(0.,std::floor((space.Width+gap+P.Tolerance)/(t.Width+gap)))));
                const auto ny=std::min<std::size_t>(S.MaxArrayAxis,static_cast<std::size_t>(std::max(0.,std::floor((space.Height+gap+P.Tolerance)/(t.Height+gap)))));
                if(!nx||!ny)continue;
                Candidate joined=base;
                for(std::size_t k=0;k<std::min(remain,nx*ny);++k)
                {
                    auto m=t.Members[0];m.Transform.X+=space.X+(k%nx)*(t.Width+gap);m.Transform.Y+=space.Y+(k/nx)*(t.Height+gap);
                    joined.Members.push_back(m);joined.Box=Unite(joined.Box,Bounds(P.Parts[m.PartIndex].Geometry,m.Transform));joined.Material+=Areas[m.PartIndex];
                }
                ScoreUtilization(context,joined);
                if(Rank(joined)<Rank(grown))
                {
                    joined.TraceID=0;
                    if(CheckCandidate(state,joined,"array.tail-partition"))grown=std::move(joined);
                }
            }
            return grown;
        };
        // The two partitions are disjoint. Try both orders so one tail cannot
        // claim the upper-right corner and silently hide the other remainder.
        for(const auto& spaces:std::array<std::array<Rect,2>,2>{{{rightFull,topLeft},{topFull,rightBottom}}})
        {
            if(B.Stop())break;
            auto candidate=grow(main,spaces[0]);candidate=grow(candidate,spaces[1]);
            if(Rank(candidate)<Rank(best))best=std::move(candidate);
        }
        return best;
    }
    void IndependentPairs(const State& state,const Pattern& pattern,std::size_t ti,
        Point pitch,Rect region,std::vector<Candidate>& result,const UtilizationContext& context,
        std::optional<Candidate>* incumbent=nullptr,
        const std::function<bool(Candidate)>& propose={})
    {
        if(!S.EnableIndependentPairArrays||pattern.Members.size()!=2||
            pattern.Members[0].PartIndex!=pattern.Members[1].PartIndex||B.Stop())return;
        const auto pi=pattern.Members[0].PartIndex;
        const auto available=P.Parts[pi].Quantity-state.Used[pi];
        if(available<3||pitch.X<=0||pitch.Y<=0)return;
        const auto begin=B.Attempts;
        const auto limit=std::min<std::size_t>(std::max<std::size_t>(1,S.MaxCandidates*16),
            (std::min(B.O.MaxPlacementAttempts,B.LocalAttemptLimit)-B.Attempts)/8);
        const auto full=[&](double extent,double offset,double size,double step)
        {return static_cast<std::size_t>(std::clamp(1+std::floor((extent-offset-size+P.Tolerance)/step),0.,static_cast<double>(S.MaxArrayAxis)));};
        std::array<Rect,2> boxes;
        std::array<std::size_t,2> columns{},rows{};
        for(std::size_t i=0;i<2;++i)
        {
            boxes[i]=Bounds(P.Parts[pi].Geometry,pattern.Members[i].Transform);
            columns[i]=std::min(available,full(region.Width,boxes[i].X,boxes[i].Width,pitch.X));
            rows[i]=std::min(available,full(region.Height,boxes[i].Y,boxes[i].Height,pitch.Y));
        }
        if(!columns[0]||!columns[1]||!rows[0]||!rows[1])return;
        // Each partner owns its rectangular row/column counts. Keeping only
        // full pair cells would incorrectly force an even original count.
        // Visit both partners as the primary group; all proposals still pass
        // the original-material and remaining-demand checks below.
        std::set<std::array<std::size_t,4>> seen;
        for(std::size_t primary=0;primary<2&&!B.Stop();++primary)
        for(std::size_t nx=columns[primary];nx&&!B.Stop()&&B.Attempts-begin<limit;--nx)
        {
            const auto other=1-primary;
            const auto maxRows=std::min(rows[primary],(available-1)/nx);
            const std::array<std::size_t,3> rowCounts{maxRows,maxRows?maxRows-1:0,1};
            for(const auto ny:rowCounts)
            {
                if(!ny||ny>rows[primary]||nx*ny>=available)continue;
                const auto remaining=available-nx*ny;
                for(std::size_t nxp=std::min(columns[other],remaining);nxp&&!B.Stop()&&B.Attempts-begin<limit;--nxp)
                {
                    const auto nyp=std::min(rows[other],remaining/nxp);if(!nyp)continue;
                    std::array<std::size_t,4> shape{};
                    shape[2*primary]=nx;shape[2*primary+1]=ny;shape[2*other]=nxp;shape[2*other+1]=nyp;
                    if(!seen.insert(shape).second)continue;
                    ++B.Attempts;
                    if(shape[0]==shape[2]&&shape[1]==shape[3])continue;
                    Candidate c;c.Part=pi;c.Template=ti;c.Columns=shape[0];c.Rows=shape[1];
                    std::optional<Rect> bounds;
                    for(std::size_t i=0;i<2;++i)
                    for(std::size_t y=0;y<shape[2*i+1];++y)
                    for(std::size_t x=0;x<shape[2*i];++x)
                    {
                        auto member=pattern.Members[i];member.Transform.X+=region.X+x*pitch.X;
                        member.Transform.Y+=region.Y+y*pitch.Y;c.Members.push_back(member);
                        const auto box=Bounds(P.Parts[pi].Geometry,member.Transform);bounds=bounds?Unite(*bounds,box):box;
                    }
                    c.Box=*bounds;c.Material=c.Members.size()*Areas[pi];
                    if(!Within(c.Box,region,P.Tolerance))continue;
                    if(propose)
                    {
                        if(propose(std::move(c)))++IndependentPairCandidates;
                        continue;
                    }
                    if(incumbent)
                    {
                        // The immutable board and final member poses already
                        // determine the complete Rank. A nonwinning proposal
                        // needs no material certificate and is not called legal.
                        ScoreUtilization(context,c);
                        if(*incumbent&&!(Rank(c)<Rank(**incumbent)))
                        {
                            Identify(c);
                            Observe(ConstructionTraceStage::Filter,[&](ConstructionTraceEvent& event)
                            {
                                event.Source="array.independent-pair";event.StateID=state.TraceID;
                                event.CandidateID=c.TraceID;event.Accepted=false;event.Reason="rank-dominated-unverified";
                                TraceTuple(event,Rank(c),{"negativeUsedSectionUtilization","negativeNetMaterial",
                                    "orientationOrOccupiedArea","intrinsicFirstMaterialCornerDistance","primaryAxis",
                                    "secondaryAxis","part","template","columns","rows"});
                                for(const auto& member:c.Members)event.Members.push_back(
                                    {member.PartIndex,std::numeric_limits<std::size_t>::max(),member.Transform,0});
                            });
                            continue;
                        }
                    }
                    if(!CheckCandidate(state,c,"array.independent-pair"))continue;
                    if(!incumbent)ScoreUtilization(context,c);
                    if(S.Kind==Kind::NfpMultiArray)c=WithTail(state,std::move(c),region,context);
                    if(!SourceEvaluation(c,pattern,region))continue;
                    if(incumbent)*incumbent=c;
                    ++IndependentPairCandidates;Keep(result,std::move(c));
                }
            }
        }
    }
    void SelectCandidates(std::vector<Candidate>& candidates,std::size_t limit)const
    {
        StageTime selectionTime(B,SelectionSeconds);
        if(candidates.empty())return;
        std::vector<Candidate> observed;
        if(B.O.ConstructionTrace)observed=candidates;
        const auto report=[&]
        {
            for(const auto& candidate:observed)
            {
                const bool kept=std::any_of(candidates.begin(),candidates.end(),[&](const Candidate& value)
                    {return value.TraceID==candidate.TraceID&&value.TraceProposal==candidate.TraceProposal;});
                Observe(ConstructionTraceStage::Filter,[&](ConstructionTraceEvent& event)
                {
                    event.CandidateID=candidate.TraceID;event.ProposalID=candidate.TraceProposal;
                    event.Source="array.frontier";event.Accepted=kept;event.Reason=kept?"retained":"candidate-cap-or-geometric-duplicate";
                    event.SearchTruncated=!kept;event.Measures.insert(event.Measures.end(),{{"before",static_cast<double>(observed.size())},
                        {"after",static_cast<double>(candidates.size())},{"limit",static_cast<double>(limit)}});
                    TraceTuple(event,Rank(candidate),{"negativeUsedSectionUtilization","negativeNetMaterial","orientationOrOccupiedArea","intrinsicFirstMaterialCornerDistance","primaryAxis","secondaryAxis","part","template","columns","rows"});
                });
            }
        };
        if(S.Kind!=Kind::ArrayMultiSpecificationSearch&&S.Kind!=Kind::FourBlockArray)
        {
            std::stable_sort(candidates.begin(),candidates.end(),[&](const auto& a,const auto& b){return Rank(a)<Rank(b);});
            if(candidates.size()>limit)candidates.resize(limit);
            report();
            return;
        }
        using Demand=std::vector<std::pair<std::size_t,std::size_t>>;
        using Geometry=std::vector<std::tuple<std::size_t,double,double,double,bool>>;
        using CandidateRank=decltype(Rank(candidates.front()));
        struct View
        {
            CandidateRank RankValue;
            Demand Needs;
            Geometry Members;
        };
        std::vector<View> views;views.reserve(candidates.size());
        for(const auto& candidate:candidates)
        {
            View view;view.RankValue=Rank(candidate);
            std::map<std::size_t,std::size_t> needs;
            for(const auto& member:candidate.Members)
            {
                ++needs[member.PartIndex];
                view.Members.emplace_back(member.PartIndex,member.Transform.X,member.Transform.Y,member.Transform.AngleDegrees,member.Transform.Mirrored);
            }
            view.Needs.assign(needs.begin(),needs.end());
            std::sort(view.Members.begin(),view.Members.end());
            views.push_back(std::move(view));
        }
        std::array<std::vector<std::size_t>,1> orders;
        for(auto& order:orders){order.resize(candidates.size());std::iota(order.begin(),order.end(),0);}
        std::stable_sort(orders[0].begin(),orders[0].end(),[&](auto a,auto b){return views[a].RankValue<views[b].RankValue;});
        // A demand vector alone is not geometric equivalence. Different poses
        // and occupied boxes must remain available for later branch choices.
        std::set<std::tuple<Geometry,double,double,double,double>> geometry;
        std::vector<bool> selected(candidates.size());
        for(const auto index:orders[0])
        {
            const auto box=candidates[index].Box;
            if(!geometry.emplace(views[index].Members,box.X,box.Y,box.Width,box.Height).second)selected[index]=true;
        }
        std::vector<std::size_t> kept;kept.reserve(std::min(limit,candidates.size()));
        std::set<Demand> represented;
        for(const bool distinctDemands:{true,false})
        {
            std::array<std::size_t,1> positions{};
            while(kept.size()<limit)
            {
                bool any=false;
                for(std::size_t order=0;order<orders.size()&&kept.size()<limit;++order)
                {
                    auto& position=positions[order];const auto& indices=orders[order];
                    while(position<indices.size())
                    {
                        const auto index=indices[position++];
                        if(selected[index]||(distinctDemands&&represented.contains(views[index].Needs)))continue;
                        selected[index]=true;represented.insert(views[index].Needs);kept.push_back(index);any=true;break;
                    }
                }
                if(!any)break;
            }
        }
        std::vector<Candidate> result;result.reserve(kept.size());
        for(const auto index:kept)result.push_back(std::move(candidates[index]));
        candidates=std::move(result);
        report();
    }
    void Keep(std::vector<Candidate>& candidates,Candidate c)
    {
        Identify(c);
        Observe(ConstructionTraceStage::Score,[&](ConstructionTraceEvent& event)
        {
            event.CandidateID=c.TraceID;event.ProposalID=c.TraceProposal;event.Source="array.rank";
            event.Reason="original-material-utilization";
            event.Width=c.Box.Width;event.Height=c.Box.Height;event.MaterialArea=c.Material;
            event.EnvelopeArea=c.OccupiedPlanningArea;
            event.Measures.insert(event.Measures.end(),{{"candidateNetMaterialArea",c.Material},
                {"occupiedNetMaterialArea",c.OccupiedMaterial},{"occupiedPlanningArea",c.OccupiedPlanningArea},
                {"usedSectionArea",c.UsedSectionArea},{"usedSectionUtilization",c.Utilization},{"localUtilization",c.Utilization}});
            TraceTuple(event,Rank(c),{"negativeUsedSectionUtilization","negativeNetMaterial","orientationOrOccupiedArea","intrinsicFirstMaterialCornerDistance","primaryAxis","secondaryAxis","part","template","columns","rows"});
            for(const auto& member:c.Members)event.Members.push_back({member.PartIndex,std::numeric_limits<std::size_t>::max(),member.Transform,0});
        });
        candidates.push_back(std::move(c));
        if(candidates.size()>2*S.MaxCandidates)SelectCandidates(candidates,S.MaxCandidates);
    }
    double TemplateInset(const Pattern& pattern,const State& state)const
    {
        bool rounded=false;double scale=1;
        for(const auto& member:pattern.Members)
        {
            const auto angle=NormalizeAngle(member.Transform.AngleDegrees);
            if(angle==0||angle==90||angle==180||angle==270)continue;
            rounded=true;
            // Match the inputs to AxisSafety::Make's roundoff bound. The
            // complete board extent covers every later grid-copy world pose;
            // the source scale also covers large cancelled local coordinates.
            scale=std::max({scale,SourceScales[member.PartIndex],
                std::abs(member.Transform.X)+2*state.Board.Width,
                std::abs(member.Transform.Y)+2*state.Board.Height});
        }
        if(!rounded)return 0;
        // One bound pays the nominal containment inset; the second covers
        // normalization/translation rounding at the carrier's outer boundary.
        // Original member geometry, nominal margin/gap and Fits stay unchanged.
        return std::nextafter(256*std::numeric_limits<double>::epsilon()*scale,
            std::numeric_limits<double>::infinity());
    }
    bool WithinMaterialDomain(Rect box,Rect domain)const
    {
        return Within(box,domain,P.Tolerance)&&box.Y+box.Height<=domain.Y+domain.Height;
    }
    bool BindMaterialDomain(Candidate& candidate,Rect domain)const
    {
        std::optional<Rect> actual;
        for(const auto& member:candidate.Members)
        {
            const auto box=Bounds(P.Parts[member.PartIndex].Geometry,member.Transform);
            if(!WithinMaterialDomain(box,domain))return false;
            actual=actual?Unite(*actual,box):box;
        }
        if(!actual)return false;
        candidate.Box=*actual;return true;
    }
    using ArrayTranslationShape=std::vector<std::tuple<std::size_t,double,double,double,bool>>;
    bool ArrayContourPositions(const State& state,const Candidate& carrier,const UtilizationContext& context,
        std::set<ArrayTranslationShape>& queried,const std::function<bool(Candidate)>& offer,
        const std::optional<Candidate>* incumbent=nullptr,bool leftSeed=false,
        const std::optional<Rect>& materialDomain={})
    {
        if(B.Stop()||state.Board.Placements.empty()||carrier.Members.empty())return false;
        // A translation query depends on the original members and their relative
        // poses, never on which MaxRects region happened to propose the carrier.
        ArrayTranslationShape shape;
        const auto anchor=carrier.Members.front().Transform;
        for(const auto& member:carrier.Members)shape.emplace_back(member.PartIndex,
            member.Transform.X-anchor.X,member.Transform.Y-anchor.Y,
            member.Transform.AngleDegrees,member.Transform.Mirrored);
        std::sort(shape.begin(),shape.end());
        if(!queried.insert(std::move(shape)).second)return false;
        if(!Generator(state).Ready())return false;
        const bool single=carrier.Members.size()==1;
        std::vector<PreparedPoseBounds> scoringBounds;scoringBounds.reserve(carrier.Members.size());
        for(const auto& member:carrier.Members)
            scoringBounds.emplace_back(P.Parts[member.PartIndex].Geometry,member.Transform);
        const auto movedTo=[&](Pose pose)
        {
            Candidate moved=carrier;moved.TraceID=0;moved.PlanningInset=0;
            std::optional<Rect> occupied;
            for(std::size_t i=0;i<moved.Members.size();++i)
            {
                auto& member=moved.Members[i];
                if(single)member.Transform=pose;
                else {member.Transform.X+=pose.X;member.Transform.Y+=pose.Y;}
                const auto box=scoringBounds[i].At(member.Transform);
                occupied=occupied?Unite(*occupied,box):box;
            }
            moved.Box=*occupied;return moved;
        };
        NfpPlacementLimits limits;limits.MaxCandidates=std::min<std::size_t>(8,S.MaxCandidates);
        limits.MaxAttempts=std::min(B.O.MaxPlacementAttempts,B.LocalAttemptLimit)-B.Attempts;
        limits.IndexedMaterialProof=true;
        limits.PrioritizeFreeDomainRetreat=true;
        if(materialDomain)
        {
            auto origin=single?carrier.Members.front().Transform:Pose{};
            origin.X=origin.Y=0;
            const auto local=movedTo(origin).Box;
            if(local.Width>materialDomain->Width+P.Tolerance||local.Height>materialDomain->Height+P.Tolerance)return false;
            const auto x=materialDomain->X-local.X,y=materialDomain->Y-local.Y;
            const auto right=materialDomain->X+materialDomain->Width-local.X-local.Width;
            const auto top=materialDomain->Y+materialDomain->Height-local.Y-local.Height;
            // Clip proposal events before their raw cap, while the actual
            // final-pose guards below still own the frozen material boundary.
            if(std::isfinite(x)&&std::isfinite(y)&&std::isfinite(right)&&std::isfinite(top)&&right>=x&&top>=y)
                limits.TranslationWindow=Rect{x,y,right-x,top-y};
            limits.NecessaryFit=[&](Pose pose,std::uint64_t)
            {return WithinMaterialDomain(movedTo(pose).Box,*materialDomain);};
        }
        if(leftSeed){limits.MaxCandidates=1;limits.StopAfterFirstFit=true;}
        if(single)limits.Mirrored=carrier.Members.front().Transform.Mirrored;
        limits.CandidatePriority=[&](Pose pose)
        {
            auto moved=movedTo(pose);ScoreUtilization(context,moved);
            if(leftSeed)return std::array<double,4>{moved.Box.X,-moved.Utilization,
                moved.OccupiedPlanningArea,moved.Box.Y};
            // Material, intrinsic corner and template identity are constant in
            // this nonempty-board query. These are all varying final Rank terms.
            return std::array<double,4>{-moved.Utilization,moved.OccupiedPlanningArea,
                moved.PrimaryAxis,moved.SecondaryAxis};
        };
        std::size_t rankScreenCalls=0,rankScreenRejected=0,rankCertifiedUpdates=0;
        std::optional<decltype(Rank(carrier))> certifiedRank;
        if(incumbent&&*incumbent)certifiedRank=Rank(**incumbent);
        if(incumbent)limits.CompetitivePose=[&](Pose pose,std::uint64_t)
        {
            ++rankScreenCalls;
            if(!certifiedRank)return true;
            auto moved=movedTo(pose);ScoreUtilization(context,moved);
            // This is the final stored-pose Rank used by offer below. A pose
            // which cannot replace the already certified greedy incumbent
            // needs no new material certificate. Keep the complete proposal
            // and retreat streams, every orientation and every rigid member;
            // search callers retain their full frontier by omitting this gate.
            if(Rank(moved)<*certifiedRank)return true;
            ++rankScreenRejected;return false;
        };
        if(incumbent)limits.AcceptFit=[&](Pose pose,std::uint64_t)
        {
            auto moved=movedTo(pose);ScoreUtilization(context,moved);
            if(certifiedRank&&!(Rank(moved)<*certifiedRank))return false;
            // A query-local improvement becomes a rejection threshold only
            // after the same complete caller-side original/inventory gate as
            // a real offer. Later raw events and actual retreat poses may
            // still beat it. Retain it in Fits for the usual final offer path.
            if(!Fits(state,moved.Members))return false;
            certifiedRank=Rank(moved);++rankCertifiedUpdates;return true;
        };
        const auto queryID=++NextQuery;limits.TraceHealth=B.O.ConstructionObservation;
        if(B.O.ConstructionTrace)limits.Trace=[&](const ConstructionTraceEvent& raw)
        {
            if(ActiveArrayQueryProfile&&raw.Stage==ConstructionTraceStage::Complete&&
                (raw.Source=="nfp.original-clearance"||raw.Source=="nfp.rigid-placement"))
                for(const auto& [name,value]:raw.Measures)
                {
                    const bool seconds=name.size()>=7&&name.compare(name.size()-7,7,"Seconds")==0;
                    const bool phase=name.rfind("phase",0)==0&&name!="phaseTimesNested";
                    // The singleton wrapper already aggregates its native
                    // ordinary/common phases. Do not count nfp.query again.
                    if((seconds||phase)&&std::isfinite(value)&&value>=0)
                        ActiveArrayQueryProfile->PhaseSums[raw.Source+"."+name]+=value;
                }
            Observe(raw.Stage,[&](ConstructionTraceEvent& event)
            {
                const auto step=event.Step;event=raw;event.Step=step;event.Route=B.O.ConstructionTraceRoute;
                event.Scope=TraceContext;event.StateID=state.TraceID;event.ProposalID=queryID;
                event.ElapsedSeconds=B.Seconds();
                if(single)for(auto& member:event.Members)member.PartIndex=carrier.Members.front().PartIndex;
            });
        };
        const auto started=B.Seconds();
        auto query=single?Generator(state).Find(state.Board,carrier.Members.front().PartIndex,
                carrier.Members.front().Transform.AngleDegrees,limits,[&]{return B.Stop();}):
            Generator(state).FindRigid(state.Board,carrier.Members,limits,[&]{return B.Stop();});
        B.Attempts+=std::min(query.Attempts,limits.MaxAttempts);
        const auto seconds=B.Seconds()-started;
        if(ActiveArrayQueryProfile)
        {
            const std::size_t kind=single?0:1;
            ++ActiveArrayQueryProfile->Queries[kind];
            ActiveArrayQueryProfile->Attempts[kind]+=query.Attempts;
            ActiveArrayQueryProfile->Generated[kind]+=query.Generated;
            ActiveArrayQueryProfile->Fits[kind]+=query.Fits.size();
            ActiveArrayQueryProfile->QuerySeconds[kind]+=seconds;
        }
        Observe(ConstructionTraceStage::Analysis,[&](ConstructionTraceEvent& event)
        {
            event.Source=single?"array.original-query-result":"array.rigid-query-result";
            event.StateID=state.TraceID;event.ProposalID=queryID;event.SearchTruncated=query.Truncated;
            event.Reason="original-contour-domain-independent-of-maxrects";
            event.Measures={{"supported",query.Supported?1.:0.},{"generated",double(query.Generated)},
                {"fits",double(query.Fits.size())},{"queryAttempts",double(query.Attempts)},
                {"members",double(carrier.Members.size())},{"placementQuerySeconds",seconds},
                {"rankScreenCalls",double(rankScreenCalls)},{"rankScreenRejected",double(rankScreenRejected)},
                {"rankCertifiedUpdates",double(rankCertifiedUpdates)}};
        });
        bool retained=false;
        for(const auto pose:query.Fits)
        {
            if(B.Stop())break;
            auto moved=movedTo(pose);moved.TraceProposal=queryID;
            moved.ConstructionSeconds+=seconds;++moved.PlacementQueries;
            moved.GeneratedPositions+=query.Generated;moved.VerifiedPositions+=query.Fits.size();
            retained=offer(std::move(moved))||retained;
        }
        return retained;
    }
    void FinishArrayQueryProfile(const State& state,const ArrayQueryProfile& profile,
        ArrayQueryProfile* previousProfile)
    {
        ActiveArrayQueryProfile=previousProfile;
        if(!B.O.ConstructionTrace)return;
        const auto wall=B.Seconds()-profile.Started;
        Observe(ConstructionTraceStage::Complete,[&](ConstructionTraceEvent& event)
        {
            event.Source="array.query-profile";event.StateID=state.TraceID;
            event.Reason="complete-array-call-totals-before-detail-sampling-nested-phases";
            std::size_t remaining=0;
            for(std::size_t i=0;i<P.Parts.size();++i)remaining+=P.Parts[i].Quantity-state.Used[i];
            event.Measures={{"arrayWallSeconds",wall},{"fixedOriginals",double(state.Board.Placements.size())},
                {"committedOriginals",double(state.Board.Placements.size()-state.ReservedOriginals)},
                {"reservedOriginals",double(state.ReservedOriginals)},
                {"remainingOriginals",double(remaining)},{"callerFitsCalls",double(profile.FitsCalls)},
                {"callerFitsSeconds",profile.FitsSeconds},{"phaseTimesNested",1.}};
            for(std::size_t kind=0;kind<2;++kind)
            {
                const std::string prefix=kind?"rigid":"single";
                event.Measures.insert(event.Measures.end(),{
                    {prefix+"Queries",double(profile.Queries[kind])},{prefix+"QuerySeconds",profile.QuerySeconds[kind]},
                    {prefix+"QueryAttempts",double(profile.Attempts[kind])},{prefix+"Generated",double(profile.Generated[kind])},
                    {prefix+"Fits",double(profile.Fits[kind])}});
            }
            for(const auto& measure:profile.PhaseSums)event.Measures.push_back(measure);
        });
    }
    enum class ArrayPhase{All,MainCombination,GapCombination,ContourCombination,MainOriginal};
    std::vector<Candidate> Arrays(const State& state,const std::string& material,
        const std::optional<std::size_t>& requestedSpecification={},
        const std::optional<Rect>& materialDomain={},ArrayPhase phase=ArrayPhase::All,
        std::vector<Candidate>* retainedPool=nullptr)
    {
        ArrayQueryProfile profile;
        auto* previousProfile=ActiveArrayQueryProfile;
        if(B.O.ConstructionTrace){profile.Started=B.Seconds();ActiveArrayQueryProfile=&profile;}
        struct ProfileScope
        {
            Impl& Owner;
            const State& BoardState;
            ArrayQueryProfile& Profile;
            ArrayQueryProfile* Previous;
            ~ProfileScope(){Owner.FinishArrayQueryProfile(BoardState,Profile,Previous);}
        } profileScope{*this,state,profile,previousProfile};
        const auto utilization=UtilizationBefore(state);
        StageTime arraysTime(B,ArraysSeconds);
        std::vector<Candidate> result;
        // Only these greedy callers consume the first stable-ranked result.
        // Search/random need their frontier, and WithTail may change Rank.
        std::optional<Candidate> greedyIncumbent;
        auto* incumbent=!Searching(S.Kind)&&S.Kind!=Kind::RandomArrays&&S.Kind!=Kind::NfpMultiArray?
            &greedyIncumbent:nullptr;
        const bool rectangleOnly=phase==ArrayPhase::MainCombination||phase==ArrayPhase::MainOriginal;
        const bool combinationOnly=phase==ArrayPhase::MainCombination||phase==ArrayPhase::GapCombination||phase==ArrayPhase::ContourCombination;
        const bool contourOnly=phase==ArrayPhase::GapCombination||phase==ArrayPhase::ContourCombination;
        const bool ordinaryContour=!rectangleOnly&&!StrictRectangles()&&!ContourArray(S.Kind)&&!state.Board.Placements.empty();
        std::set<ArrayTranslationShape> queriedTranslations;
        std::set<ArrayTranslationShape> queriedLeftSeeds;
        auto free=state.Free.FreeRectangles();
        if(ContourArray(S.Kind))
        {
            // This full-board domain sizes possible rigid grids only. Their
            // translation is solved below from member NFPs, independently of
            // the AABB partitions occupied by previously placed originals.
            free={{0,0,state.Board.Width-2*P.EdgeMargin+P.PartGap,
                state.Board.Height-2*P.EdgeMargin+P.PartGap}};
        }
        std::stable_sort(free.begin(),free.end(),[](Rect a,Rect b){return BoxArea(a)>BoxArea(b);});
        if(S.Kind==Kind::HistoricalFirstArray&&free.size()>1)free.resize(1);
        if(P.AllowCommonEdges&&P.PartGap>0)
        {
            // MaxRects keeps the usual padded obstacle domains. Also propose
            // origins inside that padding; only the unchanged original-material
            // gate can decide whether there is an actual shared straight edge.
            const auto padded=free;
            for(const auto& region:padded)
            {
                const double x=std::min(P.PartGap,std::max(0.,region.X));
                const double y=std::min(P.PartGap,std::max(0.,region.Y));
                if(x>0)free.push_back({region.X-x,region.Y,region.Width+x,region.Height});
                if(y>0)free.push_back({region.X,region.Y-y,region.Width,region.Height+y});
                if(x>0&&y>0)free.push_back({region.X-x,region.Y-y,region.Width+x,region.Height+y});
            }
        }
        struct ArrayDomain{Rect Space;bool Contour;};
        std::vector<ArrayDomain> domains;
        for(const auto space:free)domains.push_back({space,ContourArray(S.Kind)});
        if(ordinaryContour)
        {
            // Keep every existing rectangular proposal, then size additional
            // carriers on the physical board. Their true NFP translations may
            // overlap occupied AABBs; no free-rectangle fit is required here.
            domains.push_back({{0,0,state.Board.Width-2*P.EdgeMargin+P.PartGap,
                state.Board.Height-2*P.EdgeMargin+P.PartGap},true});
        }
        if(contourOnly)domains.erase(std::remove_if(domains.begin(),domains.end(),
            [](const auto& domain){return !domain.Contour;}),domains.end());
        if(materialDomain)
        {
            const Rect planning{materialDomain->X-P.EdgeMargin,materialDomain->Y-P.EdgeMargin,
                materialDomain->Width+P.PartGap,materialDomain->Height+P.PartGap};
            for(auto& domain:domains)
            {
                const auto old=domain.Space;
                const auto x=std::max(old.X,planning.X),y=std::max(old.Y,planning.Y);
                domain.Space={x,y,std::max(0.,std::min(old.X+old.Width,planning.X+planning.Width)-x),
                    std::max(0.,std::min(old.Y+old.Height,planning.Y+planning.Height)-y)};
            }
            domains.erase(std::remove_if(domains.begin(),domains.end(),[](const auto& domain)
                {return domain.Space.Width<=0||domain.Space.Height<=0;}),domains.end());
        }
        const bool firstSpecification=phase==ArrayPhase::All&&FirstSpecification(S.Kind)&&
            !(S.UseSourceArrayEvaluation&&S.Kind==Kind::ArrayGeometryImmediate);
        std::optional<std::size_t> baselineSpecification;
        std::vector<std::size_t> templateOrder(Templates.size());std::iota(templateOrder.begin(),templateOrder.end(),0);
        if(combinationOnly)std::stable_sort(templateOrder.begin(),templateOrder.end(),[&](auto a,auto b)
            {return Templates[a].MaterialArea>Templates[b].MaterialArea;});
        // Finish the established rectangular proposal pass before adding NFP
        // work. A new consideration must not prevent a later baseline pose
        // from being reached under the caller's finite construction budget.
        for(unsigned pass=0;pass<(ordinaryContour?2u:1u)&&!B.Stop();++pass)
        {
        for(std::size_t pi=0;pi<P.Parts.size()&&!B.Stop();++pi)
        {
            if(requestedSpecification&&pi!=*requestedSpecification)continue;
            if(state.Used[pi]>=P.Parts[pi].Quantity||P.Parts[pi].Material!=material||(StrictRectangles()&&!Rectangular[pi]))continue;
            if(S.Kind==Kind::SingleProductArray&&!state.Board.Placements.empty())continue;
            const bool bulkContour=ordinaryContour&&pass&&incumbent&&
                (contourOnly||P.Parts[pi].Quantity-state.Used[pi]>2);
            std::vector<Candidate> contourUnits;
            std::optional<double> largestFeasibleUnit;
            if(pass&&firstSpecification)
            {
                if(baselineSpecification&&pi>*baselineSpecification)break;
                // Earlier specifications may fit only by their contours. They
                // must establish their own legal candidate before competing
                // with the later specification's retained fallback.
                greedyIncumbent.reset();
                for(const auto& candidate:result)if(candidate.Part==pi&&
                    (!greedyIncumbent||Rank(candidate)<Rank(*greedyIncumbent)))greedyIncumbent=candidate;
            }
            for(const auto ti:templateOrder)
            {
                if(B.Stop())break;
                const auto& pattern=Templates[ti];
                if(combinationOnly&&pattern.Members.size()<2)continue;
                if(phase==ArrayPhase::MainOriginal&&pattern.Members.size()!=1)continue;
                // The generating member order is not an ownership rule. A
                // mixed template must compete before its earliest required
                // specification can be consumed by an unrelated single.
                if(pattern.Members.empty()||TemplateNeeds[ti].empty()||TemplateNeeds[ti].front().first!=pi)continue;
                if(combinationOnly&&largestFeasibleUnit&&pattern.MaterialArea<*largestFeasibleUnit)continue;
                if(S.Kind==Kind::ArrayFirstSpecificationSearch&&std::any_of(pattern.Members.begin(),pattern.Members.end(),[&](const Member& member){return member.PartIndex!=pi;}))continue;
                if(StrictRectangles()&&S.Kind!=Kind::FourBlockArray&&pattern.Members.size()!=1)continue;
                std::size_t available=std::numeric_limits<std::size_t>::max();
                for(const auto& [part,count]:TemplateNeeds[ti])
                {
                    if(P.Parts[part].Material!=material){available=0;break;}
                    available=std::min(available,(P.Parts[part].Quantity-state.Used[part])/count);
                }
                if(!available)continue;
                const std::vector<Point> boxPitch{{pattern.Width+P.PartGap,pattern.Height+P.PartGap}};
                auto pitches=ti<Pitches.size()?Pitches[ti]:boxPitch;
                if(P.PartGap>0)
                {
                    // The first empty-board array needs the same ordinary
                    // representable-clearance proposal as unit growth. Exact
                    // original gates can reject a rounded nominal .2 pitch.
                    const auto gap=P.PartGap+std::max(1e-7,4*P.Tolerance);
                    const Point safePitch{pattern.Width+gap,pattern.Height+gap};
                    if(std::none_of(pitches.begin(),pitches.end(),[&](Point old)
                        {return old.X==safePitch.X&&old.Y==safePitch.Y;}))pitches.push_back(safePitch);
                }
                if(P.AllowCommonEdges)
                {
                    const std::array<Point,3> contacts{{{pattern.Width,pattern.Height},
                        {pattern.Width,pattern.Height+P.PartGap},{pattern.Width+P.PartGap,pattern.Height}}};
                    for(auto it=contacts.rbegin();it!=contacts.rend();++it)
                        if(std::none_of(pitches.begin(),pitches.end(),[&](Point old)
                            {return std::abs(old.X-it->X)<=P.Tolerance&&std::abs(old.Y-it->Y)<=P.Tolerance;}))
                            pitches.insert(pitches.begin(),*it);
                }
                for(const auto& domain:domains)
                {
                    if(ordinaryContour&&!pass&&domain.Contour)continue;
                    if(bulkContour&&!domain.Contour)continue;
                    const auto space=domain.Space;
                    const double inset=domain.Contour&&!state.Board.Placements.empty()?0.:TemplateInset(pattern,state);
                    const auto offer=[&](Candidate placed,const char* source)
                    {
                        if(materialDomain&&!BindMaterialDomain(placed,*materialDomain))return false;
                        if(incumbent)
                        {
                            ScoreUtilization(utilization,placed);
                            const bool primaryDominated=*incumbent&&
                                std::tuple(-placed.Utilization,-placed.Material)>
                                std::tuple(-(**incumbent).Utilization,-(**incumbent).Material);
                            if(*incumbent&&(retainedPool?primaryDominated:!(Rank(placed)<Rank(**incumbent))))
                            {
                                Identify(placed);
                                Observe(ConstructionTraceStage::Filter,[&](ConstructionTraceEvent& event)
                                {
                                    event.Source=source;event.StateID=state.TraceID;event.CandidateID=placed.TraceID;
                                    event.ProposalID=placed.TraceProposal;event.Accepted=false;
                                    event.Reason=retainedPool?"primary-tier-dominated-unverified":"rank-dominated-unverified";
                                    TraceTuple(event,Rank(placed),{"negativeUsedSectionUtilization","negativeNetMaterial",
                                        "orientationOrOccupiedArea","intrinsicFirstMaterialCornerDistance","primaryAxis",
                                        "secondaryAxis","part","template","columns","rows"});
                                    for(const auto& member:placed.Members)event.Members.push_back(
                                        {member.PartIndex,std::numeric_limits<std::size_t>::max(),member.Transform,0});
                                });
                                return false;
                            }
                        }
                        if(!CheckCandidate(state,placed,source))return false;
                        if(!incumbent)ScoreUtilization(utilization,placed);
                        if(retainedPool)retainedPool->push_back(placed);
                        if(S.Kind==Kind::NfpMultiArray)placed=WithTail(state,std::move(placed),
                            {space.X+P.EdgeMargin,space.Y+P.EdgeMargin,space.Width-P.PartGap,space.Height-P.PartGap},utilization);
                        if(!SourceEvaluation(placed,pattern,
                            {space.X+P.EdgeMargin,space.Y+P.EdgeMargin,space.Width-P.PartGap,space.Height-P.PartGap}))return false;
                        if(combinationOnly)largestFeasibleUnit=pattern.MaterialArea;
                        if(incumbent&&(!*incumbent||Rank(placed)<Rank(**incumbent)))*incumbent=placed;
                        Keep(result,std::move(placed));return true;
                    };
                    const auto propose=[&](Candidate carrier,const char* source)
                    {
                        bool retained=false;
                        // A baseline's failure or low score says nothing about
                        // this carrier's still-unqueried contour translations.
                        if((!ordinaryContour||!pass)&&(!domain.Contour||state.Board.Placements.empty()))retained=offer(carrier,source);
                        if((ordinaryContour&&pass)||(!ordinaryContour&&domain.Contour))
                            retained=ArrayContourPositions(state,carrier,utilization,queriedTranslations,
                                [&](Candidate moved){return offer(std::move(moved),"array.contour-pose");},incumbent,false,materialDomain)||retained;
                        return retained;
                    };
                    if(bulkContour)
                    {
                        // Repeated units must not turn production quantities
                        // into hundreds of globally translated partial grids.
                        // Single originals and actual self-pairs keep their
                        // established small rigid path. Place the actual
                        // original/self-combination unit, then grow its legal
                        // periodic prefixes against the unchanged originals.
                        Candidate unit;unit.Part=pi;unit.Template=ti;unit.Columns=unit.Rows=1;unit.UnitMembers=pattern.Members.size();
                        unit.Material=pattern.MaterialArea;unit.RepeatPitches=pitches;
                        unit.Box={space.X+P.EdgeMargin,space.Y+P.EdgeMargin,pattern.Width,pattern.Height};
                        unit.Members=pattern.Members;
                        for(auto& member:unit.Members)
                        {member.Transform.X+=unit.Box.X;member.Transform.Y+=unit.Box.Y;}
                        // A good large grid can dominate the unit's score but
                        // not its future growth. Retain the certified seeds
                        // before comparing the unit itself with that baseline.
                        ArrayContourPositions(state,unit,utilization,queriedTranslations,
                            [&](Candidate moved)
                            {
                                if(materialDomain&&!BindMaterialDomain(moved,*materialDomain))return false;
                                ScoreUtilization(utilization,moved);
                                contourUnits.push_back(moved);return offer(std::move(moved),"array.contour-unit");
                            },nullptr,false,materialDomain);
                        // The lowest single-unit pose may sacrifice a whole
                        // future column to save a numerical sliver of height.
                        // Retain an independently certified left seed too;
                        // only the grown originals' complete Rank can choose
                        // which placement is actually useful for this batch.
                        ArrayContourPositions(state,unit,utilization,queriedLeftSeeds,
                            [&](Candidate moved)
                            {
                                if(materialDomain&&!BindMaterialDomain(moved,*materialDomain))return false;
                                ScoreUtilization(utilization,moved);
                                contourUnits.push_back(moved);return offer(std::move(moved),"array.contour-unit-left-seed");
                            },
                            nullptr,true,materialDomain);
                        continue;
                    }
                    std::set<std::tuple<std::size_t,std::size_t,double,double>> seenCopies;
                    for(std::size_t pitchIndex=0;pitchIndex<pitches.size()&&!B.Stop();++pitchIndex)
                    {
                    const bool independent=phase==ArrayPhase::All&&S.EnableIndependentPairArrays&&pattern.Members.size()==2&&
                        pattern.Members[0].PartIndex==pattern.Members[1].PartIndex&&P.Parts[pi].Quantity-state.Used[pi]>=3;
                    if(pitchIndex&&available<2&&!independent)break;
                    const double dx=pitches[pitchIndex].X,dy=pitches[pitchIndex].Y;
                    if(dx<=0||dy<=0)continue;
                    if(phase==ArrayPhase::All)IndependentPairs(state,pattern,ti,{dx,dy},
                        {space.X+P.EdgeMargin+inset,space.Y+P.EdgeMargin+inset,
                         space.Width-P.PartGap-2*inset,space.Height-P.PartGap-2*inset},result,utilization,incumbent,
                        [&](Candidate carrier){return propose(std::move(carrier),"array.independent-pair");});
                    auto nx=static_cast<std::size_t>(std::max(0.,1+std::floor((space.Width-P.PartGap-pattern.Width-2*inset+P.Tolerance)/dx)));
                    auto ny=static_cast<std::size_t>(std::max(0.,1+std::floor((space.Height-P.PartGap-pattern.Height-2*inset+P.Tolerance)/dy)));
                    nx=std::min({nx,available,S.MaxArrayAxis});ny=std::min({ny,available,S.MaxArrayAxis});
                    if(S.Kind==Kind::SingleColumn)
                    {
                        nx=std::min<std::size_t>(nx,1);
                        const auto continuation=space.Y+P.EdgeMargin-state.ColumnTop;
                        if(!state.Board.Placements.empty()&&(std::abs(pattern.Width-state.ColumnWidth)>P.Tolerance||std::abs(space.X)>P.Tolerance||
                            (std::abs(continuation-P.PartGap)>P.Tolerance&&(!P.AllowCommonEdges||std::abs(continuation)>P.Tolerance))))continue;
                    }
                    if(S.Kind==Kind::RectangleContact){nx=std::min<std::size_t>(nx,1);ny=std::min<std::size_t>(ny,1);}
                    if(!nx||!ny)continue;
                    for(std::size_t cols=1;cols<=nx&&!B.Stop();++cols)
                    {
                        const std::size_t rows=std::min(ny,(available+cols-1)/cols);
                        const auto capacity=std::min(available,cols*rows);
                        std::vector<std::pair<std::size_t,std::size_t>> counts{{capacity,cols}};
                        if(domain.Contour&&!state.Board.Placements.empty())
                        {
                            // A rigid service's work bound is not a reason to
                            // discard the remaining demand or fall all the way
                            // back to singletons. Try progressively smaller
                            // carriers, including short rows and one cell.
                            counts.clear();
                            const NfpPlacementLimits rigidLimits;
                            // Complete at least the mandatory ordinary stream.
                            // The optional common-edge stream may still retain
                            // an independently useful ordinary-domain result.
                            const auto memberLimit=std::min(rigidLimits.MaxRigidMembers,
                                rigidLimits.MaxRigidPairQueries/state.Board.Placements.size());
                            const auto limit=memberLimit/pattern.Members.size();
                            const auto copies=std::min(capacity,limit);
                            if(copies)
                            {
                                const auto columns=std::min(cols,copies);
                                counts.emplace_back(copies,columns);
                                for(auto fullRows=copies/columns;fullRows;fullRows/=2)
                                    counts.emplace_back(fullRows*columns,columns);
                                counts.emplace_back(1,1);
                            }
                        }
                        if(Searching(S.Kind))
                        {if(capacity>cols)counts.emplace_back(capacity-cols,cols);if(cols>1)counts.emplace_back(std::min(available,(cols-1)*rows),cols-1);}
                        for(auto [copies,columns]:counts)
                        {
                            if(ContourArray(S.Kind)&&copies==1&&pattern.Members.size()==1)continue;
                            Candidate c;c.Part=pi;c.Template=ti;c.Columns=columns;c.Rows=(copies+columns-1)/columns;c.Material=copies*pattern.MaterialArea;c.UnitMembers=pattern.Members.size();
                            c.PlanningInset=inset;
                            if(!seenCopies.emplace(copies,columns,std::min(copies,columns)>1?dx:0.,c.Rows>1?dy:0.).second)continue;
                            c.Box={space.X+P.EdgeMargin,space.Y+P.EdgeMargin,
                                (std::min(copies,columns)-1)*dx+pattern.Width+2*inset,(c.Rows-1)*dy+pattern.Height+2*inset};
                            for(std::size_t copy=0;copy<copies;++copy)for(auto m:pattern.Members)
                            {m.Transform.X+=c.Box.X+inset+(copy%columns)*dx;m.Transform.Y+=c.Box.Y+inset+(copy/columns)*dy;c.Members.push_back(m);}
                            propose(std::move(c),"array.grid");
                        }
                    }
                    }
                }
            }
            if(bulkContour&&!contourUnits.empty()&&!B.Stop())
            {
                const Candidate* floor=*incumbent?&**incumbent:nullptr;
                auto grown=GrowCarriers(state,contourUnits,false,floor,materialDomain);
                Observe(ConstructionTraceStage::Analysis,[&](ConstructionTraceEvent& event)
                {
                    event.Source="array.seed-origin";event.StateID=state.TraceID;event.PartIndex=pi;
                    event.Reason="ordinary-array-bulk-contour-unit-growth";
                    event.Measures={{"remainingOriginals",double(P.Parts[pi].Quantity-state.Used[pi])},
                        {"rigidMemberBound",double(NfpPlacementLimits{}.MaxRigidMembers)},
                        {"certifiedUnitSeeds",double(contourUnits.size())},{"grownCandidates",double(grown.size())}};
                });
                for(auto& candidate:grown)
                {
                    if(materialDomain&&!BindMaterialDomain(candidate,*materialDomain))continue;
                    if(materialDomain)ScoreUtilization(utilization,candidate);
                    if(retainedPool)retainedPool->push_back(candidate);
                    if(*incumbent&&!(Rank(candidate)<Rank(**incumbent)))continue;
                    if(!SourceEvaluation(candidate,Templates[candidate.Template],
                        {P.EdgeMargin,P.EdgeMargin,state.Board.Width-2*P.EdgeMargin,state.Board.Height-2*P.EdgeMargin}))continue;
                    // GrowCarriers certifies every actual original against the
                    // same board and the complete accepted new-unit prefix.
                    *incumbent=candidate;Keep(result,std::move(candidate));
                }
                if(materialDomain)
                    if(auto batch=MergeCarriers(state,result,contourUnits,false,phase!=ArrayPhase::All))
                        if(BindMaterialDomain(*batch,*materialDomain))
                        {
                            ScoreUtilization(utilization,*batch);
                            if(!*incumbent||Rank(*batch)<Rank(**incumbent))
                            {*incumbent=*batch;Keep(result,std::move(*batch));}
                        }
            }
            if(firstSpecification&&std::any_of(result.begin(),result.end(),[&](const auto& candidate){return candidate.Part==pi;}))
            {
                if(!pass)baselineSpecification=pi;
                else result.erase(std::remove_if(result.begin(),result.end(),[&](const auto& candidate){return candidate.Part!=pi;}),result.end());
                Observe(ConstructionTraceStage::Filter,[&](ConstructionTraceEvent& event){event.Source="array.first-specification";event.PartIndex=pi;
                    event.Reason="later-specifications-not-enumerated-after-first-feasible";});break;
            }
        }
        }
        SelectCandidates(result,S.MaxCandidates);
        return result;
    }
    std::vector<Candidate> HoleChoices(const State& state,const std::string& material,const std::optional<Rect>& domain={},
        const std::optional<std::size_t>& specification={})
    {
        std::vector<Candidate> result;
        if(!S.EnableHoleNesting||!HasHoleGeometry||B.Stop()||state.Board.Placements.empty()||
            std::none_of(state.Board.Placements.begin(),state.Board.Placements.end(),[&](const Placement& placed)
                {return !P.Parts[Index.at(placed.PartID)].Geometry.Holes.empty();}))return result;
        const auto utilization=UtilizationBefore(state);
        const auto begin=B.Attempts;
        const auto cap=std::max<std::size_t>(1,(std::min(B.O.MaxPlacementAttempts,B.LocalAttemptLimit)-B.Attempts)/8);
        for(std::size_t pi=0;pi<P.Parts.size()&&!B.Stop()&&B.Attempts-begin<cap;++pi)
        {
            if(specification&&pi!=*specification)continue;
            if(P.Parts[pi].Material!=material||state.Used[pi]>=P.Parts[pi].Quantity)continue;
            for(const auto orientation:Orientations[pi])
            {
                const auto angle=orientation.AngleDegrees;
                if(B.Stop()||B.Attempts-begin>=cap)break;
                NfpPlacementLimits limits;limits.MaxCandidates=std::min<std::size_t>(8,S.MaxCandidates);
                limits.Mirrored=orientation.Mirrored;
                limits.MaxAttempts=cap-(B.Attempts-begin);
                limits.MaxRawCandidates=std::min<std::size_t>(4096,std::max<std::size_t>(32,limits.MaxAttempts));
                std::uint64_t queryID=0;
                if(B.O.ConstructionTrace)
                {
                    queryID=++NextQuery;limits.TraceHealth=B.O.ConstructionObservation;
                    limits.Trace=[&,queryID,pi](const ConstructionTraceEvent& raw)
                    {
                        Observe(raw.Stage,[&](ConstructionTraceEvent& event)
                        {const auto step=event.Step;event=raw;event.Route=B.O.ConstructionTraceRoute;event.Scope=TraceContext;
                            event.Step=step;event.ProposalID=queryID;event.StateID=state.TraceID;event.PartIndex=pi;
                            event.ElapsedSeconds=B.Seconds();for(auto& member:event.Members)member.PartIndex=pi;});
                    };
                }
                auto found=FindPlaneHolePlacements(P,state.Board,pi,angle,limits,[&]{return B.Stop();});
                B.Attempts+=std::min(found.Attempts,limits.MaxAttempts);
                Observe(ConstructionTraceStage::Analysis,[&](ConstructionTraceEvent& event)
                {event.Source="hole.query-result";event.ProposalID=queryID;event.PartIndex=pi;event.SearchTruncated=found.Truncated;
                    event.Measures.insert(event.Measures.end(),{{"generated",static_cast<double>(found.Generated)},{"fits",static_cast<double>(found.Fits.size())},
                        {"queryAttempts",static_cast<double>(found.Attempts)},{"supported",found.Supported?1.:0.},{"angle",angle}});});
                for(std::size_t poseIndex=0;poseIndex<found.Fits.size();++poseIndex)
                {
                    const auto pose=found.Fits[poseIndex];
                    Candidate c;c.Part=pi;c.Members={{pi,pose}};c.Material=Areas[pi];c.Box=Bounds(P.Parts[pi].Geometry,pose);
                    c.TraceProposal=queryID;if(poseIndex<found.FitsIDs.size())c.TraceID=found.FitsIDs[poseIndex];else Identify(c);
                    if(domain&&!Within(c.Box,*domain,P.Tolerance))continue;
                    if(!CheckCandidate(state,c,"hole.original-domain"))continue;
                    ScoreUtilization(utilization,c);
                    ++HoleCandidates;result.push_back(std::move(c));
                }
            }
        }
        std::stable_sort(result.begin(),result.end(),[&](const Candidate& a,const Candidate& b){return Rank(a)<Rank(b);});
        if(result.size()>S.MaxCandidates)result.resize(S.MaxCandidates);
        return result;
    }
    void FillHoles(State& state,const std::string& material)
    {
        if(!S.EnableHoleNesting||!HasHoleGeometry||StrictRectangles())return;
        const auto begin=B.Attempts;
        const auto cap=std::max<std::size_t>(1,(std::min(B.O.MaxPlacementAttempts,B.LocalAttemptLimit)-B.Attempts)/8);
        const auto previous=B.LocalAttemptLimit;
        struct RestoreHoleLimit{Budget& Work;std::size_t Previous;~RestoreHoleLimit(){Work.LocalAttemptLimit=Previous;}}restore{B,previous};
        B.LocalAttemptLimit=std::min(previous,begin+cap);
        while(!B.Stop()&&B.Attempts-begin<cap)
        {
            auto choices=HoleChoices(state,material);if(choices.empty())break;
            Commit(state,choices.front(),true);
        }
    }
    std::optional<Candidate> PlaceOne(const State& state,const std::string& material,bool forceNfp,const std::optional<Rect>& domain={},std::vector<Candidate>* alternatives=nullptr,std::vector<Candidate>* carriers=nullptr,bool collectAllOriginals=false,
        const std::optional<std::size_t>& specification={})
    {
        const auto constructionStarted=B.Seconds();
        std::size_t placementQueries=0,generatedPositions=0,verifiedPositions=0;
        const bool lowNfp=(S.Kind==Kind::NfpAllSpecifications&&NfpTrial==0)||ContourArray(S.Kind);
        const bool orderedNfp=lowNfp&&!alternatives&&!domain&&!collectAllOriginals;
        const auto utilization=UtilizationBefore(state);
        const auto decisionDeadline=std::min(B.O.TimeLimitSeconds,B.LocalTimeLimit);
        const bool contactPlacement=S.Kind==Kind::NfpClosest||S.Kind==Kind::NfpDirectional;
        const auto scoreContact=[&](Candidate& candidate)
        {
            if(state.Board.Placements.empty()){candidate.ContactRank=0;return;}
            if(!contactPlacement)return;
            std::vector<std::pair<std::string,double>> measures;
            const auto region=domain.value_or(Rect{P.EdgeMargin,P.EdgeMargin,
                state.Board.Width-2*P.EdgeMargin,state.Board.Height-2*P.EdgeMargin});
            const auto contact=Contact(state,candidate.Part,candidate.Members.front().Transform,region,
                S.Kind==Kind::NfpDirectional,B.O.ConstructionTrace?&measures:nullptr);
            // An unavailable optional heuristic cannot exclude a certified
            // candidate or outrank a better primary utilization/material tier.
            // At equal primary quality prefer complete finite scoring; two
            // unavailable scores fall through to the stable geometry order.
            candidate.ContactRank=contact.value_or(std::numeric_limits<double>::max());
            Observe(ConstructionTraceStage::Score,[&](ConstructionTraceEvent& event)
            {
                event.Source="placement.contact";event.StateID=state.TraceID;event.CandidateID=candidate.TraceID;
                event.ProposalID=candidate.TraceProposal;event.PartIndex=candidate.Part;event.Accepted=contact.has_value();
                event.Reason=contact?"actual-certified-pose-contact-tie":"contact-unavailable-primary-and-safety-retained";
                event.Measures.insert(event.Measures.end(),measures.begin(),measures.end());
                event.Measures.insert(event.Measures.end(),{{"strategyContact",candidate.ContactRank},
                    {"contactScoringAvailable",contact?1.:0.},{"usedSectionUtilization",candidate.Utilization},
                    {"candidateNetMaterialArea",candidate.Material}});
                for(const auto& member:candidate.Members)event.Members.push_back(
                    {member.PartIndex,std::numeric_limits<std::size_t>::max(),member.Transform,0});
            });
        };
        // Empty-board shape ties use one translation-free assessment. Tiny
        // F64 differences in world AABB subtraction cannot outrank low/left.
        // Actual utilization and occupied area still use final world poses.
        const auto placementKey=[&](const Candidate& candidate)
        {
            return std::tuple(-candidate.Utilization,-candidate.Material,candidate.ContactRank,candidate.TieArea,candidate.TieCorner,
                candidate.PrimaryAxis,candidate.SecondaryAxis,
                candidate.Part,candidate.Members.front().Transform.AngleDegrees,candidate.Members.front().Transform.Mirrored);
        };
        auto holes=HoleChoices(state,material,domain,specification);
        std::optional<Candidate> best;
        auto bestKey=std::tuple(std::numeric_limits<double>::infinity(),0.,0.,0.,0.,0.,0.,std::size_t{},0.,false);
        std::vector<std::pair<decltype(bestKey),Candidate>> choices;
        for(auto& hole:holes)
        {
            if(domain&&!BindMaterialDomain(hole,*domain))continue;
            scoreContact(hole);
            const auto key=placementKey(hole);
            if(carriers)carriers->push_back(hole);
            if(alternatives)choices.emplace_back(key,hole);
            if(!best||key<bestKey){bestKey=key;best=std::move(hole);}
        }
        auto* nfp=forceNfp?&Generator(state):nullptr;
        if(nfp&&!nfp->Ready())
        {if(S.EnableHoleNesting&&HasHoleGeometry)nfp=nullptr;else return best;}
        std::vector<std::size_t> specifications(P.Parts.size());std::iota(specifications.begin(),specifications.end(),0);
        for(const auto pi:specifications)
        {
            if(specification&&pi!=*specification)continue;
            if(B.Stop())break;
            if(state.Used[pi]>=P.Parts[pi].Quantity||P.Parts[pi].Material!=material||(StrictRectangles()&&!Rectangular[pi]))continue;
            auto orientations=Orientations[pi];
            if(nfp&&orderedNfp)
                std::stable_sort(orientations.begin(),orientations.end(),[&](Pose a,Pose b)
                {
                    const auto aa=Bounds(P.Parts[pi].Geometry,a),bb=Bounds(P.Parts[pi].Geometry,b);
                    return std::tuple(aa.Height,aa.Width)<std::tuple(bb.Height,bb.Width);
                });
            const bool boundaryPass=nfp&&ContourArray(S.Kind)&&P.PartGap>0;
            for(std::size_t pass=0;pass<(boundaryPass?2u:1u);++pass)
            for(std::size_t orientationIndex=0;orientationIndex<orientations.size();++orientationIndex)
            {
                const auto orientation=orientations[orientationIndex];
                const bool basic=boundaryPass&&pass==0;
                const auto angle=orientation.AngleDegrees;
                if(B.Stop())break;
                if(ContourArray(S.Kind)&&B.Seconds()>=decisionDeadline)
                {
                    Observe(ConstructionTraceStage::Budget,[&](ConstructionTraceEvent& event)
                    {event.Source="placement.decision-budget";event.StateID=state.TraceID;event.PartIndex=pi;
                        event.SearchTruncated=true;event.Reason="constructor-global-budget-ended";
                        event.Measures={{"pass",double(pass)},{"remainingOrientations",double(orientations.size()-orientationIndex)},
                            {"decisionTimeSeconds",decisionDeadline-constructionStarted}};});
                    break;
                }
                const auto box=Bounds(P.Parts[pi].Geometry,orientation);
                if(domain&&(box.Width>domain->Width+P.Tolerance||box.Height>domain->Height+P.Tolerance))continue;
                std::vector<Pose> poses;
                std::vector<std::uint64_t> poseIDs;std::uint64_t queryID=0;
                if(nfp)
                {
                    const auto queryStarted=B.Seconds();
                    const auto queryDeadline=decisionDeadline;
                    NfpPlacementLimits limits;limits.MaxCandidates=orderedNfp?std::min<std::size_t>(8,S.MaxCandidates):S.MaxCandidates;
                    const bool greedyPool=specification.has_value()&&domain.has_value();
                    if(orderedNfp)limits.StopAfterCandidateLimit=true;
                    if(greedyPool)
                    {
                        limits.MaxCandidates=std::min<std::size_t>(8,S.MaxCandidates);
                        limits.StopAfterCandidateLimit=true;
                        limits.IndexedMaterialProof=true;
                        limits.PrioritizeFreeDomainRetreat=true;
                    }
                    limits.Mirrored=orientation.Mirrored;
                    limits.MaxAttempts=std::min(B.O.MaxPlacementAttempts,B.LocalAttemptLimit)-B.Attempts;
                    // Establish ordinary contour candidates for all poses
                    // before optional full channels/common-edge expansion.
                    // Both stages share the constructor's global protection
                    // budget and compete by the same actual utilization.
                    limits.BoundaryVerticesOnly=basic;
                    limits.IncludeCommonEdgeProposals=!basic;
                    limits.PrioritizeFreeDomainRetreat=greedyPool||S.Kind==Kind::NfpStrip||((IsNfp(S.Kind)||ContourArray(S.Kind))&&!domain);
                    if(domain)
                    {
                        const auto local=Bounds(P.Parts[pi].Geometry,{0,0,angle,orientation.Mirrored});
                        const auto x=domain->X-local.X,y=domain->Y-local.Y;
                        const auto right=domain->X+domain->Width-local.X-local.Width;
                        const auto top=domain->Y+domain->Height-local.Y-local.Height;
                        if(std::isfinite(x)&&std::isfinite(y)&&std::isfinite(right)&&std::isfinite(top)&&right>=x&&top>=y)
                            limits.TranslationWindow=Rect{x,y,right-x,top-y};
                        const auto insideDomain=[&,pi](Pose pose,std::uint64_t)
                        {
                            Candidate candidate;candidate.Part=pi;candidate.Members={{pi,pose}};
                            return BindMaterialDomain(candidate,*domain);
                        };
                        limits.NecessaryFit=insideDomain;
                        // The wrapper's direct/common proposals also pass this
                        // final original-safe gate before entering their fit cap.
                        limits.AcceptFit=insideDomain;
                    }
                    if(orderedNfp||greedyPool)
                    {
                        // Use final stored-coordinate bounds, matching the
                        // caller's actual utilization comparison. A source
                        // AABB plus translation is not exact at arbitrary angles.
                        limits.CandidatePriority=[&,pi](Pose pose)
                        {
                            const auto placed=Bounds(P.Parts[pi].Geometry,pose);
                            const auto combined=utilization.Occupied?Unite(*utilization.Occupied,placed):placed;
                            const auto area=BoxArea(combined);
                            const auto usedArea=UsedSectionArea(utilization,combined);
                            const auto ties=SinglePlacementTies(utilization,pi,pose,area);
                            return std::array<double,4>{-(utilization.Material+Areas[pi])/usedArea,-Areas[pi],ties.first,
                                utilization.Occupied?(S.XFirst?placed.X:placed.Y):ties.second};
                        };
                    }
                    const auto traceQuery=[&]
                    {
                        if(!B.O.ConstructionTrace)return;
                        queryID=++NextQuery;
                        limits.TraceHealth=B.O.ConstructionObservation;
                        limits.Trace=[&,queryID,pi](const ConstructionTraceEvent& raw)
                        {
                            Observe(raw.Stage,[&](ConstructionTraceEvent& event)
                            {const auto step=event.Step;event=raw;event.Route=B.O.ConstructionTraceRoute;event.Scope=TraceContext;
                                event.Step=step;event.ProposalID=queryID;event.StateID=state.TraceID;event.PartIndex=pi;event.ElapsedSeconds=B.Seconds();
                                for(auto& member:event.Members)member.PartIndex=pi;});
                        };
                    };
                    traceQuery();
                    auto query=nfp->Find(state.Board,pi,angle,limits,[&]{return B.Stop()||B.Seconds()>=queryDeadline;});B.Attempts+=query.Attempts;
                    ++placementQueries;generatedPositions+=query.Generated;verifiedPositions+=query.Fits.size();
                    poses=std::move(query.Fits);poseIDs=std::move(query.FitsIDs);
                    Observe(ConstructionTraceStage::Analysis,[&](ConstructionTraceEvent& event)
                    {event.Source="nfp.query-result";event.ProposalID=queryID;event.StateID=state.TraceID;event.PartIndex=pi;event.SearchTruncated=query.Truncated;
                        event.Measures.insert(event.Measures.end(),{{"generated",static_cast<double>(query.Generated)},{"fits",static_cast<double>(poses.size())},
                            {"queryAttempts",static_cast<double>(query.Attempts)},{"supported",query.Supported?1.:0.},{"domainMeasured",query.DomainMeasured?1.:0.},
                            {"freeTranslationArea",query.FreeTranslationArea},{"angle",angle},{"placementQuerySeconds",B.Seconds()-queryStarted},
                            {"queryWindowSeconds",queryDeadline-queryStarted},{"constructorTimeExhausted",B.Seconds()>=queryDeadline?1.:0.},
                            {"ordinaryBoundaryPass",basic?1.:0.}});});
                }
                else
                {
                    const Rect d=domain.value_or(Rect{P.EdgeMargin,P.EdgeMargin,state.Board.Width-2*P.EdgeMargin,state.Board.Height-2*P.EdgeMargin});
                    // Integer grid counters avoid stalled += step loops at large coordinates.
                    const auto emit=[&](double x,double y){poses.push_back({x-box.X,y-box.Y,angle,orientation.Mirrored});};
                    if(S.Kind==Kind::Scanline)
                    {
                        const double xmax=d.X+d.Width-box.Width,ymax=d.Y+d.Height-box.Height;
                        for(std::size_t row=0;!B.Stop();++row)
                        {
                            const auto outer=(S.XFirst?d.X:d.Y)+row*S.ScanStep;if(outer>(S.XFirst?xmax:ymax)+P.Tolerance)break;
                            for(std::size_t col=0;!B.Stop();++col)
                            {
                                const auto inner=(S.XFirst?d.Y:d.X)+col*S.ScanStep;if(inner>(S.XFirst?ymax:xmax)+P.Tolerance)break;
                                const double x=S.XFirst?outer:inner,y=S.XFirst?inner:outer;
                                Candidate c;c.Part=pi;c.Members={{pi,{x-box.X,y-box.Y,angle,orientation.Mirrored}}};c.Box={x,y,box.Width,box.Height};c.Material=Areas[pi];
                                if(domain&&!BindMaterialDomain(c,*domain))continue;
                                if(CheckCandidate(state,c,"scanline.grid"))
                                {
                                    ScoreUtilization(utilization,c);
                                    if(!collectAllOriginals)return !best||placementKey(c)<bestKey?std::optional<Candidate>(std::move(c)):best;
                                    const auto key=placementKey(c);
                                    if(carriers)carriers->push_back(c);
                                    if(alternatives)
                                    {
                                        choices.emplace_back(key,c);
                                        if(choices.size()>2*S.MaxCandidates)
                                        {std::stable_sort(choices.begin(),choices.end(),[](const auto& a,const auto& b){return a.first<b.first;});choices.resize(S.MaxCandidates);}
                                    }
                                    if(!best||key<bestKey){bestKey=key;best=std::move(c);}
                                }
                            }
                        }
                    }
                    else
                    {
                        emit(d.X,d.Y);
                        for(const auto& old:state.Board.Placements)
                        {
                            auto b=Bounds(P.Parts[Index.at(old.PartID)].Geometry,old.Transform);
                            emit(b.X+b.Width+P.PartGap,b.Y);emit(b.X,b.Y+b.Height+P.PartGap);
                            for(auto v:P.Parts[Index.at(old.PartID)].Geometry.Outer)
                            {auto q=Transform(v,old.Transform);emit(q.X+P.PartGap,q.Y+P.PartGap);}
                        }
                    }
                }
                for(std::size_t poseIndex=0;poseIndex<poses.size();++poseIndex)
                {
                    if(B.Stop())
                    {Observe(ConstructionTraceStage::Budget,[&](ConstructionTraceEvent& event){event.Source="placement.score";event.ProposalID=queryID;
                        event.SearchTruncated=true;event.Reason="verified-poses-not-scored-budget";event.Measures.emplace_back("unvisited",static_cast<double>(poses.size()-poseIndex));});break;}
                    const auto pose=poses[poseIndex];
                    Candidate c;c.Part=pi;c.Members={{pi,pose}};c.Box=Bounds(P.Parts[pi].Geometry,pose);c.Material=Areas[pi];
                    c.TraceProposal=queryID;if(poseIndex<poseIDs.size())c.TraceID=poseIDs[poseIndex];else Identify(c);
                    if(domain&&!BindMaterialDomain(c,*domain))
                    {ObserveCandidate(ConstructionTraceStage::Filter,state,c,"placement.domain","outside-requested-domain",false);continue;}
                    if(nfp&&orderedNfp)
                    {
                        // Find used this exact immutable board, original and
                        // final stored pose. Demand/angle/domain were checked
                        // above; its full original AxisSafety gate is reusable.
                        ObserveCandidate(ConstructionTraceStage::Filter,state,c,"placement.pose",
                            "same-state-stored-pose-original-gate-reused",true);
                    }
                    else if(!CheckCandidate(state,c,"placement.pose"))continue;
                    ScoreUtilization(utilization,c);
                    scoreContact(c);
                    const auto key=placementKey(c);
                    Observe(ConstructionTraceStage::Score,[&](ConstructionTraceEvent& event)
                    {
                        event.CandidateID=c.TraceID;event.ProposalID=c.TraceProposal;event.StateID=state.TraceID;event.Source="placement.rank";
                        event.PartIndex=pi;event.Reason=(!best||key<bestKey)?"new-best":"not-better-than-incumbent";
                        event.Accepted=!best||key<bestKey;
                        event.Measures.insert(event.Measures.end(),{{"localUtilization",c.Utilization},
                            {"candidateNetMaterialArea",c.Material},{"occupiedNetMaterialArea",c.OccupiedMaterial},
                            {"occupiedPlanningArea",c.OccupiedPlanningArea},{"usedSectionArea",c.UsedSectionArea},
                            {"usedSectionUtilization",c.Utilization}});
                        event.Measures.emplace_back("previousBestCandidate",best?static_cast<double>(best->TraceID):0.);
                        event.Members={{pi,std::numeric_limits<std::size_t>::max(),pose,0}};
                        TraceTuple(event,key,{"negativeUsedSectionUtilization","negativeNetMaterial","strategyContact",
                            utilization.Occupied?"occupiedPlanningArea":"intrinsicOrientationEnvelopeArea",
                            "intrinsicFirstMaterialCornerDistance","primaryAxis","secondaryAxis","part","angle","mirrored"});
                    });
                    // This certified pool is independent of the branch-width
                    // and near-neighbour pruning used by search alternatives.
                    if(carriers)carriers->push_back(c);
                    if(alternatives)
                    {
                        choices.emplace_back(key,c);
                        if(choices.size()>2*S.MaxCandidates)
                        {std::stable_sort(choices.begin(),choices.end(),[](const auto& a,const auto& b){return a.first<b.first;});choices.resize(S.MaxCandidates);}
                    }
                    if(!best||key<bestKey){bestKey=key;best=std::move(c);}
                }
            }
            if(!collectAllOriginals&&best&&S.Kind!=Kind::NfpAllSpecifications&&S.Kind!=Kind::NfpMultiArray)break;
            if(ContourArray(S.Kind)&&B.Seconds()>=decisionDeadline)break;
        }
        const auto annotateCost=[&](Candidate& candidate)
        {
            candidate.ConstructionSeconds=B.Seconds()-constructionStarted;
            candidate.PlacementQueries=placementQueries;candidate.GeneratedPositions=generatedPositions;
            candidate.VerifiedPositions=verifiedPositions;
        };
        if(alternatives)
        {
            std::stable_sort(choices.begin(),choices.end(),[](const auto& a,const auto& b){return a.first<b.first;});
            for(auto& choice:choices)
            {
                const auto& proposed=choice.second.Members[0];std::size_t samePart=0;bool duplicate=false;
                for(const auto& kept:*alternatives)if(kept.Part==choice.second.Part)
                {
                    ++samePart;const auto& old=kept.Members[0].Transform;
                    duplicate|=old.Mirrored==proposed.Transform.Mirrored&&
                        std::abs(old.AngleDegrees-proposed.Transform.AngleDegrees)<=P.Tolerance&&
                        std::hypot(old.X-proposed.Transform.X,old.Y-proposed.Transform.Y)<=std::max(1e-5,20*P.Tolerance);
                }
                if(duplicate||samePart>=std::max<std::size_t>(2,S.BranchWidth/2))continue;
                annotateCost(choice.second);
                alternatives->push_back(std::move(choice.second));if(alternatives->size()>=S.BranchWidth)break;
            }
        }
        if(best)
        {
            annotateCost(*best);
            ObserveCandidate(ConstructionTraceStage::Selected,state,*best,"placement.winner","minimum-recorded-lexicographic-key");
        }
        return best;
    }
    std::vector<Candidate> LocalPairChoices(const State& state,const std::string& material,
        const std::optional<std::size_t>& specification)
    {
        std::vector<Candidate> result;
        if(state.Board.Placements.empty()||B.Stop()||!Generator(state).Ready())return result;
        if(!LocalPairTemplatesReady)
        {
            std::set<std::string> seen;
            for(std::size_t ti=0;ti<Templates.size()&&!B.Stop();++ti)
            {
                const auto& source=Templates[ti];
                if(source.Members.size()<2||source.Members.size()>4||!HasSinglePartType(source.Members))continue;
                // Carry a prepared pair as a small rigid candidate. Equivalent
                // local fill does not make quarter-turns/reflections redundant
                // against the contours of the current, nonempty board.
                for(const bool mirrored:{false,true})for(const double angle:{0.,90.,180.,270.})
                {
                    auto pair=source;bool legal=true;
                    for(auto& member:pair.Members)
                    {
                        member.Transform=ComposePose({0,0,angle,mirrored},member.Transform);
                        legal&=PoseAllowed(P.Parts[member.PartIndex],member.Transform,P.Tolerance)&&
                            (B.O.OrientationProposals!=OrientationProposalPolicy::OriginalOnly||!member.Transform.Mirrored);
                    }
                    if(!legal)continue;
                    NormalizePattern(pair,P);
                    std::vector<std::string> members;
                    for(const auto& member:pair.Members)
                    {
                        std::ostringstream key;key.precision(std::numeric_limits<double>::max_digits10);
                        key<<member.PartIndex<<':'<<member.Transform.X<<':'<<member.Transform.Y<<':'
                            <<NormalizeAngle(member.Transform.AngleDegrees)<<':'<<member.Transform.Mirrored;
                        members.push_back(key.str());
                    }
                    std::sort(members.begin(),members.end());
                    std::string signature;
                    for(const auto& member:members){signature+=member;signature+=';';}
                    if(seen.insert(signature).second)
                        LocalPairTemplates.push_back({ti,std::move(pair),angle==90||angle==270});
                }
            }
            LocalPairTemplatesReady=true;
        }
        const auto utilization=UtilizationBefore(state);
        for(const auto& [ti,pair,swappedAxes]:LocalPairTemplates)
        {
            if(B.Stop())break;
            const auto& needs=TemplateNeeds[ti];
            if(needs.empty()||(specification&&needs.front().first!=*specification))continue;
            bool available=true;
            for(const auto& [part,count]:needs)
                available&=P.Parts[part].Material==material&&state.Used[part]<=P.Parts[part].Quantity&&
                    count<=P.Parts[part].Quantity-state.Used[part];
            if(!available||pair.Width>state.Board.Width-2*P.EdgeMargin+P.Tolerance||
                pair.Height>state.Board.Height-2*P.EdgeMargin+P.Tolerance)continue;
            Candidate candidate;candidate.Part=needs.front().first;candidate.Template=ti;
            candidate.Members=pair.Members;candidate.Box={0,0,pair.Width,pair.Height};
            if(ti<Pitches.size())
            {
                candidate.RepeatPitches=Pitches[ti];
                if(swappedAxes)for(auto& pitch:candidate.RepeatPitches)std::swap(pitch.X,pitch.Y);
            }
            const auto queryID=++NextQuery;
            NfpPlacementLimits limits;limits.MaxCandidates=std::min<std::size_t>(8,S.MaxCandidates);
            limits.MaxAttempts=std::min(B.O.MaxPlacementAttempts,B.LocalAttemptLimit)-B.Attempts;
            limits.CandidatePriority=[&](Pose delta)
            {
                auto moved=candidate;std::optional<Rect> occupied;
                for(auto& member:moved.Members)
                {
                    member.Transform.X+=delta.X;member.Transform.Y+=delta.Y;
                    const auto box=Bounds(P.Parts[member.PartIndex].Geometry,member.Transform);
                    occupied=occupied?Unite(*occupied,box):box;
                }
                moved.Box=*occupied;ScoreUtilization(utilization,moved);
                return std::array<double,4>{-moved.Utilization,-moved.Material,moved.OccupiedPlanningArea,moved.PrimaryAxis};
            };
            limits.TraceHealth=B.O.ConstructionObservation;
            if(B.O.ConstructionTrace)limits.Trace=[&](const ConstructionTraceEvent& raw)
            {
                Observe(raw.Stage,[&](ConstructionTraceEvent& event)
                {
                    const auto step=event.Step;event=raw;event.Step=step;event.Route=B.O.ConstructionTraceRoute;
                    event.Scope=TraceContext+"/local-pairs";event.StateID=state.TraceID;
                    event.ProposalID=queryID;event.ElapsedSeconds=B.Seconds();
                });
            };
            const auto started=B.Seconds();
            const auto query=Generator(state).FindRigid(state.Board,candidate.Members,limits,[&]{return B.Stop();});
            B.Attempts+=std::min(query.Attempts,limits.MaxAttempts);
            std::optional<Candidate> best;
            for(const auto delta:query.Fits)
            {
                if(B.Stop())break;
                auto moved=candidate;moved.TraceProposal=queryID;std::optional<Rect> occupied;
                for(auto& member:moved.Members)
                {
                    member.Transform.X+=delta.X;member.Transform.Y+=delta.Y;
                    const auto box=Bounds(P.Parts[member.PartIndex].Geometry,member.Transform);
                    occupied=occupied?Unite(*occupied,box):box;
                }
                moved.Box=*occupied;
                if(!CheckCandidate(state,moved,pair.Members.size()==2?"placement.local-pair":"placement.local-group"))continue;
                ScoreUtilization(utilization,moved);
                if(!best||Rank(moved)<Rank(*best))best=moved;
                Keep(result,std::move(moved));
            }
            // Retain the actual best pair, not merely a count of generated
            // combinations, so this placement decision can be inspected.
            Observe(ConstructionTraceStage::Analysis,[&](ConstructionTraceEvent& event)
            {
                event.Source=pair.Members.size()==2?"placement.local-pair-result":"placement.local-group-result";event.StateID=state.TraceID;
                event.ProposalID=queryID;event.PartIndex=candidate.Part;event.Accepted=best.has_value();
                event.SearchTruncated=query.Truncated;event.Reason=best?"original-safe-small-group":"no-certified-small-group";
                event.Measures={{"template",double(ti)},{"members",double(pair.Members.size())},{"generated",double(query.Generated)},
                    {"fits",double(query.Fits.size())},{"querySeconds",B.Seconds()-started}};
                for(std::size_t i=0;i<pair.Members.size();++i)
                {
                    const auto prefix="localMember["+std::to_string(i)+"].";
                    event.Measures.emplace_back(prefix+"part",double(pair.Members[i].PartIndex));
                    event.Measures.emplace_back(prefix+"angle",pair.Members[i].Transform.AngleDegrees);
                    event.Measures.emplace_back(prefix+"mirrored",pair.Members[i].Transform.Mirrored?1.:0.);
                }
                if(best)
                {
                    for(const auto& member:best->Members)event.Members.push_back(
                        {member.PartIndex,std::numeric_limits<std::size_t>::max(),member.Transform,0});
                    TraceTuple(event,Rank(*best),{"negativeUsedSectionUtilization","negativeNetMaterial",
                        "occupiedPlanningArea","intrinsicFirstMaterialCornerDistance","primaryAxis","secondaryAxis",
                        "part","template","columns","rows"});
                }
            });
        }
        SelectCandidates(result,S.MaxCandidates);
        return result;
    }
    std::vector<Candidate> GrowCarriers(const State& state,const std::vector<Candidate>& seeds,
        bool accountTime=true,const Candidate* certifiedFloor=nullptr,const std::optional<Rect>& materialDomain={})
    {
        double nestedSeconds=0;StageTime carrierTime(B,accountTime?ArraysSeconds:nestedSeconds);
        std::vector<Candidate> result;
        const auto utilization=UtilizationBefore(state);
        using CandidateRank=decltype(Rank(Candidate{}));
        const auto frontierLimit=certifiedFloor?std::size_t{1}:S.MaxCandidates;
        std::vector<CandidateRank> certifiedRanks;
        std::size_t rankDominatedPrefixes=0,deferredUnitChecks=0;
        const auto rememberCertifiedRank=[&](const Candidate& candidate)
        {
            const auto rank=Rank(candidate);
            // Equal ranks remain after their earlier certified peers. Keep
            // multiplicity: the production frontier is a stable top K list.
            certifiedRanks.insert(std::upper_bound(certifiedRanks.begin(),certifiedRanks.end(),rank),rank);
            if(certifiedRanks.size()>frontierLimit)certifiedRanks.pop_back();
        };
        using GeometryKey=std::vector<std::array<std::uint64_t,5>>;
        using PitchKey=std::vector<std::array<std::uint64_t,2>>;
        std::set<std::pair<GeometryKey,PitchKey>> seenSeeds;
        std::set<GeometryKey> originalUnitProofs;
        std::size_t originalUnitProofHits=0,originalUnitProofMisses=0;
        const auto geometryKeyFor=[](const std::vector<Member>& members)
        {
            GeometryKey key;key.reserve(members.size());
            for(const auto& member:members)key.push_back({std::uint64_t(member.PartIndex),
                std::bit_cast<std::uint64_t>(member.Transform.X),std::bit_cast<std::uint64_t>(member.Transform.Y),
                std::bit_cast<std::uint64_t>(member.Transform.AngleDegrees),std::uint64_t(member.Transform.Mirrored)});
            return key;
        };
        const auto certifyOriginalUnit=[&](const std::vector<Member>& members,bool& cacheHit,std::string* reason)
        {
            cacheHit=false;
            const bool cacheable=AxisSafety::ArithmeticSupported();
            auto key=geometryKeyFor(members);
            if(cacheable&&originalUnitProofs.contains(key))
            {cacheHit=true;++originalUnitProofHits;return true;}
            ++originalUnitProofMisses;
            const bool safe=Fits(state,members,reason);
            // A successful certificate binds the unchanged original board and
            // every internal member pair to these exact stored poses. Failed
            // checks, including interruption/uncertainty, are never cached.
            if(safe&&cacheable&&AxisSafety::ArithmeticSupported())originalUnitProofs.insert(std::move(key));
            return safe;
        };
        using GrowthKey=std::tuple<GeometryKey,std::size_t,std::size_t,std::size_t,std::size_t,std::uint64_t,std::uint64_t>;
        std::map<GrowthKey,std::uint64_t> growthProofs;
        for(auto seed:seeds)
        {
            if(B.Stop())break;
            if(!HasSinglePartType(seed.Members))continue;
            if(materialDomain&&!BindMaterialDomain(seed,*materialDomain))continue;
            std::map<std::size_t,std::size_t> needs;
            for(const auto& member:seed.Members)++needs[member.PartIndex];
            std::size_t available=std::numeric_limits<std::size_t>::max();
            for(const auto& [part,count]:needs)
                available=std::min(available,(P.Parts[part].Quantity-state.Used[part])/count);
            if(seed.Members.empty()||available<2)continue;
            if(seed.Members.size()==1)
            {
                const auto& unit=seed.Members.front();
                for(std::size_t ti=0;ti<Templates.size();++ti)
                {
                    const auto& pattern=Templates[ti];
                    if(pattern.Members.size()!=1||pattern.Members.front().PartIndex!=unit.PartIndex)continue;
                    const auto pose=pattern.Members.front().Transform;
                    if(pose.Mirrored!=unit.Transform.Mirrored||NormalizeAngle(pose.AngleDegrees)!=NormalizeAngle(unit.Transform.AngleDegrees))continue;
                    seed.Template=ti;if(ti<Pitches.size())seed.RepeatPitches=Pitches[ti];break;
                }
            }
            auto pitches=seed.RepeatPitches;
            pitches.push_back({seed.Box.Width+P.PartGap,seed.Box.Height+P.PartGap});
            if(P.PartGap>0)
            {
                // Keep exact nominal/shared-edge pitches, and also offer a
                // representable ordinary-clearance pitch. Repeated decimal
                // additions can otherwise make a nominal .2 gap slightly
                // smaller than the unchanged original-material requirement.
                const auto gap=P.PartGap+std::max(1e-7,4*P.Tolerance);
                pitches.push_back({seed.Box.Width+gap,seed.Box.Height+gap});
            }
            if(P.AllowCommonEdges)
            {
                pitches.push_back({seed.Box.Width,seed.Box.Height});
                pitches.push_back({seed.Box.Width,seed.Box.Height+P.PartGap});
                pitches.push_back({seed.Box.Width+P.PartGap,seed.Box.Height});
            }
            auto geometryKey=geometryKeyFor(seed.Members);PitchKey pitchKey;
            for(const auto pitch:pitches)pitchKey.push_back({std::bit_cast<std::uint64_t>(pitch.X),std::bit_cast<std::uint64_t>(pitch.Y)});
            if(!seenSeeds.emplace(geometryKey,std::move(pitchKey)).second)continue;
            bool seedOriginalCacheHit=false;
            const bool seedSafe=certifyOriginalUnit(seed.Members,seedOriginalCacheHit,nullptr);
            if(!seedSafe)continue;
            std::set<std::pair<double,double>> seen;
            for(const auto pitch:pitches)
            {
                if(B.Stop())break;
                if(!std::isfinite(pitch.X)||!std::isfinite(pitch.Y)||pitch.X<=0||pitch.Y<=0||!seen.emplace(pitch.X,pitch.Y).second)continue;
                const auto capacity=[&](double extent,double origin,double size,double step)
                {return static_cast<std::size_t>(std::clamp(1+std::floor((extent-P.EdgeMargin-origin-size+P.Tolerance)/step),0.,double(S.MaxArrayAxis)));};
                const auto widthExtent=materialDomain?std::min(state.Board.Width-P.EdgeMargin,
                    materialDomain->X+materialDomain->Width)+P.EdgeMargin:state.Board.Width;
                const auto heightExtent=materialDomain?std::min(state.Board.Height-P.EdgeMargin,
                    materialDomain->Y+materialDomain->Height)+P.EdgeMargin:state.Board.Height;
                const auto nx=std::min(available,capacity(widthExtent,seed.Box.X,seed.Box.Width,pitch.X));
                const auto ny=std::min(available,capacity(heightExtent,seed.Box.Y,seed.Box.Height,pitch.Y));
                for(std::size_t columns=1;columns<=nx&&!B.Stop();++columns)
                {
                    const auto copies=std::min(available,columns*ny);if(copies<2)continue;
                    const GrowthKey key{geometryKey,seed.Part,seed.Template,columns,copies,
                        columns>1?std::bit_cast<std::uint64_t>(pitch.X):0,
                        copies>columns?std::bit_cast<std::uint64_t>(pitch.Y):0};
                    const auto [path,inserted]=growthProofs.try_emplace(key,0);
                    if(!inserted)
                    {
                        Observe(ConstructionTraceStage::Analysis,[&](ConstructionTraceEvent& event)
                        {event.Source="array.seed-reuse";event.StateID=state.TraceID;event.ProposalID=path->second;
                            event.CandidateID=seed.TraceID;event.Accepted=true;event.Reason="identical-stored-prefix-and-active-pitches";});
                        continue;
                    }
                    // The immutable original board is certified separately.
                    // Keep global demand usage, stock and margin unchanged;
                    // this proof contains only the newly accepted prefix.
                    State proof=state;proof.Board.Placements.clear();
                    const auto appendProof=[&](const std::vector<Member>& members)
                    {
                        for(const auto& member:members)proof.Board.Placements.push_back(
                            {P.Parts[member.PartIndex].ID,proof.Used[member.PartIndex]++,member.Transform,0,false});
                    };
                    appendProof(seed.Members);
                    std::size_t certifiedCopies=1;
                    const auto unitAt=[&](std::size_t copy)
                    {
                        auto members=seed.Members;
                        for(auto& member:members)
                        {
                            member.Transform.X+=(copy%columns)*pitch.X;
                            member.Transform.Y+=(copy/columns)*pitch.Y;
                        }
                        return members;
                    };
                    Candidate grown=seed;grown.Columns=columns;grown.PlanningInset=0;grown.TraceProposal=++NextQuery;
                    path->second=grown.TraceProposal;
                    grown.ConstructionSeconds=0;grown.PlacementQueries=0;grown.GeneratedPositions=0;grown.VerifiedPositions=0;
                    Observe(ConstructionTraceStage::Analysis,[&](ConstructionTraceEvent& event)
                    {
                        event.Source="array.seed-origin";event.StateID=state.TraceID;event.ProposalID=grown.TraceProposal;
                        event.CandidateID=seed.TraceID;event.Accepted=true;event.Reason="certified-original-unit-periodic-growth";
                        event.Measures={{"seedProposal",double(seed.TraceProposal)},{"unitMembers",double(seed.Members.size())},
                            {"pitchX",pitch.X},{"pitchY",pitch.Y},{"columns",double(columns)},
                            {"originalBoardCertificateSeparate",1.},{"seedOriginalBoardCacheHit",seedOriginalCacheHit?1.:0.}};
                        for(const auto& member:seed.Members)event.Members.push_back(
                            {member.PartIndex,std::numeric_limits<std::size_t>::max(),member.Transform,0});
                    });
                    for(std::size_t copy=1;copy<copies&&!B.Stop();++copy)
                    {
                        const auto added=unitAt(copy);
                        bool outsideDomain=false;
                        for(const auto& member:added)
                        {
                            const auto actual=Bounds(P.Parts[member.PartIndex].Geometry,member.Transform);
                            if(materialDomain&&!WithinMaterialDomain(actual,*materialDomain))outsideDomain=true;
                            grown.Box=Unite(grown.Box,actual);
                            grown.Members.push_back(member);
                        }
                        grown.Rows=(copy+columns)/columns;grown.TraceID=0;Identify(grown);
                        // Prefix bounds can only grow. A frozen-band filler
                        // may not open a new strip, even during delayed proof.
                        if(outsideDomain|| (materialDomain&&!WithinMaterialDomain(grown.Box,*materialDomain)))break;
                        ScoreUtilization(utilization,grown);grown.Score=grown.Utilization;
                        const bool competitive=(!certifiedFloor||Rank(grown)<Rank(*certifiedFloor))&&
                            (certifiedRanks.size()<frontierLimit||Rank(grown)<certifiedRanks.back());
                        ObserveCandidate(ConstructionTraceStage::Generate,state,grown,"array.seed-growth",
                            competitive?"periodic-original-prefix":"rank-dominated-unverified",competitive);
                        if(!competitive)
                        {
                            ++rankDominatedPrefixes;
                            Observe(ConstructionTraceStage::Filter,[&](ConstructionTraceEvent& event)
                            {
                                event.Source="array.seed-growth";event.StateID=state.TraceID;
                                event.ProposalID=grown.TraceProposal;event.CandidateID=grown.TraceID;
                                event.Accepted=false;event.Reason="rank-dominated-unverified";
                                event.Measures={{"generatedCopies",double(copy+1)},{"certifiedCopies",double(certifiedCopies)},
                                    {"certifiedFrontier",double(certifiedRanks.size())},{"frontierLimit",double(frontierLimit)}};
                                TraceTuple(event,Rank(grown),{"negativeUsedSectionUtilization","negativeNetMaterial",
                                    "orientationOrOccupiedArea","intrinsicFirstMaterialCornerDistance","primaryAxis",
                                    "secondaryAxis","part","template","columns","rows"});
                            });
                            continue;
                        }
                        bool safe=true;std::string reason;
                        // A later competitive prefix must first certify every
                        // deferred unit in exactly the original pose/order.
                        // Only accepted units enter proof or consume demand.
                        for(std::size_t pending=certifiedCopies;pending<=copy&&!B.Stop();++pending)
                        {
                            if(pending<copy)++deferredUnitChecks;
                            const auto unit=unitAt(pending);
                            bool originalBoardCacheHit=false;
                            const bool originalSafe=certifyOriginalUnit(unit,originalBoardCacheHit,
                                B.O.ConstructionTrace?&reason:nullptr);
                            const bool prefixSafe=originalSafe&&Fits(proof,unit,B.O.ConstructionTrace?&reason:nullptr);
                            safe=originalSafe&&prefixSafe;
                            Observe(ConstructionTraceStage::Analysis,[&](ConstructionTraceEvent& event)
                            {
                                event.Source="array.unit-original-proof";event.StateID=state.TraceID;
                                event.ProposalID=grown.TraceProposal;event.CandidateID=grown.TraceID;event.Accepted=safe;
                                event.Reason=originalBoardCacheHit?"cached-original-board-and-separate-prefix-check":
                                    "fresh-original-board-and-separate-prefix-check";
                                event.Measures={{"originalBoardCertified",originalSafe?1.:0.},
                                    {"originalBoardCacheHit",originalBoardCacheHit?1.:0.},{"prefixChecked",originalSafe?1.:0.},
                                    {"prefixCertified",prefixSafe?1.:0.},{"unitMembers",double(unit.size())},
                                    {"unitCopy",double(pending)},{"generatedCopies",double(copy+1)},
                                    {"certifiedCopies",double(certifiedCopies)},
                                    {"originalBoardProofEntries",double(originalUnitProofs.size())}};
                            });
                            if(!safe)break;
                            appendProof(unit);++certifiedCopies;
                        }
                        if(certifiedCopies!=copy+1)
                        {safe=false;if(reason.empty())reason="pending-prefix-certification-interrupted";}
                        ObserveCandidate(ConstructionTraceStage::Filter,state,grown,"array.seed-growth",
                            safe?"original-board-and-all-prefix-pairs-certified":reason,safe);
                        if(!safe)break;
                        rememberCertifiedRank(grown);Keep(result,grown);
                    }
                }
            }
        }
        Observe(ConstructionTraceStage::Complete,[&](ConstructionTraceEvent& event)
        {
            event.Source="array.unit-original-proof-summary";event.StateID=state.TraceID;
            event.Reason="immutable-original-board-certificates-plus-current-prefix-checks";
            event.Measures={{"originalBoardProofHits",double(originalUnitProofHits)},
                {"originalBoardProofMisses",double(originalUnitProofMisses)},
                {"originalBoardProofEntries",double(originalUnitProofs.size())},
                {"rankDominatedUnverifiedPrefixes",double(rankDominatedPrefixes)},
                {"deferredUnitChecks",double(deferredUnitChecks)},
                {"certifiedFrontier",double(certifiedRanks.size())},{"frontierLimit",double(frontierLimit)}};
        });
        SelectCandidates(result,frontierLimit);return result;
    }
    std::optional<Candidate> MergeCarriers(const State& state,const std::vector<Candidate>& candidates,
        const std::vector<Candidate>& seeds,bool accountTime=true,bool firstCompatible=false)
    {
        if(candidates.empty()||B.Stop())return {};
        double nestedSeconds=0;StageTime carrierTime(B,accountTime?ArraysSeconds:nestedSeconds);
        const auto utilization=UtilizationBefore(state);
        const Candidate* first=nullptr;
        for(const auto& candidate:candidates)if(HasSinglePartType(candidate.Members)&&
            (!first||Rank(candidate)<Rank(*first)))first=&candidate;
        if(!first)return {};
        Candidate best=*first;
        const auto originalType=best.Members.front().PartIndex;
        State proof=state;if(!Fits(proof,best.Members))return {};
        const auto appendProof=[&](const std::vector<Member>& members)
        {for(const auto& member:members)proof.Board.Placements.push_back(
            {P.Parts[member.PartIndex].ID,proof.Used[member.PartIndex]++,member.Transform,0,false});};
        appendProof(best.Members);
        auto pool=seeds;pool.insert(pool.end(),candidates.begin(),candidates.end());
        std::vector<bool> consumed(pool.size());bool changed=false;
        while(!B.Stop())
        {
            std::optional<Candidate> next;std::size_t chosen=pool.size();
            for(std::size_t i=0;i<pool.size()&&!B.Stop();++i)
            {
                if(consumed[i])continue;
                const auto& unit=pool[i];
                // Do not spend placement/scoring work trying cross-type units.
                // Different types still compete as independent board placements.
                if(!HasSinglePartType(unit.Members)||unit.Members.front().PartIndex!=originalType)
                {consumed[i]=true;continue;}
                Candidate joined=best;
                if(joined.UnitMembers!=unit.UnitMembers)joined.UnitMembers=0;
                joined.Members.insert(joined.Members.end(),unit.Members.begin(),unit.Members.end());
                std::optional<Rect> bounds;
                for(const auto& member:joined.Members)
                {const auto box=Bounds(P.Parts[member.PartIndex].Geometry,member.Transform);bounds=bounds?Unite(*bounds,box):box;}
                if(!bounds)continue;
                joined.Box=*bounds;joined.Columns=joined.Rows=1;joined.PlanningInset=0;joined.RepeatPitches.clear();
                // Use the original state and the complete proposed carrier.
                // The proof view already contains best and must not enter the
                // numerator a second time. No stored pose is moved here.
                ScoreUtilization(utilization,joined);joined.Score=joined.Utilization;
                if(!(Rank(joined)<Rank(best))||(next&&!(Rank(joined)<Rank(*next))))continue;
                joined.TraceID=0;joined.TraceProposal=++NextQuery;Identify(joined);
                joined.ConstructionSeconds=0;joined.PlacementQueries=0;joined.GeneratedPositions=0;joined.VerifiedPositions=0;
                ObserveCandidate(ConstructionTraceStage::Generate,state,joined,"carrier.merge","compatible-posed-unit-proposal");
                std::string reason;
                const bool safe=Fits(proof,unit.Members,&reason);
                ObserveCandidate(ConstructionTraceStage::Filter,state,joined,"carrier.merge",
                    safe?"new-unit-certified-against-complete-prefix":reason,safe);
                if(!safe)
                {
                    if(B.Stop())break;
                    // Quantities can only decrease in this proof view. Other
                    // failures remain available for a later comparison; an
                    // uncertain arithmetic verdict is never a hard collision.
                    if(reason=="remaining-quantity")consumed[i]=true;
                    continue;
                }
                Observe(ConstructionTraceStage::Score,[&](ConstructionTraceEvent& event)
                {event.Source="carrier.merge-rank";event.StateID=state.TraceID;event.CandidateID=joined.TraceID;
                    event.ProposalID=joined.TraceProposal;event.Accepted=true;event.Reason="strictly-better-current-carrier";
                    event.Measures={{"baseCandidate",double(best.TraceID)},{"baseProposal",double(best.TraceProposal)},
                        {"unitCandidate",double(unit.TraceID)},{"unitProposal",double(unit.TraceProposal)}};
                    TraceTuple(event,Rank(joined),{"negativeUsedSectionUtilization","negativeNetMaterial",
                        "occupiedPlanningArea","intrinsicFirstMaterialCornerDistance","primaryAxis","secondaryAxis",
                        "part","template","columns","rows"});});
                chosen=i;next=std::move(joined);
                if(firstCompatible)break;
            }
            if(!next)break;
            appendProof(pool[chosen].Members);consumed[chosen]=true;best=std::move(*next);changed=true;
            ObservePlacementChoice(state,best,"merged-prefix");
        }
        return changed?std::optional<Candidate>(std::move(best)):std::nullopt;
    }
    void ObservePlacementChoice(const State& state,const Candidate& candidate,const char* family)
    {
        const auto before=UtilizationBefore(state);
        const auto beforeArea=before.Occupied?UsedSectionArea(before,*before.Occupied):0.;
        Observe(ConstructionTraceStage::Analysis,[&](ConstructionTraceEvent& event)
        {
            event.Source="placement.choice";event.StateID=state.TraceID;
            event.CandidateID=candidate.TraceID;event.ProposalID=candidate.TraceProposal;
            event.PartIndex=candidate.Part;event.Accepted=true;event.Reason=family;
            // A planning continuation may participate as a real-contour
            // obstacle, but must never be drawn as an already placed original.
            if(!state.ReservedOriginals&&ObservedDecisionStates.insert(state.TraceID).second)event.Layouts={state.Board};
            for(const auto& member:candidate.Members)event.Members.push_back(
                {member.PartIndex,std::numeric_limits<std::size_t>::max(),member.Transform,0});
            event.Measures={{"members",double(candidate.Members.size())},
                {"reservedOriginals",double(state.ReservedOriginals)},
                {"usedSectionUtilization",candidate.Utilization},{"usedSectionArea",candidate.UsedSectionArea},
                {"addedNetMaterialArea",candidate.Material},{"addedUsedSectionArea",candidate.UsedSectionArea-beforeArea}};
            TracePlacementRank(event,candidate);
        });
    }
    std::vector<Candidate> ContourArrayChoices(const State& state,const std::string& material)
    {
        // Ordinary originals are placed against the actual board contours at
        // every decision. MaxRects is an additional array proposal source; it
        // never clips this NFP translation domain, even when an array fits.
        std::vector<Candidate> result;
        // Cheap, original-safe proposals remain an incumbent if a difficult
        // NFP query is truncated. They are proposals only: every actual contour
        // query below still runs independently of these AABB partitions.
        const auto utilization=UtilizationBefore(state);
        for(std::size_t pi=0;pi<P.Parts.size()&&!B.Stop();++pi)
        {
            if(P.Parts[pi].Material!=material||state.Used[pi]>=P.Parts[pi].Quantity)continue;
            for(std::size_t ti=0;ti<Templates.size()&&!B.Stop();++ti)
            {
                const auto& pattern=Templates[ti];
                if(pattern.Members.size()!=1||pattern.Members[0].PartIndex!=pi)continue;
                const auto inset=TemplateInset(pattern,state);
                for(const auto space:state.Free.FreeRectangles())
                {
                    if(B.Stop())break;
                    if(pattern.Width+2*inset+P.PartGap>space.Width+P.Tolerance||
                        pattern.Height+2*inset+P.PartGap>space.Height+P.Tolerance)continue;
                    Candidate candidate;candidate.Part=pi;candidate.Template=ti;candidate.PlanningInset=inset;
                    candidate.Box={space.X+P.EdgeMargin,space.Y+P.EdgeMargin,pattern.Width+2*inset,pattern.Height+2*inset};
                    auto member=pattern.Members[0];member.Transform.X+=candidate.Box.X+inset;
                    member.Transform.Y+=candidate.Box.Y+inset;candidate.Members={member};
                    if(CheckCandidate(state,candidate,"placement.fast-original-proposal"))
                    {ScoreUtilization(utilization,candidate);Keep(result,std::move(candidate));}
                }
            }
            if(!result.empty()&&S.Kind!=Kind::NfpMultiArray)break;
        }
        // A pair must be considered at its own size before single-part and
        // all-remaining array searches spend the decision budget. Its NFP
        // domain is the actual board, including concavities and source holes;
        // no MaxRects planning box is allowed to exclude those positions.
        std::optional<std::size_t> localSpecification;
        if(S.Kind==Kind::NfpArray)
            for(std::size_t pi=0;pi<P.Parts.size();++pi)
                if(P.Parts[pi].Material==material&&state.Used[pi]<P.Parts[pi].Quantity)
                {localSpecification=pi;break;}
        auto pairs=LocalPairChoices(state,material,localSpecification);
        if(!pairs.empty())ObservePlacementChoice(state,*std::min_element(pairs.begin(),pairs.end(),[&](const auto& a,const auto& b)
            {return Rank(a)<Rank(b);}),"best-small-group");
        auto seeds=pairs;
        result.insert(result.end(),std::make_move_iterator(pairs.begin()),std::make_move_iterator(pairs.end()));
        if(auto original=PlaceOne(state,material,true,{},nullptr,S.Kind==Kind::NfpArray?&seeds:nullptr))
        {ObservePlacementChoice(state,*original,"best-single-original");if(S.Kind!=Kind::NfpArray)seeds.push_back(*original);Keep(result,std::move(*original));}
        std::optional<std::size_t> specification;
        if(S.Kind==Kind::NfpArray&&!result.empty())
        {
            specification=std::min_element(result.begin(),result.end(),[](const auto& a,const auto& b)
                {return a.Part<b.Part;})->Part;
            result.erase(std::remove_if(result.begin(),result.end(),[&](const auto& candidate)
                {return candidate.Part!=*specification;}),result.end());
            seeds.erase(std::remove_if(seeds.begin(),seeds.end(),[&](const auto& candidate)
                {return candidate.Part!=*specification;}),seeds.end());
        }
        // First-specification construction must compare its ordinary poses
        // with groups needing that same specification. A later pair must not
        // silently jump ahead just because single copies are queried by NFP.
        auto arrays=S.Kind==Kind::NfpArray&&!state.Board.Placements.empty()?GrowCarriers(state,seeds):Arrays(state,material,specification);
        if(!arrays.empty())ObservePlacementChoice(state,*std::min_element(arrays.begin(),arrays.end(),[&](const auto& a,const auto& b)
            {return Rank(a)<Rank(b);}),"best-array");
        result.insert(result.end(),std::make_move_iterator(arrays.begin()),std::make_move_iterator(arrays.end()));
        if(S.Kind==Kind::NfpArray&&!state.Board.Placements.empty())
            if(auto merged=MergeCarriers(state,result,seeds))
            {ObservePlacementChoice(state,*merged,"best-merged-carrier");Keep(result,std::move(*merged));}
        SelectCandidates(result,S.MaxCandidates);
        return result;
    }
    std::optional<Candidate> MainArrayChoice(const State& state,const std::string& material,
        const std::optional<Candidate>* continuation=nullptr,bool lookahead=false)
    {
        // This forward-only constructor changes the board solely by appending
        // committed originals. With no fill commits, the queried state and
        // demand are unchanged: reuse that same completed search, not a second
        // full NFP construction. The actual original gate is still rechecked.
        if(continuation&&*continuation&&(**continuation).ContinuationStateSize==state.Board.Placements.size())
        {
            auto retained=**continuation;retained.TraceID=0;
            if(CheckCandidate(state,retained,"array.unchanged-state-continuation"))
            {
                ScoreUtilization(UtilizationBefore(state),retained);
                ObservePlacementChoice(state,retained,"unchanged-state-certified-next-main-array");return retained;
            }
        }
        for(const auto pi:LargeFirstSpecifications())
        {
            if(B.Stop())break;
            if(P.Parts[pi].Material!=material||state.Used[pi]>=P.Parts[pi].Quantity)continue;
            auto choices=Arrays(state,material,pi);
            if(continuation&&*continuation&&(**continuation).Part==pi)
            {
                auto retained=**continuation;retained.TraceID=0;
                if(CheckCandidate(state,retained,"array.protected-continuation"))
                {ScoreUtilization(UtilizationBefore(state),retained);Keep(choices,std::move(retained));}
                SelectCandidates(choices,S.MaxCandidates);
            }
            if(choices.empty())continue;
            const std::size_t index=0;
            auto selected=std::move(choices[index]);
            ObservePlacementChoice(state,selected,lookahead?"next-main-array-original-material-witness":"large-first-main-array");return selected;
        }
        return {};
    }
    std::vector<std::size_t> LargeCombinationSpecifications(const State& state)const
    {
        auto order=LargeFirstSpecifications();
        std::vector<double> unitArea=Areas;
        for(std::size_t ti=0;ti<Templates.size();++ti)
        {
            const auto& unit=Templates[ti];
            if(unit.Members.size()<2||!HasSinglePartType(unit.Members))continue;
            const auto part=unit.Members.front().PartIndex;
            if(P.Parts[part].Quantity-state.Used[part]>=unit.Members.size())
                unitArea[part]=std::max(unitArea[part],unit.MaterialArea);
        }
        std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return unitArea[a]>unitArea[b];});
        return order;
    }
    void ConstructionPhase(const State& state,ConstructionTraceStage stage,const char* name,
        int phase,const std::optional<Rect>& band={})
    {
        Observe(stage,[&](ConstructionTraceEvent& event)
        {
            event.Source="construction.phase";event.StateID=state.TraceID;event.Reason=name;
            event.SearchTruncated=B.Stop();
            event.Measures={{"constructionPhase",double(phase)},{"placedOriginals",double(state.Board.Placements.size())}};
            if(band)event.Measures.emplace_back("frozenFillTop",band->Y+band->Height);
        });
    }
    void AssessCurrentFreeSpace(const State& state,std::vector<Candidate>& choices)
    {
        if(choices.empty())return;
        // With no fixed material, use the original's translation-independent
        // orientation and low/left ties. Whole empty-sheet topology must not
        // override those ties merely because a concavity faces the boundary.
        if(B.Stop()||state.Board.Placements.empty())
        {
            for(auto& candidate:choices)candidate.Space={};
            std::stable_sort(choices.begin(),choices.end(),[&](const auto& a,const auto& b)
                {return LocalPlacementRank(a)<LocalPlacementRank(b);});
            return;
        }
        const auto started=B.Seconds();
        const auto primary=[](const Candidate& c){return std::tuple(-c.Utilization,-c.Material);};
        const auto first=std::min_element(choices.begin(),choices.end(),[&](const auto& a,const auto& b)
            {return primary(a)<primary(b);});
        const auto bestPrimary=primary(*first);
        // The fill band limits placement, not the remaining-space topology.
        // Its open recesses can connect through the still-empty sheet above.
        // Clipping scoring at the current skyline would falsely call those
        // real connections separate fragments. Use the actual usable sheet;
        // its empty roll tail is one polygon, with no grid or future search.
        const Rect domain{P.EdgeMargin,P.EdgeMargin,state.Board.Width-2*P.EdgeMargin,
            state.Board.Height-2*P.EdgeMargin};
        std::map<ArrayTranslationShape,PlanePlacementSpaceEvaluation> cache;
        bool complete=true;std::size_t evaluated=0;
        try
        {
            PlanePlacementSpaceEvaluator evaluator(P,state.Board,domain);
            for(auto& candidate:choices)
            {
                if(primary(candidate)!=bestPrimary)continue;
                if(B.Stop()){complete=false;break;}
                ArrayTranslationShape key;std::vector<Placement> additions;
                for(const auto& member:candidate.Members)
                {
                    key.emplace_back(member.PartIndex,member.Transform.X,member.Transform.Y,
                        member.Transform.AngleDegrees,member.Transform.Mirrored);
                    additions.push_back({P.Parts[member.PartIndex].ID,0,member.Transform,0,false});
                }
                std::sort(key.begin(),key.end());
                auto known=cache.find(key);
                if(known==cache.end())
                {known=cache.emplace(std::move(key),evaluator.Evaluate(additions)).first;++evaluated;}
                candidate.Space=known->second;
                if(!candidate.Space.Valid)complete=false;
            }
            if(B.Stop())complete=false;
        }
        catch(const std::exception&){complete=false;}
        const auto seconds=B.Seconds()-started;
        for(auto& candidate:choices)
        {
            if(!complete)candidate.Space={};
            candidate.SpaceEvaluationSeconds=seconds;candidate.SpaceCandidatesEvaluated=evaluated;
            if(primary(candidate)!=bestPrimary)continue;
            Observe(ConstructionTraceStage::Score,[&](ConstructionTraceEvent& event)
            {
                event.Source="placement.space-rank";event.StateID=state.TraceID;
                event.CandidateID=candidate.TraceID;event.ProposalID=candidate.TraceProposal;event.PartIndex=candidate.Part;
                event.Accepted=candidate.Space.Valid;
                event.Reason=complete?"current-original-free-space-no-future-parts":"local-space-unavailable-base-rank-retained";
                for(const auto& member:candidate.Members)event.Members.push_back(
                    {member.PartIndex,std::numeric_limits<std::size_t>::max(),member.Transform,0});
                TracePlacementRank(event,candidate);
            });
        }
        std::stable_sort(choices.begin(),choices.end(),[&](const auto& a,const auto& b)
            {return LocalPlacementRank(a)<LocalPlacementRank(b);});
    }
    std::optional<Candidate> LayeredMainChoice(const State& state,const std::string& material,int& phase)
    {
        // Try large units first, and stop at the first feasible specification.
        // An AABB is only a cheap proposal for a complete, contour-certified
        // unit. If that proposal fails, try the unit's true NFP domain before
        // splitting it into originals or going on to a smaller specification.
        for(const auto pi:LargeCombinationSpecifications(state))
        {
            if(B.Stop())break;
            if(P.Parts[pi].Material!=material||state.Used[pi]>=P.Parts[pi].Quantity)continue;
            std::vector<Candidate> pool;
            auto choices=Arrays(state,material,pi,{},ArrayPhase::MainCombination,&pool);
            if(choices.empty()&&!state.Board.Placements.empty())
                choices=Arrays(state,material,pi,{},ArrayPhase::ContourCombination,&pool);
            if(!choices.empty())
            {
                pool.insert(pool.end(),std::make_move_iterator(choices.begin()),std::make_move_iterator(choices.end()));
                AssessCurrentFreeSpace(state,pool);phase=1;return std::move(pool.front());
            }
            pool.clear();choices=Arrays(state,material,pi,{},ArrayPhase::MainOriginal,&pool);
            if(!choices.empty())
            {
                pool.insert(pool.end(),std::make_move_iterator(choices.begin()),std::make_move_iterator(choices.end()));
                AssessCurrentFreeSpace(state,pool);phase=5;return std::move(pool.front());
            }
            pool.clear();
            if(auto single=PlaceOne(state,material,true,{},nullptr,&pool,false,pi))
            {
                if(pool.empty())pool.push_back(*single);
                AssessCurrentFreeSpace(state,pool);phase=5;return std::move(pool.front());
            }
        }
        return {};
    }
    void LayeredGapFill(State& state,const std::string& material,Rect band,bool combinations)
    {
        const char* name=combinations?"gap-combination-array":"gap-single-nfp";
        const int phase=combinations?2:3;
        ConstructionPhase(state,ConstructionTraceStage::Begin,name,phase,band);
        const auto order=combinations?LargeCombinationSpecifications(state):LargeFirstSpecifications();
        for(const auto pi:order)
        {
            if(B.Stop())break;
            if(P.Parts[pi].Material!=material||state.Used[pi]>=P.Parts[pi].Quantity)continue;
            std::vector<Candidate> pool;
            std::optional<std::size_t> queriedSize;
            while(!B.Stop()&&state.Used[pi]<P.Parts[pi].Quantity)
            {
                // Poses stay fixed. Reuse proposals, never a stale legality
                // verdict or score: each candidate must still fit the current
                // demand, actual original prefix and the same frozen band.
                const auto current=UtilizationBefore(state);
                std::vector<Candidate> valid;
                using PoseKey=std::array<std::uint64_t,5>;
                std::set<std::vector<PoseKey>> seen;
                for(auto candidate:pool)
                {
                    if(B.Stop())break;
                    if(candidate.Members.empty()||!HasSinglePartType(candidate.Members)||
                        candidate.Members.front().PartIndex!=pi||
                        candidate.Members.size()>P.Parts[pi].Quantity-state.Used[pi]||
                        (!combinations&&candidate.Members.size()!=1)||
                        (combinations&&candidate.UnitMembers<2)||!BindMaterialDomain(candidate,band))continue;
                    std::vector<PoseKey> key;
                    for(const auto& m:candidate.Members)key.push_back({m.PartIndex,
                        std::bit_cast<std::uint64_t>(m.Transform.X),std::bit_cast<std::uint64_t>(m.Transform.Y),
                        std::bit_cast<std::uint64_t>(m.Transform.AngleDegrees),std::uint64_t(m.Transform.Mirrored)});
                    std::sort(key.begin(),key.end());if(!seen.insert(std::move(key)).second)continue;
                    if(!CheckCandidate(state,candidate,"construction.pool-recheck"))continue;
                    ScoreUtilization(current,candidate);valid.push_back(std::move(candidate));
                }
                pool=std::move(valid);
                if(pool.empty())
                {
                    // Adding material cannot enlarge the free domain. Do not
                    // repeat a completed empty query at this unchanged state.
                    if(queriedSize==state.Board.Placements.size())break;
                    queriedSize=state.Board.Placements.size();
                    if(combinations)
                    {
                        auto choices=Arrays(state,material,pi,band,ArrayPhase::GapCombination,&pool);
                        pool.insert(pool.end(),std::make_move_iterator(choices.begin()),std::make_move_iterator(choices.end()));
                    }
                    else PlaceOne(state,material,true,band,nullptr,&pool,false,pi);
                    if(pool.empty())break;
                    continue;
                }
                auto choices=pool;
                if(combinations)
                    if(auto merged=MergeCarriers(state,pool,pool,true,true))
                        if(BindMaterialDomain(*merged,band))
                        {
                            ScoreUtilization(current,*merged);
                            choices.push_back(std::move(*merged));
                        }
                AssessCurrentFreeSpace(state,choices);
                if(B.Stop())break;
                auto best=std::move(choices.front());
                // Keep the proposal/query identity for a replayable link from
                // its original query through this current-state certificate.
                ObservePlacementChoice(state,best,name);
                Commit(state,best,true,band,phase);
            }
        }
        ConstructionPhase(state,ConstructionTraceStage::Complete,name,phase,band);
    }
    std::optional<Candidate> FillConstructionBand(State& state,const std::string& material)
    {
        if(!B.O.EnableGapFill||B.Stop()||state.Board.Placements.empty())return {};
        const auto before=UtilizationBefore(state);
        if(!before.Occupied)return {};
        // Stock consumption is full width along Y; XFirst only changes the
        // placement tie. Freeze this original top for the complete fill pass.
        const Rect band{P.EdgeMargin,P.EdgeMargin,state.Board.Width-2*P.EdgeMargin,
            before.Occupied->Y+before.Occupied->Height-P.EdgeMargin};
        if(band.Width<=0||band.Height<=0)return {};
        auto next=MainArrayChoice(state,material,nullptr,true);
        std::optional<Candidate> protectedNext,knownFill;
        if(next)
        {
            auto inside=*next;
            if(BindMaterialDomain(inside,band))knownFill=std::move(inside);
            else
            {
                protectedNext=std::move(next);
                protectedNext->ContinuationStateSize=state.Board.Placements.size();
                Observe(ConstructionTraceStage::Analysis,[&](ConstructionTraceEvent& event)
                {
                    event.Source="construction.next-array-reservation";event.StateID=state.TraceID;
                    event.CandidateID=protectedNext->TraceID;event.Reason="preserve-one-certified-next-main-array-while-filling";
                    event.Measures={{"frozenFillTop",band.Y+band.Height}};
                    for(const auto& member:protectedNext->Members)event.Members.push_back(
                        {member.PartIndex,std::numeric_limits<std::size_t>::max(),member.Transform,0});
                });
            }
        }
        Observe(ConstructionTraceStage::Analysis,[&](ConstructionTraceEvent& event)
        {
            event.Source="construction.fill-band";event.StateID=state.TraceID;
            event.Reason="main-array-committed-fill-before-next-main-array";
            event.Measures={{"bandX",band.X},{"bandY",band.Y},{"bandWidth",band.Width},
                {"bandHeight",band.Height},{"frozenTop",band.Y+band.Height}};
        });
        while(!B.Stop())
        {
            // A real original-material continuation is a temporary obstacle,
            // never a committed part or an AABB exclusion. Filling remains
            // legal now and leaves this next large array demonstrably legal.
            State probe=state;
            if(protectedNext)
            {
                probe.ReservedOriginals=protectedNext->Members.size();
                for(const auto& member:protectedNext->Members)
                {
                    probe.Board.Placements.push_back({P.Parts[member.PartIndex].ID,
                        probe.Used[member.PartIndex]++,member.Transform,0,false});
                    probe.Board.MaterialArea+=Areas[member.PartIndex];
                }
                probe.Free.Occupy({protectedNext->Box.X-P.EdgeMargin,protectedNext->Box.Y-P.EdgeMargin,
                    protectedNext->Box.Width+P.PartGap,protectedNext->Box.Height+P.PartGap});
            }
            std::optional<Candidate> best;
            const auto current=UtilizationBefore(state);
            const auto consider=[&](Candidate candidate)
            {
                if(!BindMaterialDomain(candidate,band))return;
                ScoreUtilization(current,candidate);
                if(!best||Rank(candidate)<Rank(*best))
                    best=std::move(candidate);
            };
            if(knownFill)
            {
                auto known=std::move(*knownFill);knownFill.reset();known.TraceID=0;
                if(CheckCandidate(state,known,"array.known-band-candidate"))consider(std::move(known));
            }
            // Singles, self groups and their arrays compete with one common
            // final utilization key. No first-feasible specification shortcut
            // may hide the other originals during this fill decision.
            for(const auto pi:LargeFirstSpecifications())
            {
                if(B.Stop())break;
                if(P.Parts[pi].Material!=material||probe.Used[pi]>=P.Parts[pi].Quantity)continue;
                auto choices=Arrays(probe,material,pi,band);
                for(auto& candidate:choices)consider(std::move(candidate));
            }
            auto holes=HoleChoices(probe,material,band);
            for(auto& candidate:holes)consider(std::move(candidate));
            if(!best)break;
            ObservePlacementChoice(state,*best,"best-frozen-band-single-self-group-or-array");
            Commit(state,*best,true,band);
        }
        Observe(ConstructionTraceStage::Complete,[&](ConstructionTraceEvent& event)
        {
            event.Source="construction.fill-band-complete";event.StateID=state.TraceID;
            event.Reason=B.Stop()?"construction-budget-ended":"no-certified-candidate-in-frozen-band";
            event.SearchTruncated=B.Stop();
            event.Measures={{"frozenTop",band.Y+band.Height}};
        });
        return protectedNext;
    }
    void Fill(State& state,const std::string& material,const std::optional<Rect>& domain={})
    {
        if((S.Kind==Kind::NfpArray||S.Kind==Kind::NfpMultiArray)&&!Generator(state).Ready()&&!(S.EnableHoleNesting&&HasHoleGeometry))return;
        const auto begin=B.Attempts;
        const auto cap=std::max<std::size_t>(1,(B.O.MaxPlacementAttempts-B.Attempts)/8);
        const auto previous=B.LocalAttemptLimit;B.LocalAttemptLimit=std::min(previous,begin+cap);
        while(!B.Stop()&&B.Attempts-begin<cap)
        {
            auto c=PlaceOne(state,material,Generator(state).Ready(),domain);
            if(!c)break;Commit(state,*c,true);
        }
        B.LocalAttemptLimit=previous;
    }
    std::tuple<bool,double,double,double,double> StateQuality(const State& state,bool preservePartialProgress)const
    {
        const auto context=UtilizationBefore(state);
        const auto stock=std::find_if(P.Stocks.begin(),P.Stocks.end(),[&](const auto& item){return item.ID==state.Board.StockID;});
        bool complete=stock!=P.Stocks.end();
        for(std::size_t i=0;complete&&i<P.Parts.size();++i)
            if(P.Parts[i].Material==stock->Material&&state.Used[i]<P.Parts[i].Quantity)complete=false;
        const auto area=context.Occupied?BoxArea(*context.Occupied):0.;
        const auto usedArea=context.Occupied?UsedSectionArea(context,*context.Occupied):0.;
        const auto utilization=usedArea>0?context.Material/usedArea:0.;
        return std::tuple(!complete,!complete&&preservePartialProgress?-context.Material:0.,
            -utilization,-context.Material,area);
    }
    bool BetterState(const State& a,const State& b)const
    {
        // An unfinished output records real progress, not a quality winner.
        // Frontier selection separately keeps utilization first after the
        // completeness gate, so this cannot replace candidate tightness.
        return StateQuality(a,true)<StateQuality(b,true);
    }
    void TailRepack(State& state,const std::string& material)
    {
        StageTime tailTime(B,TailRepackSeconds);
        if(!S.EnableLayoutRefinement||!B.O.EnableGapFill||(S.Kind!=Kind::NfpArray&&S.Kind!=Kind::NfpMultiArray)||
            state.Board.Placements.empty()||B.Stop())return;
        double unplacedArea=0;
        for(std::size_t i=0;i<P.Parts.size();++i)if(P.Parts[i].Material==material)
            unplacedArea+=(P.Parts[i].Quantity-state.Used[i])*Areas[i];
        if(unplacedArea<=0)return;
        const auto stock=std::find_if(P.Stocks.begin(),P.Stocks.end(),[&](const Stock& value){return value.ID==state.Board.StockID;});
        if(stock==P.Stocks.end())return;
        const auto savedTime=B.LocalTimeLimit;const auto savedAttempts=B.LocalAttemptLimit;
        const auto savedSettings=S;const auto savedNextGroup=NextGroup;
        struct Restore
        {
            Budget& Work;double Time;std::size_t Attempts;PlaneStrategySettings& Settings;
            PlaneStrategySettings SavedSettings;std::size_t& Group;std::size_t SavedGroup;
            ~Restore(){Work.LocalTimeLimit=Time;Work.LocalAttemptLimit=Attempts;Settings=SavedSettings;Group=SavedGroup;}
        } restore{B,savedTime,savedAttempts,S,savedSettings,NextGroup,savedNextGroup};
        const State original=state;State best=state;std::size_t bestNextGroup=savedNextGroup;
        const auto started=B.Seconds();
        const double usable=std::max(0.,state.Board.Width-2*P.EdgeMargin)*std::max(0.,state.Board.Height-2*P.EdgeMargin);
        const auto boards=static_cast<std::size_t>(std::clamp(
            std::ceil((unplacedArea+state.Board.MaterialArea)/std::max(1e-30,usable)),1.,100000.));
        const auto remainingSeconds=std::max(0.,std::min(savedTime,B.O.TimeLimitSeconds)-started);
        const auto attemptCap=std::min(savedAttempts,B.O.MaxPlacementAttempts);
        const auto remainingAttempts=attemptCap-std::min(attemptCap,B.Attempts);
        if(remainingSeconds<=0||!remainingAttempts)return;
        const auto tailEnd=started+remainingSeconds*.1/boards;
        const auto tailAttempts=std::min(remainingAttempts,std::max<std::size_t>(1,remainingAttempts/10/boards));
        const auto tailAttemptEnd=B.Attempts+tailAttempts;
        B.LocalTimeLimit=tailEnd;B.LocalAttemptLimit=tailAttemptEnd;
        if(!Generator(state).Ready()||B.Stop())return;
        using Plan=std::pair<bool,std::vector<std::size_t>>;
        std::vector<Plan> plans;std::set<Plan> seenPlans;
        for(unsigned mode=0;mode<3;++mode)
        {
            std::vector<std::size_t> released(original.Board.Placements.size());std::iota(released.begin(),released.end(),0);
            if(!mode)std::reverse(released.begin(),released.end());
            else std::stable_sort(released.begin(),released.end(),[&](auto a,auto b)
            {
                const auto bounds=[&](auto index)
                {const auto& item=original.Board.Placements[index];return Bounds(P.Parts[Index.at(item.PartID)].Geometry,item.Transform);};
                const auto x=bounds(a),y=bounds(b);
                const double first=mode==1?x.Y+x.Height:x.X+x.Width,second=mode==1?y.Y+y.Height:y.X+y.Width;
                return first!=second?first>second:a>b;
            });
            if(released.size()>6)released.resize(6);std::sort(released.begin(),released.end());
            Plan plan{mode==0?!savedSettings.XFirst:mode==2,std::move(released)};
            if(seenPlans.insert(plan).second)plans.push_back(std::move(plan));
        }
        for(std::size_t planIndex=0;planIndex<plans.size();++planIndex)
        {
            B.LocalTimeLimit=tailEnd;B.LocalAttemptLimit=tailAttemptEnd;
            if(B.Stop())return;
            const auto& [xFirst,releasedIndices]=plans[planIndex];
            const auto slots=plans.size()-planIndex;
            B.LocalTimeLimit=B.Seconds()+std::max(0.,tailEnd-B.Seconds())/slots;
            B.LocalAttemptLimit=B.Attempts+(tailAttemptEnd-B.Attempts)/slots;
            State trial(*stock,original.Board.StockInstance,P,original.Used);
            trial.Board=original.Board;trial.Board.Placements.clear();trial.Board.MaterialArea=0;
            trial.Product=original.Product;trial.ColumnWidth=original.ColumnWidth;trial.ColumnTop=original.ColumnTop;
            std::vector<std::vector<std::size_t>> releasedIDs(P.Parts.size());
            std::map<std::pair<std::string,std::size_t>,Placement> releasedMembers;
            for(std::size_t i=0;i<original.Board.Placements.size();++i)
            {
                const auto& item=original.Board.Placements[i];const auto pi=Index.at(item.PartID);
                if(std::binary_search(releasedIndices.begin(),releasedIndices.end(),i))
                {--trial.Used[pi];releasedIDs[pi].push_back(item.Instance);releasedMembers.emplace(std::make_pair(item.PartID,item.Instance),item);continue;}
                trial.Board.Placements.push_back(item);trial.Board.MaterialArea+=Areas[pi];
                const auto box=Bounds(P.Parts[pi].Geometry,item.Transform);
                trial.Free.Occupy({box.X-P.EdgeMargin,box.Y-P.EdgeMargin,box.Width+P.PartGap,box.Height+P.PartGap});
            }
            for(auto& ids:releasedIDs)std::sort(ids.begin(),ids.end());
            std::vector<std::size_t> restored(P.Parts.size()),fresh=original.Used;
            S.Kind=Kind::NfpDirectional;S.XFirst=xFirst;NextGroup=savedNextGroup;
            bool improved=false;
            while(!B.Stop())
            {
                std::vector<std::size_t> order;
                for(std::size_t i=0;i<P.Parts.size();++i)if(P.Parts[i].Material==material&&trial.Used[i]<P.Parts[i].Quantity)order.push_back(i);
                // The changed-axis trial starts with large remaining originals;
                // spatial bucket trials retain the input's specification order.
                if(!planIndex)std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return Areas[a]>Areas[b];});
                std::optional<Candidate> candidate;
                for(const auto pi:order)
                {
                    if(B.Stop())break;auto probe=trial;
                    for(std::size_t i=0;i<P.Parts.size();++i)if(i!=pi)probe.Used[i]=P.Parts[i].Quantity;
                    candidate=PlaceOne(probe,material,true);if(candidate)break;
                }
                if(!candidate||B.Stop())break;
                const auto pi=candidate->Part;Commit(trial,*candidate,true);auto& added=trial.Board.Placements.back();
                if(restored[pi]<releasedIDs[pi].size())
                {
                    added.Instance=releasedIDs[pi][restored[pi]++];
                    const auto& previous=releasedMembers.at({added.PartID,added.Instance});
                    added.Group=previous.Group;added.GapFiller=previous.GapFiller;
                }
                else added.Instance=fresh[pi]++;
                bool recovered=true;
                for(std::size_t i=0;i<restored.size();++i)recovered&=restored[i]==releasedIDs[i].size();
                // Restoring every released identity keeps global per-type
                // instance prefixes valid when construction opens the next board.
                if(recovered&&trial.Board.Placements.size()>original.Board.Placements.size()&&
                    trial.Board.MaterialArea>best.Board.MaterialArea){improved=true;break;}
            }
            const bool interrupted=B.Stop();
            B.LocalTimeLimit=tailEnd;B.LocalAttemptLimit=tailAttemptEnd;
            if(B.Stop())return;
            if(improved&&!interrupted)
            {
                std::map<std::size_t,std::size_t> groups;trial.Groups=trial.Fillers=0;
                for(const auto& item:trial.Board.Placements){++groups[item.Group];trial.Fillers+=item.GapFiller;}
                for(const auto& [id,count]:groups)if(id&&count>1)++trial.Groups;
                best=std::move(trial);bestNextGroup=NextGroup;
            }
            NextGroup=savedNextGroup;S=savedSettings;
        }
        if(best.Board.MaterialArea>original.Board.MaterialArea)
        {state=std::move(best);restore.SavedGroup=bestNextGroup;}
    }
    void SettleBoard(State& state)
    {
        StageTime settlingTime(B,SettlingSeconds);
        if(!S.EnableContourSettling||state.Board.Placements.empty()||B.Stop()||
            (S.Kind!=Kind::ArrayMultiSpecificationSearch&&S.Kind!=Kind::NfpArray&&S.Kind!=Kind::NfpMultiArray))return;
        // This board is a valid local partial of the original inventory,
        // including sparse instance IDs consumed on earlier boards. Other
        // originals remain demand in this private board-finishing stage.
        Result source;source.Layouts={state.Board};std::vector<std::size_t> counts(P.Parts.size());
        for(const auto& item:state.Board.Placements)++counts[Index.at(item.PartID)];
        double area=state.Board.MaterialArea;
        const auto stock=std::find_if(P.Stocks.begin(),P.Stocks.end(),[&](const Stock& value){return value.ID==state.Board.StockID;});
        if(stock==P.Stocks.end())return;
        for(std::size_t i=0;i<P.Parts.size();++i)
        {
            if(counts[i]<P.Parts[i].Quantity)source.Remaining.push_back({P.Parts[i].ID,P.Parts[i].Quantity-counts[i]});
            if(P.Parts[i].Material==stock->Material)area+=(P.Parts[i].Quantity-state.Used[i])*Areas[i];
        }
        source.State=source.Remaining.empty()?Status::Complete:Status::Partial;
        const double usable=std::max(0.,stock->Width-2*P.EdgeMargin)*std::max(0.,stock->Height-2*P.EdgeMargin);
        const double boards=std::max(1.,std::ceil(area/std::max(1e-30,usable)));
        const bool rotate=S.EnableAllowedAngleSettling&&std::any_of(state.Board.Placements.begin(),state.Board.Placements.end(),[&](const Placement& item)
            {const auto pi=Index.at(item.PartID);return !P.Parts[pi].FreeRotation&&P.Parts[pi].Geometry.Radius<=0&&Orientations[pi].size()>1;});
        const auto started=B.Seconds();
        const double seconds=std::max(0.,std::min(B.O.TimeLimitSeconds,B.LocalTimeLimit)-started)*(rotate?.15:.1)/boards;
        const auto deadline=started+seconds;
        const auto available=std::min(B.O.MaxPlacementAttempts,B.LocalAttemptLimit)-B.Attempts;
        const auto allowance=std::min(available,std::max<std::size_t>(1,static_cast<std::size_t>(available/boards/10)));
        const auto attemptEnd=B.Attempts+allowance;
        if(seconds<=0||!allowance)return;
        AxisCompactionSettings settings;settings.XFirst=false;settings.MaxPasses=4;
        settings.MaxCandidatesPerMove=256;settings.MaxContactEventsPerMove=2048;
        std::vector<PostProcessStage> stages{MakeAxisCompactionStage(settings)};
        if(rotate)
        {
            RotationCompactionSettings angles;angles.MaxPasses=1;angles.MaxAnglesPerPart=8;
            angles.MaxPivotsPerPart=2;angles.FiniteAnglesOnly=true;
            stages.push_back(MakeRotationCompactionStage(angles));
        }
        const auto skyline=[&](const Layout& layout)
        {double top=0,right=0;for(const auto& item:layout.Placements){const auto bounds=Bounds(P.Parts[Index.at(item.PartID)].Geometry,item.Transform);
            top=std::max(top,bounds.Y+bounds.Height);right=std::max(right,bounds.X+bounds.Width);}return Point{right,top};};
        bool changed=false;
        for(std::size_t stage=0;stage<stages.size()&&!B.Stop();++stage)
        {
            if(B.Attempts>=attemptEnd)break;
            auto options=B.Remaining();options.TimeLimitSeconds=std::max(0.,deadline-B.Seconds());
            options.MaxPlacementAttempts=attemptEnd-B.Attempts;options.Cancel=[&]{return B.Stop();};
            if(rotate&&stage==0)
            {options.TimeLimitSeconds=std::min(options.TimeLimitSeconds,seconds*.25);
             options.MaxPlacementAttempts=std::max<std::size_t>(1,options.MaxPlacementAttempts/4);}
            if(options.TimeLimitSeconds<=0||!options.MaxPlacementAttempts)break;
            auto settled=stages[stage].Factory()->Run(P,source,options);
            B.Attempts+=std::min(settled.Stats.PlacementAttempts,options.MaxPlacementAttempts);
            if(B.Cancelled||settled.State==Status::Cancelled||settled.Layouts.size()!=1||!Validate(P,settled).Valid)continue;
            const auto& board=settled.Layouts.front();
            if(board.StockID!=state.Board.StockID||board.StockInstance!=state.Board.StockInstance||
                board.Placements.size()!=state.Board.Placements.size())continue;
            const auto before=skyline(state.Board),after=skyline(board);
            if(after.X>before.X||after.Y>before.Y)continue;
            bool safe=true;std::size_t moves=0,angles=0;
            std::vector<AxisSafety::Geometry> geometry;
            for(const auto& item:board.Placements)
            {
                const auto old=std::find_if(state.Board.Placements.begin(),state.Board.Placements.end(),[&](const Placement& value)
                    {return value.PartID==item.PartID&&value.Instance==item.Instance;});
                if(old==state.Board.Placements.end()||old->Group!=item.Group||old->GapFiller!=item.GapFiller)
                {safe=false;break;}
                moves+=old->Transform.X!=item.Transform.X||old->Transform.Y!=item.Transform.Y||old->Transform.AngleDegrees!=item.Transform.AngleDegrees||old->Transform.Mirrored!=item.Transform.Mirrored;
                angles+=old->Transform.AngleDegrees!=item.Transform.AngleDegrees||old->Transform.Mirrored!=item.Transform.Mirrored;
                const auto shape=AxisSafety::Make(P.Parts[Index.at(item.PartID)].Geometry,item.Transform);
                if(AxisSafety::Containment(shape,board.Width,board.Height,P.EdgeMargin)!=AxisSafety::Verdict::Safe){safe=false;break;}
                for(const auto& fixed:geometry)if(AxisSafety::Pair(shape,fixed,P.PartGap,P.AllowCommonEdges,P.Tolerance)!=AxisSafety::Verdict::Safe){safe=false;break;}
                if(!safe)break;geometry.push_back(shape);
            }
            if(!safe||!moves)continue;
            state.Board=board;source=std::move(settled);SettlingMoves+=moves;SettledAngles+=angles;changed=true;
        }
        if(!changed)return;
        ++SettledBoards;
        state.Free=MaxRects(std::max(0.,stock->Width-2*P.EdgeMargin+P.PartGap),
            std::max(0.,stock->Height-2*P.EdgeMargin+P.PartGap),P.Tolerance);
        for(const auto& item:state.Board.Placements)
        {const auto box=Bounds(P.Parts[Index.at(item.PartID)].Geometry,item.Transform);
            state.Free.Occupy({box.X-P.EdgeMargin,box.Y-P.EdgeMargin,box.Width+P.PartGap,box.Height+P.PartGap});}
    }
public:
    Impl(const Problem& p,PlaneStrategySettings settings,Budget& budget,std::size_t nfpTrial=0,
        std::shared_ptr<NfpCache> geometryCache={}):P(p),S(settings),B(budget),
        Cache(geometryCache?std::move(geometryCache):std::make_shared<NfpCache>()),NfpTrial(nfpTrial)
    {
        if(B.O.ConstructionTrace)TraceContext=B.O.ConstructionTraceScope+"/kind-"+std::to_string(static_cast<int>(S.Kind))+"/trial-"+std::to_string(NfpTrial);
        for(std::size_t i=0;i<P.Parts.size();++i)
        {
            Index[P.Parts[i].ID]=i;Orientations.push_back(CandidateOrientations(P.Parts[i],P.Tolerance,B.O.OrientationProposals));Areas.push_back(Area(P.Parts[i].Geometry));
            const auto& source=P.Parts[i].Geometry;
            double sourceScale=std::max(1.,source.Radius);
            const auto measureSource=[&](const std::vector<Point>& ring)
            {for(const auto point:ring)sourceScale=std::max(sourceScale,std::abs(point.X)+std::abs(point.Y));};
            if(source.Radius>0)sourceScale=std::max(sourceScale,std::abs(source.Center.X)+std::abs(source.Center.Y));
            else {measureSource(source.Outer);for(const auto& hole:source.Holes)measureSource(hole);}
            SourceScales.push_back(sourceScale);
            HasHoleGeometry|=P.Parts[i].Quantity&&!P.Parts[i].Geometry.Holes.empty();
            Rectangular.push_back(Classify(P.Parts[i].Geometry,P.Tolerance).Kind==ShapeKind::Rectangle&&P.Parts[i].Geometry.Holes.empty());
            for(const auto orientation:Orientations.back())
            {const auto b=Bounds(P.Parts[i].Geometry,orientation);Pattern pattern;
                pattern.Members={{i,{-b.X,-b.Y,orientation.AngleDegrees,orientation.Mirrored}}};
                pattern.Width=b.Width;pattern.Height=b.Height;pattern.MaterialArea=Areas.back();Templates.push_back(std::move(pattern));}
        }
        if(!StrictRectangles()&&!IsNfp(S.Kind)&&S.Kind!=Kind::Scanline&&B.O.EnableCombinations&&!B.Stop())
        {
            StageTime patternTime(B,PatternSeconds);
            auto o=B.Remaining();auto patternProblem=P;
            if(patternProblem.PartGap>0)patternProblem.PartGap+=std::max(1e-7,4*P.Tolerance);
            const auto limit=B.Seconds()+o.TimeLimitSeconds*.15;
            std::size_t attempts=0;auto extra=BuildPatterns(patternProblem,o,[&]{return B.Stop()||B.Seconds()>=limit;},&attempts);
            B.Attempts+=attempts;
            for(auto& ptn:extra)if(ptn.Members.size()>1&&HasSinglePartType(ptn.Members))Templates.push_back(std::move(ptn));
        }
        if(S.Kind==Kind::FourBlockArray)FourBlocks();
        const bool ordinaryUnits=B.O.EnableCombinations&&!StrictRectangles()&&!IsNfp(S.Kind)&&
            !ContourArray(S.Kind)&&S.Kind!=Kind::Scanline;
        if(S.Kind==Kind::NfpArray||S.Kind==Kind::NfpMultiArray||
            ordinaryUnits)NfpTemplates();
        if(ordinaryUnits)AppendOrdinaryUnitOrientations();
        for(const auto& pattern:Templates)
        {
            std::map<std::size_t,std::size_t> counts;
            for(const auto& member:pattern.Members)++counts[member.PartIndex];
            TemplateNeeds.emplace_back(counts.begin(),counts.end());
            Observe(ConstructionTraceStage::Match,[&](ConstructionTraceEvent& event)
            {
                const auto ti=TemplateNeeds.size()-1;event.Source="preparation.retained-template";event.ProposalID=ti+1;
                event.Width=pattern.Width;event.Height=pattern.Height;event.MaterialArea=pattern.MaterialArea;
                for(const auto& member:pattern.Members)event.Members.push_back({member.PartIndex,std::numeric_limits<std::size_t>::max(),member.Transform,0});
                if(ti<Pitches.size())for(std::size_t i=0;i<Pitches[ti].size();++i)
                {event.Measures.emplace_back("pitchX["+std::to_string(i)+"]",Pitches[ti][i].X);event.Measures.emplace_back("pitchY["+std::to_string(i)+"]",Pitches[ti][i].Y);}
            });
        }
    }
    Result Run(Kernel& kernel,const BoardConstructor& construct,const Result* fixed=nullptr)
    {
        Observe(ConstructionTraceStage::Begin,[&](ConstructionTraceEvent& event){event.Source="constructor";
            event.Measures.insert(event.Measures.end(),{{"maxCandidates",static_cast<double>(S.MaxCandidates)},
                {"branchWidth",static_cast<double>(S.BranchWidth)},{"branchDecisions",static_cast<double>(S.BranchDecisions)},
                {"maxNodes",static_cast<double>(S.MaxNodes)},{"templates",static_cast<double>(Templates.size())},
                {"originalMaterialUtilizationRanking",1.},
                {"fullStockWidthUsedSectionRanking",1.},
                {"sameOriginalTypeCombinationsOnly",1.},
                {"largeFirstMainArray",ArrayFillConstruction()?1.:0.},
                {"frozenBandFillBeforeNextArray",B.O.EnableGapFill&&
                    (S.Kind==Kind::ArrayQuantityDeferred||S.Kind==Kind::ArrayQuantityImmediate||
                     S.Kind==Kind::ArrayGeometryImmediate||S.Kind==Kind::RandomArrays)?1.:0.},
                {"sourceArrayEvaluation",S.UseSourceArrayEvaluation?1.:0.},{"contourSettling",S.EnableContourSettling?1.:0.},
                {"allowedAngleSettling",S.EnableAllowedAngleSettling?1.:0.}});
            for(const auto& part:P.Parts)event.Measures.emplace_back("demand."+part.ID,static_cast<double>(part.Quantity));
        });
        Result result;std::vector<std::size_t> used(P.Parts.size()),stocks(P.Stocks.size());
        if(fixed)
        {
            for(const auto& layout:fixed->Layouts)for(const auto& q:layout.Placements)++used[Index.at(q.PartID)];
            for(const auto& layout:fixed->Layouts)
            {
                StageTime constructionTime(B,ConstructionSeconds);
                auto it=std::find_if(P.Stocks.begin(),P.Stocks.end(),[&](const Stock& s){return s.ID==layout.StockID;});
                State state(*it,layout.StockInstance,P,used);state.Board=layout;
                for(const auto& q:layout.Placements)
                {NextGroup=std::max(NextGroup,q.Group+1);auto box=Bounds(P.Parts[Index.at(q.PartID)].Geometry,q.Transform);state.Free.Occupy({box.X-P.EdgeMargin,box.Y-P.EdgeMargin,box.Width+P.PartGap,box.Height+P.PartGap});}
                while(!B.Stop())
                {
                    auto c=PlaceOne(state,it->Material,true);if(!c)break;
                    // Reuse unused original instance IDs, including holes in a partial layout.
                    Commit(state,*c,true);
                    auto& added=state.Board.Placements.back();std::set<std::size_t> occupied;
                    for(const auto& l:fixed->Layouts)for(const auto& p:l.Placements)if(p.PartID==added.PartID)occupied.insert(p.Instance);
                    for(const auto& l:result.Layouts)for(const auto& p:l.Placements)if(p.PartID==added.PartID)occupied.insert(p.Instance);
                    for(std::size_t j=layout.Placements.size();j+1<state.Board.Placements.size();++j)if(state.Board.Placements[j].PartID==added.PartID)occupied.insert(state.Board.Placements[j].Instance);
                    added.Instance=0;while(occupied.contains(added.Instance))++added.Instance;
                }
                used=state.Used;result.Stats.GapFilledParts+=state.Fillers;result.Layouts.push_back(std::move(state.Board));
            }
        }
        else for(std::size_t si=0;si<P.Stocks.size()&&!B.Stop();++si)
        {
            const auto& stock=P.Stocks[si];
            while(stocks[si]<stock.Quantity&&!B.Stop())
            {
                State state(stock,stocks[si],P,used);
                {
                    StageTime constructionTime(B,ConstructionSeconds);
                    construct(kernel,state,stock.Material);
                    if(S.Kind==Kind::FourBlockArray)CenterFilledParts+=state.Fillers;
                }
                if(state.Board.Placements.empty())break;
                state.Board.Width=stock.Width;state.Board.Height=stock.Height;
                used=state.Used;++stocks[si];result.Stats.CombinationGroups+=state.Groups;result.Stats.GapFilledParts+=state.Fillers;
                result.Layouts.push_back(std::move(state.Board));
                {
                    StageTime reuseTime(B,ReuseSeconds);
                    const auto reuse=ReuseLastBoard(P,S,B,stock,stocks[si],used,NextGroup,result);
                    ReusedBoards+=reuse.Copies;ReusedParts+=reuse.Parts;
                }
            }
        }
        result.Remaining.clear();
        for(std::size_t i=0;i<P.Parts.size();++i)if(used[i]<P.Parts[i].Quantity)result.Remaining.push_back({P.Parts[i].ID,P.Parts[i].Quantity-used[i]});
        result.State=B.Cancelled?Status::Cancelled:result.Remaining.empty()?Status::Complete:Status::Partial;
        Observe(ConstructionTraceStage::Complete,[&](ConstructionTraceEvent& event){event.Source="constructor.result";event.ResultStatus=result.State;
            event.Layouts=result.Layouts;event.Remaining=result.Remaining;event.Reason=B.Cancelled?"cancelled":B.Seconds()>=std::min(B.O.TimeLimitSeconds,B.LocalTimeLimit)?"time-limit":
                B.Attempts>=std::min(B.O.MaxPlacementAttempts,B.LocalAttemptLimit)?"attempt-limit":result.Remaining.empty()?"demand-complete":"finite-construction-ended";});
        result.Stats.PlacementAttempts=B.Attempts;result.Stats.ElapsedSeconds=B.Seconds();result.Stats.CandidatePatterns=Templates.size();
        result.Diagnostics.push_back("plane.constructor kind="+std::to_string(static_cast<int>(S.Kind))+" nodes="+std::to_string(B.Nodes)+" nfp-trial="+std::to_string(NfpTrial)+
            " pair-cache-hits="+std::to_string(PairCacheHits)+" pair-cache-misses="+std::to_string(PairCacheMisses)+"; bounded search, original-material validation");
        result.Diagnostics.push_back("plane.timing: build-patterns-s="+std::to_string(PatternSeconds)+
            " nfp-templates-s="+std::to_string(NfpTemplateSeconds)+" nfp-combinations-s="+std::to_string(NfpCombinationSeconds)+
            " nfp-pitches-s="+std::to_string(NfpPitchSeconds)+" search-s="+std::to_string(SearchSeconds)+
            " arrays-s="+std::to_string(ArraysSeconds)+" selector-s="+std::to_string(SelectionSeconds)+
            " construction-s="+std::to_string(ConstructionSeconds)+" tail-repack-s="+std::to_string(TailRepackSeconds)+
            " settling-s="+std::to_string(SettlingSeconds)+" reuse-s="+std::to_string(ReuseSeconds)+
            "; combination and pitch times are included in nfp-templates; arrays/selector are nested work inside search for search strategies, selector is also included in arrays; wall-clock stage boundaries only");
        if(S.UseSourceArrayEvaluation)result.Diagnostics.push_back("plane.array-evaluation: original net material / full-stock-width used section from board origin; no member-count or host-hole-area bonus");
        if(S.EnableContourSettling&&(S.Kind==Kind::ArrayMultiSpecificationSearch||S.Kind==Kind::NfpArray||S.Kind==Kind::NfpMultiArray))
            result.Diagnostics.push_back("plane.contour-settling: boards="+std::to_string(SettledBoards)+" moves="+std::to_string(SettlingMoves)+" angle-changes="+std::to_string(SettledAngles)+"; bounded per-board original-contour and finite allowed-angle settling; identities and board allocation retained");
        if(S.EnableSheetTemplateReuse)result.Diagnostics.push_back("plane.sheet-template-reuse: copies="+std::to_string(ReusedBoards)+
            " parts="+std::to_string(ReusedParts)+" minimum-utilization="+std::to_string(S.MinimumSheetReuseUtilization)+
            "; current-run verified physical board, complete demand vector, distinct original/stock instances; residual demand reconstructed");
        if(IndependentPairCandidates)result.Diagnostics.push_back("plane.independent-pair-arrays: candidates="+std::to_string(IndependentPairCandidates)+"; separate original counts and row/column extents; original-material validation");
        if(HoleCandidates)result.Diagnostics.push_back("plane.hole-nesting: candidates="+std::to_string(HoleCandidates)+"; real inner domains, original instance inventory and nominal clearances");
        if(CenterFilledParts)result.Diagnostics.push_back("plane.four-block-fill: parts="+std::to_string(CenterFilledParts)+"; fixed original perimeter blocks, true-material central free space");
        if(!result.Remaining.empty())result.Diagnostics.push_back("Unsupported geometry, finite candidates, stock or shared budget exhausted; no infeasibility claim.");
        if(P.PartGap>0)result.Diagnostics.push_back("plane.clearance: private conservative proposals and fixed-member offsets; exact original material, board margin and pair clearance retained");
        return result;
    }
};

Kernel::Kernel(const Problem& p,PlaneStrategySettings s,Budget& b,std::size_t trial,std::shared_ptr<NfpCache> cache)
    :Data(std::make_unique<Impl>(p,s,b,trial,std::move(cache))){}
Kernel::~Kernel()=default;
const Problem& Kernel::Input()const{return Data->P;}
const PlaneStrategySettings& Kernel::Settings()const{return Data->S;}
Budget& Kernel::Work(){return Data->B;}
Result Kernel::Run(const Result* fixed){return Data->Run(*this,MakeBoardConstructor(Data->S.Kind),fixed);}
std::optional<Candidate> Kernel::Single(const State& s,const std::string& m,bool n,const std::optional<Rect>& d)
{
    const auto started=Data->B.Seconds();
    std::vector<Candidate> pool;
    auto best=Data->PlaceOne(s,m,n,d,nullptr,&pool);
    if(!best)return {};
    // Scanline can return its first certified grid point directly. Retain it
    // alongside any certified hole positions collected before that return.
    const auto& pose=best->Members.front().Transform;
    if(std::none_of(pool.begin(),pool.end(),[&](const Candidate& candidate)
    {
        const auto& other=candidate.Members.front().Transform;
        return candidate.Part==best->Part&&other.X==pose.X&&other.Y==pose.Y&&
            other.AngleDegrees==pose.AngleDegrees&&other.Mirrored==pose.Mirrored;
    }))pool.push_back(*best);
    std::stable_sort(pool.begin(),pool.end(),[&](const Candidate& a,const Candidate& b)
        {return Data->LocalPlacementRank(a)<Data->LocalPlacementRank(b);});
    Data->AssessCurrentFreeSpace(s,pool);
    auto selected=std::move(pool.front());
    selected.ConstructionSeconds=Data->B.Seconds()-started;
    selected.PlacementQueries=best->PlacementQueries;selected.GeneratedPositions=best->GeneratedPositions;
    selected.VerifiedPositions=best->VerifiedPositions;
    Data->ObserveCandidate(ConstructionTraceStage::Selected,s,selected,"placement.space-winner",
        "minimum-certified-current-placement-key");
    return selected;
}
std::vector<Candidate> Kernel::Arrays(const State& s,const std::string& m,const std::optional<std::size_t>& p)
{
    auto choices=Data->Arrays(s,m,p);Data->AssessCurrentFreeSpace(s,choices);return choices;
}
std::vector<Candidate> Kernel::ContourArrays(const State& s,const std::string& m){return Data->ContourArrayChoices(s,m);}
std::vector<Candidate> Kernel::Holes(const State& s,const std::string& m){return Data->HoleChoices(s,m);}
std::vector<std::size_t> Kernel::LargeFirst()const{return Data->LargeFirstSpecifications();}
std::optional<Candidate> Kernel::MainArray(const State& s,const std::string& m)
{
    for(const auto part:Data->LargeFirstSpecifications())
    {
        if(Data->B.Stop())break;
        if(Data->P.Parts[part].Material!=m||s.Used[part]>=Data->P.Parts[part].Quantity)continue;
        auto choices=Arrays(s,m,part);
        if(!choices.empty())return std::move(choices.front());
    }
    return {};
}
std::optional<Candidate> Kernel::LayeredMain(const State& s,const std::string& m,int& p){return Data->LayeredMainChoice(s,m,p);}
void Kernel::GapFill(State& s,const std::string& m,Rect b,bool c){Data->LayeredGapFill(s,m,b,c);}
std::optional<Candidate> Kernel::ForwardMain(const State& s,const std::string& m,const std::optional<Candidate>* c)
{return Data->MainArrayChoice(s,m,c);}
std::optional<Candidate> Kernel::ForwardBandFill(State& s,const std::string& m)
{return Data->FillConstructionBand(s,m);}

void Kernel::Fill(State& s,const std::string& m,const std::optional<Rect>& d){Data->Fill(s,m,d);}
void Kernel::FillHoles(State& s,const std::string& m){Data->FillHoles(s,m);}
void Kernel::RefineTail(State& s,const std::string& m){Data->TailRepack(s,m);}
void Kernel::Settle(State& s){Data->SettleBoard(s);}
std::optional<Rect> Kernel::OccupiedBand(const State& s)const
{
    const auto u=Data->UtilizationBefore(s);
    if(!u.Occupied)return {};
    return Rect{Data->P.EdgeMargin,Data->P.EdgeMargin,s.Board.Width-2*Data->P.EdgeMargin,
        u.Occupied->Y+u.Occupied->Height-Data->P.EdgeMargin};
}
void Kernel::Commit(State& s,const Candidate& c,bool f,const std::optional<Rect>& b,int p){Data->Commit(s,c,f,b,p);}
void Kernel::SelectedPath(const State& s)const{Data->SelectedPath(s);}
void Kernel::Choice(const State& s,const Candidate& c,const char* f){Data->ObservePlacementChoice(s,c,f);}
void Kernel::Phase(const State& s,ConstructionTraceStage t,const char* n,int p,const std::optional<Rect>& b)
{Data->ConstructionPhase(s,t,n,p,b);}
void Kernel::Emit(ConstructionTraceStage s,const std::function<void(ConstructionTraceEvent&)>& f)const{Data->Observe(s,f);}
void Kernel::RankCandidates(const State& s,std::vector<Candidate>& c){Data->AssessCurrentFreeSpace(s,c);}
void Kernel::LimitCandidates(std::vector<Candidate>& c,std::size_t n)const{Data->SelectCandidates(c,n);}
bool Kernel::Better(const State& a,const State& b)const{return Data->BetterState(a,b);}
bool Kernel::RectanglesOnly()const{return Data->StrictRectangles();}
void Kernel::AddSearchSeconds(double s){Data->SearchSeconds+=s;}

}
