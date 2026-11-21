#include "NestingInternal.h"
#include "NestingRectangleDirections.h"
#include "NestingAxisSafety.h"
#include "NestingTrace.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <optional>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace iCAX::SheetNesting::Detail
{
namespace
{
    // Every interval is [Left, Right]. Height is the conservative occupied
    // envelope below it. Stored cavities are disjoint from that envelope's
    // actual material and from each other; they are not holes in input shapes.
    struct SkylineSpan { double Left, Right, Height; };
    struct SkylineBin
    {
        std::size_t StockIndex;
        Layout Output;
        double Width, Height;
        std::vector<SkylineSpan> Profile;
        std::vector<Rect> Cavities;
        std::vector<AxisSafety::Geometry> OriginalMaterial;
        std::uint64_t TraceStateID=0;
        std::size_t TraceBoardIndex=0;
    };
    struct SkylineChoice
    {
        std::size_t Direction;
        Rect Footprint;
        std::optional<std::size_t> Cavity;
        std::shared_ptr<const AxisSafety::Geometry> OriginalMaterial;
        std::uint64_t TraceID=0;
    };
    auto ChoiceRank(const SkylineChoice& c)
    {
        return std::tuple(c.Footprint.Y + c.Footprint.Height,
                          c.Footprint.X, c.Footprint.Y, c.Direction);
    }

    RectangleDirectionSet EnvelopeDirections(const Part& part,double tolerance,double guard,OrientationProposalPolicy policy,
        const std::function<bool()>& stop)
    {
        RectangleDirectionSet output;output.Area=Area(part.Geometry);
        for(const auto orientation:CandidateOrientations(part,tolerance,policy))
        {
            if(stop()){output.Interrupted=true;break;}
            const auto box=Bounds(part.Geometry,orientation);
            if(!std::isfinite(box.Width)||!std::isfinite(box.Height)||box.Width<=0||box.Height<=0)continue;
            // Planning envelopes only. Original rings, holes, analytic circles
            // and each allowed direction remain unchanged in the real problem.
            output.Directions.push_back({{guard-box.X,guard-box.Y,orientation.AngleDegrees,orientation.Mirrored},
                box.Width+2*guard,box.Height+2*guard});
        }
        output.Supported=!output.Directions.empty();return output;
    }

    void MergeCavities(std::vector<Rect>& cavities, const std::function<bool()>& stop)
    {
        // Merge only exact rectangular unions. Approximate coordinate matching
        // could cover occupied slivers, so tolerance never enlarges a cavity.
        bool changed = true;
        while (changed && !stop())
        {
            changed = false;
            for (bool vertical : {false, true})
            {
                if (stop()) return;
                std::sort(cavities.begin(), cavities.end(), [=](const Rect& a, const Rect& b)
                {
                    return vertical ? std::tie(a.X, a.Width, a.Y) < std::tie(b.X, b.Width, b.Y)
                                    : std::tie(a.Y, a.Height, a.X) < std::tie(b.Y, b.Height, b.X);
                });
                std::vector<Rect> joined;
                joined.reserve(cavities.size());
                for (std::size_t i = 0; i < cavities.size(); ++i)
                {
                    if ((i & 63) == 0 && stop()) return;
                    const auto r = cavities[i];
                    if (!joined.empty())
                    {
                        auto& old = joined.back();
                        if (vertical && old.X == r.X && old.Width == r.Width && old.Y + old.Height == r.Y)
                        { old.Height += r.Height; changed = true; continue; }
                        if (!vertical && old.Y == r.Y && old.Height == r.Height && old.X + old.Width == r.X)
                        { old.Width += r.Width; changed = true; continue; }
                    }
                    joined.push_back(r);
                }
                cavities = std::move(joined);
            }
        }
    }

    void AddToProfile(SkylineBin& bin, const Rect& occupied, bool recoverCavities)
    {
        std::vector<SkylineSpan> next;
        next.reserve(bin.Profile.size() + 2);
        const double left = occupied.X, right = occupied.X + occupied.Width;
        const double top = occupied.Y + occupied.Height;
        const auto append = [&](double a, double b, double height)
        {
            if (b <= a) return;
            if (!next.empty() && next.back().Right == a && next.back().Height == height)
                next.back().Right = b;
            else next.push_back({a, b, height});
        };
        for (const auto& span : bin.Profile)
        {
            const double a = std::max(left, span.Left), b = std::min(right, span.Right);
            if (b <= a) { append(span.Left, span.Right, span.Height); continue; }
            append(span.Left, a, span.Height);
            if (recoverCavities && occupied.Y > span.Height)
                bin.Cavities.push_back({a, span.Height, b - a, occupied.Y - span.Height});
            append(a, b, top);
            append(b, span.Right, span.Height);
        }
        bin.Profile = std::move(next);
    }

    void UseCavity(SkylineBin& bin, std::size_t index, const Rect& occupied)
    {
        const auto free = bin.Cavities[index];
        bin.Cavities.erase(bin.Cavities.begin() + static_cast<std::ptrdiff_t>(index));
        // A nonoverlapping two-rectangle partition of the unused portion.
        // Keeping the larger residual dimension intact helps subsequent fits.
        const double dw = free.Width - occupied.Width, dh = free.Height - occupied.Height;
        const auto add = [&](Rect r) { if (r.Width > 0 && r.Height > 0) bin.Cavities.push_back(r); };
        if (dw > dh)
        {
            add({free.X + occupied.Width, free.Y, dw, free.Height});
            add({free.X, free.Y + occupied.Height, occupied.Width, dh});
        }
        else
        {
            add({free.X + occupied.Width, free.Y, dw, occupied.Height});
            add({free.X, free.Y + occupied.Height, free.Width, dh});
        }
    }
}

static Result RunSkyline(const Problem& problem, const Options& options,bool envelopes,bool nominalBoundary=false)
{
    Result result;
    const auto begin = std::chrono::steady_clock::now();
    const auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count(); };
    bool cancelled = false, exhausted = false;
    const auto controlStop = [&]
    {
        if (cancelled) return true;
        if (options.Cancel && options.Cancel()) { cancelled = true; return true; }
        if (elapsed() >= options.TimeLimitSeconds)
        { exhausted = true; return true; }
        return false;
    };
    const std::function<bool()> stop = [&]
    {
        if(controlStop()||exhausted)return true;
        if(result.Stats.PlacementAttempts>=options.MaxPlacementAttempts){exhausted=true;return true;}
        return false;
    };
    std::vector<std::size_t> consumed(problem.Parts.size()), stockUsed(problem.Stocks.size());
    std::unordered_map<std::string,std::size_t> byID;
    double coordinateScale=1;
    for(const auto& stock:problem.Stocks)coordinateScale=std::max({coordinateScale,stock.Width,stock.Height});
    for(std::size_t i=0;i<problem.Parts.size();++i)
    {
        byID.emplace(problem.Parts[i].ID,i);
        const auto& shape=problem.Parts[i].Geometry;
        coordinateScale=std::max({coordinateScale,shape.Radius,std::abs(shape.Center.X)+std::abs(shape.Center.Y)});
        const auto account=[&](const auto& ring){for(const auto point:ring)
            coordinateScale=std::max(coordinateScale,std::abs(point.X)+std::abs(point.Y));};
        account(shape.Outer);for(const auto& hole:shape.Holes)account(hole);
    }
    const double guard=envelopes&&!nominalBoundary?std::max(4*problem.Tolerance,
        1024*std::numeric_limits<double>::epsilon()*coordinateScale):0;
    std::uint64_t nextCandidate=0,nextState=0,step=0;
    std::uint64_t cavityComparisons=0,cavityPruned=0,skylineComparisons=0,skylinePruned=0,rankPruned=0;
    const auto observe=[&](ConstructionTraceStage stage,auto&& build)
    {
        if(!envelopes)return;
        ObserveConstruction(options,stage,[&](ConstructionTraceEvent& event)
        {event.Step=++step;event.Source="envelope.skyline";event.ElapsedSeconds=elapsed();
            event.Attempts=result.Stats.PlacementAttempts;build(event);});
    };
    observe(ConstructionTraceStage::Begin,[&](ConstructionTraceEvent& event)
    {event.Reason="independent-original-demand-envelope-construction";event.Measures={{"guard",guard},{"part-gap",problem.PartGap},{"edge-margin",problem.EdgeMargin},
        {"orientationProposalPolicy",static_cast<double>(options.OrientationProposals)}};});
    std::vector<RectangleDirectionSet> descriptions(problem.Parts.size());
    for (std::size_t i = 0; i < problem.Parts.size() && !stop(); ++i)
    {
        if (!problem.Parts[i].Quantity) continue;
        descriptions[i] = envelopes?EnvelopeDirections(problem.Parts[i],problem.Tolerance,guard,options.OrientationProposals,stop):
            FindRectangleDirections(problem.Parts[i], problem.Tolerance, stop);
        result.Stats.CandidatePatterns += descriptions[i].Directions.size();
    }
    result.Stats.PreparationSeconds = elapsed();
    std::vector<SkylineBin> bins;

    const auto find = [&](const SkylineBin& bin, std::size_t partIndex) -> std::optional<SkylineChoice>
    {
        if (problem.Stocks[bin.StockIndex].Material != problem.Parts[partIndex].Material) return {};
        const auto& directions = descriptions[partIndex].Directions;
        std::optional<SkylineChoice> best;
        const auto consider = [&](std::size_t di, double x, double y, std::optional<std::size_t> cavity)
        {
            const auto& d = directions[di];
            SkylineChoice candidate{di, {x, y, d.Width + problem.PartGap, d.Height + problem.PartGap}, cavity};
            const Pose pose{x+problem.EdgeMargin+d.Offset.X,y+problem.EdgeMargin+d.Offset.Y,d.Offset.AngleDegrees,d.Offset.Mirrored};
            if(best&&!(ChoiceRank(candidate)<ChoiceRank(*best))){++rankPruned;return;}
            // Width/height filtering and dominated skyline ranks are cheap
            // planning comparisons, not generated original-part positions.
            // Charge the shared placement lease exactly when a real proposal
            // enters the original geometry gate and construction trace.
            if(stop())return;
            ++result.Stats.PlacementAttempts;
            const auto id=envelopes?++nextCandidate:0;
            candidate.TraceID=id;
            observe(ConstructionTraceStage::Generate,[&](ConstructionTraceEvent& event)
            {event.CandidateID=id;event.PartIndex=partIndex;event.StockIndex=bin.StockIndex;event.StockInstance=bin.Output.StockInstance;
                event.StateID=bin.TraceStateID;event.BoardIndex=bin.TraceBoardIndex;
                event.BoardWidth=bin.Output.Width;event.BoardHeight=bin.Output.Height;event.Width=d.Width;event.Height=d.Height;
                event.Members={{partIndex,std::numeric_limits<std::size_t>::max(),pose,0}};event.Reason="skyline-or-cavity-proposal";});
            bool safe=InsideSheet(problem.Parts[partIndex].Geometry,pose,
                bin.Output.Width,bin.Output.Height,problem.EdgeMargin,problem.Tolerance);
            if(envelopes&&safe)
            {
                auto material=std::make_shared<const AxisSafety::Geometry>(AxisSafety::Make(problem.Parts[partIndex].Geometry,pose));
                safe=AxisSafety::Containment(*material,bin.Output.Width,bin.Output.Height,problem.EdgeMargin)==AxisSafety::Verdict::Safe;
                for(std::size_t fixed=0;safe&&fixed<bin.Output.Placements.size();++fixed)
                {
                    // The final leased attempt may finish its own original
                    // checks; it cannot start another candidate. Time and
                    // cancellation still stop this gate between source pairs.
                    if(controlStop()){safe=false;break;}
                    const auto& placed=bin.Output.Placements[fixed];
                    const auto& other=bin.OriginalMaterial[fixed];
                    safe=AxisSafety::EnvelopesSeparated(*material,other,AxisSafety::Clearance(*material,other,problem.PartGap))||
                        (!ShapesConflict(problem.Parts[partIndex].Geometry,pose,problem.Parts[byID.at(placed.PartID)].Geometry,
                            placed.Transform,problem.PartGap,problem.Tolerance,problem.AllowCommonEdges)&&
                            AxisSafety::Pair(*material,other,problem.PartGap,problem.AllowCommonEdges,problem.Tolerance)==AxisSafety::Verdict::Safe);
                }
                if(safe)candidate.OriginalMaterial=std::move(material);
            }
            // A new/empty board has no fixed-member loop to poll controls.
            // Never publish a proposal whose final source check outlived the
            // wall deadline or whose observer requested cancellation.
            if(safe&&controlStop())safe=false;
            observe(ConstructionTraceStage::Filter,[&](ConstructionTraceEvent& event)
            {event.CandidateID=id;event.PartIndex=partIndex;event.StateID=bin.TraceStateID;event.BoardIndex=bin.TraceBoardIndex;
                event.StockIndex=bin.StockIndex;event.StockInstance=bin.Output.StockInstance;
                event.Accepted=safe;event.Reason=safe?"all-original-material-and-margin-safe":"original-check-or-budget-rejected";});
            if(!safe)return;
            observe(ConstructionTraceStage::Score,[&](ConstructionTraceEvent& event)
            {event.CandidateID=id;event.PartIndex=partIndex;event.StateID=bin.TraceStateID;event.BoardIndex=bin.TraceBoardIndex;
                event.StockIndex=bin.StockIndex;event.StockInstance=bin.Output.StockInstance;
                event.Accepted=true;event.Reason="lower-top-then-left-skyline-rank";
                TraceTuple(event,ChoiceRank(candidate),{"top","x","y","direction"});});
            if (!best || ChoiceRank(candidate) < ChoiceRank(*best)) best = candidate;
        };
        // Cavities are considered before raising the skyline. No input part is
        // reordered; only the placement of the current original instance varies.
        if (options.EnableGapFill)
        {
            for (std::size_t ci = 0; ci < bin.Cavities.size() && !stop(); ++ci)
            {
                const auto& free = bin.Cavities[ci];
                for (std::size_t di = 0; di < directions.size() && !stop(); ++di)
                {
                    const auto& d = directions[di];
                    ++cavityComparisons;
                    // Cavities use strict bounds: tolerances cannot let a part
                    // protrude into the surrounding already occupied material.
                    if (d.Width + problem.PartGap <= free.Width && d.Height + problem.PartGap <= free.Height)
                        consider(di, free.X, free.Y, ci);
                    else ++cavityPruned;
                }
            }
            if (best || stop()) return best;
        }
        for (std::size_t di = 0; di < directions.size() && !stop(); ++di)
        {
            const auto& direction = directions[di];
            const double width = direction.Width + problem.PartGap, height = direction.Height + problem.PartGap;
            if (width > bin.Width + problem.Tolerance || height > bin.Height + problem.Tolerance) continue;
            std::vector<double> xs;
            xs.reserve(bin.Profile.size() * 2);
            for (const auto& span : bin.Profile)
            {
                if (stop()) break;
                if (span.Left + width <= bin.Width + problem.Tolerance) xs.push_back(span.Left);
                if (span.Right >= width) xs.push_back(span.Right - width);
            }
            std::sort(xs.begin(), xs.end());
            xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
            for (double x : xs)
            {
                if (stop()) break;
                double y = 0;
                for (const auto& span : bin.Profile)
                    if (span.Left < x + width && span.Right > x) y = std::max(y, span.Height);
                ++skylineComparisons;
                if (y + height <= bin.Height + problem.Tolerance) consider(di, x, y, {});
                else ++skylinePruned;
            }
        }
        return best;
    };
    const auto commit = [&](SkylineBin& bin, std::size_t partIndex, const SkylineChoice& choice)
    {
        const auto& part = problem.Parts[partIndex];
        const auto& d = descriptions[partIndex].Directions[choice.Direction];
        const auto& r = choice.Footprint;
        bin.Output.Placements.push_back({part.ID, consumed[partIndex]++,
            {r.X + problem.EdgeMargin + d.Offset.X, r.Y + problem.EdgeMargin + d.Offset.Y, d.Offset.AngleDegrees,d.Offset.Mirrored},
            bin.Output.Placements.size() + 1, choice.Cavity.has_value()});
        bin.Output.MaterialArea += descriptions[partIndex].Area;
        if(envelopes)bin.OriginalMaterial.push_back(*choice.OriginalMaterial);
        if (choice.Cavity) { UseCavity(bin, *choice.Cavity, r); ++result.Stats.GapFilledParts; }
        else AddToProfile(bin, r, options.EnableGapFill);
        if (options.EnableGapFill && !bin.Cavities.empty()) MergeCavities(bin.Cavities, stop);
        const auto parent=bin.TraceStateID;
        if(envelopes&&options.ConstructionTrace)bin.TraceStateID=++nextState;
        observe(ConstructionTraceStage::Commit,[&](ConstructionTraceEvent& event)
        {const auto& placed=bin.Output.Placements.back();event.CandidateID=choice.TraceID;event.PartIndex=partIndex;event.StockInstance=bin.Output.StockInstance;
            event.StateID=bin.TraceStateID;event.ParentStateID=parent;event.BoardIndex=bin.TraceBoardIndex;event.StockIndex=bin.StockIndex;
            event.BoardWidth=bin.Output.Width;event.BoardHeight=bin.Output.Height;
            event.Members={{partIndex,placed.Instance,placed.Transform,placed.Group}};event.Layouts={bin.Output};
            event.Reason="original-instance-committed";});
    };

    for (std::size_t partIndex = 0; partIndex < problem.Parts.size() && !stop(); ++partIndex)
    {
        const auto& part = problem.Parts[partIndex];
        if (!descriptions[partIndex].Supported) continue;
        while (consumed[partIndex] < part.Quantity && !stop())
        {
            bool placed = false;
            for (auto& bin : bins)
            {
                if (stop()) break;
                const auto fit = find(bin, partIndex);
                if (fit && !controlStop()) { commit(bin, partIndex, *fit); placed = true; break; }
            }
            if (placed) continue;
            if (stop()) break;
            // Stock selection is also deterministic. A physical sheet is only
            // consumed after a fitting original part has actually been placed.
            for (std::size_t si = 0; si < problem.Stocks.size() && !stop(); ++si)
            {
                const auto& stock = problem.Stocks[si];
                if (stockUsed[si] >= stock.Quantity || stock.Material != part.Material) continue;
                const double w = stock.Width - 2 * problem.EdgeMargin;
                const double h = stock.Height - 2 * problem.EdgeMargin;
                if (w <= 0 || h <= 0) continue;
                SkylineBin bin{si, {stock.ID, stockUsed[si], stock.Width, stock.Height, {}, 0},
                    w + problem.PartGap, h + problem.PartGap, {{0, w + problem.PartGap, 0}}, {}};
                bin.TraceBoardIndex=bins.size();
                if (const auto fit = find(bin, partIndex); fit && !controlStop())
                {
                    commit(bin, partIndex, *fit);
                    ++stockUsed[si]; bins.push_back(std::move(bin)); placed = true; break;
                }
            }
            if (!placed) break; // Later input types may still fit this finite inventory.
        }
    }
    for (auto& bin : bins) if (!bin.Output.Placements.empty())
    {
        observe(ConstructionTraceStage::Complete,[&](ConstructionTraceEvent& event)
        {
            event.Source="board.raw-construction";event.Reason="actual-envelope-board-construction-complete";
            event.StateID=bin.TraceStateID;event.BoardIndex=bin.TraceBoardIndex;
            event.StockIndex=bin.StockIndex;event.StockInstance=bin.Output.StockInstance;
            event.BoardWidth=bin.Output.Width;event.BoardHeight=bin.Output.Height;event.Layouts={bin.Output};
        });
        result.Layouts.push_back(std::move(bin.Output));
    }
    for (std::size_t i = 0; i < problem.Parts.size(); ++i)
        if (consumed[i] < problem.Parts[i].Quantity)
            result.Remaining.push_back({problem.Parts[i].ID, problem.Parts[i].Quantity - consumed[i]});
    result.State = cancelled ? Status::Cancelled : result.Remaining.empty() ? Status::Complete : Status::Partial;
    result.Stats.PlacementSeconds = std::max(0.0, elapsed() - result.Stats.PreparationSeconds);
    if(envelopes)
    {
        const double verifyStarted=elapsed();
        const auto checked=Validate(problem,result);
        result.Stats.VerificationSeconds=elapsed()-verifyStarted;
        result.GeometryValidated=checked.Valid;
        if(!checked.Valid){result.State=Status::Failed;result.Diagnostics.insert(result.Diagnostics.end(),checked.Errors.begin(),checked.Errors.end());}
        result.Diagnostics.push_back("plane.envelope-initial: original-demand skyline/cavity construction; conservative private envelopes; guard="+
            std::to_string(guard)+"; ordinary original gap and stock inventory preserved; no completed-layout refinement");
        result.Diagnostics.push_back("plane.envelope-planning: cavity-size-comparisons="+std::to_string(cavityComparisons)+
            " cavity-size-pruned="+std::to_string(cavityPruned)+" skyline-height-comparisons="+std::to_string(skylineComparisons)+
            " skyline-height-pruned="+std::to_string(skylinePruned)+" rank-pruned="+std::to_string(rankPruned)+
            " original-proposals="+std::to_string(result.Stats.PlacementAttempts));
        observe(ConstructionTraceStage::Complete,[&](ConstructionTraceEvent& event)
        {event.Layouts=result.Layouts;event.Remaining=result.Remaining;event.ResultStatus=result.State;event.Accepted=checked.Valid;
            event.Measures={{"cavitySizeComparisons",static_cast<double>(cavityComparisons)},
                {"cavitySizePruned",static_cast<double>(cavityPruned)},
                {"skylineHeightComparisons",static_cast<double>(skylineComparisons)},
                {"skylineHeightPruned",static_cast<double>(skylinePruned)},
                {"rankPruned",static_cast<double>(rankPruned)},
                {"originalProposals",static_cast<double>(result.Stats.PlacementAttempts)}};
            event.Reason=checked.Complete?"all-original-demand-complete":"construction-budget-or-available-stock-ended";});
    }
    if (cancelled) result.Diagnostics.push_back("Rectangle skyline cancelled; original placed and remaining instances retained.");
    if (exhausted) result.Diagnostics.push_back("Rectangle skyline time or placement-attempt budget exhausted.");
    if (!cancelled && !exhausted && !result.Remaining.empty())
        result.Diagnostics.push_back("Input-order rectangle skyline left unmet demand; unsupported shapes and greedy failures are not infeasibility proofs.");
    result.Stats.ElapsedSeconds=elapsed();
    return result;
}
Result RunRectangleSkyline(const Problem& problem,const Options& options)
{return RunSkyline(problem,options,false);}
Result RunEnvelopeSkyline(const Problem& problem,const Options& options)
{
    const auto started=std::chrono::steady_clock::now();
    if(const auto errors=CheckProblem(problem);!errors.empty())
    {Result result;result.State=Status::InvalidInput;result.Diagnostics=errors;
        result.Stats.ElapsedSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();return result;}
    const double inputVerification=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    auto guardedOptions=options;guardedOptions.ConstructionTraceScope+="/guarded-envelope";
    guardedOptions.TimeLimitSeconds=std::max(0.,options.TimeLimitSeconds-inputVerification);
    auto result=RunSkyline(problem,guardedOptions,true);
    const auto observeSelection=[&](const std::string& scope,bool nominal)
    {
        ObserveConstruction(options,ConstructionTraceStage::Selected,[&](ConstructionTraceEvent& event)
        {
            event.Source="strategy.construction-selection";event.Scope=scope;
            event.Layouts=result.Layouts;event.Remaining=result.Remaining;event.ResultStatus=result.State;
            event.ElapsedSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
            event.Attempts=result.Stats.PlacementAttempts;event.Accepted=true;
            event.Reason=nominal?"actual-selected-nominal-boundary-envelope-trial":"actual-selected-guarded-envelope-trial";
            event.Measures={{"nominalBoundaryTrial",nominal?1.:0.}};
        });
    };
    result.Stats.VerificationSeconds=std::max(0.,result.Stats.VerificationSeconds)+inputVerification;
    result.Stats.ElapsedSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    if(result.State==Status::Cancelled||(result.State==Status::Complete&&result.Layouts.size()<=1))
    {observeSelection(guardedOptions.ConstructionTraceScope,false);return result;}
    // A conservative planning box must not reject a genuinely exact board fit.
    // Start another independent construction from original demand, never from
    // the first layout. Every nominal candidate retains the same strict gates.
    auto boundaryOptions=options;
    boundaryOptions.TimeLimitSeconds=std::max(0.,options.TimeLimitSeconds-
        std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());
    boundaryOptions.MaxPlacementAttempts=options.MaxPlacementAttempts-std::min(options.MaxPlacementAttempts,result.Stats.PlacementAttempts);
    boundaryOptions.ConstructionTraceScope+="/nominal-boundary-envelope";
    if(boundaryOptions.TimeLimitSeconds<=0||!boundaryOptions.MaxPlacementAttempts)
    {observeSelection(guardedOptions.ConstructionTraceScope,false);return result;}
    auto boundary=RunSkyline(problem,boundaryOptions,true,true);
    const auto attempts=result.Stats.PlacementAttempts+boundary.Stats.PlacementAttempts;
    const auto patterns=result.Stats.CandidatePatterns+boundary.Stats.CandidatePatterns;
    const auto sum=[](double first,double second){return first<0&&second<0?-1.:std::max(0.,first)+std::max(0.,second);};
    const double preparation=sum(result.Stats.PreparationSeconds,boundary.Stats.PreparationSeconds);
    const double placement=sum(result.Stats.PlacementSeconds,boundary.Stats.PlacementSeconds);
    const double verification=sum(result.Stats.VerificationSeconds,boundary.Stats.VerificationSeconds);
    const double evaluation=sum(result.Stats.EvaluationSeconds,boundary.Stats.EvaluationSeconds);
    const double postprocessing=sum(result.Stats.PostProcessingSeconds,boundary.Stats.PostProcessingSeconds);
    const bool selected=boundary.State==Status::Complete&&boundary.GeometryValidated&&
        (result.State!=Status::Complete||boundary.Layouts.size()<result.Layouts.size());
    if(selected)result=std::move(boundary);
    else if(boundary.State==Status::Cancelled)result.State=Status::Cancelled;
    result.Stats.PlacementAttempts=attempts;result.Stats.CandidatePatterns=patterns;
    result.Stats.PreparationSeconds=preparation;result.Stats.PlacementSeconds=placement;
    result.Stats.VerificationSeconds=verification;result.Stats.EvaluationSeconds=evaluation;
    result.Stats.PostProcessingSeconds=postprocessing;
    result.Stats.ElapsedSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    result.Diagnostics.push_back(std::string("plane.envelope-initial: independent original-demand nominal-boundary trial; selected=")+
        (selected?"1":"0")+"; no partial-layout repair or weakened original checks");
    observeSelection(selected?boundaryOptions.ConstructionTraceScope:guardedOptions.ConstructionTraceScope,selected);
    return result;
}
}
