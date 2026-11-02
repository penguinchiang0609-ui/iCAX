#include "NestingInternal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <numeric>
#include <unordered_map>

namespace iCAX::SheetNesting::Detail
{
    namespace
    {
        struct HorizontalItem
        {
            std::size_t PartIndex = 0;
            double Angle = 0, BottomX = 0, BottomY = 0, Width = 0, Height = 0, Shear = 0;
        };
        struct HorizontalGroup
        {
            std::string Material;
            double Height = 0, MinimumHeight = 0, RepresentativeShear = 0;
            double MinimumShear = 0, MaximumShear = 0, MaximumSlope = 0;
            std::vector<HorizontalItem> Items;
        };
        struct HorizontalBin
        {
            std::size_t StockIndex = 0;
            Layout Output;
            double NextY = 0;
        };
        std::optional<HorizontalItem> Horizontal(const Part& part, std::size_t index, double tolerance)
        {
            const auto kind = Classify(part.Geometry, tolerance).Kind;
            if (kind != ShapeKind::Parallelogram && kind != ShapeKind::Rectangle) return {};
            for (double angle : CandidateAngles(part, tolerance))
            {
                const auto bounds = Bounds(part.Geometry, {0, 0, angle});
                if (bounds.Height <= tolerance) continue;
                std::vector<double> bottom, top;
                for (const auto point : part.Geometry.Outer)
                {
                    const auto p = Transform(point, {0, 0, angle});
                    if (std::abs(p.Y-bounds.Y) <= tolerance) bottom.push_back(p.X);
                    if (std::abs(p.Y-bounds.Y-bounds.Height) <= tolerance) top.push_back(p.X);
                }
                if (bottom.size() < 2 || top.size() < 2) continue;
                const auto [b0,b1] = std::minmax_element(bottom.begin(), bottom.end());
                const auto [t0,t1] = std::minmax_element(top.begin(), top.end());
                const double width = *b1-*b0;
                if (width <= tolerance || std::abs((*t1-*t0)-width) > tolerance) continue;
                return HorizontalItem{index, angle, *b0, bounds.Y, width, bounds.Height, *t0-*b0};
            }
            return {};
        }
    }

    Result RunParallelogramShelves(const Problem& problem, const Options& options)
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
        std::unordered_map<std::string, std::size_t> partByID;
        std::vector<HorizontalGroup> groups;
        for (std::size_t index = 0; index < problem.Parts.size() && !stop(); ++index)
        {
            const auto& part = problem.Parts[index];
            partByID.emplace(part.ID, index);
            if (!part.Quantity) continue;
            const auto item = Horizontal(part, index, problem.Tolerance);
            if (!item) continue;
            auto group = std::find_if(groups.begin(), groups.end(), [&](const auto& g)
                { return g.Material == part.Material && std::abs(g.Height-item->Height) <= problem.Tolerance
                    && std::abs(g.RepresentativeShear-item->Shear) <= problem.Tolerance; });
            if (group == groups.end())
            {
                groups.push_back({part.Material, item->Height, item->Height, item->Shear,
                    std::min(0.0,item->Shear), std::max(0.0,item->Shear), std::abs(item->Shear/item->Height), {*item}});
            }
            else
            {
                group->Height = std::max(group->Height, item->Height);
                group->MinimumHeight = std::min(group->MinimumHeight, item->Height);
                group->MinimumShear = std::min(group->MinimumShear, item->Shear);
                group->MaximumShear = std::max(group->MaximumShear, item->Shear);
                group->MaximumSlope = std::max(group->MaximumSlope, std::abs(item->Shear/item->Height));
                group->Items.push_back(*item);
            }
        }
        for (auto& group : groups)
            std::stable_sort(group.Items.begin(), group.Items.end(), [&](const auto& a, const auto& b)
            { return a.Width != b.Width ? a.Width > b.Width : problem.Parts[a.PartIndex].ID < problem.Parts[b.PartIndex].ID; });
        std::stable_sort(groups.begin(), groups.end(), [&](const auto& a, const auto& b)
        {
            if (a.Height != b.Height) return a.Height > b.Height;
            if (std::abs(a.RepresentativeShear) != std::abs(b.RepresentativeShear))
                return std::abs(a.RepresentativeShear) > std::abs(b.RepresentativeShear);
            if (a.Material != b.Material) return a.Material < b.Material;
            return problem.Parts[a.Items.front().PartIndex].ID < problem.Parts[b.Items.front().PartIndex].ID;
        });
        result.Stats.CandidatePatterns = groups.size();
        const double preparation = std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        std::vector<HorizontalBin> bins;
        std::size_t nextGroup = 1;
        double verification = 0;

