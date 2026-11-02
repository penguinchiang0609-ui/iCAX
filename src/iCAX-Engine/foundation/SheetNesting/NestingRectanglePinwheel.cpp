#include "NestingRectangleDirections.h"

#include <array>
#include <chrono>
#include <deque>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>

namespace iCAX::SheetNesting::Detail
{
namespace
{
    using Footprint = std::pair<double, double>;
    struct WheelMember { std::size_t PartIndex, Instance; Pose Local; };
    struct WheelUnit
    {
        double Width, Height;
        std::vector<WheelMember> Members;
        std::size_t Depth = 0;
    };
    struct WheelCandidate
    {
        std::array<Footprint, 5> Roles;
        std::array<Point, 5> Positions;
        double Width, Height;
        std::size_t Copies = 0;
    };
    using WheelPool = std::map<Footprint, std::deque<WheelUnit>>;

    bool RoundoffEqual(double a, double b)
    {
        return std::abs(a-b) <= 64 * std::numeric_limits<double>::epsilon()
            * std::max({1., std::abs(a), std::abs(b)});
    }

    bool FilledWheel(const WheelCandidate& c)
    {
        // Verify the actual five reserved rectangles, not just their outer box.
        // No geometric tolerance may enlarge a cell or conceal an interior gap.
        double area = 0;
        for (std::size_t i = 0; i < c.Roles.size(); ++i)
        {
            const auto [w,h] = c.Roles[i]; const auto p = c.Positions[i];
            if (w <= 0 || h <= 0 || p.X < 0 || p.Y < 0 || p.X+w > c.Width || p.Y+h > c.Height) return false;
            area += w*h;
            for (std::size_t j = 0; j < i; ++j)
            {
                const auto [otherW,otherH] = c.Roles[j]; const auto q = c.Positions[j];
                if (p.X < q.X+otherW && q.X < p.X+w && p.Y < q.Y+otherH && q.Y < p.Y+h) return false;
            }
        }
        return RoundoffEqual(area, c.Width*c.Height);
    }
}

Result RunRectanglePinwheel(const Problem& problem, const Options& options)
{
    const auto started = std::chrono::steady_clock::now();
    const auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count(); };
    Result result;
    bool cancelled = false, exhausted = false, preparationLimited = false;
    std::size_t probes = 0, candidateCount = 0, mergedUnits = 0, mergeBatches = 0, maxDepth = 0, count = 0;
    const auto cancellationRequested = [&]
    {
        if (!cancelled && options.Cancel && options.Cancel()) cancelled = true;
        return cancelled;
    };
    const auto stop = [&]
    {
        if (cancelled || exhausted) return true;
        if (cancellationRequested()) return true;
        if (probes >= options.MaxPlacementAttempts || elapsed() >= options.TimeLimitSeconds)
        { exhausted = true; return true; }
        return false;
    };
    const auto untouched = [&](const char* diagnostic)
    {
        for (const auto& part : problem.Parts) if (part.Quantity) result.Remaining.push_back({part.ID, part.Quantity});
        result.State = cancelled ? Status::Cancelled : result.Remaining.empty() ? Status::Complete : Status::Partial;
        result.Stats.PlacementAttempts = probes; result.Stats.CandidatePatterns = candidateCount;
        result.Stats.PreparationSeconds = result.Stats.ElapsedSeconds = elapsed(); result.Stats.PlacementSeconds = 0;
        result.Diagnostics.push_back(diagnostic);
        return result;
    };
    if (stop()) return untouched("Pinwheel preparation stopped; original demand retained.");

    std::string material;
    bool haveMaterial = false;
    for (const auto& part : problem.Parts) if (part.Quantity)
    {
        if (stop()) return untouched("Pinwheel preparation stopped; original demand retained.");
        if ((!haveMaterial ? false : material != part.Material) || part.Quantity > 512-count)
            return untouched("Pinwheel scope: one material and at most 512 demanded rectangle instances.");
        material = part.Material; haveMaterial = true; count += part.Quantity;
    }
    if (!count) return untouched("No demanded instances.");

