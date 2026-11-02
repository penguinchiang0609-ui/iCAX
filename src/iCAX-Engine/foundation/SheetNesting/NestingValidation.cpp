#include "NestingInternal.h"
#include "NestingSpatialIndex.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <unordered_map>

namespace iCAX::SheetNesting::Detail
{
    std::vector<std::string> CheckProblem(const Problem& p)
    {
        std::vector<std::string> errors;
        const auto finite = [](double x) { return std::isfinite(x); };
        if (!finite(p.PartGap) || p.PartGap < 0 || !finite(p.EdgeMargin) || p.EdgeMargin < 0 ||
            !finite(p.Tolerance) || p.Tolerance < 1e-7 || p.Tolerance > 0.01)
            errors.push_back("Gap/margin must be finite and nonnegative; tolerance must be in [1e-7, 0.01] mm.");
        std::set<std::string> ids;
        std::size_t count = 0;
        for (const auto& part : p.Parts)
        {
            if (part.ID.empty() || !ids.insert(part.ID).second) errors.push_back("Missing or duplicate part ID: " + part.ID);
            if (part.Quantity > 100000 || count > 100000 - std::min<std::size_t>(part.Quantity, 100000))
                errors.push_back("This rule solver accepts at most 100000 demanded instances.");
            else count += part.Quantity;
            std::string error;
            if (!ShapeValid(part.Geometry, p.Tolerance, error)) errors.push_back(part.ID + ": " + error);
            for (double angle : part.AllowedAnglesDegrees)
                if (!finite(angle)) errors.push_back(part.ID + ": non-finite allowed angle.");
        }
        ids.clear();
        for (const auto& stock : p.Stocks)
        {
            if (stock.ID.empty() || !ids.insert(stock.ID).second) errors.push_back("Missing or duplicate stock ID: " + stock.ID);
            if (!finite(stock.Width) || !finite(stock.Height) || stock.Width <= 0 || stock.Height <= 0 ||
                stock.Width > 1e7 || stock.Height > 1e7 || !finite(stock.Cost) || stock.Cost < 0)
                errors.push_back(stock.ID + ": invalid stock size or cost.");
            if (stock.Quantity > 100000) errors.push_back(stock.ID + ": excessive stock quantity.");
        }
        return errors;
    }
}

