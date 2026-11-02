#include "NestingInternal.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>
#include <set>
#include <unordered_map>

namespace iCAX::SheetNesting
{
namespace
{
    struct RemnantSegment { Point A, B; Rect Bounds; };
    struct RemnantObstacle
    {
        const Shape* Original = nullptr;
        Pose Transform;
        Rect Bounds;
        Point Center;
        double Radius = 0;
        std::vector<std::vector<Point>> Rings;
        std::vector<RemnantSegment> Segments;
    };
    double Right(const Rect& r) { return r.X + r.Width; }
    double Top(const Rect& r) { return r.Y + r.Height; }
    bool Intersects(const Rect& a, const Rect& b)
    { return a.X <= Right(b) && b.X <= Right(a) && a.Y <= Top(b) && b.Y <= Top(a); }
    Rect Expanded(Rect r, double d) { return { r.X - d, r.Y - d, r.Width + 2 * d, r.Height + 2 * d }; }
    bool SegmentHitsRect(Point a, Point b, const Rect& r)
    {
        double first = 0, last = 1;
        const auto slab = [&](double origin, double delta, double lo, double hi)
        {
            if (delta == 0) return origin >= lo && origin <= hi;
            double t0 = (lo - origin) / delta, t1 = (hi - origin) / delta;
            if (t0 > t1) std::swap(t0, t1);
            first = std::max(first, t0); last = std::min(last, t1);
            return first <= last;
        };
        return slab(a.X, b.X - a.X, r.X, Right(r)) && slab(a.Y, b.Y - a.Y, r.Y, Top(r));
    }
    double PointSegmentDistance(Point p, Point a, Point b)
    {
        const double dx = b.X - a.X, dy = b.Y - a.Y, d2 = dx * dx + dy * dy;
        const double t = d2 > 0 ? std::clamp(((p.X - a.X) * dx + (p.Y - a.Y) * dy) / d2, 0.0, 1.0) : 0;
        return std::hypot(p.X - a.X - t * dx, p.Y - a.Y - t * dy);
    }
    double PointRectDistance(Point p, const Rect& r)
    { return std::hypot(p.X - std::clamp(p.X, r.X, Right(r)), p.Y - std::clamp(p.Y, r.Y, Top(r))); }
    double SegmentRectDistance(const RemnantSegment& edge, const Rect& r)
    {
        if (SegmentHitsRect(edge.A, edge.B, r)) return 0;
        double distance = std::min(PointRectDistance(edge.A, r), PointRectDistance(edge.B, r));
        for (auto p : { Point{r.X,r.Y}, Point{Right(r),r.Y}, Point{Right(r),Top(r)}, Point{r.X,Top(r)} })
            distance = std::min(distance, PointSegmentDistance(p, edge.A, edge.B));
        return distance;
    }
    int InRing(Point p, const std::vector<Point>& ring, double epsilon)
    {
        bool inside = false;
        for (std::size_t i = 0; i < ring.size(); ++i)
        {
            const auto a = ring[i], b = ring[(i + 1) % ring.size()];
            if (PointSegmentDistance(p, a, b) <= epsilon) return 0;
            if ((a.Y > p.Y) != (b.Y > p.Y))
            {
                const long double x = a.X + (static_cast<long double>(p.Y) - a.Y) * (b.X - a.X) / (b.Y - a.Y);
                if (x > p.X) inside = !inside;
            }
        }
        return inside ? 1 : -1;
    }
    bool InMaterial(Point p, const RemnantObstacle& o, double epsilon)
    {
        if (InRing(p, o.Rings.front(), epsilon) != 1) return false;
        for (std::size_t i = 1; i < o.Rings.size(); ++i) if (InRing(p, o.Rings[i], epsilon) >= 0) return false;
        return true;
    }
    double Perimeter(const Shape& shape)
    {
        if (shape.Radius > 0) return 2 * Detail::Pi * shape.Radius;
        double result = 0;
        const auto add = [&](const auto& ring)
        { for (std::size_t i = 0; i < ring.size(); ++i) result += std::hypot(ring[i].X - ring[(i + 1) % ring.size()].X, ring[i].Y - ring[(i + 1) % ring.size()].Y); };
        add(shape.Outer); for (const auto& h : shape.Holes) add(h); return result;
    }
    RemnantObstacle Prepare(const Shape& shape, Pose pose)
    {
        RemnantObstacle o; o.Original = &shape; o.Transform = pose; o.Bounds = Detail::Bounds(shape, pose);
        o.Radius = shape.Radius;
        if (shape.Radius > 0) { o.Center = Detail::Transform(shape.Center, pose); return o; }
        const auto append = [&](const std::vector<Point>& source)
        {
            auto& ring = o.Rings.emplace_back();
            for (auto p : source) ring.push_back(Detail::Transform(p, pose));
            if (ring.size() > 1 && ring.front().X == ring.back().X && ring.front().Y == ring.back().Y) ring.pop_back();
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                const auto a = ring[i], b = ring[(i + 1) % ring.size()];
                o.Segments.push_back({ a, b, { std::min(a.X, b.X), std::min(a.Y, b.Y), std::abs(a.X - b.X), std::abs(a.Y - b.Y) } });
            }
        };
        append(shape.Outer); for (const auto& h : shape.Holes) append(h);
        return o;
    }
    std::vector<Rect> GridRectangles(const std::vector<unsigned char>& free, std::size_t n, const Rect& usable, std::size_t keep)
    {
        std::vector<std::size_t> heights(n);
        std::vector<Rect> best;
        const double dx = usable.Width / n, dy = usable.Height / n;
        const auto add = [&](Rect r)
        {
            if (r.Width <= 0 || r.Height <= 0) return;
            for (const auto& old : best)
                if (old.X == r.X && old.Y == r.Y && old.Width == r.Width && old.Height == r.Height) return;
            best.push_back(r);
            std::stable_sort(best.begin(), best.end(), [](const Rect& a, const Rect& b)
            { if (a.Width * a.Height != b.Width * b.Height) return a.Width * a.Height > b.Width * b.Height;
                if (a.Y != b.Y) return a.Y < b.Y; return a.X < b.X; });
            if (best.size() > keep) best.resize(keep);
        };
        for (std::size_t y = 0; y < n; ++y)
        {
            for (std::size_t x = 0; x < n; ++x) heights[x] = free[y * n + x] ? heights[x] + 1 : 0;
            std::vector<std::pair<std::size_t, std::size_t>> stack;
            for (std::size_t x = 0; x <= n; ++x)
            {
                const auto h = x == n ? 0 : heights[x]; std::size_t start = x;
                while (!stack.empty() && stack.back().second > h)
                {
                    const auto item = stack.back(); stack.pop_back(); start = item.first;
                    add({ usable.X + start * dx, usable.Y + (y + 1 - item.second) * dy, (x - start) * dx, item.second * dy });
                }
                if (h && (stack.empty() || stack.back().second < h)) stack.emplace_back(start, h);
            }
        }
        return best;
    }

