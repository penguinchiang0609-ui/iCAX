#include "NestingInternal.h"
#include "NestingRectangleDirections.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <optional>
#include <tuple>
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
    };
    struct SkylineChoice
    {
        std::size_t Direction;
        Rect Footprint;
        std::optional<std::size_t> Cavity;
    };
    auto ChoiceRank(const SkylineChoice& c)
    {
        return std::tuple(c.Footprint.Y + c.Footprint.Height,
                          c.Footprint.X, c.Footprint.Y, c.Direction);
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

Result RunRectangleSkyline(const Problem& problem, const Options& options)
{
    Result result;
    const auto begin = std::chrono::steady_clock::now();
    const auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count(); };
    bool cancelled = false, exhausted = false;
    const std::function<bool()> stop = [&]
    {
        if (cancelled || exhausted) return true;
        if (options.Cancel && options.Cancel()) { cancelled = true; return true; }
        if (result.Stats.PlacementAttempts >= options.MaxPlacementAttempts || elapsed() >= options.TimeLimitSeconds)
        { exhausted = true; return true; }
        return false;
    };
    std::vector<std::size_t> consumed(problem.Parts.size()), stockUsed(problem.Stocks.size());
    std::vector<RectangleDirectionSet> descriptions(problem.Parts.size());
    for (std::size_t i = 0; i < problem.Parts.size() && !stop(); ++i)
    {
        if (!problem.Parts[i].Quantity) continue;
        descriptions[i] = FindRectangleDirections(problem.Parts[i], problem.Tolerance, stop);
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
            if (!InsideSheet(problem.Parts[partIndex].Geometry,
                {x + problem.EdgeMargin + d.Offset.X, y + problem.EdgeMargin + d.Offset.Y, d.Offset.AngleDegrees},
                bin.Output.Width, bin.Output.Height, problem.EdgeMargin, problem.Tolerance)) return;
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
                    ++result.Stats.PlacementAttempts;
                    const auto& d = directions[di];
                    // Cavities use strict bounds: tolerances cannot let a part
                    // protrude into the surrounding already occupied material.
                    if (d.Width + problem.PartGap <= free.Width && d.Height + problem.PartGap <= free.Height)
                        consider(di, free.X, free.Y, ci);
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
                ++result.Stats.PlacementAttempts;
                double y = 0;
                for (const auto& span : bin.Profile)
                    if (span.Left < x + width && span.Right > x) y = std::max(y, span.Height);
                if (y + height <= bin.Height + problem.Tolerance) consider(di, x, y, {});
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
            {r.X + problem.EdgeMargin + d.Offset.X, r.Y + problem.EdgeMargin + d.Offset.Y, d.Offset.AngleDegrees},
            bin.Output.Placements.size() + 1, choice.Cavity.has_value()});
        bin.Output.MaterialArea += descriptions[partIndex].Area;
        if (choice.Cavity) { UseCavity(bin, *choice.Cavity, r); ++result.Stats.GapFilledParts; }
        else AddToProfile(bin, r, options.EnableGapFill);
        if (options.EnableGapFill && !bin.Cavities.empty()) MergeCavities(bin.Cavities, stop);
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
                if (fit && !cancelled) { commit(bin, partIndex, *fit); placed = true; break; }
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
                if (const auto fit = find(bin, partIndex); fit && !cancelled)
                {
                    commit(bin, partIndex, *fit);
                    ++stockUsed[si]; bins.push_back(std::move(bin)); placed = true; break;
                }
            }
            if (!placed) break; // Later input types may still fit this finite inventory.
        }
    }
    for (auto& bin : bins) if (!bin.Output.Placements.empty()) result.Layouts.push_back(std::move(bin.Output));
    for (std::size_t i = 0; i < problem.Parts.size(); ++i)
        if (consumed[i] < problem.Parts[i].Quantity)
            result.Remaining.push_back({problem.Parts[i].ID, problem.Parts[i].Quantity - consumed[i]});
    result.State = cancelled ? Status::Cancelled : result.Remaining.empty() ? Status::Complete : Status::Partial;
    result.Stats.ElapsedSeconds = elapsed();
    result.Stats.PlacementSeconds = std::max(0.0, result.Stats.ElapsedSeconds - result.Stats.PreparationSeconds);
    if (cancelled) result.Diagnostics.push_back("Rectangle skyline cancelled; original placed and remaining instances retained.");
    if (exhausted) result.Diagnostics.push_back("Rectangle skyline time or placement-attempt budget exhausted.");
    if (!cancelled && !exhausted && !result.Remaining.empty())
        result.Diagnostics.push_back("Input-order rectangle skyline left unmet demand; unsupported shapes and greedy failures are not infeasibility proofs.");
    return result;
}
}