    WheelPool pool;
    std::vector<double> areas(problem.Parts.size());
    for (std::size_t i = 0; i < problem.Parts.size(); ++i)
    {
        const auto& part = problem.Parts[i]; if (!part.Quantity) continue;
        if (stop()) return untouched("Pinwheel direction preparation stopped; original demand retained.");
        auto description = FindRectangleDirections(part, problem.Tolerance, stop);
        if (description.Interrupted || stop()) return untouched("Pinwheel direction preparation stopped; original demand retained.");
        if (!description.Supported)
            return untouched("Pinwheel cells require original rectangles without holes and a legal axis-aligned direction.");
        // One deterministic legal direction per original specification. Other
        // constructors retain their own independent orientation searches.
        const auto& direction = description.Directions.front();
        if (!RoundoffEqual(description.Area, direction.Width*direction.Height))
            return untouched("Pinwheel cells require filled rectangles; near-rectangle envelopes are not substituted.");
        const double w = direction.Width+problem.PartGap, h = direction.Height+problem.PartGap;
        if (!std::isfinite(w) || !std::isfinite(h) || !std::isfinite(w*h))
            return untouched("Pinwheel expanded footprint is outside finite numeric scope.");
        areas[i] = description.Area;
        auto& queue = pool[{w,h}];
        for (std::size_t instance = 0; instance < part.Quantity; ++instance)
        {
            if (stop()) return untouched("Pinwheel instance preparation stopped; original demand retained.");
            queue.push_back({w,h,{{i,instance,direction.Offset}},0});
        }
    }

    // Reserve a bounded portion for the ordinary rectangle planner. Discovery
    // probes, each materialized composite and final placements share one cap.
    const auto reserve = std::min(options.MaxPlacementAttempts/4, 4*count+1);
    const auto preparationCap = options.MaxPlacementAttempts-reserve;
    const auto preparationStop = [&]
    {
        if (stop()) return true;
        if (probes >= preparationCap || elapsed() >= options.TimeLimitSeconds*.75)
        { preparationLimited = true; return true; }
        return false;
    };
    const auto probe = [&]
    {
        if (preparationStop()) return false;
        ++probes; return true;
    };
    const auto fitsStock = [&](double w, double h)
    {
        for (const auto& stock : problem.Stocks)
            if (stock.Quantity && stock.Material == material &&
                w <= stock.Width-2*problem.EdgeMargin+problem.PartGap+problem.Tolerance &&
                h <= stock.Height-2*problem.EdgeMargin+problem.PartGap+problem.Tolerance) return true;
        return false;
    };

