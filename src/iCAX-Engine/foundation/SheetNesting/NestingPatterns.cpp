#include "NestingInternal.h"
#include <clipper2/clipper.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <unordered_map>

namespace iCAX::SheetNesting::Detail
{
namespace
{
    struct Edge { Point A, B; double Length = 0; bool Reentrant = false; Point ExteriorNormal; };
    void SelectEdges(std::vector<Edge>& edges)
    {
        std::stable_sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b)
        { if (a.Reentrant != b.Reentrant) return a.Reentrant; return a.Length > b.Length; });
        if (edges.size() > 64) edges.resize(64);
    }
    std::vector<Edge> Edges(const Shape& shape, Pose pose, double tolerance)
    {
        std::vector<Edge> edges;
        auto append = [&](const std::vector<Point>& ring)
        {
            auto points = ring;
            const auto distance = [](Point a, Point b) { return std::hypot(a.X - b.X, a.Y - b.Y); };
            if (points.size() > 1 && distance(points.front(), points.back()) <= tolerance) points.pop_back();
            // Imported collinear subdivisions are not distinct contact features. Without reducing them,
            // a few long outside walls can crowd every short notch edge out of the feature budget.
            bool changed = true;
            while (changed && points.size() > 3)
            {
                changed = false;
                for (std::size_t i = 0; i < points.size(); ++i)
                {
                    const auto a = points[(i + points.size() - 1) % points.size()], p = points[i], b = points[(i + 1) % points.size()];
                    const double dx = b.X - a.X, dy = b.Y - a.Y, length2 = dx * dx + dy * dy;
                    const double t = length2 > 0 ? std::clamp(((p.X - a.X) * dx + (p.Y - a.Y) * dy) / length2, 0.0, 1.0) : 0;
                    if (distance(p, { a.X + t * dx, a.Y + t * dy }) <= tolerance)
                    { points.erase(points.begin() + static_cast<std::ptrdiff_t>(i)); changed = true; break; }
                }
            }
            long double signedArea = 0;
            const Point origin = points.empty() ? Point{} : points.front();
            for (std::size_t i = 0; i < points.size(); ++i)
            {
                const auto a = points[i], b = points[(i + 1) % points.size()];
                signedArea += (static_cast<long double>(a.X) - origin.X) * (static_cast<long double>(b.Y) - origin.Y)
                    - (static_cast<long double>(a.Y) - origin.Y) * (static_cast<long double>(b.X) - origin.X);
            }
            const auto reentrant = [&](std::size_t i)
            {
                const auto a = points[(i + points.size() - 1) % points.size()], b = points[i], c = points[(i + 1) % points.size()];
                const double cross = (b.X - a.X) * (c.Y - b.Y) - (b.Y - a.Y) * (c.X - b.X);
                return (signedArea >= 0 ? cross : -cross) < -tolerance * std::max(distance(a, b), distance(b, c));
            };
            for (std::size_t i = 0; i < points.size(); ++i)
            {
                const auto a = Transform(points[i], pose), b = Transform(points[(i + 1) % points.size()], pose);
                const double length = std::hypot(b.X - a.X, b.Y - a.Y);
                if (length > tolerance)
                {
                    const double side = signedArea >= 0 ? 1.0 : -1.0;
                    edges.push_back({ a, b, length, reentrant(i) || reentrant((i + 1) % points.size()),
                        { side * (b.Y - a.Y) / length, -side * (b.X - a.X) / length } });
                }
            }
        };
        // Closed holes are reserved for the later gap-fill pass. External concave edges remain candidates.
        append(shape.Outer);
        SelectEdges(edges);
        return edges;
    }
    double EdgeAngle(const Edge& edge) { return std::atan2(edge.B.Y - edge.A.Y, edge.B.X - edge.A.X) * 180 / Pi; }
    Rect PatternBounds(const Pattern& pattern, const Problem& problem)
    {
        if (pattern.Members.empty()) return {};
        double x0 = std::numeric_limits<double>::infinity(), y0 = x0, x1 = -x0, y1 = -x0;
        for (const auto& m : pattern.Members)
        {
            const auto b = Bounds(problem.Parts[m.PartIndex].Geometry, m.Transform);
            x0 = std::min(x0, b.X); y0 = std::min(y0, b.Y);
            x1 = std::max(x1, b.X + b.Width); y1 = std::max(y1, b.Y + b.Height);
        }
        return { x0, y0, x1 - x0, y1 - y0 };
    }
    std::string Key(const Pattern& pattern, double tolerance)
    {
        std::vector<std::string> members;
        const double quantum = std::max(tolerance, 1e-7);
        for (const auto& m : pattern.Members)
        {
            std::ostringstream s; s << m.PartIndex << ':' << std::llround(m.Transform.X / quantum) << ':'
                << std::llround(m.Transform.Y / quantum) << ':' << std::llround(NormalizeAngle(m.Transform.AngleDegrees) * 1e6);
            members.push_back(s.str());
        }
        std::sort(members.begin(), members.end()); std::string key;
        for (const auto& s : members) key += s + ';';
        return key;
    }
    ShapeKind FilledBasicKind(const Pattern& pattern, const Problem& problem)
    {
        if (problem.PartGap > 0 || pattern.Members.size() < 2) return ShapeKind::Irregular;
        Clipper2Lib::PathsD paths;
        for (std::size_t i = 0; i < pattern.Members.size(); ++i)
        {
            const auto& member = pattern.Members[i];
            const auto& shape = problem.Parts[member.PartIndex].Geometry;
            if (shape.Radius > 0) return ShapeKind::Irregular; // Never replace analytic discs by inscribed polygons.
            for (std::size_t j = 0; j < i; ++j)
            {
                const auto& other = pattern.Members[j];
                if (ShapesConflict(shape, member.Transform, problem.Parts[other.PartIndex].Geometry,
                    other.Transform, 0, problem.Tolerance)) return ShapeKind::Irregular;
            }
            const auto append = [&](const std::vector<Point>& ring)
            {
                auto& path = paths.emplace_back();
                for (auto point : ring) { point = Transform(point, member.Transform); path.emplace_back(point.X, point.Y); }
            };
            append(shape.Outer); for (const auto& hole : shape.Holes) append(hole);
        }
        try
        {
            const auto material = Clipper2Lib::Union(paths, Clipper2Lib::FillRule::EvenOdd, 8);
            if (material.size() != 1) return ShapeKind::Irregular; // Holes and disconnected material remain explicit.
            std::vector<Point> points;
            for (const auto& p : material.front()) points.push_back({ p.x, p.y });
            std::sort(points.begin(), points.end(), [](Point a, Point b) { return a.X < b.X || (a.X == b.X && a.Y < b.Y); });
            points.erase(std::unique(points.begin(), points.end(), [](Point a, Point b) { return a.X == b.X && a.Y == b.Y; }), points.end());
            if (points.size() < 4) return ShapeKind::Irregular;
            const auto cross = [](Point a, Point b, Point c)
            { return (static_cast<long double>(b.X) - a.X) * (c.Y - a.Y) - (static_cast<long double>(b.Y) - a.Y) * (c.X - a.X); };
            std::vector<Point> hull(2 * points.size()); std::size_t count = 0;
            for (auto p : points) { while (count >= 2 && cross(hull[count - 2], hull[count - 1], p) <= 0) --count; hull[count++] = p; }
            const auto lower = count + 1;
            for (std::size_t i = points.size() - 1; i-- > 0;)
            { while (count >= lower && cross(hull[count - 2], hull[count - 1], points[i]) <= 0) --count; hull[count++] = points[i]; }
            hull.resize(count - 1);
            Shape envelope; envelope.Outer = std::move(hull);
            const auto kind = Classify(envelope, problem.Tolerance).Kind;
            if (kind != ShapeKind::Rectangle && kind != ShapeKind::Parallelogram) return ShapeKind::Irregular;
            double perimeter = 0;
            for (std::size_t i = 0; i < envelope.Outer.size(); ++i)
            { const auto a = envelope.Outer[i], b = envelope.Outer[(i + 1) % envelope.Outer.size()]; perimeter += std::hypot(b.X - a.X, b.Y - a.Y); }
            const double materialArea = std::abs(Clipper2Lib::Area(material));
            const double allowance = perimeter * problem.Tolerance;
            if (std::abs(Area(envelope) - materialArea) > allowance
                || std::abs(pattern.MaterialArea - materialArea) > allowance) return ShapeKind::Irregular;
            return kind;
        }
        catch (...) { return ShapeKind::Irregular; }
    }
    std::vector<Edge> PatternEdges(const Pattern& pattern, const Problem& problem)
    {
        std::vector<Edge> edges;
        bool polygonal = problem.PartGap == 0 && pattern.Members.size() > 1;
        for (const auto& m : pattern.Members) polygonal &= problem.Parts[m.PartIndex].Geometry.Radius == 0;
        if (polygonal)
        {
            Clipper2Lib::PathsD paths;
            for (const auto& m : pattern.Members)
            {
                const auto append = [&](const std::vector<Point>& ring)
                { auto& path = paths.emplace_back(); for (auto p : ring) { p = Transform(p, m.Transform); path.emplace_back(p.X, p.Y); } };
                const auto& shape = problem.Parts[m.PartIndex].Geometry;
                append(shape.Outer); for (const auto& hole : shape.Holes) append(hole);
            }
            try
            {
                const auto material = Clipper2Lib::Union(paths, Clipper2Lib::FillRule::EvenOdd, 8);
                for (const auto& path : material)
                {
                    if (Clipper2Lib::Area(path) <= 0) continue; // Closed internal voids remain for gap filling.
                    Shape outline; for (const auto& p : path) outline.Outer.push_back({ p.x, p.y });
                    auto visible = Edges(outline, {}, problem.Tolerance);
                    edges.insert(edges.end(), visible.begin(), visible.end());
                }
                if (!edges.empty()) { SelectEdges(edges); return edges; }
            }
            catch (...) { /* Preserve original contact features when union construction is unavailable. */ }
        }
        for (const auto& m : pattern.Members)
        { auto e = Edges(problem.Parts[m.PartIndex].Geometry, m.Transform, problem.Tolerance); edges.insert(edges.end(), e.begin(), e.end()); }
        SelectEdges(edges); return edges;
    }
    double PatternScore(const Pattern& p, const Problem& problem)
    {
        double separate = 0;
        for (const auto& m : p.Members)
        { const auto b = Bounds(problem.Parts[m.PartIndex].Geometry, m.Transform); separate += (b.Width + problem.PartGap) * (b.Height + problem.PartGap); }
        const double combined = (p.Width + problem.PartGap) * (p.Height + problem.PartGap);
        if (separate <= 0 || combined <= 0) return -std::numeric_limits<double>::infinity();
        return (separate - combined) / separate + .1 * std::clamp(p.MaterialArea / (p.Width * p.Height), 0.0, 1.0);
    }
    bool Better(const Pattern& a, const Pattern& b, const Problem& problem)
    {
        const auto basic = [](const Pattern& p) { return p.Kind == ShapeKind::Rectangle || p.Kind == ShapeKind::Parallelogram; };
        if (basic(a) != basic(b)) return basic(a);
        const auto density = [](const Pattern& p)
        { return std::round(std::clamp(p.MaterialArea / std::max(p.Width * p.Height, 1e-20), 0.0, 1.0) * 1e8) / 1e8; };
        const double da = density(a), db = density(b);
        if (da != db) return da > db;
        const double sa = PatternScore(a, problem), sb = PatternScore(b, problem);
        if (std::abs(sa - sb) > 1e-12) return sa > sb;
        if (a.MaterialArea != b.MaterialArea) return a.MaterialArea > b.MaterialArea;
        if (a.Width != b.Width) return a.Width < b.Width;
        if (a.Height != b.Height) return a.Height < b.Height;
        return Key(a, problem.Tolerance) < Key(b, problem.Tolerance);
    }
}

