#include "NestingShapeFeatures.h"
#include "NestingAxisSafety.h"
#include <algorithm>
#include <array>
#include <clipper2/clipper.h>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <tuple>

namespace iCAX::SheetNesting::Detail
{
namespace
{
using namespace Clipper2Lib;
constexpr std::size_t MaxSourceVertices = 4096, MaxStripBands = 64, MaxMouths = 64, MaxIndexEntries = 32768;
struct Stopper
{
    const std::function<bool()> &Callback;
    bool Stopped = false;
    bool Poll()
    {
        if (!Stopped && Callback && Callback())
            Stopped = true;
        return Stopped;
    }
};
double Cross(Point a, Point b, Point c)
{
    return (b.X - a.X) * (c.Y - a.Y) - (b.Y - a.Y) * (c.X - a.X);
}
double Dot(Point a, Point b)
{
    return a.X * b.X + a.Y * b.Y;
}
Point Add(Point a, Point b)
{
    return {a.X + b.X, a.Y + b.Y};
}
Point Subtract(Point a, Point b)
{
    return {a.X - b.X, a.Y - b.Y};
}
Point Scale(Point a, double factor)
{
    return {a.X * factor, a.Y * factor};
}
double Length(Point value)
{
    return std::hypot(value.X, value.Y);
}
double SignedArea(const std::vector<Point> &ring)
{
    long double value = 0;
    for (std::size_t i = 1; i + 1 < ring.size(); ++i)
        value += (static_cast<long double>(ring[i].X) - ring[0].X) * (ring[i + 1].Y - ring[0].Y) -
                 (static_cast<long double>(ring[i].Y) - ring[0].Y) * (ring[i + 1].X - ring[0].X);
    return static_cast<double>(value * .5L);
}
PathD Path(const std::vector<Point> &source, bool positive = true)
{
    const auto ring = MaterialRing(source);
    PathD path;
    path.reserve(ring.size());
    for (const auto point : ring)
        path.emplace_back(point.X, point.Y);
    if (!path.empty() && IsPositive(path) != positive)
        std::reverse(path.begin(), path.end());
    return path;
}
PathsD Material(const Shape &shape)
{
    PathsD paths{Path(shape.Outer)};
    for (const auto &hole : shape.Holes)
        paths.push_back(Path(hole, false));
    return paths;
}
std::vector<Point> Hull(std::vector<Point> points, Stopper &stop)
{
    if (stop.Poll())
        return {};
    std::sort(points.begin(), points.end(), [](Point a, Point b) { return std::tie(a.X, a.Y) < std::tie(b.X, b.Y); });
    points.erase(std::unique(points.begin(), points.end(), [](Point a, Point b) { return a.X == b.X && a.Y == b.Y; }),
                 points.end());
    if (points.size() < 3)
        return points;
    std::vector<Point> hull;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        if ((i & 63) == 0 && stop.Poll())
            return {};
        while (hull.size() > 1 && Cross(hull[hull.size() - 2], hull.back(), points[i]) <= 0)
            hull.pop_back();
        hull.push_back(points[i]);
    }
    const auto lower = hull.size();
    for (std::size_t i = points.size() - 1; i > 0; --i)
    {
        if ((i & 63) == 0 && stop.Poll())
            return {};
        const auto point = points[i - 1];
        while (hull.size() > lower && Cross(hull[hull.size() - 2], hull.back(), point) <= 0)
            hull.pop_back();
        hull.push_back(point);
    }
    hull.pop_back();
    return hull;
}
struct Frame
{
    Point Origin, U{1, 0}, V{0, 1};
    Point Local(Point p) const
    {
        const auto d = Subtract(p, Origin);
        return {Dot(d, U), Dot(d, V)};
    }
    Point World(Point p) const
    {
        return Add(Origin, Add(Scale(U, p.X), Scale(V, p.Y)));
    }
};
struct Box
{
    double Left = 0, Bottom = 0, Right = 0, Top = 0;
    double Area() const
    {
        return (Right - Left) * (Top - Bottom);
    }
};
Box Project(const std::vector<Point> &points, const Frame &frame, Stopper &stop)
{
    Box box{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        if ((i & 63) == 0 && stop.Poll())
            return {};
        const auto p = frame.Local(points[i]);
        box.Left = std::min(box.Left, p.X);
        box.Right = std::max(box.Right, p.X);
        box.Bottom = std::min(box.Bottom, p.Y);
        box.Top = std::max(box.Top, p.Y);
    }
    return box;
}
Frame MainFrame(const std::vector<Point> &ring)
{
    Frame frame;
    frame.Origin = ring.front();
    double longest = 0;
    for (std::size_t i = 0; i < ring.size(); ++i)
    {
        const auto edge = Subtract(ring[(i + 1) % ring.size()], ring[i]);
        const auto length = Length(edge);
        if (length > longest)
        {
            longest = length;
            frame.U = Scale(edge, 1 / length);
        }
    }
    frame.V = {-frame.U.Y, frame.U.X};
    return frame;
}
Shape Rectangle(const Frame &frame, const Box &box)
{
    Shape shape;
    shape.Outer = {frame.World({box.Left, box.Bottom}), frame.World({box.Right, box.Bottom}),
                   frame.World({box.Right, box.Top}), frame.World({box.Left, box.Top})};
    if (SignedArea(shape.Outer) < 0)
        std::reverse(shape.Outer.begin(), shape.Outer.end());
    return shape;
}
double MinimumObb(const std::vector<Point> &hull, Stopper &stop)
{
    if (hull.size() < 3)
        return 0;
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < hull.size(); ++i)
    {
        if (stop.Poll())
            return 0;
        const auto edge = Subtract(hull[(i + 1) % hull.size()], hull[i]);
        const double length = Length(edge);
        if (length == 0)
            continue;
        Frame frame;
        frame.Origin = hull[i];
        frame.U = Scale(edge, 1 / length);
        frame.V = {-frame.U.Y, frame.U.X};
        best = std::min(best, Project(hull, frame, stop).Area());
    }
    return std::isfinite(best) ? best : 0;
}
Shape CoreStrip(const Shape &shape, Frame frame, bool vertical, Stopper &stop, bool &truncated)
{
    if (vertical)
        std::swap(frame.U, frame.V);
    std::vector<std::vector<Point>> rings;
    std::vector<double> levels;
    const auto add = [&](const auto &source)
    {
        auto &ring = rings.emplace_back();
        ring.reserve(source.size());
        for (const auto p : MaterialRing(source))
        {
            const auto q = frame.Local(p);
            ring.push_back(q);
            levels.push_back(q.Y);
        }
    };
    add(shape.Outer);
    for (const auto &hole : shape.Holes)
        add(hole);
    std::sort(levels.begin(), levels.end());
    levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
    std::vector<std::size_t> bands;
    for (std::size_t i = 1; i < levels.size(); ++i)
        if (levels[i] > levels[i - 1])
            bands.push_back(i);
    std::stable_sort(bands.begin(), bands.end(),
                     [&](auto a, auto b) { return levels[a] - levels[a - 1] > levels[b] - levels[b - 1]; });
    if (bands.size() > MaxStripBands)
    {
        bands.resize(MaxStripBands);
        truncated = true;
    }
    Box best;
    double bestArea = 0;
    for (const auto band : bands)
    {
        if (stop.Poll())
            return {};
        const double y = levels[band - 1] + (levels[band] - levels[band - 1]) * .5;
        std::vector<double> cuts;
        for (const auto &ring : rings)
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                if ((i & 63) == 0 && stop.Poll())
                    return {};
                const auto a = ring[i], b = ring[(i + 1) % ring.size()];
                if ((a.Y <= y && y < b.Y) || (b.Y <= y && y < a.Y))
                    cuts.push_back(a.X + (b.X - a.X) * (y - a.Y) / (b.Y - a.Y));
            }
        std::sort(cuts.begin(), cuts.end());
        for (std::size_t i = 1; i < cuts.size(); i += 2)
        {
            const Box candidate{cuts[i - 1], levels[band - 1], cuts[i], levels[band]};
            if (candidate.Area() > bestArea)
            {
                best = candidate;
                bestArea = candidate.Area();
            }
        }
    }
    if (bestArea <= 0)
        return {};
    // A proper central strip exposes positive residuals on convex fillers.
    if (std::abs(bestArea - Area(shape)) <= 1e-12 * std::max(1., bestArea))
    {
        const double inset = (best.Top - best.Bottom) * .25;
        best.Bottom += inset;
        best.Top -= inset;
    }
    return Rectangle(frame, best);
}
std::size_t ShapeBytes(const Shape &shape)
{
    std::size_t bytes = shape.Outer.capacity() * sizeof(Point) + shape.Holes.capacity() * sizeof(std::vector<Point>);
    for (const auto &ring : shape.Holes)
        bytes += ring.capacity() * sizeof(Point);
    return bytes;
}
std::size_t ModelBytes(const ShapeFeatureModel &model)
{
    std::size_t bytes = model.Carriers.capacity() * sizeof(FeatureCarrier);
    if (model.Diagnostic.capacity() > std::string{}.capacity())
        bytes += model.Diagnostic.capacity() + 1;
    for (const auto &carrier : model.Carriers)
    {
        bytes += ShapeBytes(carrier.Geometry) + carrier.Features.capacity() * sizeof(EncodedShapeFeature);
        for (const auto &feature : carrier.Features)
        {
            bytes += feature.Rings.capacity() * sizeof(std::vector<Point>) +
                     feature.Mouths.capacity() * sizeof(FeatureMouth);
            for (const auto &ring : feature.Rings)
                bytes += ring.capacity() * sizeof(Point);
        }
    }
    return bytes;
}
bool PrecisionSensitive(const Shape &shape)
{
    const auto sensitive = [](const auto &source)
    {
        const auto ring = MaterialRing(source);
        for (std::size_t i = 0; i < ring.size(); ++i)
        {
            const auto a = ring[(i + ring.size() - 1) % ring.size()], b = ring[i], c = ring[(i + 1) % ring.size()];
            if (Length(Subtract(b, a)) < 4e-8)
                return true;
            const double chord = Length(Subtract(c, a)), cross = Cross(a, b, c);
            if (cross != 0 && chord > 0 && std::abs(cross) / chord < 4e-8)
                return true;
        }
        return false;
    };
    if (sensitive(shape.Outer))
        return true;
    return std::any_of(shape.Holes.begin(), shape.Holes.end(), sensitive);
}
void Mouths(EncodedShapeFeature &feature, const Shape &carrier, double tolerance, Stopper &stop, bool &truncated)
{
    const auto boundary = MaterialRing(carrier.Outer);
    for (std::size_t edge = 0; edge < boundary.size(); ++edge)
    {
        if (stop.Poll())
            return;
        const auto origin = boundary[edge], delta = Subtract(boundary[(edge + 1) % boundary.size()], origin);
        const auto length = Length(delta);
        if (length == 0)
            continue;
        const auto along = Scale(delta, 1 / length);
        Point into{-along.Y, along.X};
        if (feature.Polarity == FeaturePolarity::Protrusion)
            into = Scale(into, -1);
        std::vector<std::pair<double, double>> intervals;
        for (const auto &ring : feature.Rings)
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                if ((i & 63) == 0 && stop.Poll())
                    return;
                const auto a = Subtract(ring[i], origin), b = Subtract(ring[(i + 1) % ring.size()], origin);
                if (std::abs(a.X * along.Y - a.Y * along.X) > tolerance ||
                    std::abs(b.X * along.Y - b.Y * along.X) > tolerance)
                    continue;
                const auto pa = Dot(a, along), pb = Dot(b, along);
                const double lo = std::max(0., std::min(pa, pb)), hi = std::min(length, std::max(pa, pb));
                if (hi > lo)
                    intervals.emplace_back(lo, hi);
            }
        std::sort(intervals.begin(), intervals.end());
        for (std::size_t first = 0; first < intervals.size();)
        {
            auto last = first + 1;
            double high = intervals[first].second;
            while (last < intervals.size() && intervals[last].first <= high)
            {
                high = std::max(high, intervals[last].second);
                ++last;
            }
            FeatureMouth mouth;
            mouth.Start = Add(origin, Scale(along, intervals[first].first));
            mouth.End = Add(origin, Scale(along, high));
            mouth.Into = into;
            mouth.Width = high - intervals[first].first;
            for (const auto &ring : feature.Rings)
                for (const auto point : ring)
                    mouth.Depth = std::max(mouth.Depth, Dot(Subtract(point, mouth.Start), into));
            if (feature.Mouths.size() == MaxMouths)
            {
                truncated = true;
                return;
            }
            feature.Mouths.push_back(mouth);
            first = last;
        }
    }
}
void DifferenceFeatures(const PathsD &subject, const PathsD &clip, FeaturePolarity polarity, FeatureCarrier &carrier,
                        double tolerance, const ShapeFeatureLimits &limits, Stopper &stop, bool &truncated)
{
    if (stop.Poll())
        return;
    ClipperD engine(8);
    engine.AddSubject(subject);
    engine.AddClip(clip);
    PolyTreeD tree;
    if (!engine.Execute(ClipType::Difference, FillRule::NonZero, tree))
    {
        truncated = true;
        return;
    }
    std::vector<const PolyPathD *> pending;
    for (std::size_t i = 0; i < tree.Count(); ++i)
        pending.push_back(tree.Child(i));
    while (!pending.empty() && !stop.Poll())
    {
        const auto *node = pending.back();
        pending.pop_back();
        for (std::size_t i = 0; i < node->Count(); ++i)
            pending.push_back(node->Child(i));
        if (node->IsHole() || node->Polygon().size() < 3)
            continue;
        if (carrier.Features.size() >= limits.MaxFeaturesPerCarrier)
        {
            truncated = true;
            continue;
        }
        EncodedShapeFeature feature;
        feature.Polarity = polarity;
        const auto append = [&](const PathD &path)
        {
            auto &ring = feature.Rings.emplace_back();
            ring.reserve(path.size());
            for (const auto p : path)
                ring.push_back({p.x, p.y});
            feature.Area += SignedArea(ring);
        };
        append(node->Polygon());
        for (std::size_t i = 0; i < node->Count(); ++i)
            if (node->Child(i)->IsHole())
                append(node->Child(i)->Polygon());
        feature.Area = std::max(0., feature.Area);
        Shape boundsShape;
        for (const auto &ring : feature.Rings)
            boundsShape.Outer.insert(boundsShape.Outer.end(), ring.begin(), ring.end());
        feature.Bounds = Bounds(boundsShape);
        Mouths(feature, carrier.Geometry, tolerance, stop, truncated);
        carrier.Features.push_back(std::move(feature));
    }
}
PathsD FeaturePaths(const FeatureCarrier &carrier, FeaturePolarity polarity)
{
    PathsD paths;
    for (const auto &feature : carrier.Features)
        if (feature.Polarity == polarity)
            for (const auto &ring : feature.Rings)
            {
                PathD path;
                for (const auto p : ring)
                    path.emplace_back(p.X, p.Y);
                paths.push_back(std::move(path));
            }
    return paths;
}
struct FeatureRef
{
    std::size_t Part = 0, Carrier = 0, Feature = 0, Mouth = 0;
};
struct Edge
{
    Point Start, End;
    double Length = 0;
};
struct PartInfo
{
    std::vector<Point> Hull;
    std::vector<Edge> Edges;
    double SoloObb = 0, Material = 0;
};
double Direction(Point p)
{
    return std::atan2(p.Y, p.X) * 180 / Pi;
}
bool PairSafe(const Problem &p, std::size_t a, Pose pa, std::size_t b, Pose pb, bool &truncated)
{
    if (!AngleAllowed(p.Parts[a], pa.AngleDegrees, p.Tolerance) ||
        !AngleAllowed(p.Parts[b], pb.AngleDegrees, p.Tolerance))
        return false;
    if (ShapesConflict(p.Parts[a].Geometry, pa, p.Parts[b].Geometry, pb, p.PartGap, p.Tolerance, p.AllowCommonEdges))
        return false;
    const auto verdict =
        AxisSafety::Pair(AxisSafety::Make(p.Parts[a].Geometry, pa), AxisSafety::Make(p.Parts[b].Geometry, pb),
                         p.PartGap, p.AllowCommonEdges, p.Tolerance);
    if (verdict == AxisSafety::Verdict::Uncertain)
        truncated = true;
    return verdict == AxisSafety::Verdict::Safe;
}
} // namespace

