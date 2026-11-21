#include "SheetNestingRotationCompaction.h"
#include "SheetNestingAxisCompaction.h"
#include "NestingAxisSafety.h"
#include "NestingExecution.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace iCAX::SheetNesting
{
namespace
{
    using Clock=std::chrono::steady_clock;
    using Parts=std::map<std::string,const Part*>;
    using Potential=std::tuple<double,double,double>;
    struct Identity
    {
        std::string Stock,Part;
        std::size_t StockInstance=0,Instance=0;
    };
    struct Position { std::size_t Board,Piece; };
    struct Edge { Point A,B; double Length,Angle; };
    struct Pivot { Point Local,World; double Distance=0; };
    struct Candidate
    {
        JobResult State;
        Detail::QualityKey Key;
        Potential Guide;
    };

    std::optional<Position> Find(const Result& result,const Identity& identity)
    {
        for (std::size_t b=0;b<result.Layouts.size();++b)
        {
            const auto& board=result.Layouts[b];
            if (board.StockID!=identity.Stock||board.StockInstance!=identity.StockInstance) continue;
            for (std::size_t p=0;p<board.Placements.size();++p)
            {
                const auto& piece=board.Placements[p];
                if (piece.PartID==identity.Part&&piece.Instance==identity.Instance) return Position{b,p};
            }
        }
        return {};
    }
    double AngleDistance(double a,double b)
    {
        const double difference=std::abs(Detail::NormalizeAngle(a)-Detail::NormalizeAngle(b));
        return std::min(difference,360-difference);
    }
    double BoxDistance(Rect a,Rect b)
    {
        return std::hypot(std::max({0.,a.X-b.X-b.Width,b.X-a.X-a.Width}),
            std::max({0.,a.Y-b.Y-b.Height,b.Y-a.Y-a.Height}));
    }
    bool Near(Rect a,Rect b,double clearance)
    {
        return a.X+a.Width+clearance>=b.X&&b.X+b.Width+clearance>=a.X&&
            a.Y+a.Height+clearance>=b.Y&&b.Y+b.Height+clearance>=a.Y;
    }
    std::vector<Edge> Edges(const Shape& shape,Pose pose,const std::function<bool()>& stop)
    {
        std::vector<Edge> result;
        const auto append=[&](const std::vector<Point>& ring)
        {
            for (std::size_t i=0;i<ring.size();++i)
            {
                if (stop()) return;
                const auto a=Detail::Transform(ring[i],pose);
                const auto b=Detail::Transform(ring[(i+1)%ring.size()],pose);
                const double length=std::hypot(b.X-a.X,b.Y-a.Y);
                if (length>0) result.push_back({a,b,length,std::atan2(b.Y-a.Y,b.X-a.X)*180/Detail::Pi});
            }
        };
        append(shape.Outer);
        for (const auto& hole:shape.Holes) append(hole);
        std::stable_sort(result.begin(),result.end(),[](const Edge& a,const Edge& b)
        { return a.Length>b.Length; });
        return result;
    }
    std::vector<std::size_t> Neighbours(const Layout& board,std::size_t piece,const Parts& parts)
    {
        const auto& moving=board.Placements[piece];
        const auto box=Detail::Bounds(parts.at(moving.PartID)->Geometry,moving.Transform);
        std::vector<std::pair<double,std::size_t>> ranked;
        for (std::size_t i=0;i<board.Placements.size();++i)
        {
            if (i==piece) continue;
            const auto& other=board.Placements[i];
            ranked.emplace_back(BoxDistance(box,Detail::Bounds(parts.at(other.PartID)->Geometry,other.Transform)),i);
        }
        std::stable_sort(ranked.begin(),ranked.end());
        std::vector<std::size_t> result;
        for (std::size_t i=0;i<std::min<std::size_t>(4,ranked.size());++i) result.push_back(ranked[i].second);
        return result;
    }
    double PointSegmentDistance(Point point,const Edge& edge)
    {
        const double dx=edge.B.X-edge.A.X,dy=edge.B.Y-edge.A.Y;
        const double t=std::clamp(((point.X-edge.A.X)*dx+(point.Y-edge.A.Y)*dy)/(dx*dx+dy*dy),0.,1.);
        return std::hypot(point.X-edge.A.X-t*dx,point.Y-edge.A.Y-t*dy);
    }
    std::vector<Pivot> Pivots(const Problem& input,const Layout& board,std::size_t piece,
        const Parts& parts,std::size_t maximum,const std::function<bool()>& stop)
    {
        const auto& moving=board.Placements[piece];
        const auto& shape=parts.at(moving.PartID)->Geometry;
        std::vector<Edge> nearbyEdges;
        std::vector<std::pair<Point,double>> circles;
        for (const auto index:Neighbours(board,piece,parts))
        {
            if (stop()) return {};
            const auto& other=board.Placements[index];
            const auto& original=parts.at(other.PartID)->Geometry;
            if (original.Radius>0) circles.emplace_back(Detail::Transform(original.Center,other.Transform),original.Radius);
            else
            {
                auto edges=Edges(original,other.Transform,stop);
                nearbyEdges.insert(nearbyEdges.end(),edges.begin(),edges.end());
            }
        }
        std::vector<Pivot> all;
        const auto append=[&](const std::vector<Point>& ring)
        {
            for (const auto local:ring)
            {
                if (stop()) return;
                const auto world=Detail::Transform(local,moving.Transform);
                double distance=std::min({std::abs(world.X-input.EdgeMargin),std::abs(world.Y-input.EdgeMargin),
                    std::abs(board.Width-input.EdgeMargin-world.X),std::abs(board.Height-input.EdgeMargin-world.Y)});
                for (const auto& edge:nearbyEdges) distance=std::min(distance,PointSegmentDistance(world,edge));
                for (const auto& [center,radius]:circles)
                    distance=std::min(distance,std::abs(std::hypot(world.X-center.X,world.Y-center.Y)-radius));
                all.push_back({local,world,distance});
            }
        };
        append(shape.Outer);
        for (const auto& hole:shape.Holes) append(hole);
        if (all.empty()||stop()) return {};
        std::vector<Pivot> result;
        const auto add=[&](const Pivot& pivot)
        {
            if (result.size()>=maximum) return;
            if (std::none_of(result.begin(),result.end(),[&](const Pivot& old)
                { return old.Local.X==pivot.Local.X&&old.Local.Y==pivot.Local.Y; })) result.push_back(pivot);
        };
        for (const bool xAxis:{true,false})
        {
            const auto coordinate=[&](const Pivot& pivot) { return xAxis?pivot.World.X:pivot.World.Y; };
            const auto limits=std::minmax_element(all.begin(),all.end(),[&](const Pivot& a,const Pivot& b)
            { return coordinate(a)<coordinate(b); });
            add(*limits.first);add(*limits.second);
        }
        std::stable_sort(all.begin(),all.end(),[](const Pivot& a,const Pivot& b)
        { return std::tie(a.Distance,a.World.Y,a.World.X)<std::tie(b.Distance,b.World.Y,b.World.X); });
        for (const auto& pivot:all) add(pivot);
        return result;
    }
    std::vector<double> Angles(const Problem& input,const Layout& board,std::size_t piece,const Parts& parts,
        const RotationCompactionSettings& settings,double step,std::size_t pass,bool& limited,
        const std::function<bool()>& stop)
    {
        const auto& placed=board.Placements[piece];
        const auto& original=*parts.at(placed.PartID);
        std::vector<double> result;
        const auto add=[&](double angle)
        {
            if (!Detail::PoseAllowed(original,{0,0,angle,placed.Transform.Mirrored},input.Tolerance)) return;
            angle=Detail::NormalizeAngle(angle);
            if (AngleDistance(angle,placed.Transform.AngleDegrees)<1e-10) return;
            if (std::any_of(result.begin(),result.end(),[&](double old) { return AngleDistance(old,angle)<1e-10; })) return;
            if (result.size()>=settings.MaxAnglesPerPart) { limited=true; return; }
            result.push_back(angle);
        };
        if (!original.FreeRotation)
        {
            const auto domain=Detail::CandidateAngles(original,input.Tolerance);
            for (std::size_t i=0;i<domain.size();++i)
                add(domain[(i+pass*settings.MaxAnglesPerPart)%domain.size()]);
            return result;
        }
        add(placed.Transform.AngleDegrees-step);add(placed.Transform.AngleDegrees+step);
        const auto local=Edges(original.Geometry,{0,0,0,placed.Transform.Mirrored},stop);
        std::vector<double> aligned;
        for (const auto neighbour:Neighbours(board,piece,parts))
        {
            if (stop()) break;
            const auto& other=board.Placements[neighbour];
            const auto fixed=Edges(parts.at(other.PartID)->Geometry,other.Transform,stop);
            for (std::size_t i=0;i<std::min<std::size_t>(8,local.size());++i)
                for (std::size_t j=0;j<std::min<std::size_t>(8,fixed.size());++j)
                {
                    aligned.push_back(Detail::NormalizeAngle(fixed[j].Angle-local[i].Angle));
                    aligned.push_back(Detail::NormalizeAngle(fixed[j].Angle-local[i].Angle+180));
                }
        }
        std::stable_sort(aligned.begin(),aligned.end(),[&](double a,double b)
        { return AngleDistance(a,placed.Transform.AngleDegrees)<AngleDistance(b,placed.Transform.AngleDegrees); });
        const auto neighbourEnd=std::min<std::size_t>(8,aligned.size());
        for (std::size_t i=0;i<neighbourEnd;++i) add(aligned[i]);
        for (const auto orientation:Detail::CandidateOrientations(original,input.Tolerance))
            if(orientation.Mirrored==placed.Transform.Mirrored)add(orientation.AngleDegrees);
        for (std::size_t i=neighbourEnd;i<aligned.size();++i) add(aligned[i]);
        return result;
    }
    bool SafeLayout(const Problem& input,const Result& result,const Parts& parts,const std::function<bool()>& stop,
        std::optional<Position> changed={})
    {
        for (std::size_t b=0;b<result.Layouts.size();++b)
        {
            if (changed&&changed->Board!=b) continue;
            const auto& board=result.Layouts[b];
            std::vector<AxisSafety::Geometry> geometry;
            std::vector<Rect> boxes;
            for (const auto& item:board.Placements)
            {
                if (stop()) return false;
                const auto& shape=parts.at(item.PartID)->Geometry;
                geometry.push_back(AxisSafety::Make(shape,item.Transform));
                boxes.push_back(Detail::Bounds(shape,item.Transform));
            }
            for (std::size_t i=0;i<geometry.size();++i)
            {
                if (stop()) return false;
                if ((!changed||i==changed->Piece)&&!AxisSafety::Inside(geometry[i],board.Width,board.Height,input.EdgeMargin))
                    return false;
                for (std::size_t j=0;j<i;++j)
                {
                    if (changed&&i!=changed->Piece&&j!=changed->Piece) continue;
                    if (stop()) return false;
                    if (Near(boxes[i],boxes[j],input.PartGap+input.Tolerance+geometry[i].Roundoff+geometry[j].Roundoff)&&
                        AxisSafety::Pair(geometry[i],geometry[j],input.PartGap,input.AllowCommonEdges,input.Tolerance)!=AxisSafety::Verdict::Safe)
                        return false;
                }
            }
        }
        return true;
    }
    Potential Guide(const Candidate& candidate,bool xFirst)
    {
        // The shared key already computes original-material centroids with
        // holes removed. Its final Y/X terms provide a private gravity guide.
        if (xFirst) return {std::get<13>(candidate.Key),std::get<12>(candidate.Key),candidate.State.Checked.UsedEnvelopeArea};
        return {std::get<12>(candidate.Key),std::get<13>(candidate.Key),candidate.State.Checked.UsedEnvelopeArea};
    }
    bool SamePose(const Candidate& a,const Candidate& b,const Identity& identity)
    {
        const auto first=Find(a.State.Output,identity),second=Find(b.State.Output,identity);
        if (!first||!second) return false;
        const auto x=a.State.Output.Layouts[first->Board].Placements[first->Piece].Transform;
        const auto y=b.State.Output.Layouts[second->Board].Placements[second->Piece].Transform;
        return x.X==y.X&&x.Y==y.Y&&x.AngleDegrees==y.AngleDegrees&&x.Mirrored==y.Mirrored;
    }

    class RotationCompaction final:public IPostProcessor
    {
        RotationCompactionSettings Settings;
    public:
        explicit RotationCompaction(RotationCompactionSettings settings):Settings(settings) {}
        Result Run(const Problem& input,const Result& source,const Options& options) override
        {
            const auto started=Clock::now();
            Statistics work;
            work.VerificationSeconds=work.EvaluationSeconds=0;
            Parts parts;
            Candidate best,working;
            bool bestChanged=false,cancelled=false,callbackFailed=false;
            bool workingSafe=false;
            std::size_t evaluated=0,bestUpdates=0,workingMoves=0,settleCalls=0,passes=0;
            std::size_t angleLimitedParts=0;
            const auto elapsed=[&] { return std::chrono::duration<double>(Clock::now()-started).count(); };
            const auto pollCancel=[&]
            {
                if (cancelled||callbackFailed) return true;
                try { if (options.Cancel&&options.Cancel()) cancelled=true; }
                catch (...) { callbackFailed=true; }
                return cancelled||callbackFailed;
            };
            const auto lifeStop=[&]
            {
                return pollCancel()||!std::isfinite(options.TimeLimitSeconds)||
                    options.TimeLimitSeconds<=0||elapsed()>=options.TimeLimitSeconds;
            };
            const auto stop=[&] { return lifeStop()||work.PlacementAttempts>=options.MaxPlacementAttempts; };
            const auto finish=[&](const std::string& reason)
            {
                Result result=bestChanged?best.State.Output:source;
                if (cancelled) result.State=Status::Cancelled;
                result.Stats=work;
                result.Stats.CombinationGroups=source.Stats.CombinationGroups;
                result.Stats.CircleLatticeGroups=source.Stats.CircleLatticeGroups;
                result.Stats.GapFilledParts=source.Stats.GapFilledParts;
                result.Stats.ElapsedSeconds=result.Stats.PostProcessingSeconds=elapsed();
                std::ostringstream diagnostic;
                diagnostic<<"compact.rotation.v1 stop="<<(callbackFailed?"callback-failed":cancelled?"cancelled":reason)
                    <<" passes="<<passes<<" evaluated="<<evaluated<<" best-updates="<<bestUpdates
                    <<" working-moves="<<workingMoves<<" settle-calls="<<settleCalls
                    <<" angle-limited-parts="<<angleLimitedParts
                    <<" attempts="<<work.PlacementAttempts<<" original-material=1 shared-quality=1";
                result.Diagnostics.push_back(diagnostic.str());
                return result;
            };
            const auto assess=[&](Result result)->std::optional<Candidate>
            {
                if (lifeStop()) return {};
                Candidate candidate;
                candidate.State.Output=std::move(result);
                candidate.State.Output.Stats={};
                candidate.State.Output.Stats.VerificationSeconds=candidate.State.Output.Stats.EvaluationSeconds=0;
                Detail::AssessCandidate(input,candidate.State);
                work.VerificationSeconds+=candidate.State.Output.Stats.VerificationSeconds;
                work.EvaluationSeconds+=candidate.State.Output.Stats.EvaluationSeconds;
                ++evaluated;
                if (!Detail::Eligible(candidate.State)) return {};
                candidate.Key=Detail::Quality(candidate.State,input,options.Ranking);
                candidate.Guide=Guide(candidate,options.BasicCompactionXFirst);
                return candidate;
            };
            const auto consider=[&](const Candidate& candidate)
            {
                if (candidate.Key<best.Key)
                {
                    best=candidate;bestChanged=true;++bestUpdates;
                }
            };
            const auto settle=[&](const Candidate& candidate,std::size_t slots,double secondsCap)->std::optional<Candidate>
            {
                if (!Settings.SettleWithAxis||stop()) return {};
                const auto& output=candidate.State.Output;
                if (const auto report=GetAxisCompactionReport(input,output);report&&report->Closed) return {};
                Options local=options;
                local.TimeLimitSeconds=std::min(secondsCap,std::max(0.,options.TimeLimitSeconds-elapsed())/
                    double(std::max<std::size_t>(1,slots)));
                if (local.TimeLimitSeconds<=0) return {};
                const auto attempts=options.MaxPlacementAttempts-work.PlacementAttempts;
                local.MaxPlacementAttempts=std::min(attempts,std::max<std::size_t>(1,attempts/std::max<std::size_t>(1,slots)));
                local.EnableBasicCompaction=false;
                local.Cancel=pollCancel;
                AxisCompactionSettings axisSettings;
                axisSettings.XFirst=options.BasicCompactionXFirst;
                auto result=MakeAxisCompactionStage(axisSettings).Factory()->Run(input,output,local);
                ++settleCalls;
                work.PlacementAttempts+=std::min(result.Stats.PlacementAttempts,
                    options.MaxPlacementAttempts-work.PlacementAttempts);
                if (result.Stats.VerificationSeconds>=0) work.VerificationSeconds+=result.Stats.VerificationSeconds;
                if (result.Stats.EvaluationSeconds>=0) work.EvaluationSeconds+=result.Stats.EvaluationSeconds;
                if (result.State==Status::Cancelled&&!callbackFailed) cancelled=true;
                return assess(std::move(result));
            };
            try
            {
                if (stop()) return finish("budget");
                if (source.State!=Status::Complete&&source.State!=Status::Partial) return finish("ineligible-incumbent");
                auto initial=assess(source);
                if (!initial) return finish("invalid-or-interrupted-incumbent");
                best=working=std::move(*initial);
                for (const auto& part:input.Parts) parts.emplace(part.ID,&part);
                workingSafe=SafeLayout(input,working.State.Output,parts,lifeStop);
                std::vector<Identity> order;
                for (const auto& board:working.State.Output.Layouts) for (const auto& piece:board.Placements)
                    order.push_back({board.StockID,piece.PartID,board.StockInstance,piece.Instance});
                work.PreparationSeconds=elapsed();
                if (auto normalized=settle(working,order.size()*2+1,options.TimeLimitSeconds))
                {
                    consider(*normalized);
                    if (normalized->Guide<working.Guide)
                    {
                        working=std::move(*normalized);++workingMoves;
                        workingSafe=SafeLayout(input,working.State.Output,parts,lifeStop);
                    }
                }
                for (std::size_t pass=0;pass<Settings.MaxPasses&&!stop();++pass)
                {
                    bool changed=false;
                    const double step=std::max(Settings.MinimumAngleStepDegrees,
                        Settings.InitialAngleStepDegrees*std::pow(.2,double(pass)));
                    for (std::size_t entry=0;entry<order.size()&&!stop();++entry)
                    {
                        const auto tasksLeft=(Settings.MaxPasses-pass-1)*order.size()+order.size()-entry;
                        const double partStarted=elapsed();
                        const double partSeconds=std::max(0.,options.TimeLimitSeconds-partStarted)/
                            double(std::max<std::size_t>(1,tasksLeft));
                        const double proposalDeadline=partStarted+partSeconds*.6;
                        const double partDeadline=partStarted+partSeconds;
                        const auto attemptsLeft=options.MaxPlacementAttempts-work.PlacementAttempts;
                        const auto proposalAttemptEnd=work.PlacementAttempts+
                            std::max<std::size_t>(1,attemptsLeft/std::max<std::size_t>(1,tasksLeft));
                        const auto pieceStop=[&]
                        {
                            return stop()||elapsed()>=proposalDeadline||work.PlacementAttempts>=proposalAttemptEnd;
                        };
                        const auto& identity=order[entry];
                        const auto location=Find(working.State.Output,identity);
                        if (!location) throw std::logic_error("Rotation compaction lost an original identity");
                        const auto base=working;
                        const auto& board=base.State.Output.Layouts[location->Board];
                        const auto& item=board.Placements[location->Piece];
                        const auto& part=*parts.at(item.PartID);
                        if (part.Geometry.Radius>0||(Settings.FiniteAnglesOnly&&part.FreeRotation)) continue;
                        bool angleLimited=false;
                        const auto angles=Angles(input,board,location->Piece,parts,Settings,step,pass,angleLimited,pieceStop);
                        if (angleLimited) ++angleLimitedParts;
                        const auto pivots=Pivots(input,board,location->Piece,parts,Settings.MaxPivotsPerPart,pieceStop);
                        work.CandidatePatterns+=angles.size();
                        std::optional<Candidate> qualityCandidate,potentialCandidate;
                        const auto local=Detail::Bounds(part.Geometry);
                        const auto previousBox=Detail::Bounds(part.Geometry,item.Transform);
                        const Point localCenter{local.X+local.Width*.5,local.Y+local.Height*.5};
                        const auto fixedCenter=Detail::Transform(localCenter,item.Transform);
                        std::vector<Pose> proposals;
                        const auto addProposal=[&](Pose pose)
                        {
                            if (std::none_of(proposals.begin(),proposals.end(),[&](const Pose& old)
                                { return old.X==pose.X&&old.Y==pose.Y&&old.AngleDegrees==pose.AngleDegrees&&old.Mirrored==pose.Mirrored; }))
                                proposals.push_back(pose);
                        };
                        for (const double angle:angles)
                        {
                            if (pieceStop()) break;
                            const Pose orientation{0,0,angle,item.Transform.Mirrored};
                            const auto center=Detail::Transform(localCenter,orientation);
                            addProposal({fixedCenter.X-center.X,fixedCenter.Y-center.Y,angle,item.Transform.Mirrored});
                            const auto rotatedBox=Detail::Bounds(part.Geometry,orientation);
                            addProposal({previousBox.X-rotatedBox.X,previousBox.Y-rotatedBox.Y,angle,item.Transform.Mirrored});
                            for (const auto& pivot:pivots)
                            {
                                const auto rotated=Detail::Transform(pivot.Local,orientation);
                                addProposal({pivot.World.X-rotated.X,pivot.World.Y-rotated.Y,angle,item.Transform.Mirrored});
                            }
                        }
                        for (const auto proposal:proposals)
                        {
                            if (pieceStop()) break;
                            const auto geometry=AxisSafety::Make(part.Geometry,proposal);
                            const double magnitude=std::max({1.,std::abs(item.Transform.X),std::abs(item.Transform.Y),
                                std::abs(proposal.X),std::abs(proposal.Y),std::abs(local.X),std::abs(local.Y),
                                std::abs(local.X+local.Width),std::abs(local.Y+local.Height),
                                std::abs(previousBox.X),std::abs(previousBox.Y),
                                std::abs(previousBox.X+previousBox.Width),std::abs(previousBox.Y+previousBox.Height)});
                            const double retreat=std::max(geometry.Roundoff*2,
                                8*std::numeric_limits<double>::epsilon()*magnitude);
                            const std::array<Point,9> offsets{{{0,0},{retreat,0},{0,retreat},
                                {-retreat,0},{0,-retreat},{retreat,retreat},{retreat,-retreat},
                                {-retreat,retreat},{-retreat,-retreat}}};
                            for (const auto offset:offsets)
                            {
                                if (pieceStop()) break;
                                auto trial=base.State.Output;
                                auto& pose=trial.Layouts[location->Board].Placements[location->Piece].Transform;
                                pose=proposal;pose.X+=offset.X;pose.Y+=offset.Y;
                                ++work.PlacementAttempts;
                                const auto verifyStarted=Clock::now();
                                const bool safe=SafeLayout(input,trial,parts,lifeStop,
                                    workingSafe?location:std::optional<Position>{});
                                work.VerificationSeconds+=std::chrono::duration<double>(Clock::now()-verifyStarted).count();
                                if (!safe) continue;
                                auto candidate=assess(std::move(trial));
                                if (!candidate) continue;
                                consider(*candidate);
                                if (!qualityCandidate||candidate->Key<qualityCandidate->Key) qualityCandidate=*candidate;
                                if (!potentialCandidate||candidate->Guide<potentialCandidate->Guide) potentialCandidate=std::move(*candidate);
                                // One proven numerical retreat represents this registered pose.
                                break;
                            }
                        }
                        std::vector<Candidate> finalists;
                        if (qualityCandidate) finalists.push_back(std::move(*qualityCandidate));
                        if (potentialCandidate&&(finalists.empty()||!SamePose(finalists.front(),*potentialCandidate,identity)))
                            finalists.push_back(std::move(*potentialCandidate));
                        for (auto& candidate:finalists)
                        {
                            if (!stop()) if (auto normalized=settle(candidate,tasksLeft*2,
                                std::max(0.,partDeadline-elapsed())))
                            {
                                consider(*normalized);
                                if (normalized->Guide<candidate.Guide||normalized->Key<candidate.Key) candidate=std::move(*normalized);
                            }
                            if (candidate.Guide<working.Guide)
                            {
                                working=std::move(candidate);workingSafe=true;++workingMoves;changed=true;
                            }
                        }
                    }
                    ++passes;
                    // Smaller free-angle steps still deserve their next pass
                    // even when the current coarse step cannot improve a fit.
                    if (!changed&&step<=Settings.MinimumAngleStepDegrees) return finish("no-improvement");
                }
                return finish(stop()?"budget":"pass-limit");
            }
            catch (const std::exception& error)
            {
                auto result=finish("failed-safely");
                result.Diagnostics.push_back(std::string("compact.rotation failure: ")+error.what());
                return result;
            }
            catch (...)
            {
                auto result=finish("failed-safely");
                result.Diagnostics.push_back("compact.rotation failure: unknown exception");
                return result;
            }
        }
    };
}

PostProcessStage MakeRotationCompactionStage(RotationCompactionSettings settings)
{
    if (!settings.MaxPasses||settings.MaxPasses>64||!settings.MaxAnglesPerPart||settings.MaxAnglesPerPart>1024||
        !settings.MaxPivotsPerPart||settings.MaxPivotsPerPart>256||
        !std::isfinite(settings.InitialAngleStepDegrees)||settings.InitialAngleStepDegrees<=0||settings.InitialAngleStepDegrees>180||
        !std::isfinite(settings.MinimumAngleStepDegrees)||settings.MinimumAngleStepDegrees<=0||
        settings.MinimumAngleStepDegrees>settings.InitialAngleStepDegrees)
        throw std::invalid_argument("Invalid bounded rotation compaction settings");
    return {"compact.rotation",[settings] { return std::make_unique<RotationCompaction>(settings); }};
}
}
