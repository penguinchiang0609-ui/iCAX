#include "NestingInternal.h"
#include "NestingRectangleDirections.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <tuple>

namespace iCAX::SheetNesting::Detail
{
namespace
{
    struct OrderedRectangle
    {
        std::size_t PartIndex;
        double Area;
        std::vector<RectangleDirection> Directions;
    };
    struct OrderedBin
    {
        std::size_t StockIndex;
        Layout Output;
        MaxRects Packing;

        OrderedBin(std::size_t index, std::size_t instance, const Stock& stock, const Problem& p)
            : StockIndex(index), Output{stock.ID, instance, stock.Width, stock.Height, {}, 0},
              Packing(stock.Width - 2 * p.EdgeMargin + p.PartGap,
                  stock.Height - 2 * p.EdgeMargin + p.PartGap, p.Tolerance) {}
    };
    struct OrderedFit
    {
        std::size_t BinIndex, DirectionIndex;
        RectFit Fit;
        Pose Transform;
    };
}

Result RunRectangleOrdered(const Problem& p, const Options& options, RectScore score)
{
    const auto started = std::chrono::steady_clock::now();
    const auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(); };
    Result result;
    bool cancelled = false, exhausted = false;
    std::size_t unsupported = 0, nextGroup = 1;
    std::vector<std::size_t> consumed(p.Parts.size(), 0), stockUsed(p.Stocks.size(), 0);
    std::vector<OrderedRectangle> rectangles;
    std::vector<OrderedBin> bins;
    const auto stop = [&]
    {
        if (cancelled || exhausted) return true;
        if (options.Cancel && options.Cancel()) { cancelled = true; return true; }
        if (elapsed() >= options.TimeLimitSeconds || result.Stats.PlacementAttempts >= options.MaxPlacementAttempts)
        { exhausted = true; return true; }
        return false;
    };
    const auto finish = [&]()
    {
        for (auto& bin : bins)
            if (!bin.Output.Placements.empty()) result.Layouts.push_back(std::move(bin.Output));
        for (std::size_t i = 0; i < p.Parts.size(); ++i)
            if (consumed[i] < p.Parts[i].Quantity)
                result.Remaining.push_back({p.Parts[i].ID, p.Parts[i].Quantity - consumed[i]});
        result.State = cancelled ? Status::Cancelled : result.Remaining.empty() ? Status::Complete : Status::Partial;
        result.Stats.ElapsedSeconds = elapsed();
        if (result.Stats.PreparationSeconds < 0) result.Stats.PreparationSeconds = result.Stats.ElapsedSeconds;
        result.Stats.PlacementSeconds = std::max(0.0, result.Stats.ElapsedSeconds - result.Stats.PreparationSeconds);
        result.Diagnostics.push_back("Ordered rectangle construction preserves received specification and instance order; attempts count direction/bin probes.");
        if (unsupported) result.Diagnostics.push_back("Unsupported rectangle specifications retained in remaining demand: " + std::to_string(unsupported));
        if (exhausted) result.Diagnostics.push_back("Ordered rectangle construction exhausted its time or attempt budget.");
        return result;
    };

    // Keep only planning descriptions. Quantities never duplicate source geometry,
    // and the caller's input order is retained even when a specification is skipped.
    for (std::size_t i = 0; i < p.Parts.size() && !stop(); ++i)
    {
        const auto& part = p.Parts[i];
        if (!part.Quantity) continue;
        auto extracted = FindRectangleDirections(part, p.Tolerance, stop);
        if (extracted.Interrupted || stop()) return finish();
        if (!extracted.Supported) { ++unsupported; continue; }
        OrderedRectangle rectangle{i, extracted.Area, {}};
        for (const auto& direction : extracted.Directions)
        {
            if (stop()) return finish();
            // Equivalent footprints need one legal pose, not four searches for
            // rotations that consume exactly the same rectangular space.
            if (std::any_of(rectangle.Directions.begin(), rectangle.Directions.end(), [&](const auto& old)
                { return std::abs(old.Width - direction.Width) <= p.Tolerance && std::abs(old.Height - direction.Height) <= p.Tolerance; })) continue;
            rectangle.Directions.push_back(direction);
        }
        result.Stats.CandidatePatterns += rectangle.Directions.size();
        rectangles.push_back(std::move(rectangle));
    }
    result.Stats.PreparationSeconds = elapsed();
    if (stop()) return finish();