std::optional<double> MinimumPatternObbArea(const Pattern &pattern, const Problem &problem,
                                            const std::function<bool()> &callback)
{
    Stopper stop{callback};
    std::vector<Point> vertices;
    for (const auto &member : pattern.Members)
    {
        if (stop.Poll() || member.PartIndex >= problem.Parts.size())
            return std::nullopt;
        const auto &part = problem.Parts[member.PartIndex];
        if (part.Geometry.Radius != 0 || part.Geometry.Outer.size() < 3 ||
            part.Geometry.Outer.size() > 65536 - vertices.size() || !std::isfinite(member.Transform.X) ||
            !std::isfinite(member.Transform.Y) || !AngleAllowed(part, member.Transform.AngleDegrees, problem.Tolerance))
            return std::nullopt;
        for (std::size_t index = 0; index < part.Geometry.Outer.size(); ++index)
        {
            if ((index & 63) == 0 && stop.Poll())
                return std::nullopt;
            const auto point = Transform(part.Geometry.Outer[index], member.Transform);
            if (!std::isfinite(point.X) || !std::isfinite(point.Y))
                return std::nullopt;
            vertices.push_back(point);
        }
    }
    const auto hull = Hull(std::move(vertices), stop);
    const auto area = MinimumObb(hull, stop);
    if (stop.Stopped || area <= 0)
        return std::nullopt;
    return area;
}