void NormalizePattern(Pattern& pattern, const Problem& problem)
{
    if (pattern.Members.empty()) { pattern.Width = pattern.Height = pattern.MaterialArea = 0; return; }
    const auto box = PatternBounds(pattern, problem);
    pattern.MaterialArea = 0;
    for (auto& m : pattern.Members)
    {
        m.Transform.X -= box.X; m.Transform.Y -= box.Y;
        m.Transform.AngleDegrees = NormalizeAngle(m.Transform.AngleDegrees);
        pattern.MaterialArea += Area(problem.Parts[m.PartIndex].Geometry);
    }
    pattern.Width = box.Width; pattern.Height = box.Height;
    if (pattern.Members.size() == 1) pattern.Kind = Classify(problem.Parts[pattern.Members.front().PartIndex].Geometry, problem.Tolerance).Kind;
    else
    {
        pattern.Kind = FilledBasicKind(pattern, problem);
        if (pattern.Kind == ShapeKind::Irregular && box.Width * box.Height > 0 && pattern.MaterialArea / (box.Width * box.Height) >= .85)
            pattern.Kind = ShapeKind::NearRectangle;
    }
}

std::vector<Pattern> BuildPatterns(const Problem& problem, const Options& options, const std::function<bool()>& stop)
{
    std::vector<Pattern> output;
    if (options.MaxPatterns == 0 || options.MaxPatternMembers == 0) return output;
    const auto stopped = [&]() { return stop && stop(); };
    std::vector<std::vector<double>> angles(problem.Parts.size());
    for (std::size_t i = 0; i < problem.Parts.size() && !stopped(); ++i)
        if (problem.Parts[i].Quantity > 0) angles[i] = CandidateAngles(problem.Parts[i], problem.Tolerance);
    const std::size_t maxDepth = std::min<std::size_t>(4, options.MaxPatternMembers);
    const bool combinations = options.EnableCombinations && maxDepth >= 2;
    std::unordered_set<std::string> keys;
    // Singleton candidates are the baseline representation of every active type
    // and its supported directions. The additional-combination budget must not
    // silently remove an input type or its only direction that fits the sheet.
    // Round robin also keeps types represented fairly when the job is cancelled.
    for (std::size_t angleIndex = 0; !stopped(); ++angleIndex)
    {
        bool any = false;
        for (std::size_t i = 0; i < angles.size() && !stopped(); ++i)
        {
            if (angleIndex >= angles[i].size()) continue;
            any = true; Pattern p; p.Members.push_back({ i, { 0, 0, angles[i][angleIndex] } });
            NormalizePattern(p, problem);
            if (keys.insert(Key(p, problem.Tolerance)).second) output.push_back(std::move(p));
        }
        if (!any) break;
    }
    if (!combinations || stopped()) return output;
    const std::size_t singletonCount = output.size();
    const std::size_t totalLimit = options.MaxPatterns > std::numeric_limits<std::size_t>::max() - singletonCount
        ? std::numeric_limits<std::size_t>::max() : singletonCount + options.MaxPatterns;
    auto frontier = output;
    std::size_t attempts = 0;
    const std::size_t patternBudget = options.MaxPatterns > std::numeric_limits<std::size_t>::max() / 256
        ? std::numeric_limits<std::size_t>::max() : options.MaxPatterns * 256;
    const std::size_t attemptLimit = std::min(options.MaxPlacementAttempts, patternBudget);
    std::vector<double> partAreas(problem.Parts.size());
    std::vector<std::vector<Edge>> partFeatures(problem.Parts.size());
    std::unordered_map<std::string, double> largestSheetArea;
    for (std::size_t i = 0; i < problem.Parts.size() && !stopped(); ++i)
    { partAreas[i] = Area(problem.Parts[i].Geometry); partFeatures[i] = Edges(problem.Parts[i].Geometry, {}, problem.Tolerance); }
    for (const auto& stock : problem.Stocks) if (stock.Quantity)
        largestSheetArea[stock.Material] = std::max(largestSheetArea[stock.Material],
            std::max(0.0, stock.Width - 2 * problem.EdgeMargin) * std::max(0.0, stock.Height - 2 * problem.EdgeMargin));
    struct Work
    {
        const Pattern* Anchor = nullptr;
        std::vector<Edge> Contact;
        std::vector<std::pair<std::size_t, std::size_t>> Demand;
        std::vector<std::size_t> Next;
        std::size_t ConcaveFeatures = 0;
    };
    for (std::size_t depth = 2; depth <= maxDepth && output.size() < totalLimit && !stopped(); ++depth)
    {
        std::vector<Pattern> pool;
        std::vector<Work> work;
        std::size_t taskCount = 0;
        for (const auto& anchor : frontier)
        {
            if (stopped()) break;
            Work w; w.Anchor = &anchor;
            for (const auto& m : anchor.Members)
            {
                auto d = std::find_if(w.Demand.begin(), w.Demand.end(), [&](auto item) { return item.first == m.PartIndex; });
                if (d == w.Demand.end()) w.Demand.emplace_back(m.PartIndex, 1); else ++d->second;
            }
            w.Contact = PatternEdges(anchor, problem);
            w.ConcaveFeatures = std::count_if(w.Contact.begin(), w.Contact.end(), [](const Edge& e) { return e.Reentrant; });
            const auto& material = problem.Parts[anchor.Members.front().PartIndex].Material;
            const double maxArea = largestSheetArea[material];
            struct Partner { int MatchingNotchFeatures; double Promise; std::size_t Index; };
            std::vector<Partner> next;
            for (std::size_t i = 0; i < problem.Parts.size() && !stopped(); ++i)
            {
                const auto& part = problem.Parts[i];
                if (!part.Quantity || part.Material != material || angles[i].empty()) continue;
                const auto d = std::find_if(w.Demand.begin(), w.Demand.end(), [&](auto item) { return item.first == i; });
                if (d != w.Demand.end() && d->second >= part.Quantity) continue;
                const double sum = anchor.MaterialArea + partAreas[i];
                if (sum > maxArea + std::max(1.0, maxArea) * 1e-12) continue;
                double promise = std::numeric_limits<double>::infinity();
                for (double angle : angles[i])
                {
                    const auto box = Bounds(part.Geometry, { 0, 0, angle });
                    const double overlay = std::max(anchor.Width, box.Width) * std::max(anchor.Height, box.Height);
                    if (overlay > 0) promise = std::min(promise, std::abs(sum - overlay) / overlay);
                }
                int matching = 0;
                for (const auto& local : partFeatures[i])
                {
                    bool match = false;
                    for (const auto& a : w.Contact)
                    {
                        if (!(local.Reentrant || a.Reentrant) || std::abs(local.Length - a.Length) > 8 * problem.Tolerance) continue;
                        if (part.FreeRotation) { match = true; break; }
                        for (double angle : angles[i])
                        {
                            const auto p0 = Transform(local.A, { 0, 0, angle }), p1 = Transform(local.B, { 0, 0, angle });
                            const double cross = (a.B.X - a.A.X) * (p1.Y - p0.Y) - (a.B.Y - a.A.Y) * (p1.X - p0.X);
                            if (std::abs(cross) <= problem.Tolerance * std::max(a.Length, local.Length)) { match = true; break; }
                        }
                        if (match) break;
                    }
                    if (match) ++matching;
                }
                next.push_back({ matching, promise, i });
            }
            std::stable_sort(next.begin(), next.end(), [](const auto& a, const auto& b)
            {
                if (a.MatchingNotchFeatures != b.MatchingNotchFeatures) return a.MatchingNotchFeatures > b.MatchingNotchFeatures;
                if (a.Promise != b.Promise) return a.Promise < b.Promise;
                return a.Index < b.Index;
            });
            // The job has finite candidate/time budgets; retain promising partners without an unbounded pair matrix.
            if (next.size() > 64) next.resize(64);
            for (auto item : next) w.Next.push_back(item.Index);
            if (!w.Next.empty()) { taskCount += w.Next.size(); work.push_back(std::move(w)); }
        }
        if (work.empty() || stopped()) break;
        std::stable_sort(work.begin(), work.end(), [](const Work& a, const Work& b)
        {
            const auto basic = [](const Pattern& p) { return p.Kind == ShapeKind::Rectangle || p.Kind == ShapeKind::Parallelogram; };
            if (basic(*a.Anchor) != basic(*b.Anchor)) return basic(*a.Anchor);
            return a.ConcaveFeatures > b.ConcaveFeatures;
        });
        // Reserve work for deeper motifs instead of allowing exhaustive pairs to consume every attempt.
        const std::size_t depthEnd = attempts + (attemptLimit - attempts) / (maxDepth - depth + 1);
        if (depthEnd <= attempts) break;
        const std::size_t pairQuota = std::clamp<std::size_t>((depthEnd - attempts) / std::max<std::size_t>(1, taskCount), 48, 384);
        const auto processPair = [&](const Work& w, std::size_t next)
        {
            if (attempts >= depthEnd || stopped()) return;
            const auto& anchor = *w.Anchor;
            const auto& part = problem.Parts[next];
            const auto& anchorEdges = w.Contact;
            const auto localEdges = Edges(part.Geometry, {}, problem.Tolerance);
            const bool prioritizeContacts = w.ConcaveFeatures > 0 && std::any_of(localEdges.begin(), localEdges.end(),
                [](const Edge& edge) { return edge.Reentrant; });
            auto candidateAngles = angles[next];
            if (part.FreeRotation)
            {
                const auto addAngle = [&](double a)
                {
                    a = NormalizeAngle(a);
                    for (auto old : candidateAngles)
                    { const auto d = std::abs(a - old); if (std::min(d, 360 - d) <= problem.Tolerance) return; }
                    if (candidateAngles.size() < 32) candidateAngles.push_back(a);
                };
                for (const auto& a : anchorEdges) for (const auto& b : localEdges)
                { addAngle(EdgeAngle(a) - EdgeAngle(b)); addAngle(EdgeAngle(a) - EdgeAngle(b) + 180); }
            }
            const std::size_t pairEnd = attempts + std::min(pairQuota, depthEnd - attempts);
            std::size_t angleEnd = pairEnd;
            std::vector<Pattern> best;
            std::unordered_set<std::string> seenPoses;
            const auto pairDone = [&]() { return attempts >= pairEnd || stopped(); };
            const auto angleDone = [&]() { return attempts >= angleEnd || pairDone(); };
            const auto tryPose = [&](Pose pose)
            {
                if (angleDone()) return;
                std::ostringstream poseKey;
                poseKey << std::llround(pose.X / problem.Tolerance) << ':' << std::llround(pose.Y / problem.Tolerance)
                    << ':' << std::llround(NormalizeAngle(pose.AngleDegrees) * 1e6);
                if (!seenPoses.insert(poseKey.str()).second) return;
                ++attempts;
                if (!AngleAllowed(part, pose.AngleDegrees, problem.Tolerance)) return;
                Pattern p = anchor; p.Members.push_back({ next, pose });
                const auto box = PatternBounds(p, problem);
                double separate = 0;
                for (const auto& m : p.Members)
                { const auto b = Bounds(problem.Parts[m.PartIndex].Geometry, m.Transform); separate += (b.Width + problem.PartGap) * (b.Height + problem.PartGap); }
                if ((box.Width + problem.PartGap) * (box.Height + problem.PartGap) > separate * 1.000001) return;
                for (const auto& m : anchor.Members)
                {
                    if (ShapesConflict(problem.Parts[m.PartIndex].Geometry, m.Transform, part.Geometry, pose,
                        problem.PartGap, problem.Tolerance)) return;
                    for (const auto& hole : problem.Parts[m.PartIndex].Geometry.Holes)
                    {
                        Shape filledHole; filledHole.Outer = hole;
                        if (ShapesConflict(filledHole, m.Transform, part.Geometry, pose, 0, problem.Tolerance)) return;
                    }
                    for (const auto& hole : part.Geometry.Holes)
                    {
                        Shape filledHole; filledHole.Outer = hole;
                        if (ShapesConflict(filledHole, pose, problem.Parts[m.PartIndex].Geometry, m.Transform, 0, problem.Tolerance)) return;
                    }
                }
                NormalizePattern(p, problem);
                if (!keys.insert(Key(p, problem.Tolerance)).second) return;
                if (best.size() < 4) best.push_back(std::move(p));
                else
                {
                    const auto worst = std::min_element(best.begin(), best.end(), [&](const Pattern& a, const Pattern& b) { return Better(b, a, problem); });
                    if (Better(p, *worst, problem)) *worst = std::move(p);
                }
            };
            const double gap = problem.PartGap;
            for (const double angle : candidateAngles)
            {
                if (pairDone()) break;
                angleEnd = attempts + std::min(std::max<std::size_t>(12, pairQuota / std::max<std::size_t>(1, candidateAngles.size())), pairEnd - attempts);
                const auto b = Bounds(part.Geometry, { 0, 0, angle });
                const double xs[] = { 0, (anchor.Width - b.Width) / 2, anchor.Width - b.Width };
                const double ys[] = { 0, (anchor.Height - b.Height) / 2, anchor.Height - b.Height };
                // Overlay phases find complementary L/step shapes before their conservative boxes are put side by side.
                for (const double x : xs) for (const double y : ys) tryPose({ x - b.X, y - b.Y, angle });
                const auto rotatedEdges = Edges(part.Geometry, { 0, 0, angle }, problem.Tolerance);
                struct ContactPose { Pose Transform; double EnvelopeArea = 0; bool Facing = false; };
                std::vector<ContactPose> contacts;
                // Inspect the bounded feature list before spending geometric trials. A single early
                // edge pair must not consume an angle's entire allowance while a shorter outside
                // wall supplies the useful interlock. Facing is independent of input ring winding.
                for (const auto& a : anchorEdges)
                {
                    if (angleDone()) break;
                    for (const auto& e : rotatedEdges)
                    {
                        if (angleDone()) break;
                        const double cross = (a.B.X - a.A.X) * (e.B.Y - e.A.Y) - (a.B.Y - a.A.Y) * (e.B.X - e.A.X);
                        if (std::abs(cross) > problem.Tolerance * std::max(a.Length, e.Length)) continue;
                        const Point n{ -(a.B.Y - a.A.Y) / a.Length, (a.B.X - a.A.X) / a.Length };
                        const Point ap[] = { a.A, a.B, { (a.A.X + a.B.X) / 2, (a.A.Y + a.B.Y) / 2 } };
                        const Point bp[] = { e.A, e.B, { (e.A.X + e.B.X) / 2, (e.A.Y + e.B.Y) / 2 } };
                        for (int side : { -1, 1 }) for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
                        {
                            const Pose pose{ ap[i].X - bp[j].X + side * gap * n.X,
                                ap[i].Y - bp[j].Y + side * gap * n.Y, angle };
                            if (!prioritizeContacts) { tryPose(pose); continue; }
                            const double width = std::max(anchor.Width, b.X + pose.X + b.Width) - std::min(0.0, b.X + pose.X);
                            const double height = std::max(anchor.Height, b.Y + pose.Y + b.Height) - std::min(0.0, b.Y + pose.Y);
                            const double envelope = width * height;
                            // Disjoint real material cannot occupy a smaller box than its total area.
                            // Keep an area allowance derived from the geometry's linear tolerance.
                            const double allowance = 8 * problem.Tolerance * (width + height) * (anchor.Members.size() + 1)
                                + 64 * std::numeric_limits<double>::epsilon() * std::max(envelope, anchor.MaterialArea + partAreas[next]);
                            if (anchor.MaterialArea + partAreas[next] > envelope + allowance) continue;
                            const bool facing = a.ExteriorNormal.X * e.ExteriorNormal.X + a.ExteriorNormal.Y * e.ExteriorNormal.Y < -.5
                                && side * (n.X * a.ExteriorNormal.X + n.Y * a.ExteriorNormal.Y) > .5;
                            contacts.push_back({ pose, envelope, facing });
                        }
                    }
                }
                std::stable_sort(contacts.begin(), contacts.end(), [](const ContactPose& a, const ContactPose& b)
                {
                    if (a.Facing != b.Facing) return a.Facing;
                    return a.EnvelopeArea < b.EnvelopeArea;
                });
                for (const auto& contact : contacts)
                { if (angleDone()) break; tryPose(contact.Transform); }
                for (const double y : ys)
                { tryPose({ anchor.Width + gap - b.X, y - b.Y, angle }); tryPose({ -b.Width - gap - b.X, y - b.Y, angle }); }
                for (const double x : xs)
                { tryPose({ x - b.X, anchor.Height + gap - b.Y, angle }); tryPose({ x - b.X, -b.Height - gap - b.Y, angle }); }
            }
            for (auto& p : best) pool.push_back(std::move(p));
        };
        // Round robin across anchors/specifications: no early type exhausts the budget before later types get a turn.
        for (std::size_t partnerRank = 0; attempts < depthEnd && !stopped(); ++partnerRank)
        {
            bool any = false;
            for (const auto& w : work)
            {
                if (attempts >= depthEnd || stopped()) break;
                if (partnerRank >= w.Next.size()) continue;
                any = true; processPair(w, w.Next[partnerRank]);
            }
            if (!any) break;
        }
        if (pool.empty()) break;
        std::stable_sort(pool.begin(), pool.end(), [&](const Pattern& a, const Pattern& b) { return Better(a, b, problem); });
        const auto remaining = totalLimit - output.size();
        const auto quota = std::max<std::size_t>(1, remaining / (maxDepth - depth + 1));
        const auto take = std::min(quota, pool.size());
        std::vector<bool> selected(pool.size());
        std::vector<std::size_t> represented(problem.Parts.size());
        std::vector<Rect> representedBounds(problem.Parts.size());
        const auto basic = [](const Pattern& p) { return p.Kind == ShapeKind::Rectangle || p.Kind == ShapeKind::Parallelogram; };
        std::size_t count = 0;
        // Filled basic shapes retain priority. Within the remaining finite pool, preserve a few
        // distinct repeating concave demands even when their real density is below NearRectangle.
        // Otherwise unrelated dense mixed-specification patterns can erase every useful self-pair.
        while (count < take && basic(pool[count])) { selected[count] = true; ++count; }
        const auto firstNonBasic = count;
        const auto selfQuota = count == take ? 0 : std::max<std::size_t>(1, (take - count) / 4);
        std::size_t selfCount = 0;
        for (std::size_t pass = 0; pass < 2 && selfCount < selfQuota; ++pass)
        for (std::size_t i = firstNonBasic; i < pool.size() && selfCount < selfQuota; ++i)
        {
            const auto& pattern = pool[i];
            const auto partIndex = pattern.Members.front().PartIndex;
            if (selected[i] || represented[partIndex] != pass || !std::all_of(pattern.Members.begin(), pattern.Members.end(),
                [&](const Member& member) { return member.PartIndex == partIndex; })) continue;
            if (pass && representedBounds[partIndex].Width <= pattern.Width + problem.Tolerance
                && representedBounds[partIndex].Height <= pattern.Height + problem.Tolerance) continue;
            if (!std::any_of(partFeatures[partIndex].begin(), partFeatures[partIndex].end(),
                [](const Edge& edge) { return edge.Reentrant; })) continue;
            double separate = 0;
            for (const auto& member : pattern.Members)
            {
                const auto box = Bounds(problem.Parts[partIndex].Geometry, member.Transform);
                separate += (box.Width + problem.PartGap) * (box.Height + problem.PartGap);
            }
            const double combined = (pattern.Width + problem.PartGap) * (pattern.Height + problem.PartGap);
            if (separate - combined <= 8 * problem.Tolerance * (pattern.Width + pattern.Height) * pattern.Members.size()
                + 64 * std::numeric_limits<double>::epsilon() * std::max(separate, combined)) continue;
            if (!pass) representedBounds[partIndex] = { 0, 0, pattern.Width, pattern.Height };
            ++represented[partIndex]; selected[i] = true; ++selfCount; ++count;
        }
        for (std::size_t i = 0; i < pool.size() && count < take; ++i)
            if (!selected[i]) { selected[i] = true; ++count; }
        frontier.clear(); frontier.reserve(take);
        for (std::size_t i = 0; i < pool.size(); ++i)
            if (selected[i]) { frontier.push_back(pool[i]); output.push_back(std::move(pool[i])); }
        if (attempts >= attemptLimit) break;
    }
    return output;
}
}