    std::vector<Rect> SymmetricGridRectangles(const std::vector<unsigned char>& free, std::size_t n, const Rect& usable, std::size_t keep, const std::vector<Rect>& legacy)
    {
        auto seeds = legacy;
        const double dx = usable.Width / n, dy = usable.Height / n;
        // Run the same histogram in every quarter-turn. Map integer cell bounds
        // back before making world rectangles, so coordinate rounding cannot
        // invent different representations of the same seed.
        for (int turn = 1; turn < 4; ++turn)
        {
            std::vector<unsigned char> rotated(n * n);
            for (std::size_t y = 0; y < n; ++y) for (std::size_t x = 0; x < n; ++x)
            {
                const auto rx = turn == 1 ? n - 1 - y : turn == 2 ? n - 1 - x : y;
                const auto ry = turn == 1 ? x : turn == 2 ? n - 1 - y : n - 1 - x;
                rotated[ry * n + rx] = free[y * n + x];
            }
            const Rect rotatedUsable{0,0,turn == 2 ? usable.Width : usable.Height,turn == 2 ? usable.Height : usable.Width};
            const double rxStep = rotatedUsable.Width / n, ryStep = rotatedUsable.Height / n;
            for (const auto& r : GridRectangles(rotated, n, rotatedUsable, keep))
            {
                const auto x0 = static_cast<std::size_t>(std::llround(r.X / rxStep));
                const auto y0 = static_cast<std::size_t>(std::llround(r.Y / ryStep));
                const auto x1 = x0 + static_cast<std::size_t>(std::llround(r.Width / rxStep));
                const auto y1 = y0 + static_cast<std::size_t>(std::llround(r.Height / ryStep));
                const auto left = turn == 1 ? y0 : turn == 2 ? n - x1 : n - y1;
                const auto bottom = turn == 1 ? n - x1 : turn == 2 ? n - y1 : x0;
                const auto width = turn == 2 ? x1 - x0 : y1 - y0;
                const auto height = turn == 2 ? y1 - y0 : x1 - x0;
                Rect mapped{usable.X + left * dx, usable.Y + bottom * dy, width * dx, height * dy};
                if (std::none_of(seeds.begin(), seeds.end(), [&](const Rect& old)
                    {return old.X == mapped.X && old.Y == mapped.Y && old.Width == mapped.Width && old.Height == mapped.Height;})) seeds.push_back(mapped);
            }
        }
        std::stable_sort(seeds.begin(), seeds.end(), [](const Rect& a, const Rect& b)
            {return a.Width * a.Height > b.Width * b.Height;});
        std::size_t limit = std::min(keep, seeds.size());
        while (limit < seeds.size() && seeds[limit].Width * seeds[limit].Height == seeds[limit - 1].Width * seeds[limit - 1].Height) ++limit;
        seeds.resize(limit);
        for (const auto& r : legacy) if (std::none_of(seeds.begin(), seeds.end(), [&](const Rect& old)
            {return old.X == r.X && old.Y == r.Y && old.Width == r.Width && old.Height == r.Height;})) seeds.push_back(r);
        return seeds;
    }
}

