#include "SheetNestingRepeatCell.h"
#include "NestingRectangleBlockSearch.h"
#include "NestingRectangleDirections.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace iCAX::SheetNesting
{
    namespace
    {
        constexpr std::size_t PrivateInstanceCap = 4096;
        std::size_t Add(std::size_t a, std::size_t b)
        { return a + std::min(b, std::numeric_limits<std::size_t>::max() - a); }
        std::size_t Multiply(std::size_t a, std::size_t b)
        { return b && a > std::numeric_limits<std::size_t>::max() / b ? std::numeric_limits<std::size_t>::max() : a * b; }
        bool SameArea(double a, double b)
        {
            return std::isfinite(a) && std::isfinite(b) &&
                std::abs(a-b) <= 128 * std::numeric_limits<double>::epsilon() * std::max({1.0,std::abs(a),std::abs(b)});
        }

        class RepeatCell final : public IStrategy
        {
        public:
            RepeatCell(StrategyFactory constructor, std::size_t columns, std::size_t rows)
                : Constructor(std::move(constructor)), Columns(columns), Rows(rows) {}

            Result Run(const Problem& input, const Options& options) override
            {
                Detail::RectangleBlockBudget budget{options};
                Statistics work;
                std::map<std::string, std::vector<Detail::RectangleDirection>> rectangles;
                std::map<std::string, double> sourceScales;
                double preparation = 0, verification = 0, expansion = 0;
                const auto probe = [&]
                {
                    Detail::CheckRectangleBlockBudget(budget, true);
                    Detail::ProbeRectangleBlockBudget(budget);
                };
                const auto check = [&]
                {
                    Detail::CheckRectangleBlockBudget(budget, true);
                    Detail::CheckRectangleBlockBudget(budget);
                };
                const auto finish = [&](Result result)
                {
                    result.Stats = work;
                    result.Stats.PlacementAttempts = budget.Attempts;
                    result.Stats.ElapsedSeconds = budget.Elapsed();
                    result.Stats.PreparationSeconds = std::max(0.0, work.PreparationSeconds) + preparation;
                    result.Stats.VerificationSeconds = std::max(0.0, work.VerificationSeconds) + verification;
                    result.Stats.PlacementSeconds = std::max(0.0, work.PlacementSeconds) + expansion;
                    return result;
                };
                const auto empty = [&](std::string reason, Status state = Status::Partial)
                {
                    Result result;
                    // Demand metadata is retained even after the cooperative work
                    // budget stops. Probe saturates without doing more geometry.
                    for (const auto& part : input.Parts)
                    {
                        budget.Probe();
                        if (part.Quantity) result.Remaining.push_back({part.ID, part.Quantity});
                    }
                    result.State = budget.Cancelled ? Status::Cancelled : state;
                    result.Diagnostics.push_back(std::move(reason));
                    return finish(std::move(result));
                };
                // Validate is a shared API without a budget parameter. Charge its
                // input walks and a conservative all-pairs placement audit first;
                // its actual elapsed time remains inside this caller deadline.
                const auto validate = [&](const Problem& problem, const Result& result)
                {
                    const double start = budget.Elapsed();
                    for (const auto& part : problem.Parts)
                    {
                        probe();
                        std::size_t vertices = part.Geometry.Outer.size();
                        for (const auto& hole : part.Geometry.Holes) { probe(); vertices = Add(vertices, hole.size()); }
                        for (std::size_t i = 0; i < vertices; ++i)
                        {
                            probe();
                            for (std::size_t j = 0; j < i; ++j) probe();
                        }
                        for (double angle : part.AllowedAnglesDegrees) { (void)angle; probe(); }
                    }
                    for (const auto& stock : problem.Stocks) { (void)stock; probe(); }
                    for (const auto& layout : result.Layouts)
                    {
                        probe();
                        for (std::size_t i = 0; i < layout.Placements.size(); ++i)
                        {
                            probe();
                            for (std::size_t j = 0; j < i; ++j) probe();
                        }
                    }
                    for (const auto& item : result.Remaining) { (void)item; probe(); }
                    auto checked = Validate(problem, result);
                    if (checked.Valid && checked.Complete)
                        for (const auto& layout : result.Layouts)
                        {
                            const Stock* physicalStock=nullptr;
                            for(const auto& stock:problem.Stocks)
                            {probe();if(stock.ID==layout.StockID)physicalStock=&stock;}
                            if(!physicalStock)
                            {checked.Valid=checked.Complete=false;break;}
                            struct AuditedBox { Rect Bounds; double Roundoff; };
                            std::vector<AuditedBox> boxes;
                            std::map<std::string, const Part*> parts;
                            for (const auto& part : problem.Parts) { probe(); parts.emplace(part.ID, &part); }
                            for (const auto& placed : layout.Placements)
                            {
                                probe();
                                const auto found = rectangles.find(placed.PartID);
                                if (found == rectangles.end())
                                { checked.Valid = checked.Complete = false; break; }
                                const auto& part = *parts.at(placed.PartID);
                                const auto& pose = placed.Transform;
                                const double tolerance = 128 * std::numeric_limits<double>::epsilon() *
                                    std::max({1.0,sourceScales.at(placed.PartID),physicalStock->Width,physicalStock->Height,std::abs(pose.X),std::abs(pose.Y)});
                                const double angleTolerance = 128 * std::numeric_limits<double>::epsilon() * 360;
                                const auto box = Detail::Bounds(part.Geometry, pose);
                                bool directionAllowed = false;
                                for (const auto& direction : found->second)
                                {
                                    probe();
                                    const double delta = std::abs(Detail::NormalizeAngle(pose.AngleDegrees) - Detail::NormalizeAngle(direction.Offset.AngleDegrees));
                                    if (std::min(delta,360-delta) <= angleTolerance &&
                                        std::abs(box.Width-direction.Width) <= tolerance &&
                                        std::abs(box.Height-direction.Height) <= tolerance)
                                        directionAllowed = true;
                                }
                                if (!directionAllowed || !std::isfinite(tolerance) ||
                                    box.X < problem.EdgeMargin-tolerance || box.Y < problem.EdgeMargin-tolerance ||
                                    box.X+box.Width > physicalStock->Width-problem.EdgeMargin+tolerance ||
                                    box.Y+box.Height > physicalStock->Height-problem.EdgeMargin+tolerance)
                                { checked.Valid = checked.Complete = false; break; }
                                // Recheck every actual edge at the returned pose;
                                // the bounding rectangle is not substituted for material.
                                const auto& ring = part.Geometry.Outer;
                                for (std::size_t i=0;i<ring.size();++i)
                                {
                                    probe();
                                    const auto a=Detail::Transform(ring[i],pose), b=Detail::Transform(ring[(i+1)%ring.size()],pose);
                                    const bool boundary=std::abs(a.X-box.X)<=tolerance||std::abs(a.X-box.X-box.Width)<=tolerance||
                                        std::abs(a.Y-box.Y)<=tolerance||std::abs(a.Y-box.Y-box.Height)<=tolerance;
                                    if (!boundary || std::min(std::abs(a.X-b.X),std::abs(a.Y-b.Y))>tolerance)
                                    { checked.Valid = checked.Complete = false; break; }
                                }
                                if (!checked.Valid) break;
                                for (const auto& previous : boxes)
                                {
                                    probe();
                                    const double t=std::max(tolerance,previous.Roundoff);
                                    const auto& other=previous.Bounds;
                                    const double overlapX=std::min(box.X+box.Width,other.X+other.Width)-std::max(box.X,other.X);
                                    const double overlapY=std::min(box.Y+box.Height,other.Y+other.Height)-std::max(box.Y,other.Y);
                                    if (overlapX>t && overlapY>t)
                                    { checked.Valid = checked.Complete = false; break; }
                                }
                                if (!checked.Valid) break;
                                boxes.push_back({box,tolerance});
                            }
                            if (!checked.Valid)
                            { checked.Errors.push_back("Repeat-cell roundoff-level true-rectangle bounds/overlap audit failed."); break; }
                        }
                    verification += budget.Elapsed() - start;
                    check();
                    return checked;
                };
                try
                {
                    check();
                    if (!std::isfinite(options.TimeLimitSeconds) || options.TimeLimitSeconds <= 0 ||
                        !std::isfinite(input.EdgeMargin) || input.EdgeMargin < 0)
                        return empty("repeat-cell requires finite limits and margin.");
                    const std::size_t cells = Columns * Rows;
                    std::size_t total = 0;
                    for (const auto& part : input.Parts)
                    {
                        probe();
                        if (part.Quantity > PrivateInstanceCap - total)
                            return empty("repeat-cell private scope is at most 4096 original instances.");
                        total += part.Quantity;
                        if (part.Quantity && part.Quantity % cells)
                            return empty("repeat-cell raw-row scope requires each positive Quantity divisible by cells; equivalent quantity-one rows are not merged.");
                    }
                    if (!total)
                    {
                        Result result; result.State = Status::Complete;
                        const auto checked = validate(input, result);
                        if (!checked.Valid || !checked.Complete)
                            return empty("repeat-cell empty-demand original input is invalid.", Status::InvalidInput);
                        check(); result.GeometryValidated = true;
                        preparation = std::max(0.0, budget.Elapsed() - verification);
                        return finish(std::move(result));
                    }
                    if (input.PartGap != 0)
                        return empty("repeat-cell requires zero PartGap for nonempty repeated units.");
                    const Stock* selected = nullptr;
                    for (const auto& stock : input.Stocks)
                    {
                        probe(); bool compatible = stock.Quantity > 0;
                        for (const auto& part : input.Parts)
                        { probe(); if (part.Quantity && part.Material != stock.Material) compatible = false; }
                        if (!compatible) continue;
                        if (selected) return empty("repeat-cell requires exactly one compatible stock specification.");
                        selected = &stock;
                    }
                    if (!selected) return empty("repeat-cell has no single compatible stock specification.");
                    const double width = (selected->Width - 2 * input.EdgeMargin) / double(Columns);
                    const double height = (selected->Height - 2 * input.EdgeMargin) / double(Rows);
                    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
                        return empty("repeat-cell stock has no positive usable interior.");
                    double demandedArea = 0, areaCompensation = 0;
                    for (const auto& part : input.Parts)
                    {
                        probe();
                        double sourceScale=1;
                        for (auto point : part.Geometry.Outer)
                        { probe(); sourceScale=std::max({sourceScale,std::abs(point.X),std::abs(point.Y)}); }
                        for (const auto& hole : part.Geometry.Holes)
                            for (auto point : hole) { (void)point; probe(); }
                        if (part.Quantity)
                        {
                            if (!std::isfinite(sourceScale) || part.Geometry.Radius != 0 || !part.Geometry.Holes.empty())
                                return empty("repeat-cell dense scope requires true polygon rectangles without circles or holes.");
                            const double roundoff=128*std::numeric_limits<double>::epsilon()*std::max({sourceScale,selected->Width,selected->Height,1.0});
                            // Charge the bounded direction-by-vertex planning walks too.
                            const auto directions=part.FreeRotation?std::size_t(8):std::max<std::size_t>(1,part.AllowedAnglesDegrees.size());
                            for(std::size_t direction=0;direction<directions;++direction)
                                for(auto point:part.Geometry.Outer){(void)point;probe();}
                            const auto description=Detail::FindRectangleDirections(part,roundoff,[&]{probe();return false;});
                            std::vector<Detail::RectangleDirection> strict;
                            for(const auto& direction:description.Directions)
                            {probe();if(SameArea(description.Area,direction.Width*direction.Height))strict.push_back(direction);}
                            if (!description.Supported || strict.empty())
                                return empty("repeat-cell original contour is not a materially full axis-alignable rectangle at roundoff precision.");
                            if (!rectangles.emplace(part.ID,std::move(strict)).second)
                                return empty("repeat-cell duplicate original Part ID.");
                            sourceScales.emplace(part.ID,sourceScale);
                        }
                        const double term = Detail::Area(part.Geometry) * double(part.Quantity);
                        if (!std::isfinite(term) || term < 0) return empty("repeat-cell demanded area is invalid.");
                        const double value = term - areaCompensation, next = demandedArea + value;
                        areaCompensation = (next-demandedArea)-value; demandedArea = next;
                    }
                    if (!SameArea(demandedArea, width * height * double(cells)))
                        return empty("repeat-cell supports materially full rectangular repetition only; original demand does not fill the usable stock area.");
                    Problem unit; unit.PartGap = 0; unit.EdgeMargin = 0; unit.Tolerance = input.Tolerance;
                    auto stock = *selected; stock.Width = width; stock.Height = height; stock.Quantity = 1;
                    unit.Stocks.push_back(std::move(stock));
                    std::map<std::string, std::size_t> quantities;
                    std::size_t unitCount = 0;
                    for (const auto& original : input.Parts)
                    {
                        probe(); Part part;
                        part.ID = original.ID; part.Material = original.Material;
                        part.Quantity = original.Quantity / cells; part.FreeRotation = original.FreeRotation;
                        part.Geometry.Radius = original.Geometry.Radius; part.Geometry.Center = original.Geometry.Center;
                        for (auto point : original.Geometry.Outer) { probe(); part.Geometry.Outer.push_back(point); }
                        for (const auto& hole : original.Geometry.Holes)
                        {
                            probe(); std::vector<Point> copy;
                            for (auto point : hole) { probe(); copy.push_back(point); }
                            part.Geometry.Holes.push_back(std::move(copy));
                        }
                        for (double angle : original.AllowedAnglesDegrees) { probe(); part.AllowedAnglesDegrees.push_back(angle); }
                        if (!quantities.emplace(part.ID, part.Quantity).second) return empty("repeat-cell duplicate original Part ID.");
                        unitCount += part.Quantity; unit.Parts.push_back(std::move(part));
                    }
                    check();
                    auto constructor = Constructor(); // A fresh inner strategy for every Run.
                    check();
                    if (!constructor) return empty("repeat-cell constructor factory returned null.", Status::Failed);
                    preparation = budget.Elapsed();
                    Options inner = options;
                    inner.TimeLimitSeconds = options.TimeLimitSeconds - budget.Elapsed();
                    inner.MaxPlacementAttempts = options.MaxPlacementAttempts - budget.Attempts;
                    inner.Cancel = [&]
                    {
                        // A deadline is not cancellation. Latch only a true caller callback.
                        if (!budget.Cancelled && options.Cancel && options.Cancel()) budget.Cancelled = true;
                        return budget.Cancelled;
                    };
                    auto cell = constructor->Run(unit, inner);
                    work = cell.Stats;
                    const auto remaining = options.MaxPlacementAttempts - budget.Attempts;
                    budget.Attempts += std::min(remaining, cell.Stats.PlacementAttempts);
                    if (cell.Stats.PlacementAttempts > remaining) budget.Exhausted = true;
                    check();
                    if (cell.State != Status::Complete || cell.Layouts.size() != 1 ||
                        cell.Layouts[0].Placements.size() != unitCount)
                        return empty("repeat-cell constructor did not return one complete unit; no partial unit is expanded.");
                    const auto checkedUnit = validate(unit, cell);
                    if (!checkedUnit.Valid || !checkedUnit.Complete)
                        return empty("repeat-cell constructor unit failed original-geometry/inventory validation.");
                    if (!SameArea(checkedUnit.MaterialArea, width * height))
                        return empty("repeat-cell unit is not materially full; sparse units are not repeated.");
                    std::map<std::size_t, std::size_t> groups;
                    for (const auto& placed : cell.Layouts[0].Placements)
                    {
                        probe();
                        if (placed.Group) groups.try_emplace(placed.Group, groups.size() + 1);
                    }
                    Result result; result.State = Status::Complete;
                    Layout layout; layout.StockID = selected->ID; layout.StockInstance = 0;
                    layout.Width = selected->Width; layout.Height = selected->Height;
                    layout.MaterialArea = checkedUnit.MaterialArea * double(cells);
                    const double expansionStart = budget.Elapsed();
                    for (std::size_t cy = 0; cy < Rows; ++cy)
                        for (std::size_t cx = 0; cx < Columns; ++cx)
                        {
                            probe(); const auto cellIndex = cy * Columns + cx;
                            for (const auto& placed : cell.Layouts[0].Placements)
                            {
                                probe(); auto expanded = placed;
                                const auto quantity = quantities.at(placed.PartID);
                                expanded.Instance = cellIndex * quantity + placed.Instance;
                                expanded.Transform.X += input.EdgeMargin + double(cx) * width;
                                expanded.Transform.Y += input.EdgeMargin + double(cy) * height;
                                if (placed.Group) expanded.Group = cellIndex * groups.size() + groups.at(placed.Group);
                                layout.Placements.push_back(std::move(expanded));
                            }
                        }
                    expansion += budget.Elapsed() - expansionStart;
                    result.Layouts.push_back(std::move(layout));
                    const auto checkedOriginal = validate(input, result);
                    if (!checkedOriginal.Valid || !checkedOriginal.Complete)
                        return empty("repeat-cell expansion failed original input validation.");
                    check(); result.GeometryValidated = true;
                    work.CombinationGroups = Multiply(work.CombinationGroups, cells);
                    work.CircleLatticeGroups = Multiply(work.CircleLatticeGroups, cells);
                    work.GapFilledParts = Multiply(work.GapFilledParts, cells);
                    result.Diagnostics.push_back("repeat-cell raw-row hypothesis " + std::to_string(Columns) + "x" + std::to_string(Rows));
                    check();
                    return finish(std::move(result));
                }
                catch (const Detail::RectangleBlockInterrupted&)
                { return empty("repeat-cell caller budget or cancellation stopped the job; no unit/expanded layouts retained."); }
                catch (const std::exception& error)
                { return empty(std::string("repeat-cell constructor/wrapper failed: ") + error.what(), Status::Failed); }
                catch (...)
                { return empty("repeat-cell constructor/wrapper threw a non-standard exception.", Status::Failed); }
            }
        private:
            StrategyFactory Constructor;
            std::size_t Columns, Rows;
        };
    }

    StrategyFactory MakeRectangleRepeatCellFactory(StrategyFactory constructor, std::size_t columns, std::size_t rows)
    {
        if (!constructor || !columns || !rows || columns > PrivateInstanceCap || rows > PrivateInstanceCap / columns)
            throw std::invalid_argument("repeat-cell needs a constructor and positive columns*rows <=4096.");
        return [constructor = std::move(constructor), columns, rows]
        { return std::make_unique<RepeatCell>(constructor, columns, rows); };
    }
}