namespace iCAX::SheetNesting
{
    Validation Validate(const Problem& problem, const Result& result)
    {
        Validation checked;
        checked.Errors = Detail::CheckProblem(problem);
        if (!checked.Errors.empty()) return checked;
        std::unordered_map<std::string, const Part*> parts;
        std::unordered_map<std::string, const Stock*> stocks;
        std::unordered_map<std::string, double> areas;
        std::unordered_map<std::string, std::size_t> counts, remaining;
        for (const auto& p : problem.Parts) { parts.emplace(p.ID, &p); areas.emplace(p.ID, Detail::Area(p.Geometry)); }
        for (const auto& s : problem.Stocks) stocks.emplace(s.ID, &s);
        std::set<std::pair<std::string, std::size_t>> usedSheets, usedInstances;
        const auto fail = [&](const std::string& message) { checked.Errors.push_back(message); };
        for (const auto& layout : result.Layouts)
        {
            const auto it = stocks.find(layout.StockID);
            if (it == stocks.end()) { fail("Unknown stock: " + layout.StockID); continue; }
            const auto& stock = *it->second;
            if (layout.Placements.empty()) fail("An unused empty sheet must not be reported as consumed stock: " + layout.StockID);
            if (layout.StockInstance >= stock.Quantity || !usedSheets.emplace(layout.StockID, layout.StockInstance).second)
                fail("Stock instance duplicated or outside inventory: " + layout.StockID);
            if (!std::isfinite(layout.Width) || !std::isfinite(layout.Height) ||
                std::abs(layout.Width - stock.Width) > problem.Tolerance || std::abs(layout.Height - stock.Height) > problem.Tolerance)
                fail("Layout dimensions differ from stock: " + layout.StockID);
            checked.MaterialCost += stock.Cost;
            checked.SheetArea += stock.Width * stock.Height;
            std::vector<Rect> bounds(layout.Placements.size());
            std::vector<Detail::SpatialBounds> spatial(layout.Placements.size(),
                {std::numeric_limits<double>::quiet_NaN(),0,0,0});
            for(std::size_t i=0;i<layout.Placements.size();++i)
            {
                const auto& placed=layout.Placements[i];const auto part=parts.find(placed.PartID);
                if(part==parts.end()||!std::isfinite(placed.Transform.X)||!std::isfinite(placed.Transform.Y)||
                    !std::isfinite(placed.Transform.AngleDegrees))continue;
                bounds[i]=Detail::Bounds(part->second->Geometry,placed.Transform);
                spatial[i]=Detail::SpatialBounds::From(bounds[i]);
            }
            const Detail::SpatialIndex index(std::move(spatial));
            double minX = stock.Width, minY = stock.Height, maxX = 0, maxY = 0;
            for (std::size_t i = 0; i < layout.Placements.size(); ++i)
            {
                const auto& placed = layout.Placements[i];
                const auto pit = parts.find(placed.PartID);
                if (pit == parts.end()) { fail("Unknown part: " + placed.PartID); continue; }
                const auto& part = *pit->second;
                ++checked.PlacedCount;
                ++counts[part.ID];
                if (placed.Instance >= part.Quantity || !usedInstances.emplace(part.ID, placed.Instance).second)
                    fail("Part instance duplicated or outside demand: " + part.ID);
                const auto& pose = placed.Transform;
                if (!std::isfinite(pose.X) || !std::isfinite(pose.Y) || !std::isfinite(pose.AngleDegrees))
                { fail("Non-finite placement: " + part.ID); continue; }
                if (part.Material != stock.Material) fail("Material mismatch: " + part.ID);
                if (!Detail::AngleAllowed(part, pose.AngleDegrees, problem.Tolerance)) fail("Forbidden angle: " + part.ID);
                if (!Detail::InsideSheet(part.Geometry, pose, stock.Width, stock.Height, problem.EdgeMargin, problem.Tolerance))
                    fail("Part outside usable sheet: " + part.ID);
                const auto box = bounds[i];
                minX = std::min(minX, box.X); minY = std::min(minY, box.Y);
                maxX = std::max(maxX, box.X + box.Width); maxY = std::max(maxY, box.Y + box.Height);
                checked.MaterialArea += areas.at(part.ID);
                for (const auto j : index.Query(Detail::SpatialBounds::From(box),problem.PartGap,i))
                {
                    const auto other = parts.find(layout.Placements[j].PartID);
                    if (other != parts.end() && Detail::ShapesConflict(part.Geometry, pose, other->second->Geometry,
                        layout.Placements[j].Transform, problem.PartGap, problem.Tolerance))
                        fail("Overlap or insufficient gap: " + part.ID + " / " + other->first);
                }
            }
            if (!layout.Placements.empty()) checked.UsedEnvelopeArea += (maxX - minX) * (maxY - minY);
        }
        for (const auto& item : result.Remaining)
        {
            if (!parts.contains(item.PartID) || remaining.contains(item.PartID)) fail("Unknown/duplicate remaining demand: " + item.PartID);
            remaining[item.PartID] = item.Quantity;
        }
        checked.Complete = true;
        for (const auto& p : problem.Parts)
        {
            if (counts[p.ID] > p.Quantity || remaining[p.ID] != p.Quantity - std::min(p.Quantity, counts[p.ID]))
                fail("Placed and remaining quantities do not conserve demand: " + p.ID);
            if (counts[p.ID] != p.Quantity) checked.Complete = false;
        }
        if (result.State == Status::Complete && !checked.Complete) fail("Complete status with missing demand.");
        checked.Valid = checked.Errors.empty();
        return checked;
    }
}