    while (options.EnableCombinations && !preparationStop())
    {
        std::vector<Footprint> keys;
        std::map<double,std::vector<Footprint>> byWidth, byHeight;
        for (const auto& [key,units] : pool) if (!units.empty())
        { keys.push_back(key); byWidth[key.first].push_back(key); byHeight[key.second].push_back(key); }
        std::vector<WheelCandidate> candidates;
        using Identity = std::tuple<std::array<Footprint,5>,double,double>;
        std::set<Identity> seen;
        const auto full = [&] { return candidates.size() >= options.MaxPatterns; };
        // E is the centre. A is lower-left; B/C/D follow counterclockwise.
        // Width/height indexes derive the other roles; no five-deep enumeration.
        for (const auto& e : keys)
        {
            if (preparationStop() || full()) break;
            for (const auto& a : keys)
            {
                if (full() || !probe()) break;
                const double dw = a.first-e.first, bh = a.second+e.second;
                if (dw <= 0) continue;
                const auto bs = byHeight.find(bh); if (bs == byHeight.end()) continue;
                for (const auto& b : bs->second)
                {
                    if (full() || !probe()) break;
                    const auto cs = byWidth.find(b.first+e.first); if (cs == byWidth.end()) continue;
                    for (const auto& c : cs->second)
                    {
                        if (full() || !probe()) break;
                        const Footprint d{dw,c.second+e.second};
                        const auto found = pool.find(d); if (found == pool.end() || found->second.empty()) continue;
                        WheelCandidate candidate{{a,b,c,d,e},{{{0,0},{a.first,0},{dw,bh},{0,a.second},{dw,a.second}}},
                            a.first+b.first,bh+c.second};
                        if (!fitsStock(candidate.Width,candidate.Height)) continue;
                        std::map<Footprint,std::size_t> demand;
                        for (const auto& role : candidate.Roles) ++demand[role];
                        candidate.Copies = count;
                        for (const auto& [role,quantity] : demand)
                            candidate.Copies = std::min(candidate.Copies,pool.at(role).size()/quantity);
                        if (!candidate.Copies) continue;
                        auto sorted = candidate.Roles; std::sort(sorted.begin(),sorted.end());
                        if (!seen.emplace(sorted,candidate.Width,candidate.Height).second || !FilledWheel(candidate)) continue;
                        candidates.push_back(std::move(candidate)); ++candidateCount;
                    }
                }
            }
        }
        if (cancelled || candidates.empty()) break;
        const auto chosen = *std::min_element(candidates.begin(),candidates.end(),[](const auto& a,const auto& b)
        {
            // Preserve repeated kits before shrinking individual cells. A
            // candidate that uses the same footprint twice can otherwise split
            // four identical kits into incompatible pairs of partial kits.
            if (a.Copies != b.Copies) return a.Copies > b.Copies;
            return std::tuple(a.Width*a.Height,a.Width,a.Height,a.Roles)
                < std::tuple(b.Width*b.Height,b.Width,b.Height,b.Roles);
        });
        std::map<Footprint,std::size_t> demand;
        for (const auto& role : chosen.Roles) ++demand[role];
        std::size_t copies = count;
        for (const auto& [role,quantity] : demand) copies = std::min(copies,pool.at(role).size()/quantity);
        std::size_t made = 0;
        for (; made < copies; ++made)
        {
            if (!probe()) break; // A materialized composite also consumes a probe.
            WheelUnit combined{chosen.Width,chosen.Height,{},0};
            for (std::size_t role = 0; role < chosen.Roles.size(); ++role)
            {
                auto& queue = pool.at(chosen.Roles[role]);
                auto unit = std::move(queue.front()); queue.pop_front();
                combined.Depth = std::max(combined.Depth,unit.Depth+1);
                for (auto member : unit.Members)
                {
                    member.Local.X += chosen.Positions[role].X;
                    member.Local.Y += chosen.Positions[role].Y;
                    combined.Members.push_back(member);
                }
            }
            maxDepth = std::max(maxDepth,combined.Depth);
            pool[{combined.Width,combined.Height}].push_back(std::move(combined));
            ++mergedUnits;
        }
        if (made) ++mergeBatches;
        if (!made || preparationLimited) break;
    }
    if (stop()) return untouched("Pinwheel preparation exhausted the job budget or was cancelled; original demand retained.");

