#include "NestingInternal.h"
#include "NestingAxisSafety.h"
#include <clipper2/clipper.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

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
        auto result = Detail::MaterialRing(source);
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
    // A separating projection certifies disjoint original material before the
    // fixed-precision clipping path. In particular, rounding two touching
    // oblique edges independently must not manufacture a thin intersection.
    // This is only an early separation certificate: concave rings and holes
    // still use the topology calculation whenever no such axis is found.
    bool SeparatedOnEdges(const std::vector<Point>& edges,
        const std::vector<Point>& a, const std::vector<Point>& b, double gap)
    {
        for (std::size_t i = 0; i < edges.size(); ++i)
        {
            const auto origin = edges[i], end = edges[(i + 1) % edges.size()];
            const long double nx = -(static_cast<long double>(end.Y) - origin.Y);
            const long double ny = static_cast<long double>(end.X) - origin.X;
            if (nx == 0 && ny == 0) continue;
            const auto range = [&](const std::vector<Point>& ring)
            {
                long double low = std::numeric_limits<long double>::infinity(), high = -low;
                for (const auto q : ring)
                {
                    const auto projected = nx * (static_cast<long double>(q.X) - origin.X)
                        + ny * (static_cast<long double>(q.Y) - origin.Y);
                    low = std::min(low, projected); high = std::max(high, projected);
                }
                return std::pair(low, high);
            };
            const auto [al, ah] = range(a); const auto [bl, bh] = range(b);
            const long double required = static_cast<long double>(gap) * std::hypot(nx, ny);
            if (ah + required <= bl || bh + required <= al) return true;
        }
        return false;
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
        if (!AxisSafety::ArithmeticSupported())
        { error = "polygon topology requires supported exact arithmetic"; return false; }
        std::vector<AxisSafety::Vector> points;
        points.reserve(ring.size());
        for (const auto point : ring)
        {
            if (!Finite(point) || std::abs(point.X) > CoordinateLimit || std::abs(point.Y) > CoordinateLimit)
            { error = "polygon coordinates must be finite and within 1e7 mm"; return false; }
            points.push_back({AxisSafety::Number(point.X), AxisSafety::Number(point.Y)});
        }
        try
        {
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                const auto next = (i + 1) % ring.size(), previous = (i + ring.size() - 1) % ring.size();
                const auto p = ring[i], q = ring[next];
                if (p.X == q.X && p.Y == q.Y) { error = "polygon has a degenerate edge"; return false; }
                const auto& a = points[i]; const auto& b = points[next];
                if (AxisSafety::OrientationSign(points[previous], a, b, true) == 0 &&
                    AxisSafety::ProjectionSign(points[previous], a, b, a, true) > 0)
                { error = "polygon has a reversing or overlapping adjacent edge"; return false; }
                for (std::size_t j = i + 1; j < ring.size(); ++j)
                {
                    if (j == next || (i == 0 && j + 1 == ring.size())) continue;
                    const auto end = (j + 1) % ring.size();
                    const auto r = ring[j], s = ring[end];
                    // Strict source-coordinate bounds preserve every nonzero gap and short edge.
                    if (std::max(p.X, q.X) < std::min(r.X, s.X) || std::max(r.X, s.X) < std::min(p.X, q.X) ||
                        std::max(p.Y, q.Y) < std::min(r.Y, s.Y) || std::max(r.Y, s.Y) < std::min(p.Y, q.Y)) continue;
                    const auto& c = points[j]; const auto& d = points[end];
                    const int ac = AxisSafety::OrientationSign(a, b, c, true), ad = AxisSafety::OrientationSign(a, b, d, true);
                    const int ca = AxisSafety::OrientationSign(c, d, a, true), cb = AxisSafety::OrientationSign(c, d, b, true);
                    if ((ac * ad < 0 && ca * cb < 0) ||
                        (ac == 0 && AxisSafety::Between(c, a, b, true)) ||
                        (ad == 0 && AxisSafety::Between(d, a, b, true)) ||
                        (ca == 0 && AxisSafety::Between(a, c, d, true)) ||
                        (cb == 0 && AxisSafety::Between(b, c, d, true)))
                    { error = "polygon ring self-intersects or touches itself"; return false; }
                }
            }
        }
        catch (const AxisSafety::ArithmeticUncertainty&)
        { error = "polygon topology could not be certified by exact arithmetic"; return false; }
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
    struct TopologyRing
    {
        std::vector<Point> Points;
        std::vector<AxisSafety::EdgeBounds> Edges;
        AxisSafety::EdgeBounds Bounds;
        std::unique_ptr<AxisSafety::EdgeIndex> Index;
        double Scale=1;
        explicit TopologyRing(std::vector<Point> points,bool filtered):Points(std::move(points))
        {
            if(!filtered||Points.empty())return;
            Bounds={{Points.front().X,Points.front().X},{Points.front().Y,Points.front().Y},true};
            Edges.reserve(Points.size());
            for(std::size_t i=0;i<Points.size();++i)
            {
                const auto p=Points[i],q=Points[(i+1)%Points.size()];
                const bool supported=Finite(p)&&Finite(q)&&std::abs(p.X)<=CoordinateLimit&&std::abs(p.Y)<=CoordinateLimit&&
                    std::abs(q.X)<=CoordinateLimit&&std::abs(q.Y)<=CoordinateLimit;
                Edges.push_back({{std::min(p.X,q.X),std::max(p.X,q.X)},
                    {std::min(p.Y,q.Y),std::max(p.Y,q.Y)},supported});
                Bounds.Valid=Bounds.Valid&&supported;
                Bounds.X.Low=std::min(Bounds.X.Low,p.X);Bounds.X.High=std::max(Bounds.X.High,p.X);
                Bounds.Y.Low=std::min(Bounds.Y.Low,p.Y);Bounds.Y.High=std::max(Bounds.Y.High,p.Y);
                Scale=std::max({Scale,std::abs(p.X),std::abs(p.Y)});
            }
            // This object stays at a fixed address. The index references this
            // exact edge vector; no query handle refers to a moved local ring.
            // Invalid coordinates must reach SimpleRing's existing diagnostic,
            // never the ordering predicate used to construct an index.
            if(Bounds.Valid)Index=std::make_unique<AxisSafety::EdgeIndex>(Edges);
        }
    };
    double TopologyPadding(const TopologyRing& a,const TopologyRing& b,double tolerance)
    {
        // Source coordinates are bounded by 1e7. Retain a generous outward
        // allowance for subtraction/projection/hypot rounding in the original
        // floating distance predicate. Near-threshold pairs use that predicate.
        return std::nextafter(tolerance+512*std::numeric_limits<double>::epsilon()*std::max(a.Scale,b.Scale),
            std::numeric_limits<double>::infinity());
    }
    bool TopologyBoundsClear(const TopologyRing& a,const TopologyRing& b,double tolerance)
    {
        return AxisSafety::ArithmeticSupported()&&AxisSafety::BoundsClear(a.Bounds,b.Bounds,TopologyPadding(a,b,tolerance));
    }
    bool RingWithinTolerance(const TopologyRing& a,const TopologyRing& b,double tolerance,bool filtered)
    {
        if(!filtered||!AxisSafety::ArithmeticSupported()||!a.Bounds.Valid||!b.Bounds.Valid||!b.Index)
            return RingDistance(a.Points,b.Points)<=tolerance;
        const auto padding=TopologyPadding(a,b,tolerance);
        if(!std::isfinite(padding))return RingDistance(a.Points,b.Points)<=tolerance;
        if(AxisSafety::BoundsClear(a.Bounds,b.Bounds,padding))return false;
        for(std::size_t i=0;i<a.Points.size();++i)
            for(const auto j:b.Index->Query(a.Edges[i],padding))
                if(SegmentDistance(a.Points[i],a.Points[(i+1)%a.Points.size()],
                    b.Points[j],b.Points[(j+1)%b.Points.size()])<=tolerance)return true;
        return false;
    }
    bool RingsBelowClearance(const TopologyRing& a,const TopologyRing& b,double gap,double tolerance,bool filtered)
    {
        const auto reference=[&]{return RingDistance(a.Points,b.Points)+tolerance<gap;};
        if(!filtered||!AxisSafety::ArithmeticSupported()||!a.Bounds.Valid||!b.Bounds.Valid||!b.Index||
            !std::isfinite(gap)||!std::isfinite(tolerance)||tolerance<0)
            return reference();
        // Use the larger full gap for conservative source-coordinate queries.
        // Never rewrite the leaf predicate as distance < gap - tolerance:
        // the original addition and strict comparison define boundary cases.
        const auto padding=TopologyPadding(a,b,gap);
        if(!std::isfinite(padding))return reference();
        if(AxisSafety::BoundsClear(a.Bounds,b.Bounds,padding))return false;
        const auto boundaryAllowance=TopologyPadding(a,b,0)+16*std::numeric_limits<double>::epsilon()*
            std::max(std::abs(gap),std::abs(tolerance));
        for(std::size_t i=0;i<a.Points.size();++i)
            for(const auto j:b.Index->Query(a.Edges[i],padding))
            {
                const double comparison=SegmentDistance(a.Points[i],a.Points[(i+1)%a.Points.size()],
                    b.Points[j],b.Points[(j+1)%b.Points.size()])+tolerance;
                // Retain the complete original scan around the declared
                // boundary, including its rounding and minimum ordering.
                if(std::abs(comparison-gap)<=boundaryAllowance)return reference();
                if(comparison<gap)return true;
            }
        return false;
    }
    std::vector<std::vector<Point>> WorldRings(const Shape& shape, Pose pose)
    {
        std::vector<std::vector<Point>> rings{ Ring(shape.Outer) };
        for (const auto& h : shape.Holes) rings.push_back(Ring(h));
        for (auto& ring : rings) for (auto& p : ring) p = Detail::Transform(p, pose);
        return rings;
    }
    bool ShareStraightBoundary(const std::vector<std::vector<Point>>& a,
        const std::vector<std::vector<Point>>& b, double tolerance,
        const std::vector<bool>& aAllowed, const std::vector<bool>& bAllowed)
    {
        // Common-edge permission concerns coincident lines, not merely lines
        // closer than the machining/validation tolerance. Permit only floating
        // point transformation roundoff, capped by the declared tolerance.
        for (std::size_t r = 0; r < a.size(); ++r)
        {
            if (!aAllowed.empty() && (r >= aAllowed.size() || !aAllowed[r])) continue;
            const auto& ar = a[r];
            for (std::size_t i = 0; i < ar.size(); ++i)
            {
                const auto p = ar[i], q = ar[(i + 1) % ar.size()];
                const long double dx = static_cast<long double>(q.X) - p.X;
                const long double dy = static_cast<long double>(q.Y) - p.Y;
                const long double length = std::hypot(dx, dy);
                if (length <= tolerance) continue;
                for (std::size_t s = 0; s < b.size(); ++s)
                {
                    if (!bAllowed.empty() && (s >= bAllowed.size() || !bAllowed[s])) continue;
                    const auto& br = b[s];
                    for (std::size_t j = 0; j < br.size(); ++j)
                    {
                        const auto u = br[j], v = br[(j + 1) % br.size()];
                        const long double scale = std::max({1.L, length,
                            std::abs(static_cast<long double>(p.X)), std::abs(static_cast<long double>(p.Y)),
                            std::abs(static_cast<long double>(q.X)), std::abs(static_cast<long double>(q.Y)),
                            std::abs(static_cast<long double>(u.X)), std::abs(static_cast<long double>(u.Y)),
                            std::abs(static_cast<long double>(v.X)), std::abs(static_cast<long double>(v.Y))});
                        const long double collinearTolerance = std::min(static_cast<long double>(tolerance),
                            64 * std::numeric_limits<double>::epsilon() * scale);
                        const auto distance = [&](Point point)
                        {
                            return std::abs(dx * (static_cast<long double>(point.Y) - p.Y)
                                - dy * (static_cast<long double>(point.X) - p.X)) / length;
                        };
                        if (distance(u) > collinearTolerance || distance(v) > collinearTolerance) continue;
                        const auto projection = [&](Point point)
                        {
                            return (dx * (static_cast<long double>(point.X) - p.X)
                                + dy * (static_cast<long double>(point.Y) - p.Y)) / length;
                        };
                        const long double first = projection(u), second = projection(v);
                        const long double overlap = std::min(length, std::max(first, second))
                            - std::max(0.L, std::min(first, second));
                        if (overlap > tolerance) return true;
                    }
                }
            }
        }
        return false;
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
    std::vector<Point> MaterialRing(const std::vector<Point>& source)
    {
        std::vector<Point> result;
        result.reserve(source.size());
        for (const auto point : source)
            if (result.empty() || point.X != result.back().X || point.Y != result.back().Y)
                result.push_back(point);
        if (result.size() > 1 && result.front().X == result.back().X && result.front().Y == result.back().Y)
            result.pop_back();
        return result;
    }
    Shape ReflectedShape(const Shape& source)
    {
        Shape reflected=source;
        reflected.Center.Y=-reflected.Center.Y;
        for(auto& point:reflected.Outer)point.Y=-point.Y;
        for(auto& ring:reflected.Holes)for(auto& point:ring)point.Y=-point.Y;
        return reflected;
    }
    Point Transform(Point p, Pose pose)
    {
        if(pose.Mirrored)p.Y=-p.Y;
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
    Point InverseTransform(Point p,Pose pose)
    {
        auto local=Transform({p.X-pose.X,p.Y-pose.Y},{0,0,-pose.AngleDegrees});
        if(pose.Mirrored)local.Y=-local.Y;
        return local;
    }
    Pose ComposePose(Pose parent,Pose child)
    {
        const auto origin=Transform({child.X,child.Y},parent);
        return {origin.X,origin.Y,NormalizeAngle(parent.AngleDegrees+
            (parent.Mirrored?-child.AngleDegrees:child.AngleDegrees)),parent.Mirrored!=child.Mirrored};
    }
    Pose InversePose(Pose pose)
    {
        Pose inverse{0,0,NormalizeAngle(pose.Mirrored?pose.AngleDegrees:-pose.AngleDegrees),pose.Mirrored};
        const auto origin=Transform({-pose.X,-pose.Y},inverse);
        inverse.X=origin.X;inverse.Y=origin.Y;
        return inverse;
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
    bool PoseAllowed(const Part& part,Pose pose,double tolerance)
    {
        return std::isfinite(pose.X)&&std::isfinite(pose.Y)&&(!pose.Mirrored||part.AllowMirror)&&
            AngleAllowed(part,pose.AngleDegrees,tolerance);
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
    std::vector<Pose> CandidateOrientations(const Part& part,double tolerance,OrientationProposalPolicy policy)
    {
        const auto angles=CandidateAngles(part,tolerance);
        const bool reflectedProposals=part.AllowMirror&&policy==OrientationProposalPolicy::AllowedReflections;
        std::vector<Pose> orientations;orientations.reserve(angles.size()*(reflectedProposals?2:1));
        for(const auto angle:angles)
        {
            orientations.push_back({0,0,angle,false});
            if(reflectedProposals)
            {
                // Reflection negates every source edge's direction. Free-angle
                // alignment must therefore negate the original sampled turn;
                // explicit allowed rotations still constrain the final angle.
                const auto reflectedAngle=part.FreeRotation?NormalizeAngle(-angle):angle;
                const Pose reflected{0,0,reflectedAngle,true};
                if(PoseAllowed(part,reflected,tolerance))orientations.push_back(reflected);
            }
        }
        return orientations;
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
    static bool ShapeValidImpl(const Shape& shape, double tolerance, std::string& error,bool filtered)
    {
        error.clear();
        if (!std::isfinite(tolerance) || tolerance < MinimumTolerance)
        { error = "geometry tolerance must be at least 1e-7 mm"; return false; }
        if (!std::isfinite(shape.Radius) || shape.Radius < 0)
        { error = "circle radius must be finite and nonnegative"; return false; }
        if (shape.Radius > 0)
        {
            if (!shape.Outer.empty() || !shape.Holes.empty()) { error = "circle and polygon representations are mutually exclusive"; return false; }
            if (!shape.CommonEdgeRings.empty()) { error = "a circle cannot declare polygon common-edge rings"; return false; }
            if (!Finite(shape.Center) || shape.Radius <= tolerance || std::abs(shape.Center.X) + shape.Radius > CoordinateLimit
                || std::abs(shape.Center.Y) + shape.Radius > CoordinateLimit)
            { error = "circle must be nondegenerate and within 1e7 mm"; return false; }
            return true;
        }
        if (!shape.CommonEdgeRings.empty() && shape.CommonEdgeRings.size() != 1 + shape.Holes.size())
        { error = "common-edge ring flags must match the outer ring and every hole"; return false; }
        TopologyRing outer(MaterialRing(shape.Outer),filtered);
        if (!SimpleRing(outer.Points, tolerance, error)) return false;
        std::vector<std::unique_ptr<TopologyRing>> holes;holes.reserve(shape.Holes.size());
        for (const auto& h : shape.Holes)
        {
            auto hole=std::make_unique<TopologyRing>(MaterialRing(h),filtered);
            if (!SimpleRing(hole->Points, tolerance, error)) return false;
            if (PointInRing(hole->Points.front(), outer.Points, tolerance) != 1 || RingWithinTolerance(*hole,outer,tolerance,filtered))
            { error = "hole must lie strictly inside the outer ring"; return false; }
            for (const auto& other : holes)
            {
                if(filtered&&TopologyBoundsClear(*hole,*other,tolerance))continue;
                if (RingWithinTolerance(*hole,*other,tolerance,filtered) || PointInRing(hole->Points.front(),other->Points,tolerance)>=0
                    || PointInRing(other->Points.front(),hole->Points,tolerance)>=0)
                { error = "holes must be disjoint and cannot nest"; return false; }
            }
            holes.push_back(std::move(hole));
        }
        return true;
    }
    bool ShapeValid(const Shape& shape,double tolerance,std::string& error)
    {return ShapeValidImpl(shape,tolerance,error,true);}
    // Independent exhaustive topology reference for differential tests. The
    // production filter never replaces the existing narrow-phase predicates.
    bool ShapeValidReference(const Shape& shape,double tolerance,std::string& error)
    {return ShapeValidImpl(shape,tolerance,error,false);}
    bool ShapesHaveCommonEdge(const Shape& a, Pose pa, const Shape& b, Pose pb, double tolerance)
    {
        if (a.Radius > 0 || b.Radius > 0 || !std::isfinite(pa.X) || !std::isfinite(pa.Y)
            || !std::isfinite(pa.AngleDegrees) || !std::isfinite(pb.X) || !std::isfinite(pb.Y)
            || !std::isfinite(pb.AngleDegrees)) return false;
        return ShareStraightBoundary(WorldRings(a, pa), WorldRings(b, pb), tolerance,
            a.CommonEdgeRings, b.CommonEdgeRings);
    }
    static bool ShapesConflictImpl(const Shape& a, Pose pa, const Shape& b, Pose pb, double gap, double tolerance,
        bool allowCommonEdge,bool filtered)
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
        const double requiredGap = allowCommonEdge && ShareStraightBoundary(ar, br, tolerance,
            a.CommonEdgeRings, b.CommonEdgeRings) ? 0 : gap;
        try
        {
            const auto overlap = Clipper2Lib::Intersect(Paths(ar), Paths(br), Clipper2Lib::FillRule::EvenOdd, 8);
            if (std::abs(Clipper2Lib::Area(overlap)) > tolerance * tolerance)
            {
                if (SeparatedOnEdges(ar.front(), ar.front(), br.front(), requiredGap)
                    || SeparatedOnEdges(br.front(), ar.front(), br.front(), requiredGap)) return false;
                return true;
            }
        }
        catch (...) { return true; } // Failed topology calculation is never accepted as a fit.
        if (requiredGap > tolerance)
        {
            if(!filtered)
            {
                for(const auto& x:ar)for(const auto& y:br)
                    if(RingDistance(x,y)+tolerance<requiredGap)return true;
            }
            else
            {
                std::vector<std::unique_ptr<TopologyRing>> preparedB;preparedB.reserve(br.size());
                for(const auto& y:br)preparedB.push_back(std::make_unique<TopologyRing>(y,true));
                for(const auto& x:ar)
                {
                    const TopologyRing preparedA(x,true);
                    for(const auto& y:preparedB)
                        if(RingsBelowClearance(preparedA,*y,requiredGap,tolerance,true))return true;
                }
            }
        }
        return false;
    }
    bool ShapesConflict(const Shape& a,Pose pa,const Shape& b,Pose pb,double gap,double tolerance,bool allowCommonEdge)
    {return ShapesConflictImpl(a,pa,b,pb,gap,tolerance,allowCommonEdge,true);}
    bool ShapesConflictReference(const Shape& a,Pose pa,const Shape& b,Pose pb,double gap,double tolerance,bool allowCommonEdge)
    {return ShapesConflictImpl(a,pa,b,pb,gap,tolerance,allowCommonEdge,false);}
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
