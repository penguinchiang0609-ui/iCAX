#include "NestingInternal.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace iCAX::SheetNesting::Detail
{
namespace
{
    struct ContactEdge { Point A, B; double Length; };
    struct ParaBin { std::size_t StockIndex; Layout Output; };
    struct ContactChoice
    {
        Pose Transform;
        double EnvelopeArea = 0, Top = 0, Right = 0;
    };
    auto ContactRank(const ContactChoice& c)
    {
        return std::tuple(c.EnvelopeArea, c.Top, c.Right, c.Transform.Y, c.Transform.X, c.Transform.AngleDegrees);
    }
    std::vector<ContactEdge> ContactEdges(const Shape& shape, Pose pose, double tolerance)
    {
        std::vector<ContactEdge> result;
        for (std::size_t i = 0; i < shape.Outer.size(); ++i)
        {
            const Point a = Transform(shape.Outer[i], pose), b = Transform(shape.Outer[(i + 1) % shape.Outer.size()], pose);
            const double length = std::hypot(b.X - a.X, b.Y - a.Y);
            if (length > tolerance) result.push_back({ a, b, length });
        }
        std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.Length > b.Length; });
        // Classify accepts collinear subdivisions. Keep long contact features bounded even for subdivided input.
        if (result.size() > 16) result.resize(16);
        return result;
    }
    double ContactAngle(const ContactEdge& edge)
    { return std::atan2(edge.B.Y - edge.A.Y, edge.B.X - edge.A.X) * 180 / Pi; }
}