    const auto better = [&](const OrderedFit& a, const OrderedFit& b)
    {
        if (a.Fit.Primary < b.Fit.Primary - p.Tolerance) return true;
        if (a.Fit.Primary > b.Fit.Primary + p.Tolerance) return false;
        if (a.Fit.Secondary < b.Fit.Secondary - p.Tolerance) return true;
        if (a.Fit.Secondary > b.Fit.Secondary + p.Tolerance) return false;
        return std::tie(a.Fit.Box.Y, a.Fit.Box.X, a.BinIndex, a.DirectionIndex)
            < std::tie(b.Fit.Box.Y, b.Fit.Box.X, b.BinIndex, b.DirectionIndex);
    };
    const auto query = [&](const OrderedBin& bin, std::size_t binIndex, const OrderedRectangle& rectangle)
        -> std::optional<OrderedFit>
    {
        const auto& part = p.Parts[rectangle.PartIndex];
        if (part.Material != p.Stocks[bin.StockIndex].Material) return {};
        std::optional<OrderedFit> best;
        for (std::size_t di = 0; di < rectangle.Directions.size(); ++di)
        {
            if (stop()) break;
            ++result.Stats.PlacementAttempts;
            const auto& direction = rectangle.Directions[di];
            const auto fit = bin.Packing.Find(direction.Width + p.PartGap, direction.Height + p.PartGap, score);
            if (!fit) continue;
            const Pose pose{fit->Box.X + p.EdgeMargin + direction.Offset.X,
                fit->Box.Y + p.EdgeMargin + direction.Offset.Y, direction.Offset.AngleDegrees};
            if (!InsideSheet(part.Geometry, pose, bin.Output.Width, bin.Output.Height, p.EdgeMargin, p.Tolerance)) continue;
            OrderedFit candidate{binIndex, di, *fit, pose};
            if (!best || better(candidate, *best)) best = candidate;
        }
        return cancelled ? std::nullopt : best;
    };
    const auto commit = [&](OrderedBin& bin, const OrderedRectangle& rectangle, const OrderedFit& fit)
    {
        const auto& part = p.Parts[rectangle.PartIndex];
        bin.Output.Placements.push_back({part.ID, consumed[rectangle.PartIndex]++, fit.Transform, nextGroup++, false});
        bin.Output.MaterialArea += rectangle.Area;
        bin.Packing.Occupy(fit.Fit.Box);
    };

    for (const auto& rectangle : rectangles)
    {
        const auto& part = p.Parts[rectangle.PartIndex];
        std::vector<std::size_t> activeBins;
        activeBins.reserve(bins.size());
        for (std::size_t bi = 0; bi < bins.size(); ++bi) activeBins.push_back(bi);
        while (consumed[rectangle.PartIndex] < part.Quantity && !stop())
        {
            std::optional<OrderedFit> best;
            for (std::size_t ai = 0; ai < activeBins.size() && !stop();)
            {
                const auto bi = activeBins[ai];
                if (auto fit = query(bins[bi], bi, rectangle))
                { if (!best || better(*fit, *best)) best = fit; ++ai; }
                // The current specification's legal footprints stay fixed, and
                // occupied sheets only lose free space. Repeated quantities need
                // not probe every previously exhausted sheet again.
                else if (!cancelled && !exhausted)
                { activeBins[ai] = activeBins.back(); activeBins.pop_back(); }
                else ++ai;
            }
            if (cancelled) break;
            // An evaluated placement remains usable when the last probe reached
            // the attempt budget; no additional search is performed to commit it.
            if (best) { commit(bins[best->BinIndex], rectangle, *best); continue; }
            if (stop()) break;
            bool opened = false;
            // Existing compatible sheets always precede new stock. New stock
            // follows its input order and is consumed only after a successful fit.
            for (std::size_t si = 0; si < p.Stocks.size() && !stop(); ++si)
            {
                const auto& stock = p.Stocks[si];
                if (stockUsed[si] >= stock.Quantity || stock.Material != part.Material ||
                    stock.Width <= 2 * p.EdgeMargin || stock.Height <= 2 * p.EdgeMargin) continue;
                OrderedBin candidate(si, stockUsed[si], stock, p);
                if (auto fit = query(candidate, bins.size(), rectangle))
                {
                    commit(candidate, rectangle, *fit);
                    ++stockUsed[si];
                    bins.push_back(std::move(candidate));
                    activeBins.push_back(bins.size() - 1);
                    opened = true;
                    break;
                }
            }
            // An unplaceable specification does not block later smaller parts or
            // another material. Its complete unmet quantity remains in the result.
            if (!opened) break;
        }
        if (stop()) break;
    }
    return finish();
}
}
