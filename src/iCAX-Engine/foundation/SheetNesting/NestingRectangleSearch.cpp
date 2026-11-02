#include "NestingInternal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <tuple>

namespace iCAX::SheetNesting::Detail
{
    namespace
    {
        struct ShelfItem
        {
            std::size_t PartIndex = 0;
            double Angle = 0;
            Rect PhysicalBounds;
            double Width = 0, Height = 0;
        };
        struct Shelf { double Y = 0, Height = 0, Used = 0; };
        struct ShelfBin
        {
            std::size_t StockIndex = 0;
            Layout Output;
            double Width = 0, Height = 0, NextY = 0;
            std::vector<Shelf> Rows;
        };
        struct ShelfFit
        {
            std::size_t BinIndex = 0, RowIndex = 0;
            double X = 0, Y = 0, HeightWaste = 0, WidthWaste = 0;
            bool NewRow = false;
        };
        auto FitRank(const ShelfFit& fit)
        { return std::tuple(fit.NewRow, fit.HeightWaste, fit.WidthWaste, fit.Y, fit.X, fit.BinIndex, fit.RowIndex); }
    }

    // First-fit decreasing-height shelves, with a transposed coordinate system
    // providing an independent column rule. A direction is fixed before sorting;
    // the other strategies retain their independent orientation/placement rules.
    Result RunRectangleShelves(const Problem& problem, const Options& options, bool columns)
    {
        Result result;
        const auto started = std::chrono::steady_clock::now();
        bool cancelled = false, exhausted = false;
        const auto stop = [&]
        {
            if (cancelled || exhausted) return true;
            if (options.Cancel && options.Cancel()) { cancelled = true; return true; }
            if (result.Stats.PlacementAttempts >= options.MaxPlacementAttempts ||
                std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count() >= options.TimeLimitSeconds)
            { exhausted = true; return true; }
            return false;
        };
        std::vector<std::size_t> consumed(problem.Parts.size()), stockUsed(problem.Stocks.size());
        std::vector<ShelfItem> items;
        for (std::size_t index = 0; index < problem.Parts.size() && !stop(); ++index)
        {
            const auto& part = problem.Parts[index];
            if (part.Quantity == 0 || Classify(part.Geometry, problem.Tolerance).Kind != ShapeKind::Rectangle) continue;
            const double area = Area(part.Geometry);
            for (double angle : CandidateAngles(part, problem.Tolerance))
            {
                if (stop()) break;
                const auto bounds = Bounds(part.Geometry, {0, 0, angle});
                // A permitted direction need not align with the board axes.
                // Only actual axis-aligned rectangles belong to this shelf rule.
                if (bounds.Width <= problem.Tolerance || bounds.Height <= problem.Tolerance ||
                    std::abs(bounds.Width*bounds.Height-area) > problem.Tolerance*(bounds.Width+bounds.Height)*2) continue;
                items.push_back({index, angle, bounds,
                    columns ? bounds.Height : bounds.Width, columns ? bounds.Width : bounds.Height});
                break;
            }
        }
        result.Stats.CandidatePatterns = items.size();
        std::stable_sort(items.begin(), items.end(), [&](const ShelfItem& a, const ShelfItem& b)
        {
            if (a.Height != b.Height) return a.Height > b.Height;
            if (a.Width != b.Width) return a.Width > b.Width;
            return problem.Parts[a.PartIndex].ID < problem.Parts[b.PartIndex].ID;
        });
        std::vector<ShelfBin> bins;
        const auto findFit = [&](const ShelfBin& bin, std::size_t binIndex, const ShelfItem& item) -> std::optional<ShelfFit>
        {
            if (problem.Parts[item.PartIndex].Material != problem.Stocks[bin.StockIndex].Material) return {};
            std::optional<ShelfFit> best;
            for (std::size_t row = 0; row < bin.Rows.size(); ++row)
            {
                if (stop()) break;
                ++result.Stats.PlacementAttempts;
                const auto& shelf = bin.Rows[row];
                if (item.Height > shelf.Height+problem.Tolerance || shelf.Used+item.Width > bin.Width+problem.Tolerance) continue;
                ShelfFit candidate{binIndex, row, shelf.Used, shelf.Y,
                    std::max(0.0, shelf.Height-item.Height), std::max(0.0, bin.Width-shelf.Used-item.Width), false};
                if (!best || FitRank(candidate) < FitRank(*best)) best = candidate;
            }
            if (!best && !stop())
            {
                ++result.Stats.PlacementAttempts;
                if (item.Width <= bin.Width+problem.Tolerance && bin.NextY+item.Height <= bin.Height+problem.Tolerance)
                    best = ShelfFit{binIndex, bin.Rows.size(), 0, bin.NextY, 0, std::max(0.0, bin.Width-item.Width), true};
            }
            return best;
        };
        const auto commit = [&](ShelfBin& bin, const ShelfItem& item, const ShelfFit& fit)
        {
            if (fit.NewRow)
            {
                bin.Rows.push_back({fit.Y, item.Height, 0});
                bin.NextY = fit.Y+item.Height+problem.PartGap;
            }
            bin.Rows[fit.RowIndex].Used = fit.X+item.Width+problem.PartGap;
            const auto& part = problem.Parts[item.PartIndex];
            const double physicalX = (columns ? fit.Y : fit.X)+problem.EdgeMargin;
            const double physicalY = (columns ? fit.X : fit.Y)+problem.EdgeMargin;
            bin.Output.Placements.push_back({part.ID, consumed[item.PartIndex]++,
                {physicalX-item.PhysicalBounds.X, physicalY-item.PhysicalBounds.Y, item.Angle},
                bin.Output.Placements.size()+1, false});
            bin.Output.MaterialArea += Area(part.Geometry);
        };
        std::vector<std::size_t> stockOrder(problem.Stocks.size());
        std::iota(stockOrder.begin(), stockOrder.end(), 0);
        std::stable_sort(stockOrder.begin(), stockOrder.end(), [&](auto a, auto b)
        {
            const auto& left = problem.Stocks[a]; const auto& right = problem.Stocks[b];
            const double la = left.Width*left.Height, ra = right.Width*right.Height;
            if (la != ra) return la > ra;
            if (left.Cost != right.Cost) return left.Cost < right.Cost;
            return left.ID < right.ID;
        });
        for (const auto& item : items)
        {
            const auto& part = problem.Parts[item.PartIndex];
            while (consumed[item.PartIndex] < part.Quantity && !stop())
            {
                std::optional<ShelfFit> best;
                for (std::size_t bi = 0; bi < bins.size() && !stop(); ++bi)
                    if (auto fit = findFit(bins[bi], bi, item))
                        if (!best || FitRank(*fit) < FitRank(*best)) best = fit;
                if (best) { commit(bins[best->BinIndex], item, *best); continue; }
                if (stop()) break;
                bool opened = false;
                for (auto si : stockOrder)
                {
                    if (stop()) break;
                    const auto& stock = problem.Stocks[si];
                    if (stockUsed[si] >= stock.Quantity || part.Material != stock.Material) continue;
                    const double width = (columns ? stock.Height : stock.Width)-2*problem.EdgeMargin;
                    const double height = (columns ? stock.Width : stock.Height)-2*problem.EdgeMargin;
                    if (width <= 0 || height <= 0) continue;
                    ShelfBin bin{si, {stock.ID, stockUsed[si], stock.Width, stock.Height, {}, 0}, width, height, 0, {}};
                    if (auto fit = findFit(bin, bins.size(), item))
                    {
                        commit(bin, item, *fit);
                        ++stockUsed[si]; bins.push_back(std::move(bin)); opened = true; break;
                    }
                }
                if (!opened) break; // Later smaller/different-material types still get an attempt.
            }
            if (stop()) break;
        }
        for (auto& bin : bins) if (!bin.Output.Placements.empty()) result.Layouts.push_back(std::move(bin.Output));
        for (std::size_t i = 0; i < problem.Parts.size(); ++i)
            if (consumed[i] < problem.Parts[i].Quantity) result.Remaining.push_back({problem.Parts[i].ID, problem.Parts[i].Quantity-consumed[i]});
        result.State = cancelled ? Status::Cancelled : result.Remaining.empty() ? Status::Complete : Status::Partial;
        if (cancelled) result.Diagnostics.push_back("Rectangle shelf job cancelled; completed placements retained.");
        if (exhausted) result.Diagnostics.push_back("Rectangle shelf time or attempt budget exhausted.");
        if (!cancelled && !exhausted && !result.Remaining.empty())
            result.Diagnostics.push_back("Axis-aligned rectangle shelf rules or stock exhausted; remaining shapes are not proven infeasible.");
        result.Stats.ElapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        return result;
    }
}