namespace iCAX::SheetNesting::Detail
{
std::vector<Pattern> BuildClosurePatterns(const Problem& problem, const Options& options, const std::function<bool()>& stop)
{
    bool interrupted = false;
    const auto halted = [&]() { return interrupted || (interrupted = stop && stop()); };
    // This is an independent preparation strategy. The broad BuildPatterns
    // strategy retains its entire original budget and candidate order.
    const std::size_t maxDepth = std::min<std::size_t>(4, options.MaxPatternMembers);
    std::size_t inventory = 0;
    for (const auto& part : problem.Parts)
    {
        if (part.Quantity > maxDepth - inventory) { inventory = maxDepth + 1; break; }
        inventory += part.Quantity;
    }
    if (!options.EnableCombinations || problem.PartGap > 0 || inventory < 2 || inventory > maxDepth)
    {
        auto base = options; base.EnableCombinations = false;
        return BuildPatterns(problem, base, halted);
    }
    // Reach the complete inventory before a wide layer of partial assemblies
    // consumes the preparation allowance. Every narrow-beam candidate uses the
    // unchanged member geometry, angle, quantity and conflict validation.
    auto narrow = options;
    narrow.MaxPatterns = std::min<std::size_t>(12, options.MaxPatterns);
    narrow.MaxPlacementAttempts = std::min<std::size_t>(3072, options.MaxPlacementAttempts / 4);
    auto closed = BuildPatterns(problem, narrow, halted);
    for (const auto& p : closed)
    {
        if (p.Members.size() != inventory || (p.Kind != ShapeKind::Rectangle && p.Kind != ShapeKind::Parallelogram)) continue;
        for (const auto& stock : problem.Stocks)
            if (stock.Quantity && stock.Material == problem.Parts[p.Members.front().PartIndex].Material
                && p.Width <= stock.Width - 2 * problem.EdgeMargin + problem.Tolerance
                && p.Height <= stock.Height - 2 * problem.EdgeMargin + problem.Tolerance)
                return closed;
    }
    if (halted()) return closed;
    // Failure to close is not infeasibility. Spend only this job's remaining
    // attempt/time allowance on the normal wider preparation.
    auto remaining = options;
    remaining.MaxPlacementAttempts -= narrow.MaxPlacementAttempts;
    auto wider = BuildPatterns(problem, remaining, halted);
    return wider.empty() ? closed : wider;
}
}