ShapeFeatureModel EncodeShapeFeatures(const Shape &shape, double tolerance, const ShapeFeatureLimits &limits,
                                      const std::function<bool()> &callback)
{
    ShapeFeatureModel model;
    Stopper stop{callback};
    const auto finish = [&]()
    {
        model.Truncated = model.Truncated || stop.Stopped;
        std::size_t recess = 0, protrusion = 0;
        for (const auto &carrier : model.Carriers)
            for (const auto &feature : carrier.Features)
                feature.Polarity == FeaturePolarity::Recess ? ++recess : ++protrusion;
        std::ostringstream note;
        note << "carriers=" << model.Carriers.size() << " recess=" << recess << " protrusion=" << protrusion;
        if (!model.Diagnostic.empty())
            model.Diagnostic += " | ";
        model.Diagnostic += note.str();
        if (ModelBytes(model) > limits.MaxBytes)
        {
            std::string{}.swap(model.Diagnostic);
            model.Truncated = true;
        }
        if (ModelBytes(model) > limits.MaxBytes)
        {
            std::vector<FeatureCarrier>{}.swap(model.Carriers);
            model.Truncated = true;
        }
        model.RetainedBytes = ModelBytes(model);
        return model;
    };
    if (stop.Poll())
    {
        model.Truncated = true;
        return finish();
    }
    std::size_t vertices = shape.Outer.size();
    for (const auto &hole : shape.Holes)
        vertices += hole.size();
    if (shape.Radius != 0 || vertices > MaxSourceVertices || shape.Holes.size() > 64 || !std::isfinite(tolerance) ||
        tolerance <= 0)
    {
        model.Truncated = vertices > MaxSourceVertices;
        model.Diagnostic = "Unsupported polygon feature scope.";
        return finish();
    }
    std::string error;
    if (!ShapeValid(shape, tolerance, error))
    {
        model.Diagnostic = error;
        return finish();
    }
    model.Supported = true;
    if (!limits.MaxCarriers || limits.MaxBytes < sizeof(FeatureCarrier))
    {
        model.Truncated = true;
        return finish();
    }
    try
    {
        const auto ring = MaterialRing(shape.Outer);
        const auto hull = Hull(ring, stop);
        if (hull.size() < 3)
            return finish();
        const auto frame = MainFrame(ring);
        const auto material = Material(shape);
        std::vector<std::pair<FeatureCarrierKind, Shape>> bases;
        Shape convex;
        convex.Outer = hull;
        bases.emplace_back(FeatureCarrierKind::ConvexHull, std::move(convex));
        bases.emplace_back(FeatureCarrierKind::OuterRectangle, Rectangle(frame, Project(ring, frame, stop)));
        for (const bool vertical : {false, true})
        {
            if (stop.Poll())
                break;
            auto core = CoreStrip(shape, frame, vertical, stop, model.Truncated);
            if (core.Outer.size() >= 3)
                bases.emplace_back(FeatureCarrierKind::CoreRectangle, std::move(core));
        }
        const bool sensitive = PrecisionSensitive(shape);
        if (sensitive)
        {
            model.Truncated = true;
            model.Diagnostic = "Sub-grid source detail requires an unverified feature encoding.";
        }
        for (auto &[kind, geometry] : bases)
        {
            if (stop.Poll())
                break;
            if (model.Carriers.size() >= limits.MaxCarriers)
            {
                model.Truncated = true;
                break;
            }
            FeatureCarrier carrier;
            carrier.Kind = kind;
            carrier.Geometry = std::move(geometry);
            bool incomplete = false;
            const PathsD base{Path(carrier.Geometry.Outer)};
            DifferenceFeatures(base, material, FeaturePolarity::Recess, carrier, tolerance, limits, stop, incomplete);
            DifferenceFeatures(material, base, FeaturePolarity::Protrusion, carrier, tolerance, limits, stop,
                               incomplete);
            if (stop.Poll())
                break;
            auto rebuilt = Difference(base, FeaturePaths(carrier, FeaturePolarity::Recess), FillRule::NonZero, 8);
            if (stop.Poll())
                break;
            auto positive = FeaturePaths(carrier, FeaturePolarity::Protrusion);
            rebuilt.insert(rebuilt.end(), positive.begin(), positive.end());
            rebuilt = Union(rebuilt, FillRule::NonZero, 8);
            if (stop.Poll())
                break;
            carrier.ReconstructionErrorArea = std::abs(Clipper2Lib::Area(Xor(rebuilt, material, FillRule::NonZero, 8)));
            if (stop.Poll())
                break;
            const double areaTolerance = tolerance * std::max(1., std::sqrt(Area(shape))) * 8 + tolerance * tolerance;
            const auto outers =
                std::count_if(rebuilt.begin(), rebuilt.end(), [](const PathD &path) { return IsPositive(path); });
            const auto holes = rebuilt.size() - static_cast<std::size_t>(outers);
            carrier.ReconstructionVerified = !incomplete && !sensitive &&
                                             carrier.ReconstructionErrorArea <= areaTolerance && outers == 1 &&
                                             holes == shape.Holes.size();
            model.Truncated = model.Truncated || incomplete || !carrier.ReconstructionVerified;
            model.Carriers.push_back(std::move(carrier));
            if (ModelBytes(model) > limits.MaxBytes)
            {
                model.Carriers.pop_back();
                model.Carriers.shrink_to_fit();
                model.Truncated = true;
                break;
            }
        }
    }
    catch (const std::exception &error)
    {
        model.Truncated = true;
        model.Diagnostic = error.what();
    }
    catch (...)
    {
        model.Truncated = true;
        model.Diagnostic = "Feature Boolean encoding stopped safely.";
    }
    return finish();
}