namespace iCAX::SheetNesting::Detail
{
    namespace
    {
        struct HeightGroup
        {
            double Height = 0;
            std::string Material;
            std::vector<ShelfItem> Items;
        };
        struct RowPlan
        {
            std::vector<std::size_t> Counts;
            std::size_t Members = 0;
            double Width = 0;
        };
        RowPlan MeasureRow(const HeightGroup& group, std::vector<std::size_t> counts, double gap)
        {
            RowPlan plan;
            plan.Counts = std::move(counts);
            for (std::size_t i = 0; i < plan.Counts.size(); ++i)
            {
                plan.Members += plan.Counts[i];
                plan.Width += group.Items[i].Width*plan.Counts[i];
            }
            if (plan.Members > 1) plan.Width += gap*(plan.Members-1);
            return plan;
        }
        RowPlan GreedyGroupedRow(const HeightGroup& group, const std::vector<std::size_t>& available,
            double capacity, double gap, double tolerance, const std::function<bool()>& stop)
        {
            std::vector<std::size_t> counts(available.size());
            double used = 0;
            std::size_t members = 0;
            // Items have a stable decreasing-width order. This is the bounded
            // fallback, not an unbounded real-valued subset-sum search.
            for (std::size_t i = 0; i < counts.size(); ++i)
                while (counts[i] < available[i] && !stop())
                {
                    const double next = used+group.Items[i].Width+(members ? gap : 0);
                    if (next > capacity+tolerance) break;
                    used = next; ++counts[i]; ++members;
                }
            return MeasureRow(group, std::move(counts), gap);
        }
        RowPlan GroupedKnapsack(const HeightGroup& group, const std::vector<std::size_t>& available,
            double capacity, double gap, double tolerance, const std::function<bool()>& stop, std::size_t& fallbacks)
        {
            constexpr std::size_t maxStates = 4096, maxTypes = 64, maxTransitions = 2000000;
            const auto fallback = [&]
            { ++fallbacks; return GreedyGroupedRow(group, available, capacity, gap, tolerance, stop); };
            if (group.Items.empty() || stop()) return {};
            if (group.Items.size() > maxTypes || !std::isfinite(capacity+gap)) return fallback();
            auto all = MeasureRow(group, available, gap);
            if (all.Members && all.Width <= capacity+tolerance) return all;
            const double effectiveCapacity = capacity+gap;
            if (effectiveCapacity <= 0) return {};

            // Prefer an exact decimal grid reduced by GCD. An input with a very
            // fine real grid instead gets conservative upward item rounding and
            // downward capacity rounding on a bounded 4096-state grid.
            std::vector<std::size_t> weights;
            std::size_t integerCapacity = 0;
            double smallest = std::numeric_limits<double>::infinity();
            for (const auto& item : group.Items) smallest = std::min(smallest, item.Width+gap);
            const double maximumMembers = std::max(1.0, std::min(100000.0, effectiveCapacity/smallest));
            const double snapTolerance = tolerance/(4*maximumMembers);
            for (double scale = 1; scale <= 1000000 && weights.empty(); scale *= 10)
            {
                if (!std::isfinite(effectiveCapacity*scale) || effectiveCapacity*scale > 9e15) break;
                const auto cap = static_cast<std::int64_t>(std::llround(effectiveCapacity*scale));
                if (cap <= 0 || std::abs(cap/scale-effectiveCapacity) > tolerance/4) continue;
                std::vector<std::int64_t> ticks;
                std::int64_t divisor = cap;
                bool exact = true;
                for (const auto& item : group.Items)
                {
                    const double value = item.Width+gap;
                    if (!std::isfinite(value*scale) || value*scale > 9e15) { exact = false; break; }
                    const auto tick = static_cast<std::int64_t>(std::llround(value*scale));
                    if (tick <= 0 || std::abs(tick/scale-value) > snapTolerance) { exact = false; break; }
                    ticks.push_back(tick); divisor = std::gcd(divisor, tick);
                }
                if (!exact || divisor <= 0 || cap/divisor > static_cast<std::int64_t>(maxStates)) continue;
                integerCapacity = static_cast<std::size_t>(cap/divisor);
                for (const auto tick : ticks) weights.push_back(static_cast<std::size_t>(tick/divisor));
            }
            if (weights.empty())
            {
                const double quantum = effectiveCapacity/maxStates;
                if (!std::isfinite(quantum) || quantum <= 0) return fallback();
                integerCapacity = maxStates;
                for (const auto& item : group.Items)
                {
                    const double ticks = std::ceil((item.Width+gap)/quantum);
                    if (!std::isfinite(ticks) || ticks > maxStates) weights.push_back(maxStates+1);
                    else weights.push_back(std::max<std::size_t>(1, static_cast<std::size_t>(ticks)));
                }
            }
            const double infinity = std::numeric_limits<double>::infinity();
            std::vector<double> previous(integerCapacity+1, infinity), next;
            previous[0] = 0;
            std::vector<std::vector<std::size_t>> choices(group.Items.size(), std::vector<std::size_t>(integerCapacity+1));
            double totalWidth = 0;
            for (std::size_t i = 0; i < available.size(); ++i) totalWidth += (group.Items[i].Width+gap)*available[i];
            const double fraction = std::min(1.0, effectiveCapacity/totalWidth);
            std::size_t transitions = 0;
            for (std::size_t i = 0; i < group.Items.size(); ++i)
            {
                if (stop()) return {};
                next.assign(integerCapacity+1, infinity);
                const double target = available[i]*fraction;
                for (std::size_t used = 0; used <= integerCapacity; ++used)
                {
                    if (!std::isfinite(previous[used])) continue;
                    const auto maximum = std::min(available[i], (integerCapacity-used)/weights[i]);
                    for (std::size_t count = 0; count <= maximum; ++count)
                    {
                        if (++transitions >= maxTransitions) return fallback();
                        if (transitions % 128 == 0 && stop()) return {};
                        const auto destination = used+count*weights[i];
                        const double delta = count-target;
                        const double penalty = previous[used]+delta*delta/std::max<std::size_t>(1, available[i]);
                        if (penalty < next[destination])
                        { next[destination] = penalty; choices[i][destination] = count; }
                    }
                }
                previous = std::move(next);
            }
            std::size_t best = integerCapacity;
            while (best && !std::isfinite(previous[best])) --best;
            std::vector<std::size_t> counts(group.Items.size());
            for (std::size_t i = group.Items.size(); i-- > 0;)
            {
                counts[i] = choices[i][best];
                best -= counts[i]*weights[i];
            }
            auto plan = MeasureRow(group, std::move(counts), gap);
            // Decimal normalization is only a search aid. Actual double widths,
            // actual member counts and the real final gap decide acceptance.
            if (!plan.Members || plan.Width > capacity+tolerance) return fallback();
            return plan;
        }
    }