Result RunParallelogramRules(const Problem& problem, const Options& options)
{
    const auto started = std::chrono::steady_clock::now();
    Result result;
    auto errors = CheckProblem(problem);
    if (!std::isfinite(options.TimeLimitSeconds) || options.TimeLimitSeconds <= 0 || options.MaxPlacementAttempts == 0)
        errors.push_back("Parallelogram rules need a positive finite time and attempt budget.");
    if (!errors.empty())
    {
        result.State = Status::InvalidInput; result.Diagnostics = std::move(errors);
        for (const auto& p : problem.Parts) if (p.Quantity) result.Remaining.push_back({ p.ID, p.Quantity });
        return result;
    }
    bool cancelled = false, exhausted = false;
    const auto stop = [&]()
    {
        if (cancelled || exhausted) return true;
        if (options.Cancel && options.Cancel()) { cancelled = true; return true; }
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        if (elapsed >= options.TimeLimitSeconds || result.Stats.PlacementAttempts >= options.MaxPlacementAttempts)
        { exhausted = true; return true; }
        return false;
    };
    std::vector<std::size_t> consumed(problem.Parts.size()), stockUsed(problem.Stocks.size()), order;
    std::vector<double> areas(problem.Parts.size());
    std::unordered_map<std::string, std::size_t> partByID;
    std::size_t skipped = 0;
    for (std::size_t i = 0; i < problem.Parts.size() && !stop(); ++i)
    {
        partByID.emplace(problem.Parts[i].ID, i);
        const auto kind = Classify(problem.Parts[i].Geometry, problem.Tolerance).Kind;
        if (kind == ShapeKind::Rectangle || kind == ShapeKind::Parallelogram)
        { areas[i] = Area(problem.Parts[i].Geometry); if (problem.Parts[i].Quantity) order.push_back(i); }
        else if (problem.Parts[i].Quantity) ++skipped;
    }
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b)
    {
        if (areas[a] != areas[b]) return areas[a] > areas[b];
        return problem.Parts[a].ID < problem.Parts[b].ID;
    });
    std::vector<std::size_t> stockOrder(problem.Stocks.size());
    std::iota(stockOrder.begin(), stockOrder.end(), 0);
    std::stable_sort(stockOrder.begin(), stockOrder.end(), [&](auto a, auto b)
    {
        const auto& x = problem.Stocks[a]; const auto& y = problem.Stocks[b];
        const double dx = x.Cost / (x.Width * x.Height), dy = y.Cost / (y.Width * y.Height);
        if (dx != dy) return dx < dy;
        if (x.Cost != y.Cost) return x.Cost < y.Cost;
        return x.ID < y.ID;
    });
    std::vector<ParaBin> bins;
    std::size_t nextGroup = 1;

    const auto choose = [&](const ParaBin& bin, std::size_t partIndex) -> std::optional<ContactChoice>
    {
        if (stop()) return std::nullopt;
        const auto& part = problem.Parts[partIndex];
        if (part.Material != problem.Stocks[bin.StockIndex].Material) return std::nullopt;
        auto angles = CandidateAngles(part, problem.Tolerance);
        const auto localEdges = ContactEdges(part.Geometry, {}, problem.Tolerance);
        std::vector<ContactEdge> placedEdges;
        double usedMinX = bin.Output.Width, usedMinY = bin.Output.Height, usedRight = 0, usedTop = 0;
        for (const auto& placed : bin.Output.Placements)
        {
            if (stop()) return std::nullopt;
            const auto& other = problem.Parts[partByID.at(placed.PartID)];
            const auto b = Bounds(other.Geometry, placed.Transform);
            usedMinX = std::min(usedMinX, b.X); usedMinY = std::min(usedMinY, b.Y);
            usedRight = std::max(usedRight, b.X + b.Width); usedTop = std::max(usedTop, b.Y + b.Height);
            auto edges = ContactEdges(other.Geometry, placed.Transform, problem.Tolerance);
            placedEdges.insert(placedEdges.end(), edges.begin(), edges.end());
        }
        // Rotation restrictions remain hard constraints. Free parts additionally align to actual neighbours.
        if (part.FreeRotation)
        {
            const auto addAngle = [&](double a)
            {
                a = NormalizeAngle(a);
                for (double old : angles)
                { const double d = std::abs(a - old); if (std::min(d, 360 - d) <= problem.Tolerance) return; }
                if (angles.size() < 32) angles.push_back(a);
            };
            for (const auto& e : placedEdges)
            {
                if (stop()) return std::nullopt;
                for (const auto& local : localEdges)
                { addAngle(ContactAngle(e) - ContactAngle(local)); addAngle(ContactAngle(e) - ContactAngle(local) + 180); }
            }
        }
        std::optional<ContactChoice> best;
        std::unordered_set<std::string> seen;
        for (double angle : angles)
        {
            if (stop()) break;
            const auto box = Bounds(part.Geometry, { 0, 0, angle });
            const auto rotatedEdges = ContactEdges(part.Geometry, { 0, 0, angle }, problem.Tolerance);
            const double margin = problem.EdgeMargin, gap = problem.PartGap;
            const auto emit = [&](double x, double y)
            {
                if (stop() || !std::isfinite(x) || !std::isfinite(y)) return;
                std::ostringstream key;
                key << std::llround(x / problem.Tolerance) << ':' << std::llround(y / problem.Tolerance)
                    << ':' << std::llround(NormalizeAngle(angle) * 1e7);
                if (!seen.insert(key.str()).second) return;
                ++result.Stats.PlacementAttempts;
                const Pose pose{ x, y, NormalizeAngle(angle) };
                if (!AngleAllowed(part, pose.AngleDegrees, problem.Tolerance)
                    || !InsideSheet(part.Geometry, pose, bin.Output.Width, bin.Output.Height, margin, problem.Tolerance)) return;
                const auto b = Bounds(part.Geometry, pose);
                const double minX = bin.Output.Placements.empty() ? b.X : std::min(usedMinX, b.X);
                const double minY = bin.Output.Placements.empty() ? b.Y : std::min(usedMinY, b.Y);
                const double right = std::max(usedRight, b.X + b.Width), top = std::max(usedTop, b.Y + b.Height);
                ContactChoice candidate{ pose, (right - minX) * (top - minY), top, right };
                if (best && !(ContactRank(candidate) < ContactRank(*best))) return;
                for (const auto& placed : bin.Output.Placements)
                {
                    if (stop()) return;
                    const auto& other = problem.Parts[partByID.at(placed.PartID)];
                    if (ShapesConflict(part.Geometry, pose, other.Geometry, placed.Transform, gap, problem.Tolerance)) return;
                }
                best = candidate;
            };
            // Four board phases provide deterministic seeds, including restricted and non-axis-aligned parts.
            const double left = margin - box.X, bottom = margin - box.Y;
            const double right = bin.Output.Width - margin - box.X - box.Width;
            const double top = bin.Output.Height - margin - box.Y - box.Height;
            emit(left, bottom); emit(right, bottom); emit(left, top); emit(right, top);
            for (const auto& placed : bin.Output.Placements)
            {
                if (stop()) break;
                const auto& other = problem.Parts[partByID.at(placed.PartID)];
                const auto ob = Bounds(other.Geometry, placed.Transform);
                const auto edges = ContactEdges(other.Geometry, placed.Transform, problem.Tolerance);
                // Bounded basis-row and endpoint phases, valid across different lengths and shears.
                for (const auto& a : edges)
                {
                    if (stop()) break;
                    const double ux = a.B.X - a.A.X, uy = a.B.Y - a.A.Y;
                    if (placed.PartID == part.ID && std::abs(NormalizeAngle(angle - placed.Transform.AngleDegrees)) <= problem.Tolerance)
                    {
                        emit(placed.Transform.X + ux, placed.Transform.Y + uy);
                        emit(placed.Transform.X - ux, placed.Transform.Y - uy);
                    }
                    for (const auto& b : rotatedEdges)
                    {
                        if (stop()) break;
                        const double vx = b.B.X - b.A.X, vy = b.B.Y - b.A.Y;
                        if (std::abs(ux * vy - uy * vx) > problem.Tolerance * std::max(a.Length, b.Length)) continue;
                        const Point normal{ -uy / a.Length, ux / a.Length };
                        const Point ap[] = { a.A, a.B, { (a.A.X + a.B.X) / 2, (a.A.Y + a.B.Y) / 2 } };
                        const Point bp[] = { b.A, b.B, { (b.A.X + b.B.X) / 2, (b.A.Y + b.B.Y) / 2 } };
                        for (int side : { -1, 1 }) for (auto p : ap) for (auto q : bp)
                            emit(p.X - q.X + side * gap * normal.X, p.Y - q.Y + side * gap * normal.Y);
                    }
                }
                // Bounding contacts add rectangular phases; material checks, rather than these boxes, decide fit.
                const double ys[] = { ob.Y - box.Y, ob.Y + ob.Height - box.Height - box.Y,
                    ob.Y + ob.Height + gap - box.Y, ob.Y - box.Height - gap - box.Y };
                const double xs[] = { ob.X - box.X, ob.X + ob.Width - box.Width - box.X,
                    ob.X + ob.Width + gap - box.X, ob.X - box.Width - gap - box.X };
                for (double x : xs) for (double y : ys) emit(x, y);
            }
        }
        result.Stats.CandidatePatterns += angles.size();
        return best;
    };
    const auto commit = [&](ParaBin& bin, std::size_t index, const ContactChoice& choice)
    {
        const auto& part = problem.Parts[index];
        bin.Output.Placements.push_back({ part.ID, consumed[index]++, choice.Transform, nextGroup++, false });
        bin.Output.MaterialArea += areas[index];
    };
    // Each type is tried against all already-open sheets before consuming another physical stock instance.
    for (const auto index : order)
    {
        while (consumed[index] < problem.Parts[index].Quantity && !stop())
        {
            std::optional<ContactChoice> best;
            std::size_t bestBin = 0;
            for (std::size_t bi = 0; bi < bins.size() && !stop(); ++bi)
                if (auto choice = choose(bins[bi], index); choice && (!best || ContactRank(*choice) < ContactRank(*best)))
                { best = choice; bestBin = bi; }
            if (best) { commit(bins[bestBin], index, *best); continue; }
            bool opened = false;
            for (const auto si : stockOrder)
            {
                if (stop()) break;
                const auto& stock = problem.Stocks[si];
                if (stockUsed[si] >= stock.Quantity || stock.Material != problem.Parts[index].Material) continue;
                ParaBin candidate{ si, { stock.ID, stockUsed[si], stock.Width, stock.Height, {}, 0 } };
                if (auto choice = choose(candidate, index))
                {
                    commit(candidate, index, *choice); ++stockUsed[si]; bins.push_back(std::move(candidate)); opened = true; break;
                }
            }
            if (!opened) break;
        }
        if (stop()) break;
    }
    for (auto& bin : bins) if (!bin.Output.Placements.empty()) result.Layouts.push_back(std::move(bin.Output));
    for (std::size_t i = 0; i < problem.Parts.size(); ++i)
        if (consumed[i] < problem.Parts[i].Quantity) result.Remaining.push_back({ problem.Parts[i].ID, problem.Parts[i].Quantity - consumed[i] });
    result.State = cancelled ? Status::Cancelled : result.Remaining.empty() ? Status::Complete : Status::Partial;
    if (skipped) result.Diagnostics.push_back("This strategy places original rectangles/parallelograms only; other shape types remain for other jobs.");
    if (cancelled) result.Diagnostics.push_back("Job cancelled; completed placements and unconsumed demand are retained.");
    if (exhausted) result.Diagnostics.push_back("Parallelogram contact-rule time or placement-attempt budget exhausted.");
    if (!result.Remaining.empty() && !exhausted && !cancelled)
        result.Diagnostics.push_back("Finite edge/basis contact candidates or stock exhausted; this is not an infeasibility proof.");
    result.Stats.ElapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return result;
}
}