ShapeFeatureMatches BuildShapeFeatureMatches(const Problem &problem, const std::vector<ShapeFeatureModel> &models,
                                             const ShapeFeatureLimits &limits, std::size_t &attempts,
                                             const std::function<bool()> &callback,
                                             std::shared_ptr<NfpCache> sharedCache)
{
    ShapeFeatureMatches output;
    Stopper stop{callback};
    if (models.size() != problem.Parts.size() || !AxisSafety::ArithmeticSupported())
    {
        output.Truncated = true;
        return output;
    }
    if (!limits.MaxProposals || !limits.MaxPartnersPerFeature || attempts >= limits.MaxAttempts)
    {
        output.Truncated = true;
        return output;
    }
    const auto minimumMatchBytes = sizeof(FeatureMatch) + 2 * sizeof(Member);
    if (limits.MaxBytes < minimumMatchBytes || stop.Poll())
    {
        output.Truncated = true;
        return output;
    }
    const bool contactFrontier = limits.MatchMode == ShapeFeatureMatchMode::ContactFrontier;
    if (contactFrontier && !sharedCache)
        sharedCache = std::make_shared<NfpCache>();
    std::vector<NfpPreparedShape> nfpPrepared(contactFrontier ? problem.Parts.size() : 0);
    NfpBuildOptions nfpBuild;
    nfpBuild.TimeLimitSeconds = 1;
    nfpBuild.Cancel = [&] { return stop.Poll(); };
    std::vector<PartInfo> parts(problem.Parts.size());
    std::map<int, std::map<std::size_t, std::vector<FeatureRef>>> index;
    std::vector<std::vector<FeatureRef>> recesses(problem.Parts.size());
    std::size_t entries = 0;
    const auto mouth = [&](FeatureRef ref) -> const FeatureMouth &
    { return models[ref.Part].Carriers[ref.Carrier].Features[ref.Feature].Mouths[ref.Mouth]; };
    const auto bin = [](double width) { return static_cast<int>(std::floor(std::log2(std::max(width, 1e-12)))); };
    const auto indexLimit =
        std::min(MaxIndexEntries, limits.MaxBytes / std::max<std::size_t>(1, sizeof(FeatureRef) + 128));
    for (std::size_t part = 0; part < problem.Parts.size() && !stop.Poll(); ++part)
    {
        // A preassembled feature pair may only repeat this exact original
        // specification. A singleton still enters ordinary board placement.
        if (problem.Parts[part].Quantity < 2)
            continue;
        output.Truncated = output.Truncated || models[part].Truncated || !models[part].Supported;
        if (!models[part].Supported)
            continue;
        if (contactFrontier)
            nfpPrepared[part] = sharedCache->Prepare(problem.Parts[part].Geometry, nfpBuild);
        auto &info = parts[part];
        info.Hull = Hull(MaterialRing(problem.Parts[part].Geometry.Outer), stop);
        info.SoloObb = MinimumObb(info.Hull, stop);
        info.Material = Area(problem.Parts[part].Geometry);
        const auto ring = MaterialRing(problem.Parts[part].Geometry.Outer);
        for (std::size_t i = 0; i < ring.size(); ++i)
        {
            const auto a = ring[i], b = ring[(i + 1) % ring.size()];
            const auto length = Length(Subtract(b, a));
            if (length > 0)
                info.Edges.push_back({a, b, length});
        }
        std::stable_sort(info.Edges.begin(), info.Edges.end(),
                         [](const Edge &a, const Edge &b) { return a.Length > b.Length; });
        if (info.Edges.size() > 8)
            info.Edges.resize(8);
        for (std::size_t carrier = 0; carrier < models[part].Carriers.size(); ++carrier)
            for (std::size_t feature = 0; feature < models[part].Carriers[carrier].Features.size(); ++feature)
            {
                const auto &encoded = models[part].Carriers[carrier].Features[feature];
                for (std::size_t opening = 0; opening < encoded.Mouths.size(); ++opening)
                {
                    if (entries >= indexLimit)
                    {
                        output.Truncated = true;
                        break;
                    }
                    const FeatureRef ref{part, carrier, feature, opening};
                    ++entries;
                    if (encoded.Polarity == FeaturePolarity::Recess)
                        recesses[part].push_back(ref);
                    else
                        index[bin(mouth(ref).Width)][part].push_back(ref);
                }
            }
    }
    std::vector<FeatureRef> pockets;
    for (std::size_t ordinal = 0;; ++ordinal)
    {
        bool any = false;
        for (const auto &refs : recesses)
            if (ordinal < refs.size())
            {
                pockets.push_back(refs[ordinal]);
                any = true;
            }
        if (!any)
            break;
    }
    using PoseKey = std::tuple<std::size_t, std::size_t, double, double, double, double>;
    std::set<PoseKey> seen;
    std::vector<FeatureMatch> alternatives;
    const auto alternativeLimit = std::min(limits.MaxProposals, limits.MaxBytes / minimumMatchBytes);
    const auto retainBytes = [&]
    {
        std::size_t bytes = output.Matches.capacity() * sizeof(FeatureMatch);
        for (const auto &match : output.Matches)
            bytes += match.Value.Members.capacity() * sizeof(Member);
        return bytes;
    };
    const auto maxPartnerRounds = std::min(limits.MaxPartnersPerFeature, std::size_t{1});
    const auto better = [](const FeatureMatch &a, const FeatureMatch &b)
    { return std::tie(a.FootprintSaving, a.CarrierFill) > std::tie(b.FootprintSaving, b.CarrierFill); };
    const auto footprintDominates = [&](const FeatureMatch &first, const FeatureMatch &second)
    {
        const double areaTolerance = problem.Tolerance * std::max({1., first.Value.Width, first.Value.Height,
                                                                   second.Value.Width, second.Value.Height});
        const bool noWorse = first.FootprintSaving >= second.FootprintSaving - areaTolerance &&
                             first.Value.Width <= second.Value.Width + problem.Tolerance &&
                             first.Value.Height <= second.Value.Height + problem.Tolerance;
        const bool improves = first.FootprintSaving > second.FootprintSaving + areaTolerance ||
                              first.Value.Width < second.Value.Width - problem.Tolerance ||
                              first.Value.Height < second.Value.Height - problem.Tolerance;
        return noWorse && improves;
    };
    const auto samePose = [&](const FeatureMatch &first, const FeatureMatch &second)
    {
        const auto key = [](const Pattern &pattern)
        {
            std::vector<std::tuple<std::size_t, double, double, double>> values;
            for (const auto &member : pattern.Members)
                values.emplace_back(member.PartIndex, member.Transform.AngleDegrees, member.Transform.X,
                                    member.Transform.Y);
            std::sort(values.begin(), values.end());
            return values;
        };
        const auto left = key(first.Value), right = key(second.Value);
        for (std::size_t i = 0; i < left.size(); ++i)
            if (std::get<0>(left[i]) != std::get<0>(right[i]) ||
                std::abs(std::get<1>(left[i]) - std::get<1>(right[i])) > problem.Tolerance ||
                std::abs(std::get<2>(left[i]) - std::get<2>(right[i])) > problem.Tolerance ||
                std::abs(std::get<3>(left[i]) - std::get<3>(right[i])) > problem.Tolerance)
                return false;
        return true;
    };
    const auto retainCandidate = [&](std::vector<FeatureMatch> &pool, FeatureMatch candidate)
    {
        for (auto &existing : pool)
        {
            if (samePose(existing, candidate))
            {
                if (better(candidate, existing))
                    existing = std::move(candidate);
                return;
            }
            if (footprintDominates(existing, candidate))
                return;
        }
        pool.erase(std::remove_if(pool.begin(), pool.end(),
                                  [&](const auto &old) { return footprintDominates(candidate, old); }),
                   pool.end());
        pool.push_back(std::move(candidate));
        const auto capacity = std::min<std::size_t>(8, limits.MaxProposals);
        if (pool.size() <= capacity)
            return;
        std::set<std::size_t> protectedIndices;
        const auto protect = [&](const auto &compare)
        {
            const auto best = std::min_element(pool.begin(), pool.end(), compare);
            protectedIndices.insert(static_cast<std::size_t>(best - pool.begin()));
        };
        protect(better);
        protect([](const auto &x, const auto &y) { return x.Value.Width < y.Value.Width; });
        protect([](const auto &x, const auto &y) { return x.Value.Height < y.Value.Height; });
        protect([](const auto &x, const auto &y)
                { return x.Value.Width * x.Value.Height < y.Value.Width * y.Value.Height; });
        std::size_t remove = pool.size();
        for (std::size_t i = 0; i < pool.size(); ++i)
            if (!protectedIndices.contains(i) && (remove == pool.size() || better(pool[remove], pool[i])))
                remove = i;
        if (remove == pool.size())
            remove = static_cast<std::size_t>(std::max_element(pool.begin(), pool.end(), better) - pool.begin());
        pool.erase(pool.begin() + remove);
        output.Truncated = true;
    };
    std::map<std::pair<std::size_t, std::size_t>, std::vector<FeatureMatch>> pairPools;
    const auto pairLimit =
        std::min(limits.MaxProposals, std::max<std::size_t>(1, limits.MaxBytes / (8 * minimumMatchBytes)));
    for (std::size_t partnerRound = 0; partnerRound < maxPartnerRounds && !stop.Poll(); ++partnerRound)
        for (const auto pocket : pockets)
        {
            if (stop.Poll() || attempts >= limits.MaxAttempts ||
                (!contactFrontier && output.Matches.size() >= limits.MaxProposals))
            {
                output.Truncated = true;
                break;
            }
            std::vector<FeatureRef> partners;
            const auto target = bin(mouth(pocket).Width);
            for (int offset = 0; offset < 8 && partners.size() < limits.MaxPartnersPerFeature; ++offset)
            {
                const auto found = index.find(target - offset);
                if (found == index.end())
                    continue;
                // Resolve the same original index before proposing geometry;
                // matching another specification is not a self combination.
                const auto own = found->second.find(pocket.Part);
                if (own == found->second.end())
                    continue;
                const auto &refs = own->second;
                const auto best = std::min_element(refs.begin(), refs.end(),
                                                   [&](auto a, auto b)
                {
                    return std::abs(mouth(a).Width - mouth(pocket).Width) <
                           std::abs(mouth(b).Width - mouth(pocket).Width);
                });
                if (best != refs.end())
                {
                    partners.push_back(*best);
                    break;
                }
            }
            if (partnerRound >= partners.size())
                continue;
            const auto tooth = partners[partnerRound];
            const auto a = pocket.Part, b = tooth.Part;
            std::vector<FeatureMatch> candidates;
            const auto localLimit = std::min<std::size_t>(8, limits.MaxProposals);
            const auto end = attempts + std::min<std::size_t>(128, limits.MaxAttempts - attempts);
            const auto trial = [&](Pose pa, Pose pb)
            {
                if (stop.Poll() || attempts >= end)
                    return;
                ++attempts;
                if (!PairSafe(problem, a, pa, b, pb, output.Truncated))
                    return;
                Pattern pattern;
                pattern.Members = {{a, pa}, {b, pb}};
                NormalizePattern(pattern, problem);
                if (!PairSafe(problem, a, pattern.Members[0].Transform, b, pattern.Members[1].Transform,
                              output.Truncated))
                    return;
                std::vector<Point> vertices;
                for (const auto &member : pattern.Members)
                    for (const auto p : parts[member.PartIndex].Hull)
                        vertices.push_back(Transform(p, member.Transform));
                const auto pairHull = Hull(std::move(vertices), stop);
                const double pairObb = MinimumObb(pairHull, stop);
                if (pairObb <= 0)
                    return;
                // The common absolute-area measure uses exact source hulls only
                // to solve each minimum enclosing oriented rectangle.
                const double saving = parts[a].SoloObb + parts[b].SoloObb - pairObb;
                if (!contactFrontier && saving < -problem.Tolerance * std::max(1., std::sqrt(pairObb)))
                    return;
                const double fill = (parts[a].Material + parts[b].Material) / pairObb;
                FeatureMatch candidate;
                candidate.Value = std::move(pattern);
                candidate.FootprintSaving = saving;
                candidate.CarrierFill = fill;
                candidate.FirstCarrier = pocket.Carrier;
                candidate.FirstFeature = pocket.Feature;
                candidate.SecondCarrier = tooth.Carrier;
                candidate.SecondFeature = tooth.Feature;
                if (contactFrontier)
                    retainCandidate(candidates, std::move(candidate));
                else
                {
                    const auto sameEnvelope = [&](const FeatureMatch &existing)
                    {
                        if (std::abs(existing.Value.Width - candidate.Value.Width) > problem.Tolerance ||
                            std::abs(existing.Value.Height - candidate.Value.Height) > problem.Tolerance)
                            return false;
                        for (std::size_t i = 0; i < 2; ++i)
                        {
                            const double angle = NormalizeAngle(existing.Value.Members[i].Transform.AngleDegrees -
                                                                candidate.Value.Members[i].Transform.AngleDegrees);
                            if (std::min(angle, 360 - angle) > problem.Tolerance)
                                return false;
                        }
                        return true;
                    };
                    const auto existing = std::find_if(candidates.begin(), candidates.end(), sameEnvelope);
                    if (existing != candidates.end())
                    {
                        if (better(candidate, *existing))
                            *existing = std::move(candidate);
                        return;
                    }
                    if (candidates.size() < localLimit)
                        candidates.push_back(std::move(candidate));
                    else
                    {
                        const auto worst = std::max_element(candidates.begin(), candidates.end(), better);
                        if (better(candidate, *worst))
                            *worst = std::move(candidate);
                        output.Truncated = true;
                    }
                }
            };
            auto anglesA = CandidateAngles(problem.Parts[a], problem.Tolerance),
                 anglesB = CandidateAngles(problem.Parts[b], problem.Tolerance);
            const auto &ma = mouth(pocket);
            const auto &mb = mouth(tooth);
            const auto addAngle = [](std::vector<double> &angles, double value)
            {
                value = NormalizeAngle(value);
                if (std::find(angles.begin(), angles.end(), value) == angles.end())
                    angles.push_back(value);
            };
            if (problem.Parts[a].FreeRotation)
                for (const auto angle : anglesB)
                    for (const double flip : {0., 180.})
                        addAngle(anglesA, angle + Direction(mb.Into) - Direction(ma.Into) + flip);
            const auto edgeEnd = attempts + std::min<std::size_t>(contactFrontier ? 32 : 48, end - attempts);
            // Cover legal global orientations for each boundary-edge pair
            // before moving on. One angle must not exhaust the local budget.
            for (const auto &ea : parts[a].Edges)
            {
                if (stop.Poll() || attempts >= edgeEnd)
                    break;
                for (const auto &eb : parts[b].Edges)
                    for (const auto aa : anglesA)
                    {
                        if (stop.Poll() || attempts >= edgeEnd)
                            break;
                        auto bs = anglesB;
                        if (problem.Parts[b].FreeRotation)
                            addAngle(bs, aa + Direction(Subtract(ea.End, ea.Start)) -
                                             Direction(Subtract(eb.End, eb.Start)) + 180);
                        for (const auto ba : bs)
                        {
                            if (attempts >= edgeEnd || stop.Poll())
                                break;
                            const auto x = Transform(ea.Start, {0, 0, aa}), y = Transform(eb.End, {0, 0, ba});
                            const auto shift = Subtract(x, y);
                            trial({0, 0, aa}, {shift.X, shift.Y, ba});
                        }
                    }
            }
            if (contactFrontier)
            {
                struct ContactEvents
                {
                    double FirstAngle = 0, SecondAngle = 0;
                    std::vector<Point> Translations;
                };
                std::vector<ContactEvents> contactEvents;
                if (nfpPrepared[a] && nfpPrepared[b])
                    for (const auto aa : anglesA)
                    {
                        auto secondAngles = anglesB;
                        if (problem.Parts[b].FreeRotation)
                            for (const double flip : {0., 180.})
                                addAngle(secondAngles, aa + Direction(ma.Into) - Direction(mb.Into) + flip);
                        for (const auto ba : secondAngles)
                        {
                            if (stop.Poll() || attempts >= end || contactEvents.size() >= 8)
                                break;
                            const auto lookup = sharedCache->Get(nfpPrepared[a], aa, nfpPrepared[b], ba, nfpBuild);
                            if (lookup.State != NfpStatus::Ready)
                            {
                                output.Truncated = true;
                                continue;
                            }
                            ContactEvents events;
                            events.FirstAngle = aa;
                            events.SecondAngle = ba;
                            for (const auto &loop : lookup.Region->Loops)
                            {
                                for (const auto vertex : loop)
                                {
                                    if (events.Translations.size() >= 64)
                                    {
                                        output.Truncated = true;
                                        break;
                                    }
                                    events.Translations.push_back(Transform(vertex, lookup.Transform));
                                }
                                if (events.Translations.size() >= 64)
                                    break;
                            }
                            const auto fixed = Bounds(problem.Parts[a].Geometry, {0, 0, aa});
                            const auto areaAt = [&](Point translation)
                            {
                                const auto moving =
                                    Bounds(problem.Parts[b].Geometry, {translation.X, translation.Y, ba});
                                const double width = std::max(fixed.X + fixed.Width, moving.X + moving.Width) -
                                                     std::min(fixed.X, moving.X);
                                const double height = std::max(fixed.Y + fixed.Height, moving.Y + moving.Height) -
                                                      std::min(fixed.Y, moving.Y);
                                return width * height;
                            };
                            std::stable_sort(events.Translations.begin(), events.Translations.end(),
                                             [&](Point l, Point r) { return areaAt(l) < areaAt(r); });
                            contactEvents.push_back(std::move(events));
                        }
                        if (contactEvents.size() >= 8)
                            break;
                    }
                const auto contactEnd = attempts + std::min<std::size_t>(32, end - attempts);
                for (std::size_t ordinal = 0; attempts < contactEnd && !stop.Poll(); ++ordinal)
                {
                    bool any = false;
                    for (const auto &events : contactEvents)
                    {
                        if (attempts >= contactEnd || stop.Poll())
                            break;
                        if (ordinal >= events.Translations.size())
                            continue;
                        any = true;
                        const auto point = events.Translations[ordinal];
                        trial({0, 0, events.FirstAngle}, {point.X, point.Y, events.SecondAngle});
                    }
                    if (!any)
                        break;
                }
            }
            for (const auto aa : anglesA)
            {
                if (stop.Poll() || attempts >= end)
                    break;
                auto bs = anglesB;
                if (problem.Parts[b].FreeRotation)
                    for (const double flip : {0., 180.})
                        addAngle(bs, aa + Direction(ma.Into) - Direction(mb.Into) + flip);
                for (const auto ba : bs)
                {
                    if (stop.Poll() || attempts >= end)
                        break;
                    const auto as = Transform(ma.Start, {0, 0, aa}), ae = Transform(ma.End, {0, 0, aa});
                    const auto bs0 = Transform(mb.Start, {0, 0, ba}), be = Transform(mb.End, {0, 0, ba});
                    const auto into = Transform(ma.Into, {0, 0, aa});
                    const double sourceScale =
                        std::max({1., std::abs(as.X), std::abs(as.Y), std::abs(ae.X), std::abs(ae.Y), std::abs(bs0.X),
                                  std::abs(bs0.Y), std::abs(be.X), std::abs(be.Y), ma.Depth, mb.Depth});
                    // The residual Boolean uses decimal precision 8. Bound its
                    // endpoint/projection error together with source-scale ULPs.
                    const double sourceError = 4e-8 + 64 * std::numeric_limits<double>::epsilon() * sourceScale;
                    for (const double lateral : {0., .5, 1.})
                        for (const double depth : {0., std::max(0., ma.Depth - mb.Depth)})
                        {
                            const auto anchor = Add(as, Scale(Subtract(ae, as), lateral));
                            const auto moving = Add(bs0, Scale(Subtract(be, bs0), lateral));
                            const auto shift = Add(Subtract(anchor, moving), Scale(into, depth));
                            trial({0, 0, aa}, {shift.X, shift.Y, ba});
                            if (depth <= 0 || stop.Poll() || attempts >= end)
                                continue;
                            double openingClearance = std::numeric_limits<double>::infinity();
                            for (const auto point : parts[b].Hull)
                            {
                                const auto relative = Subtract(Transform(point, {0, 0, ba}), moving);
                                openingClearance = std::min(openingClearance, depth + Dot(relative, into));
                            }
                            // Keep exact contact above. An interior alternative
                            // spends only existing original-material clearance.
                            const double retreat = std::min(sourceError, openingClearance * .5);
                            if (retreat > 0)
                            {
                                const auto interior = Subtract(shift, Scale(into, retreat));
                                trial({0, 0, aa}, {interior.X, interior.Y, ba});
                            }
                        }
                }
            }
            if (attempts >= end && end < limits.MaxAttempts)
                output.Truncated = true;
            if (contactFrontier)
            {
                const std::pair<std::size_t, std::size_t> pair{std::min(a, b), std::max(a, b)};
                auto found = pairPools.find(pair);
                if (found == pairPools.end())
                {
                    if (pairPools.size() >= pairLimit)
                    {
                        output.Truncated = true;
                        continue;
                    }
                    found = pairPools.emplace(pair, std::vector<FeatureMatch>{}).first;
                }
                for (auto &candidate : candidates)
                    retainCandidate(found->second, std::move(candidate));
            }
            else
            {
                std::stable_sort(candidates.begin(), candidates.end(), better);
                bool firstCandidate = true;
                for (auto &candidate : candidates)
                {
                    const bool primary = firstCandidate;
                    firstCandidate = false;
                    if (output.Matches.size() >= limits.MaxProposals)
                    {
                        output.Truncated = true;
                        break;
                    }
                    const auto &first = candidate.Value.Members[0];
                    const auto &second = candidate.Value.Members[1];
                    const PoseKey key{a,
                                      b,
                                      first.Transform.AngleDegrees,
                                      second.Transform.AngleDegrees,
                                      second.Transform.X - first.Transform.X,
                                      second.Transform.Y - first.Transform.Y};
                    if (!seen.insert(key).second)
                        continue;
                    // Visit other original-part pairs before consuming the output
                    // budget with global-orientation alternatives of this pair.
                    if (!primary)
                    {
                        if (alternatives.size() < alternativeLimit)
                            alternatives.push_back(std::move(candidate));
                        else
                            output.Truncated = true;
                        continue;
                    }
                    output.Matches.push_back(std::move(candidate));
                    if (retainBytes() > limits.MaxBytes)
                    {
                        output.Matches.pop_back();
                        output.Matches.shrink_to_fit();
                        output.Truncated = true;
                        break;
                    }
                }
            }
        }
    if (contactFrontier)
    {
        for (auto &[pair, pool] : pairPools)
            std::stable_sort(pool.begin(), pool.end(), better);
        bool full = false;
        for (std::size_t ordinal = 0; ordinal < 8 && !full; ++ordinal)
            for (auto &[pair, pool] : pairPools)
            {
                if (ordinal >= pool.size())
                    continue;
                if (output.Matches.size() >= limits.MaxProposals)
                {
                    output.Truncated = true;
                    full = true;
                    break;
                }
                output.Matches.push_back(std::move(pool[ordinal]));
                if (retainBytes() > limits.MaxBytes)
                {
                    output.Matches.pop_back();
                    output.Matches.shrink_to_fit();
                    output.Truncated = true;
                    full = true;
                    break;
                }
            }
    }
    else
    {
        for (auto &candidate : alternatives)
        {
            if (output.Matches.size() >= limits.MaxProposals)
            {
                output.Truncated = true;
                break;
            }
            output.Matches.push_back(std::move(candidate));
            if (retainBytes() > limits.MaxBytes)
            {
                output.Matches.pop_back();
                output.Matches.shrink_to_fit();
                output.Truncated = true;
                break;
            }
        }
    }
    std::stable_sort(output.Matches.begin(), output.Matches.end(), better);
    output.Truncated = output.Truncated || stop.Stopped || attempts >= limits.MaxAttempts;
    if (retainBytes() > limits.MaxBytes)
    {
        std::vector<FeatureMatch>{}.swap(output.Matches);
        output.Truncated = true;
    }
    output.RetainedBytes = retainBytes();
    return output;
}
} // namespace iCAX::SheetNesting::Detail
