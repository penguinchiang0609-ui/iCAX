#include "NestingInternal.h"
#include <clipper2/clipper.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace iCAX::SheetNesting
{
namespace
{
    constexpr double CoordinateLimit = 1e7;
    constexpr double MinimumTolerance = 1e-7; // ClipperD precision 8, plus coordinate roundoff.
    double Cross(Point a, Point b, Point c)
    {
        return static_cast<double>((static_cast<long double>(b.X) - a.X) * (static_cast<long double>(c.Y) - a.Y)
            - (static_cast<long double>(b.Y) - a.Y) * (static_cast<long double>(c.X) - a.X));
    }
    double Distance(Point a, Point b) { return std::hypot(a.X - b.X, a.Y - b.Y); }
    bool Finite(Point p) { return std::isfinite(p.X) && std::isfinite(p.Y); }
    std::vector<Point> Ring(const std::vector<Point>& source, double tolerance = 0)
    {
        auto result = source;
        if (result.size() > 1 && Distance(result.front(), result.back()) <= tolerance) result.pop_back();
        return result;
    }
    double SignedArea(const std::vector<Point>& ring)
    {
        if (ring.size() < 3) return 0;
        const auto origin = ring.front();
        long double result = 0;
        for (std::size_t i = 1; i + 1 < ring.size(); ++i)
            result += (static_cast<long double>(ring[i].X) - origin.X) * (static_cast<long double>(ring[i + 1].Y) - origin.Y)
                - (static_cast<long double>(ring[i].Y) - origin.Y) * (static_cast<long double>(ring[i + 1].X) - origin.X);
        return static_cast<double>(result / 2);
    }
    double PointSegmentDistance(Point p, Point a, Point b)
    {
        const double x = b.X - a.X, y = b.Y - a.Y;
        const double length2 = x * x + y * y;
        if (length2 == 0) return Distance(p, a);
        const double t = std::clamp(((p.X - a.X) * x + (p.Y - a.Y) * y) / length2, 0.0, 1.0);
        return Distance(p, { a.X + t * x, a.Y + t * y });
    }
    double SegmentDistance(Point a, Point b, Point c, Point d)
    {
        const double abC = Cross(a, b, c), abD = Cross(a, b, d), cdA = Cross(c, d, a), cdB = Cross(c, d, b);
        if (((abC > 0 && abD < 0) || (abC < 0 && abD > 0))
            && ((cdA > 0 && cdB < 0) || (cdA < 0 && cdB > 0))) return 0;
        return std::min({ PointSegmentDistance(a, c, d), PointSegmentDistance(b, c, d),
            PointSegmentDistance(c, a, b), PointSegmentDistance(d, a, b) });
    }
    // -1 outside, 0 boundary, +1 inside. Winding of the input is immaterial.
    int PointInRing(Point p, const std::vector<Point>& ring, double tolerance)
    {
        bool inside = false;
        for (std::size_t i = 0; i < ring.size(); ++i)
        {
            const auto a = ring[i], b = ring[(i + 1) % ring.size()];
            if (PointSegmentDistance(p, a, b) <= tolerance) return 0;
            if ((a.Y > p.Y) != (b.Y > p.Y))
            {
                const long double x = a.X + (static_cast<long double>(p.Y) - a.Y) * (b.X - a.X) / (b.Y - a.Y);
                if (x > p.X) inside = !inside;
            }
        }
        return inside ? 1 : -1;
    }
    bool SimpleRing(const std::vector<Point>& ring, double tolerance, std::string& error)
    {
        if (ring.size() < 3) { error = "polygon ring needs at least three vertices"; return false; }
        for (std::size_t i = 0; i < ring.size(); ++i)
        {
            const auto p = ring[i], q = ring[(i + 1) % ring.size()];
            if (!Finite(p) || std::abs(p.X) > CoordinateLimit || std::abs(p.Y) > CoordinateLimit)
            { error = "polygon coordinates must be finite and within 1e7 mm"; return false; }
            if (Distance(p, q) <= tolerance) { error = "polygon has a degenerate edge"; return false; }
            const auto previous = ring[(i + ring.size() - 1) % ring.size()];
            if (PointSegmentDistance(q, previous, p) <= tolerance)
            { error = "polygon has a reversing or overlapping adjacent edge"; return false; }
            for (std::size_t j = i + 1; j < ring.size(); ++j)
            {
                if (j == i + 1 || (i == 0 && j + 1 == ring.size())) continue;
                if (SegmentDistance(p, q, ring[j], ring[(j + 1) % ring.size()]) <= tolerance)
                { error = "polygon ring self-intersects or touches itself"; return false; }
            }
        }
        if (std::abs(SignedArea(ring)) <= tolerance * tolerance)
        { error = "polygon ring has zero area"; return false; }
        return true;
    }
    double RingDistance(const std::vector<Point>& a, const std::vector<Point>& b)
    {
        double result = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < a.size(); ++i)
            for (std::size_t j = 0; j < b.size(); ++j)
                result = std::min(result, SegmentDistance(a[i], a[(i + 1) % a.size()], b[j], b[(j + 1) % b.size()]));
        return result;
    }
    std::vector<std::vector<Point>> WorldRings(const Shape& shape, Pose pose)
    {
        std::vector<std::vector<Point>> rings{ Ring(shape.Outer) };
        for (const auto& h : shape.Holes) rings.push_back(Ring(h));
        for (auto& ring : rings) for (auto& p : ring) p = Detail::Transform(p, pose);
        return rings;
    }
    Clipper2Lib::PathsD Paths(const std::vector<std::vector<Point>>& rings)
    {
        Clipper2Lib::PathsD result;
        for (const auto& ring : rings)
        {
            auto& path = result.emplace_back();
            for (const auto p : ring) path.emplace_back(p.X, p.Y);
        }
        return result;
    }
    bool CirclePolygonConflict(Point center, double radius, const std::vector<std::vector<Point>>& rings,
        double gap, double tolerance)
    {
        const int outer = PointInRing(center, rings.front(), tolerance);
        bool inMaterial = outer > 0;
        if (outer == 0) return true;
        for (std::size_t i = 1; i < rings.size(); ++i)
        {
            const int position = PointInRing(center, rings[i], tolerance);
            if (position == 0) return true;
            if (position > 0) inMaterial = false;
        }
        if (inMaterial) return true;
        double distance = std::numeric_limits<double>::infinity();
        for (const auto& ring : rings)
            for (std::size_t i = 0; i < ring.size(); ++i)
                distance = std::min(distance, PointSegmentDistance(center, ring[i], ring[(i + 1) % ring.size()]));
        return distance + tolerance < radius + gap;
    }
    std::vector<Point> ReducedRing(const std::vector<Point>& source, double tolerance)
    {
        auto result = Ring(source, tolerance);
        bool changed = true;
        while (changed && result.size() > 3)
        {
            changed = false;
            for (std::size_t i = 0; i < result.size(); ++i)
            {
                if (PointSegmentDistance(result[i], result[(i + result.size() - 1) % result.size()],
                    result[(i + 1) % result.size()]) <= tolerance)
                { result.erase(result.begin() + static_cast<std::ptrdiff_t>(i)); changed = true; break; }
            }
        }
        return result;
    }
    std::vector<Point> ConvexHull(std::vector<Point> p)
    {
        std::sort(p.begin(), p.end(), [](Point a, Point b) { return a.X < b.X || (a.X == b.X && a.Y < b.Y); });
        p.erase(std::unique(p.begin(), p.end(), [](Point a, Point b) { return a.X == b.X && a.Y == b.Y; }), p.end());
        if (p.size() < 3) return p;
        std::vector<Point> h(2 * p.size()); std::size_t k = 0;
        for (auto point : p) { while (k >= 2 && Cross(h[k - 2], h[k - 1], point) <= 0) --k; h[k++] = point; }
        const auto lower = k + 1;
        for (std::size_t i = p.size() - 1; i-- > 0;)
        { while (k >= lower && Cross(h[k - 2], h[k - 1], p[i]) <= 0) --k; h[k++] = p[i]; }
        h.resize(k - 1); return h;
    }
    bool Parallel(Point a, Point b, Point c, Point d, double tolerance)
    {
        const double x = b.X - a.X, y = b.Y - a.Y, u = d.X - c.X, v = d.Y - c.Y;
        return std::abs(x * v - y * u) <= tolerance * std::max(std::hypot(x, y), std::hypot(u, v));
    }
}
namespace Detail
{
    Point Transform(Point p, Pose pose)
    {
        const double degrees=NormalizeAngle(pose.AngleDegrees);
        // Exact quarter turns are exact coordinate permutations. Avoid introducing
        // a spurious cos(90) component into dimensions used by tight row/hole fits.
        if(degrees==0)return {p.X+pose.X,p.Y+pose.Y};
        if(degrees==90)return {-p.Y+pose.X,p.X+pose.Y};
        if(degrees==180)return {-p.X+pose.X,-p.Y+pose.Y};
        if(degrees==270)return {p.Y+pose.X,-p.X+pose.Y};
        const double a = degrees * Pi / 180, c = std::cos(a), s = std::sin(a);
        return { c * p.X - s * p.Y + pose.X, s * p.X + c * p.Y + pose.Y };
    }
    double NormalizeAngle(double degrees)
    {
        if (!std::isfinite(degrees)) return degrees;
        degrees = std::fmod(degrees, 360.0); if (degrees < 0) degrees += 360;
        return degrees == 360 || degrees == 0 ? 0 : degrees;
    }
    bool AngleAllowed(const Part& part, double degrees, double tolerance)
    {
        if (!std::isfinite(degrees)) return false;
        if (part.FreeRotation) return true;
        const auto near = [&](double a) { const double delta = std::abs(NormalizeAngle(a) - NormalizeAngle(degrees));
            return std::min(delta, 360 - delta) <= tolerance; };
        if (part.AllowedAnglesDegrees.empty()) return near(0);
        return std::any_of(part.AllowedAnglesDegrees.begin(), part.AllowedAnglesDegrees.end(), near);
    }
    std::vector<double> CandidateAngles(const Part& part, double tolerance)
    {
        std::vector<double> angles;
        auto add = [&](double angle)
        {
            if (!AngleAllowed(part, angle, tolerance)) return;
            angle = NormalizeAngle(angle);
            for (double old : angles) { const double delta = std::abs(old - angle);
                if (std::min(delta, 360 - delta) <= tolerance) return; }
            angles.push_back(angle);
        };
        if (!part.FreeRotation)
        {
            if (part.AllowedAnglesDegrees.empty()) add(0);
            else for (double a : part.AllowedAnglesDegrees) add(a);
            return angles;
        }
        add(0);
        if (part.Geometry.Radius > 0) return angles;
        add(90); add(180); add(270);
        std::vector<std::pair<double, double>> edges;
        const auto ring = Ring(part.Geometry.Outer);
        for (std::size_t i = 0; i < ring.size(); ++i)
        { const auto p = ring[i], q = ring[(i + 1) % ring.size()]; edges.emplace_back(Distance(p, q), std::atan2(q.Y - p.Y, q.X - p.X) * 180 / Pi); }
        std::stable_sort(edges.begin(), edges.end(), [](auto a, auto b) { return a.first > b.first; });
        for (std::size_t i = 0; i < std::min<std::size_t>(4, edges.size()); ++i)
            for (int turn = 0; turn < 4; ++turn) add(-edges[i].second + turn * 90);
        return angles;
    }
    Rect Bounds(const Shape& shape, Pose pose)
    {
        if (shape.Radius > 0)
        { const auto c = Transform(shape.Center, pose); return { c.X - shape.Radius, c.Y - shape.Radius, shape.Radius * 2, shape.Radius * 2 }; }
        if (shape.Outer.empty()) return {};
        auto p = Transform(shape.Outer.front(), pose); double x0 = p.X, x1 = p.X, y0 = p.Y, y1 = p.Y;
        for (auto point : shape.Outer)
        { p = Transform(point, pose); x0 = std::min(x0, p.X); x1 = std::max(x1, p.X); y0 = std::min(y0, p.Y); y1 = std::max(y1, p.Y); }
        return { x0, y0, x1 - x0, y1 - y0 };
    }
    double Area(const Shape& shape)
    {
        if (shape.Radius > 0) return Pi * shape.Radius * shape.Radius;
        double area = std::abs(SignedArea(Ring(shape.Outer)));
        for (const auto& h : shape.Holes) area -= std::abs(SignedArea(Ring(h)));
        return area;
    }
    bool ShapeValid(const Shape& shape, double tolerance, std::string& error)
    {
        error.clear();
        if (!std::isfinite(tolerance) || tolerance < MinimumTolerance)
        { error = "geometry tolerance must be at least 1e-7 mm"; return false; }
        if (!std::isfinite(shape.Radius) || shape.Radius < 0)
        { error = "circle radius must be finite and nonnegative"; return false; }
        if (shape.Radius > 0)
        {
            if (!shape.Outer.empty() || !shape.Holes.empty()) { error = "circle and polygon representations are mutually exclusive"; return false; }
            if (!Finite(shape.Center) || shape.Radius <= tolerance || std::abs(shape.Center.X) + shape.Radius > CoordinateLimit
                || std::abs(shape.Center.Y) + shape.Radius > CoordinateLimit)
            { error = "circle must be nondegenerate and within 1e7 mm"; return false; }
            return true;
        }
        const auto outer = Ring(shape.Outer, tolerance);
        if (!SimpleRing(outer, tolerance, error)) return false;
        std::vector<std::vector<Point>> holes;
        for (const auto& h : shape.Holes)
        {
            auto hole = Ring(h, tolerance);
            if (!SimpleRing(hole, tolerance, error)) return false;
            if (PointInRing(hole.front(), outer, tolerance) != 1 || RingDistance(hole, outer) <= tolerance)
            { error = "hole must lie strictly inside the outer ring"; return false; }
            for (const auto& other : holes)
                if (RingDistance(hole, other) <= tolerance || PointInRing(hole.front(), other, tolerance) >= 0
                    || PointInRing(other.front(), hole, tolerance) >= 0)
                { error = "holes must be disjoint and cannot nest"; return false; }
            holes.push_back(std::move(hole));
        }
        return true;
    }
    bool ShapesConflict(const Shape& a, Pose pa, const Shape& b, Pose pb, double gap, double tolerance)
    {
        if (!std::isfinite(pa.X) || !std::isfinite(pa.Y) || !std::isfinite(pa.AngleDegrees)
            || !std::isfinite(pb.X) || !std::isfinite(pb.Y) || !std::isfinite(pb.AngleDegrees)) return true;
        const auto ba = Bounds(a, pa), bb = Bounds(b, pb);
        if (ba.X + ba.Width + gap <= bb.X + tolerance || bb.X + bb.Width + gap <= ba.X + tolerance
            || ba.Y + ba.Height + gap <= bb.Y + tolerance || bb.Y + bb.Height + gap <= ba.Y + tolerance) return false;
        if (a.Radius > 0 && b.Radius > 0)
            return Distance(Transform(a.Center, pa), Transform(b.Center, pb)) + tolerance < a.Radius + b.Radius + gap;
        if (a.Radius > 0) return CirclePolygonConflict(Transform(a.Center, pa), a.Radius, WorldRings(b, pb), gap, tolerance);
        if (b.Radius > 0) return CirclePolygonConflict(Transform(b.Center, pb), b.Radius, WorldRings(a, pa), gap, tolerance);
        const auto ar = WorldRings(a, pa), br = WorldRings(b, pb);
        try
        {
            const auto overlap = Clipper2Lib::Intersect(Paths(ar), Paths(br), Clipper2Lib::FillRule::EvenOdd, 8);
            if (std::abs(Clipper2Lib::Area(overlap)) > tolerance * tolerance) return true;
        }
        catch (...) { return true; } // Failed topology calculation is never accepted as a fit.
        if (gap > tolerance)
            for (const auto& x : ar) for (const auto& y : br)
                if (RingDistance(x, y) + tolerance < gap) return true;
        return false;
    }
    bool InsideSheet(const Shape& shape, Pose pose, double width, double height, double margin, double tolerance)
    {
        if (!std::isfinite(pose.X) || !std::isfinite(pose.Y) || !std::isfinite(pose.AngleDegrees)) return false;
        const auto box = Bounds(shape, pose);
        return box.X + tolerance >= margin && box.Y + tolerance >= margin
            && box.X + box.Width <= width - margin + tolerance && box.Y + box.Height <= height - margin + tolerance;
    }
}
Classification Classify(const Shape& shape, double tolerance)
{
    Classification result; std::string error;
    if (!Detail::ShapeValid(shape, tolerance, error)) return result;
    result.Area = Detail::Area(shape);
    if (shape.Radius > 0)
    { result.Kind = ShapeKind::Circle; result.EnvelopeWidth = result.EnvelopeHeight = 2 * shape.Radius; result.EnvelopeFill = Detail::Pi / 4; return result; }
    const auto simple = ReducedRing(shape.Outer, tolerance), hull = ConvexHull(simple);
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < hull.size(); ++i)
    {
        double angle = Detail::NormalizeAngle(std::atan2(hull[(i + 1) % hull.size()].Y - hull[i].Y,
            hull[(i + 1) % hull.size()].X - hull[i].X) * 180 / Detail::Pi);
        if (angle >= 180) angle -= 180;
        const auto box = Detail::Bounds(shape, { 0, 0, -angle }); const double area = box.Width * box.Height;
        if (area < best - tolerance * tolerance || (std::abs(area - best) <= tolerance * tolerance && angle < result.AlignmentDegrees))
        { best = area; result.AlignmentDegrees = angle; result.EnvelopeWidth = box.Width; result.EnvelopeHeight = box.Height; }
    }
    result.EnvelopeFill = best > 0 ? std::clamp(result.Area / best, 0.0, 1.0) : 0;
    if (shape.Holes.empty() && simple.size() == 3) result.Kind = ShapeKind::Triangle;
    else if (shape.Holes.empty() && simple.size() == 4 && hull.size() == 4)
    {
        const bool parallel1 = Parallel(simple[0], simple[1], simple[2], simple[3], tolerance);
        const bool parallel2 = Parallel(simple[1], simple[2], simple[3], simple[0], tolerance);
        const Point u{ simple[1].X - simple[0].X, simple[1].Y - simple[0].Y };
        const Point v{ simple[2].X - simple[1].X, simple[2].Y - simple[1].Y };
        if (parallel1 && parallel2)
            result.Kind = std::abs(u.X * v.X + u.Y * v.Y) <= tolerance * std::max(std::hypot(u.X, u.Y), std::hypot(v.X, v.Y))
                ? ShapeKind::Rectangle : ShapeKind::Parallelogram;
        else if (parallel1 || parallel2) result.Kind = ShapeKind::Trapezoid;
    }
    if (result.Kind == ShapeKind::Irregular && result.EnvelopeFill >= .85) result.Kind = ShapeKind::NearRectangle;
    return result;
}
}