    Problem cells; cells.Stocks = problem.Stocks; cells.PartGap = problem.PartGap;
    cells.EdgeMargin = problem.EdgeMargin; cells.Tolerance = problem.Tolerance;
    std::vector<WheelUnit> units;
    std::unordered_map<std::string,std::size_t> cellIndex;
    for (auto& [key,queue] : pool)
        for (auto& unit : queue)
        {
            if (stop()) return untouched("Pinwheel publication preparation stopped; original demand retained.");
            const double w = unit.Width-problem.PartGap, h = unit.Height-problem.PartGap;
            if (w <= 0 || h <= 0) return untouched("Pinwheel normalization produced an unsupported cell.");
            Part cell; cell.ID = "pinwheel-cell-"+std::to_string(units.size()); cell.Material = material;
            cell.FreeRotation = false; cell.AllowedAnglesDegrees = {0};
            cell.Geometry.Outer = {{0,0},{w,0},{w,h},{0,h}};
            cellIndex.emplace(cell.ID,units.size()); cells.Parts.push_back(std::move(cell)); units.push_back(std::move(unit));
        }
    // Cell order is a single deterministic constructor choice. Grouping equal
    // heights avoids mixing short cells into rows needed by taller composites.
    std::stable_sort(cells.Parts.begin(),cells.Parts.end(),[&](const auto& a,const auto& b)
    {
        const auto& left=units[cellIndex.at(a.ID)]; const auto& right=units[cellIndex.at(b.ID)];
        if(left.Height!=right.Height)return left.Height>right.Height;
        return left.Width>right.Width;
    });
    const double preparation = elapsed();
    auto inner = options;
    inner.MaxPlacementAttempts = options.MaxPlacementAttempts-probes;
    inner.TimeLimitSeconds = options.TimeLimitSeconds-preparation;
    inner.Cancel = cancellationRequested;
    if (!inner.MaxPlacementAttempts || inner.TimeLimitSeconds <= 0)
        return untouched("Pinwheel discovery left no placement budget; original demand retained.");
    // The cell's physical side is expandedSide-gap. The shared planner adds the
    // gap once, so internal and external clearances are not counted twice.
    auto planned = RunRectangleOrdered(cells,inner,RectScore::ShortSide);
    result.Stats = planned.Stats;
    result.Stats.PlacementAttempts += probes;
    result.Stats.CandidatePatterns += candidateCount;
    result.Stats.CombinationGroups = 0;
    result.Diagnostics = std::move(planned.Diagnostics);
    std::vector<std::size_t> consumed(problem.Parts.size());
    std::size_t group = 0;
    // Expansion publishes already planned cells atomically, retaining every
    // original identity even if cancellation is observed after construction.
    for (const auto& placedLayout : planned.Layouts)
    {
        Layout layout{placedLayout.StockID,placedLayout.StockInstance,placedLayout.Width,placedLayout.Height,{},0};
        for (const auto& placed : placedLayout.Placements)
        {
            const auto& unit = units.at(cellIndex.at(placed.PartID)); ++group;
            if (unit.Members.size() > 1) ++result.Stats.CombinationGroups;
            for (const auto& member : unit.Members)
            {
                const auto& source = problem.Parts[member.PartIndex];
                layout.Placements.push_back({source.ID,member.Instance,
                    {placed.Transform.X+member.Local.X,placed.Transform.Y+member.Local.Y,member.Local.AngleDegrees},group,false});
                layout.MaterialArea += areas[member.PartIndex]; ++consumed[member.PartIndex];
            }
        }
        if (!layout.Placements.empty()) result.Layouts.push_back(std::move(layout));
    }
    for (std::size_t i = 0; i < problem.Parts.size(); ++i)
        if (consumed[i] < problem.Parts[i].Quantity) result.Remaining.push_back({problem.Parts[i].ID,problem.Parts[i].Quantity-consumed[i]});
    cancellationRequested();
    result.State = cancelled || planned.State == Status::Cancelled ? Status::Cancelled
        : result.Remaining.empty() ? Status::Complete : Status::Partial;
    result.Stats.ElapsedSeconds = elapsed();
    result.Stats.PreparationSeconds = preparation+std::max(0.0,planned.Stats.PreparationSeconds);
    result.Stats.PlacementSeconds = std::max(0.0,result.Stats.ElapsedSeconds-result.Stats.PreparationSeconds);
    result.Diagnostics.push_back("Pinwheel dimension composition: "+std::to_string(mergedUnits)+" composites in "+
        std::to_string(mergeBatches)+" batches, depth "+std::to_string(maxDepth)+"; "+std::to_string(probes)+
        " discovery/materialization probes. Largest repeatable batch, then smallest area; one legal direction per source specification; descending-height cell packing.");
    if (preparationLimited) result.Diagnostics.push_back("Pinwheel discovery budget limited; remaining budget was retained for ordinary rectangle placement.");
    if (!result.Remaining.empty()) result.Diagnostics.push_back("Pinwheel candidate/stock limitations are not an infeasibility proof.");
    return result;
}
}