    Result RunRectanglePatternShelves(const Problem& problem, const Options& options, bool columns)
    {
        Result result;
        const auto started = std::chrono::steady_clock::now();
        bool cancelled = false, exhausted = false;
        const auto stop = [&]
        {
            if (cancelled || exhausted) return true;
            if (options.Cancel && options.Cancel()) { cancelled = true; return true; }
            if (result.Stats.PlacementAttempts >= options.MaxPlacementAttempts ||
                std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count() >= options.TimeLimitSeconds)
            { exhausted = true; return true; }
            return false;
        };
        std::vector<std::size_t> consumed(problem.Parts.size()), stockUsed(problem.Stocks.size());
        std::vector<HeightGroup> groups;
        for (std::size_t index = 0; index < problem.Parts.size() && !stop(); ++index)
        {
            const auto& part = problem.Parts[index];
            if (part.Quantity == 0 || Classify(part.Geometry, problem.Tolerance).Kind != ShapeKind::Rectangle) continue;
            for (double angle : CandidateAngles(part, problem.Tolerance))
            {
                if (stop()) break;
                const auto bounds = Bounds(part.Geometry, {0, 0, angle});
                if (bounds.Width <= problem.Tolerance || bounds.Height <= problem.Tolerance ||
                    std::abs(bounds.Width*bounds.Height-Area(part.Geometry)) > problem.Tolerance*(bounds.Width+bounds.Height)*2) continue;
                ShelfItem item{index, angle, bounds, columns ? bounds.Height : bounds.Width, columns ? bounds.Width : bounds.Height};
                auto group = std::find_if(groups.begin(), groups.end(), [&](const auto& candidate)
                    { return candidate.Material == part.Material && std::abs(candidate.Height-item.Height) <= problem.Tolerance; });
                if (group == groups.end()) groups.push_back({item.Height, part.Material, {item}});
                else { group->Height = std::max(group->Height, item.Height); group->Items.push_back(item); }
                break;
            }
        }
        for (auto& group : groups)
            std::stable_sort(group.Items.begin(), group.Items.end(), [&](const auto& a, const auto& b)
            { return a.Width != b.Width ? a.Width > b.Width : problem.Parts[a.PartIndex].ID < problem.Parts[b.PartIndex].ID; });
        std::stable_sort(groups.begin(), groups.end(), [&](const auto& a, const auto& b)
        {
            if (a.Height != b.Height) return a.Height > b.Height;
            if (a.Material != b.Material) return a.Material < b.Material;
            return problem.Parts[a.Items.front().PartIndex].ID < problem.Parts[b.Items.front().PartIndex].ID;
        });
        result.Stats.CandidatePatterns = groups.size();
        std::vector<ShelfBin> bins;
        std::size_t nextGroup = 1, fallbacks = 0;
        const auto available = [&](const HeightGroup& group)
        {
            std::vector<std::size_t> values;
            for (const auto& item : group.Items) values.push_back(problem.Parts[item.PartIndex].Quantity-consumed[item.PartIndex]);
            return values;
        };
        const auto commit = [&](ShelfBin& bin, const HeightGroup& group, const RowPlan& plan)
        {
            double x = 0;
            std::size_t count = 0;
            for (std::size_t i = 0; i < plan.Counts.size(); ++i)
                for (std::size_t repeat = 0; repeat < plan.Counts[i] && !stop(); ++repeat)
                {
                    ++result.Stats.PlacementAttempts;
                    const auto& item = group.Items[i]; const auto& part = problem.Parts[item.PartIndex];
                    // Recheck the accumulated physical width as well as the
                    // batched plan sum; very large instance counts can round
                    // differently under sequential coordinate accumulation.
                    if (x+item.Width > bin.Width+problem.Tolerance) break;
                    const double px = (columns ? bin.NextY : x)+problem.EdgeMargin;
                    const double py = (columns ? x : bin.NextY)+problem.EdgeMargin;
                    bin.Output.Placements.push_back({part.ID, consumed[item.PartIndex]++,
                        {px-item.PhysicalBounds.X, py-item.PhysicalBounds.Y, item.Angle}, nextGroup, false});
                    bin.Output.MaterialArea += Area(part.Geometry);
                    x += item.Width+problem.PartGap; ++count;
                }
            if (count) { bin.NextY += group.Height+problem.PartGap; ++nextGroup; }
            return count;
        };
        std::vector<std::size_t> stockOrder(problem.Stocks.size());
        std::iota(stockOrder.begin(), stockOrder.end(), 0);
        std::stable_sort(stockOrder.begin(), stockOrder.end(), [&](auto a, auto b)
        {
            const auto& x = problem.Stocks[a]; const auto& y = problem.Stocks[b];
            if (x.Width*x.Height != y.Width*y.Height) return x.Width*x.Height > y.Width*y.Height;
            return x.Cost != y.Cost ? x.Cost < y.Cost : x.ID < y.ID;
        });
        for (const auto& group : groups)
        {
            while (!stop())
            {
                const auto counts = available(group);
                if (std::all_of(counts.begin(), counts.end(), [](auto count) { return count == 0; })) break;
                bool placed = false;
                for (auto& bin : bins)
                {
                    if (stop()) break;
                    if (group.Material != problem.Stocks[bin.StockIndex].Material || bin.NextY+group.Height > bin.Height+problem.Tolerance) continue;
                    const auto plan = GroupedKnapsack(group, counts, bin.Width, problem.PartGap, problem.Tolerance, stop, fallbacks);
                    if (plan.Members && commit(bin, group, plan)) { placed = true; break; }
                }
                if (placed) continue;
                if (stop()) break;
                for (auto si : stockOrder)
                {
                    if (stop()) break;
                    const auto& stock = problem.Stocks[si];
                    if (stockUsed[si] >= stock.Quantity || group.Material != stock.Material) continue;
                    const double width = (columns ? stock.Height : stock.Width)-2*problem.EdgeMargin;
                    const double height = (columns ? stock.Width : stock.Height)-2*problem.EdgeMargin;
                    if (width <= 0 || group.Height > height+problem.Tolerance) continue;
                    const auto plan = GroupedKnapsack(group, counts, width, problem.PartGap, problem.Tolerance, stop, fallbacks);
                    if (!plan.Members || stop()) continue;
                    ShelfBin bin{si, {stock.ID, stockUsed[si], stock.Width, stock.Height, {}, 0}, width, height, 0, {}};
                    if (commit(bin, group, plan))
                    { ++stockUsed[si]; bins.push_back(std::move(bin)); placed = true; break; }
                }
                if (!placed) break;
            }
            if (stop()) break;
        }
        for (auto& bin : bins) if (!bin.Output.Placements.empty()) result.Layouts.push_back(std::move(bin.Output));
        for (std::size_t i = 0; i < problem.Parts.size(); ++i)
            if (consumed[i] < problem.Parts[i].Quantity) result.Remaining.push_back({problem.Parts[i].ID, problem.Parts[i].Quantity-consumed[i]});
        result.State = cancelled ? Status::Cancelled : result.Remaining.empty() ? Status::Complete : Status::Partial;
        if (cancelled) result.Diagnostics.push_back("Rectangle grouped-pattern job cancelled; completed placements retained.");
        if (exhausted) result.Diagnostics.push_back("Rectangle grouped-pattern time or attempt budget exhausted.");
        if (fallbacks) result.Diagnostics.push_back("Bounded row knapsack used deterministic conservative fallback on "+std::to_string(fallbacks)+" row attempts.");
        if (!cancelled && !exhausted && !result.Remaining.empty())
            result.Diagnostics.push_back("Grouped rectangle row patterns or stock exhausted; no infeasibility proof.");
        result.Stats.ElapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        return result;
    }
}