RegularRemnant FindRegularRemnant(const Problem& problem, const Layout& layout, std::size_t resolution)
{
    RegularRemnant result;
    const std::size_t n = std::clamp<std::size_t>(resolution, 1, 256); result.Resolution = n;
    if (!std::isfinite(problem.PartGap) || problem.PartGap < 0 || !std::isfinite(problem.EdgeMargin) || problem.EdgeMargin < 0
        || !std::isfinite(problem.Tolerance) || problem.Tolerance < 1e-7 || problem.Tolerance > .01) return result;
    const auto stock = std::find_if(problem.Stocks.begin(), problem.Stocks.end(), [&](const Stock& s) { return s.ID == layout.StockID; });
    if (stock == problem.Stocks.end() || !std::isfinite(stock->Width) || !std::isfinite(stock->Height) || stock->Width <= 0 || stock->Height <= 0
        || stock->Width > 1e7 || stock->Height > 1e7 || layout.StockInstance >= stock->Quantity || !std::isfinite(layout.Width) || !std::isfinite(layout.Height)
        || layout.Width <= 0 || layout.Height <= 0 || std::abs(layout.Width - stock->Width) > problem.Tolerance
        || std::abs(layout.Height - stock->Height) > problem.Tolerance) return result;
    const Rect usable{ problem.EdgeMargin, problem.EdgeMargin,
        std::max(0.0, layout.Width - 2 * problem.EdgeMargin), std::max(0.0, layout.Height - 2 * problem.EdgeMargin) };
    const double usableRight = layout.Width - problem.EdgeMargin, usableTop = layout.Height - problem.EdgeMargin;
    const auto bounded = [&](const Rect& r)
    {
        const double x = std::max(usable.X, r.X), y = std::max(usable.Y, r.Y);
        const double right = std::min(usableRight, Right(r)), top = std::min(usableTop, Top(r));
        Rect clipped{ x, y, std::max(0.0, right - x), std::max(0.0, top - y) };
        // A subtraction followed by addition can overshoot the endpoint by one ULP.
        // Keep the returned certificate strictly on the usable sheet, without spending
        // the geometric tolerance or discarding an otherwise safe boundary grid seed.
        if (Right(clipped) > right) clipped.Width = std::nextafter(clipped.Width, 0.0);
        if (Top(clipped) > top) clipped.Height = std::nextafter(clipped.Height, 0.0);
        return clipped;
    };
    result.Verified = true;
    if (usable.Width <= 0 || usable.Height <= 0) return result;
    std::unordered_map<std::string, const Part*> byID;
    for (const auto& p : problem.Parts) if (!byID.emplace(p.ID, &p).second) { result.Verified = false; return result; }
    std::vector<RemnantObstacle> obstacles;
    std::set<std::pair<std::string, std::size_t>> instances;
    double materialArea = 0, perimeter = 0;
    for (const auto& placed : layout.Placements)
    {
        const auto part = byID.find(placed.PartID);
        if (part == byID.end() || placed.Instance >= part->second->Quantity || !instances.emplace(placed.PartID, placed.Instance).second
            || part->second->Material != stock->Material || !Detail::AngleAllowed(*part->second, placed.Transform.AngleDegrees, problem.Tolerance)
            || !Detail::InsideSheet(part->second->Geometry, placed.Transform, layout.Width, layout.Height, problem.EdgeMargin, problem.Tolerance))
        { result.Verified = false; return result; }
        std::string error;
        if (!Detail::ShapeValid(part->second->Geometry, problem.Tolerance, error)) { result.Verified = false; return result; }
        auto o = Prepare(part->second->Geometry, placed.Transform);
        for (const auto& other : obstacles)
            if (Intersects(o.Bounds, other.Bounds) && Detail::ShapesConflict(*o.Original, o.Transform,
                *other.Original, other.Transform, 0, problem.Tolerance)) { result.Verified = false; return result; }
        materialArea += Detail::Area(part->second->Geometry); perimeter += Perimeter(part->second->Geometry);
        obstacles.push_back(std::move(o));
    }
    const double epsilon = std::max(2 * problem.Tolerance,
        16 * std::numeric_limits<double>::epsilon() * std::max(layout.Width, layout.Height));
    if (!obstacles.empty() && problem.PartGap >= std::hypot(layout.Width, layout.Height) + epsilon) return result;
    const double dx = usable.Width / n, dy = usable.Height / n;
    std::vector<unsigned char> lower(n * n, 1), upper(n * n, 1);
    for (const auto& o : obstacles)
    {
        const auto expanded = Expanded(o.Bounds, problem.PartGap + epsilon);
        if (!Intersects(expanded, usable)) continue;
        const auto index = [&](double v, double origin, double step)
        { return static_cast<std::size_t>(std::clamp(std::floor((v - origin) / step), 0.0, static_cast<double>(n - 1))); };
        const auto x0 = index(expanded.X, usable.X, dx), x1 = index(Right(expanded), usable.X, dx);
        const auto y0 = index(expanded.Y, usable.Y, dy), y1 = index(Top(expanded), usable.Y, dy);
        for (std::size_t y = y0; y <= y1; ++y) for (std::size_t x = x0; x <= x1; ++x)
        {
            const auto cellIndex = y * n + x;
            if (!lower[cellIndex] && !upper[cellIndex]) continue;
            const Rect cell{ usable.X + x * dx, usable.Y + y * dy, dx, dy };
            if (o.Radius > 0)
            {
                const double nearX = std::clamp(o.Center.X, cell.X, Right(cell)), nearY = std::clamp(o.Center.Y, cell.Y, Top(cell));
                if (std::hypot(o.Center.X - nearX, o.Center.Y - nearY) <= o.Radius + problem.PartGap + epsilon) lower[cellIndex] = 0;
                const double farX = std::max(std::abs(o.Center.X - cell.X), std::abs(o.Center.X - Right(cell)));
                const double farY = std::max(std::abs(o.Center.Y - cell.Y), std::abs(o.Center.Y - Top(cell)));
                if (std::hypot(farX, farY) < o.Radius - epsilon) upper[cellIndex] = 0;
                continue;
            }
            const bool inside = InMaterial({ cell.X + dx / 2, cell.Y + dy / 2 }, o, epsilon);
            bool nearBoundary = false, rawBoundary = false;
            const auto inflatedCell = Expanded(cell, problem.PartGap + epsilon), rawCell = Expanded(cell, epsilon);
            for (const auto& edge : o.Segments)
            {
                if (!nearBoundary && Intersects(edge.Bounds, inflatedCell) && SegmentHitsRect(edge.A, edge.B, inflatedCell)) nearBoundary = true;
                if (!rawBoundary && Intersects(edge.Bounds, rawCell) && SegmentHitsRect(edge.A, edge.B, rawCell)) rawBoundary = true;
                if (nearBoundary && rawBoundary) break;
            }
            if (inside || nearBoundary) lower[cellIndex] = 0;
            if (inside && !rawBoundary) upper[cellIndex] = 0;
        }
    }
    auto legacySeeds = GridRectangles(lower, n, usable, 16);
    auto seeds = SymmetricGridRectangles(lower, n, usable, 16, legacySeeds);
    for (auto& seed : legacySeeds) seed = bounded(seed);
    for (auto& seed : seeds) seed = bounded(seed);
    const auto optimistic = GridRectangles(upper, n, usable, 1);
    const double gridUpper = optimistic.empty() ? 0 : optimistic.front().Width * optimistic.front().Height;
    const double count = static_cast<double>(obstacles.size());
    const double areaAllowance = perimeter * problem.Tolerance + count * std::max(0.0, count - 1) * problem.Tolerance * problem.Tolerance / 2;
    const double freeAreaUpper = std::clamp(usable.Width * usable.Height - materialArea + areaAllowance, 0.0, usable.Width * usable.Height);
    result.AreaUpperBound = std::min(gridUpper, freeAreaUpper);
    const auto verify = [&](const Rect& r)
    {
        if (!std::isfinite(r.X) || !std::isfinite(r.Y) || !std::isfinite(r.Width) || !std::isfinite(r.Height) || r.Width <= 0 || r.Height <= 0) return false;
        if (r.X < usable.X || r.Y < usable.Y || Right(r) > usableRight || Top(r) > usableTop) return false;
        Shape rectangle; rectangle.Outer = {{0,0},{r.Width,0},{r.Width,r.Height},{0,r.Height}};
        const Pose pose{ r.X, r.Y, 0 };
        if (!Detail::InsideSheet(rectangle, pose, layout.Width, layout.Height, problem.EdgeMargin, problem.Tolerance)) return false;
        const auto broad = Expanded(r, problem.PartGap + epsilon);
        for (const auto& o : obstacles)
            if (Intersects(broad, o.Bounds))
            {
                // Explicit segment/material tests also protect sub-grid features: intersection area
                // thresholds in a polygon Boolean must never let a tiny enclosed island be reclaimed.
                if (o.Radius > 0)
                {
                    if (PointRectDistance(o.Center, r) < o.Radius + problem.PartGap + problem.Tolerance) return false;
                }
                else
                {
                    if (InMaterial({ r.X + r.Width / 2, r.Y + r.Height / 2 }, o, epsilon)) return false;
                    for (const auto& edge : o.Segments)
                        if (Intersects(edge.Bounds, broad) && SegmentRectDistance(edge, r) < problem.PartGap + problem.Tolerance) return false;
                }
                if (Detail::ShapesConflict(rectangle, pose, *o.Original, o.Transform,
                    problem.PartGap + 2 * problem.Tolerance, problem.Tolerance)) return false;
            }
        return true;
    };
    const auto addSeed = [&](Rect r)
    {
        r = bounded(r);
        if (r.Width <= 0 || r.Height <= 0 || !verify(r)) return;
        legacySeeds.push_back(r); seeds.push_back(r);
    };
    if (obstacles.empty()) addSeed(usable);
    else
    {
        double left = Right(usable), right = usable.X, bottom = Top(usable), top = usable.Y;
        for (const auto& o : obstacles)
        {
            left = std::min(left, o.Bounds.X - problem.PartGap - epsilon);
            right = std::max(right, Right(o.Bounds) + problem.PartGap + epsilon);
            bottom = std::min(bottom, o.Bounds.Y - problem.PartGap - epsilon);
            top = std::max(top, Top(o.Bounds) + problem.PartGap + epsilon);
            for (std::size_t i = 1; i < o.Rings.size(); ++i)
            {
                Shape hole; hole.Outer = o.Rings[i]; const auto hb = Detail::Bounds(hole);
                addSeed({ hb.X + problem.PartGap + epsilon, hb.Y + problem.PartGap + epsilon,
                    hb.Width - 2 * (problem.PartGap + epsilon), hb.Height - 2 * (problem.PartGap + epsilon) });
                const double small = std::min({ dx, dy, hb.Width / 4, hb.Height / 4 });
                if (small > epsilon) addSeed({ hb.X + hb.Width / 2 - small / 2, hb.Y + hb.Height / 2 - small / 2, small, small });
            }
        }
        addSeed({ usable.X, usable.Y, left - usable.X, usable.Height });
        addSeed({ right, usable.Y, Right(usable) - right, usable.Height });
        addSeed({ usable.X, usable.Y, usable.Width, bottom - usable.Y });
        addSeed({ usable.X, top, usable.Width, Top(usable) - top });
    }
    const auto larger = [](const Rect& a, const Rect& b) { return a.Width * a.Height > b.Width * b.Height; };
    std::stable_sort(legacySeeds.begin(), legacySeeds.end(), larger);
    if (legacySeeds.size() > 24) legacySeeds.resize(24);
    const auto same = [](const Rect& a, const Rect& b)
    { return a.X == b.X && a.Y == b.Y && a.Width == b.Width && a.Height == b.Height; };
    std::vector<Rect> selected;
    for (const auto& seed : legacySeeds)
        if (std::none_of(selected.begin(), selected.end(), [&](const Rect& r) { return same(r, seed); })) selected.push_back(seed);
    std::vector<Rect> additional;
    for (const auto& seed : seeds)
        if (std::none_of(selected.begin(), selected.end(), [&](const Rect& r) { return same(r, seed); })
            && std::none_of(additional.begin(), additional.end(), [&](const Rect& r) { return same(r, seed); })) additional.push_back(seed);
    std::stable_sort(additional.begin(), additional.end(), larger);
    // Preserve every legacy certificate seed. Add complete equal-area groups only;
    // a coordinate tie-break must not choose one orientation of a group at the cap.
    for (std::size_t first = 0; first < additional.size(); )
    {
        std::size_t last = first + 1;
        while (last < additional.size() && additional[last].Width * additional[last].Height
            == additional[first].Width * additional[first].Height) ++last;
        if (last - first > 64 - selected.size()) break;
        selected.insert(selected.end(), additional.begin() + first, additional.begin() + last);
        first = last;
    }
    seeds = std::move(selected);

    // A fixed whole-sheet grid can miss small, repeated voids on a large sheet.
    // Probe a bounded set of local circle-pair midpoints only when no existing
    // seed is safe. Every accepted square is certified against every circle.
    if (!obstacles.empty() && std::all_of(obstacles.begin(), obstacles.end(), [](const RemnantObstacle& o) { return o.Radius > 0; })
        && std::none_of(seeds.begin(), seeds.end(), verify))
    {
        constexpr std::size_t maxAnchors = 32, maxNeighbors = 16, maxFallbackSeeds = 8;
        constexpr std::size_t maxCircleVisits = 4 * 1024 * 1024;
        struct CircleRank { std::size_t Index; double Radius, Distance; };
        struct SeedRank { Rect Bounds; double Area, Distance; };
        const Point boardCenter{ (usable.X + usableRight) / 2, (usable.Y + usableTop) / 2 };
        std::vector<CircleRank> ranks;
        for (std::size_t i = 0; i < obstacles.size(); ++i)
            ranks.push_back({ i, obstacles[i].Radius, std::hypot(obstacles[i].Center.X - boardCenter.X, obstacles[i].Center.Y - boardCenter.Y) });
        std::stable_sort(ranks.begin(), ranks.end(), [](const CircleRank& a, const CircleRank& b)
        { return a.Radius != b.Radius ? a.Radius > b.Radius : a.Distance < b.Distance; });
        std::vector<Point> probed;
        std::vector<SeedRank> fallback;
        std::size_t anchors = 0, circleVisits = 0;
        for (std::size_t first = 0; first < ranks.size() && anchors < maxAnchors; )
        {
            std::size_t last = first + 1;
            while (last < ranks.size() && std::abs(ranks[last].Radius - ranks[first].Radius) <= epsilon
                && std::abs(ranks[last].Distance - ranks[first].Distance) <= epsilon) ++last;
            const auto groupCount = last - first;
            const auto maximumWork = groupCount * obstacles.size() * (maxNeighbors + 1);
            // Reserve the worst-case work for the whole invariant anchor group;
            // a budget boundary must not retain only one orientation of a tie.
            if (groupCount <= maxAnchors - anchors && maximumWork <= maxCircleVisits - circleVisits)
            {
                anchors += groupCount;
                for (std::size_t ai = first; ai < last; ++ai)
                {
                    const auto anchorIndex = ranks[ai].Index;
                    const auto center = obstacles[anchorIndex].Center;
                    std::vector<std::pair<double,std::size_t>> neighbors;
                    for (std::size_t i = 0; i < obstacles.size(); ++i)
                    {
                        ++circleVisits;
                        if (i != anchorIndex) neighbors.emplace_back(std::hypot(obstacles[i].Center.X - center.X, obstacles[i].Center.Y - center.Y), i);
                    }
                    std::stable_sort(neighbors.begin(), neighbors.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
                    std::size_t limit = std::min(maxNeighbors, neighbors.size());
                    while (limit && limit < neighbors.size() && std::abs(neighbors[limit].first - neighbors[limit - 1].first) <= epsilon) --limit;
                    for (std::size_t ni = 0; ni < limit; ++ni)
                    {
                        const auto other = obstacles[neighbors[ni].second].Center;
                        const Point middle{ (center.X + other.X) / 2, (center.Y + other.Y) / 2 };
                        if (std::any_of(probed.begin(), probed.end(), [&](Point p) { return std::hypot(p.X - middle.X, p.Y - middle.Y) <= epsilon; })) continue;
                        probed.push_back(middle);
                        double clearance = std::numeric_limits<double>::infinity();
                        for (const auto& o : obstacles)
                        {
                            ++circleVisits;
                            clearance = std::min(clearance, std::hypot(middle.X - o.Center.X, middle.Y - o.Center.Y) - o.Radius - problem.PartGap);
                            if (clearance <= epsilon) break;
                        }
                        if (clearance <= epsilon) continue;
                        const double half = std::min({ clearance / std::sqrt(2.0) - epsilon, middle.X - usable.X,
                            usableRight - middle.X, middle.Y - usable.Y, usableTop - middle.Y });
                        if (half <= epsilon) continue;
                        const auto r = bounded({ middle.X - half, middle.Y - half, 2 * half, 2 * half });
                        fallback.push_back({ r, r.Width * r.Height, std::hypot(middle.X - boardCenter.X, middle.Y - boardCenter.Y) });
                    }
                }
            }
            first = last;
        }
        std::stable_sort(fallback.begin(), fallback.end(), [](const SeedRank& a, const SeedRank& b) { return a.Area > b.Area; });
        seeds.clear(); // Every old seed was checked above and found unsafe.
        for (std::size_t first = 0; first < fallback.size() && seeds.size() < maxFallbackSeeds; )
        {
            std::size_t last = first + 1;
            const double areaTolerance = epsilon * std::max(1.0, std::sqrt(fallback[first].Area));
            while (last < fallback.size() && std::abs(fallback[last].Area - fallback[first].Area) <= areaTolerance) ++last;
            std::stable_sort(fallback.begin() + first, fallback.begin() + last, [](const SeedRank& a, const SeedRank& b) { return a.Distance < b.Distance; });
            for (std::size_t begin = first; begin < last && seeds.size() < maxFallbackSeeds; )
            {
                std::size_t end = begin + 1;
                while (end < last && std::abs(fallback[end].Distance - fallback[begin].Distance) <= epsilon) ++end;
                if (end - begin > maxFallbackSeeds - seeds.size()) break;
                for (std::size_t i = begin; i < end; ++i) if (verify(fallback[i].Bounds)) seeds.push_back(fallback[i].Bounds);
                begin = end;
            }
            first = last;
        }
    }
    const std::array<std::array<int,4>,4> orders{{ {{0,1,2,3}}, {{2,3,0,1}}, {{1,3,0,2}}, {{3,1,2,0}} }};
    const double refinement = std::max(problem.Tolerance / 8, epsilon / 8);
    std::map<std::tuple<double,double,double,double,int>, Rect> expansionCache;
    for (const auto& seed : seeds)
    {
        if (!verify(seed)) continue;
        for (const auto& order : orders)
        {
            Rect r = seed;
            for (const int side : order)
            {
                const auto key = std::make_tuple(r.X, r.Y, r.Width, r.Height, side);
                const auto found = expansionCache.find(key);
                if (found != expansionCache.end()) { r = found->second; continue; }
                const auto remember = [&]() { expansionCache.emplace(key, r); };
                const double maximum = side == 0 ? r.X - usable.X : side == 1 ? Right(usable) - Right(r)
                    : side == 2 ? r.Y - usable.Y : Top(usable) - Top(r);
                if (maximum <= refinement) continue;
                const auto extended = [&](double amount)
                {
                    Rect candidate = r;
                    if (side == 0) { candidate.X -= amount; candidate.Width += amount; }
                    else if (side == 1) candidate.Width += amount;
                    else if (side == 2) { candidate.Y -= amount; candidate.Height += amount; }
                    else candidate.Height += amount;
                    return bounded(candidate);
                };
                if (verify(extended(maximum))) { r = extended(maximum); remember(); continue; }
                double safe = 0, unsafe = maximum;
                for (int iteration = 0; iteration < 48 && unsafe - safe > refinement; ++iteration)
                { const double mid = (safe + unsafe) / 2; if (verify(extended(mid))) safe = mid; else unsafe = mid; }
                r = extended(safe); remember();
            }
            const double area = r.Width * r.Height;
            if (area > result.Area && verify(r)) { result.Bounds = r; result.Area = area; }
        }
    }
    result.AreaUpperBound = std::max(result.Area, result.AreaUpperBound);
    return result;
}

RemnantEvaluation EvaluateRemnants(const Problem& problem, const Result& result, std::size_t resolution)
{
    RemnantEvaluation measured; measured.PerLayout.resize(result.Layouts.size());
    struct Group { std::size_t BestIndex = 0; double Area = -1, Upper = 0; };
    std::map<std::string, Group> groups;
    bool valid = true;
    for (std::size_t i = 0; i < result.Layouts.size(); ++i)
    {
        const auto& layout = result.Layouts[i];
        if (layout.Placements.empty()) continue; // An unused/empty sheet cannot manufacture a final-sheet score.
        auto remnant = FindRegularRemnant(problem, layout, resolution);
        measured.PerLayout[i] = remnant;
        if (!remnant.Verified) { valid = false; continue; }
        const auto stock = std::find_if(problem.Stocks.begin(), problem.Stocks.end(), [&](const Stock& s) { return s.ID == layout.StockID; });
        if (stock == problem.Stocks.end()) { valid = false; continue; }
        auto& group = groups[stock->Material];
        group.Upper = std::max(group.Upper, remnant.AreaUpperBound);
        if (remnant.Area > group.Area) { group.Area = remnant.Area; group.BestIndex = i; }
    }
    if (!valid) return measured; // Publish an aggregate score only after every actually used sheet is measured.
    for (const auto& [material, group] : groups)
    {
        (void)material;
        measured.FinalLayoutIndices.push_back(group.BestIndex);
        measured.LastSheetRegularArea += std::max(0.0, group.Area);
        measured.LastSheetAreaUpperBound += group.Upper;
    }
    measured.Measured = true;
    return measured;
}
}