        const auto fill = [&](HorizontalBin& bin, const HorizontalGroup& group)
        {
            if (stop()) return false;
            const auto& stock = problem.Stocks[bin.StockIndex];
            const double width = stock.Width-2*problem.EdgeMargin;
            const double height = stock.Height-2*problem.EdgeMargin-bin.NextY;
            if (stock.Material != group.Material || height+problem.Tolerance < group.Height) return false;
            const double horizontalGap = problem.PartGap*std::sqrt(1+group.MaximumSlope*group.MaximumSlope);
            const double span = group.MaximumShear-group.MinimumShear;
            if (!std::isfinite(horizontalGap) || width-span <= problem.Tolerance) return false;

            // Reduce this same-height/shear row to the existing bounded rectangle
            // row planner. Its tokens are virtualWidth+gap = baseWidth+horizontalGap.
            // The matching virtual capacity subtracts the shear span exactly once.
            // Returned poses are only a plan; all published identities/geometry
            // refer to the original physical input and are checked below.
            Problem virtualProblem;
            virtualProblem.PartGap = problem.PartGap;
            virtualProblem.Tolerance = problem.Tolerance;
            std::unordered_map<std::string, const HorizontalItem*> mapping;
            for (const auto& item : group.Items)
            {
                const auto& part = problem.Parts[item.PartIndex];
                const auto remaining = part.Quantity-consumed[item.PartIndex];
                if (!remaining) continue;
                const double virtualWidth = item.Width+horizontalGap-problem.PartGap;
                if (!std::isfinite(virtualWidth) || virtualWidth <= problem.Tolerance) continue;
                Part virtualPart;
                virtualPart.ID = part.ID; virtualPart.Material = part.Material;
                virtualPart.Quantity = remaining; virtualPart.FreeRotation = false;
                virtualPart.Geometry.Outer = {{0,0},{virtualWidth,0},{virtualWidth,group.Height},{0,group.Height}};
                virtualProblem.Parts.push_back(std::move(virtualPart));
                mapping.emplace(part.ID, &item);
            }
            if (virtualProblem.Parts.empty()) return false;
            const double virtualWidth = width-span+horizontalGap-problem.PartGap;
            if (!std::isfinite(virtualWidth) || virtualWidth <= problem.Tolerance) return false;
            virtualProblem.Stocks.push_back({"row-plan", virtualWidth, height, 1, 1, group.Material});
            Options inner = options;
            inner.Cancel = stop;
            inner.TimeLimitSeconds = std::max(1e-9, options.TimeLimitSeconds-
                std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());
            inner.MaxPlacementAttempts = options.MaxPlacementAttempts-result.Stats.PlacementAttempts;
            const auto planned = RunRectanglePatternShelves(virtualProblem, inner, false);
            result.Stats.PlacementAttempts += planned.Stats.PlacementAttempts;
            if (planned.Layouts.empty()) return false;
            const double initialY = bin.NextY;
            std::map<std::size_t, std::size_t> rowGroups;
            std::map<std::size_t, std::size_t> rowCounts;
            std::size_t committed = 0;
            // A cancelled inner planner still owns completed placements. Mapping
            // those already computed values is an atomic publication step, not a
            // new placement search that can discard their quantity bookkeeping.
            for (const auto& placement : planned.Layouts.front().Placements)
            {
                const auto found = mapping.find(placement.PartID);
                if (found == mapping.end()) continue;
                const auto& item = *found->second; const auto& part = problem.Parts[item.PartIndex];
                if (consumed[item.PartIndex] >= part.Quantity) continue;
                const Pose pose{problem.EdgeMargin-group.MinimumShear+placement.Transform.X-item.BottomX,
                    problem.EdgeMargin+initialY+placement.Transform.Y-item.BottomY, item.Angle};
                const auto checkedAt = std::chrono::steady_clock::now();
                bool valid = AngleAllowed(part, pose.AngleDegrees, problem.Tolerance) &&
                    InsideSheet(part.Geometry, pose, stock.Width, stock.Height, problem.EdgeMargin, problem.Tolerance);
                for (const auto& other : bin.Output.Placements)
                {
                    if (!valid) break;
                    const auto index = partByID.at(other.PartID);
                    if (ShapesConflict(part.Geometry, pose, problem.Parts[index].Geometry, other.Transform,
                        problem.PartGap, problem.Tolerance)) valid = false;
                }
                verification += std::chrono::duration<double>(std::chrono::steady_clock::now()-checkedAt).count();
                if (!valid) continue;
                if (!rowGroups.contains(placement.Group)) rowGroups.emplace(placement.Group, nextGroup++);
                bin.Output.Placements.push_back({part.ID, consumed[item.PartIndex]++, pose, rowGroups.at(placement.Group), false});
                bin.Output.MaterialArea += Area(part.Geometry);
                ++rowCounts[placement.Group]; ++committed;
                bin.NextY = std::max(bin.NextY, initialY+placement.Transform.Y+group.Height+problem.PartGap);
            }
            for (auto [row,count] : rowCounts) if (count>1) ++result.Stats.CombinationGroups;
            return committed>0;
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
                if (std::all_of(group.Items.begin(),group.Items.end(),[&](const auto& item)
                    { return consumed[item.PartIndex] == problem.Parts[item.PartIndex].Quantity; })) break;
                bool placed = false;
                for (auto& bin : bins) if (fill(bin,group)) { placed = true; break; }
                if (placed) continue;
                if (stop()) break;
                for (auto index : stockOrder)
                {
                    if (stop()) break;
                    const auto& stock = problem.Stocks[index];
                    if (stockUsed[index] >= stock.Quantity || stock.Material != group.Material) continue;
                    HorizontalBin bin{index, {stock.ID,stockUsed[index],stock.Width,stock.Height,{},0}, 0};
                    if (fill(bin,group))
                    { ++stockUsed[index]; bins.push_back(std::move(bin)); placed = true; break; }
                }
                if (!placed) break;
            }
            if (stop()) break;
        }
        for (auto& bin : bins) if (!bin.Output.Placements.empty()) result.Layouts.push_back(std::move(bin.Output));
        for (std::size_t i = 0; i < problem.Parts.size(); ++i)
            if (consumed[i] < problem.Parts[i].Quantity) result.Remaining.push_back({problem.Parts[i].ID,problem.Parts[i].Quantity-consumed[i]});
        result.State = cancelled ? Status::Cancelled : result.Remaining.empty() ? Status::Complete : Status::Partial;
        if (cancelled) result.Diagnostics.push_back("Parallelogram shelf job cancelled; completed physical placements retained.");
        if (exhausted) result.Diagnostics.push_back("Parallelogram shelf time or attempt budget exhausted.");
        if (!cancelled && !exhausted && !result.Remaining.empty())
            result.Diagnostics.push_back("Horizontal same-height/shear row rules or stock exhausted; no infeasibility proof.");
        result.Stats.ElapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        result.Stats.PreparationSeconds = preparation;
        result.Stats.VerificationSeconds = verification;
        result.Stats.PlacementSeconds = std::max(0.0,result.Stats.ElapsedSeconds-preparation-verification);
        return result;
    }
}
