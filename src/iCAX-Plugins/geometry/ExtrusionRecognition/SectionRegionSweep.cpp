#include "pch.h"
#include "SectionRegionPartitionDetail.h"

#include <numbers>

namespace iCAX::ExtrusionRecognition::section_partition_detail {
namespace {

constexpr double FullTurn = 2.0 * std::numbers::pi;
constexpr double AngularTolerance = 1.e-5;
constexpr double ParameterTolerance = 1.e-8;

Point operator+(Point a, Point b) { return {a[0] + b[0], a[1] + b[1]}; }
Point operator-(Point a, Point b) { return {a[0] - b[0], a[1] - b[1]}; }
Point operator*(Point p, double k) { return {p[0] * k, p[1] * k}; }
double Dot(Point a, Point b) { return a[0] * b[0] + a[1] * b[1]; }
double Cross(Point a, Point b) { return a[0] * b[1] - a[1] * b[0]; }
double Length(Point p) { return std::hypot(p[0], p[1]); }
double Distance(Point a, Point b) { return Length(a - b); }
double Sign(double value) { return (value > 0) - (value < 0); }

Point Position(const Curve& curve, double parameter)
{
    if (!curve.IsArc)
        return curve.Start + (curve.End - curve.Start) * parameter;
    const double angle = curve.StartAngle + curve.Sweep * parameter;
    return curve.Center + Point{std::cos(angle), std::sin(angle)} * curve.Radius;
}

Point Tangent(const Curve& curve, double parameter)
{
    if (!curve.IsArc)
        return (curve.End - curve.Start) * (1 / Distance(curve.Start, curve.End));
    const double angle = curve.StartAngle + curve.Sweep * parameter;
    return Point{-std::sin(angle), std::cos(angle)} * Sign(curve.Sweep);
}

struct Edge
{
    const Curve* Shape;
    bool Inner;
    double MaterialSide;
};

Point Inward(const Edge& edge, double parameter)
{
    const Point tangent = Tangent(*edge.Shape, parameter);
    return Point{-tangent[1], tangent[0]} * edge.MaterialSide;
}

double CurveLength(const Curve& curve)
{
    return curve.IsArc ? curve.Radius * std::abs(curve.Sweep) : Distance(curve.Start, curve.End);
}

double SignedArea(const Ring& ring)
{
    const Point origin = Position(ring.Curves.front(), 0);
    double area = 0;
    for (const Curve& curve : ring.Curves) {
        const Point start = Position(curve, 0) - origin;
        const Point end = Position(curve, 1) - origin;
        area += curve.IsArc ? Cross(curve.Center - origin, end - start) +
            curve.Radius * curve.Radius * curve.Sweep : Cross(start, end);
    }
    return area / 2;
}

struct Model
{
    std::vector<Edge> Edges;
    std::size_t OuterCount = 0;
    bool HasInner = false;
    double Tolerance = 0;
};

std::optional<Model> Prepare(const std::vector<Ring>& rings, double tolerance)
{
    Model model;
    model.Tolerance = tolerance;
    if (!std::isfinite(tolerance) || tolerance <= 0 || rings.empty())
        return std::nullopt;
    const auto outer = std::find_if(rings.begin(), rings.end(), [](const Ring& ring) { return !ring.IsInner; });
    if (outer == rings.end() || std::count_if(rings.begin(), rings.end(),
            [](const Ring& ring) { return !ring.IsInner; }) != 1)
        return std::nullopt;

    const auto append = [&](const Ring& ring) {
        if (ring.Curves.empty())
            return false;
        const double area = SignedArea(ring);
        if (!std::isfinite(area) || std::abs(area) <= tolerance * tolerance)
            return false;
        const double side = Sign(area) * (ring.IsInner ? -1 : 1);
        for (std::size_t i = 0; i < ring.Curves.size(); ++i) {
            const Curve& curve = ring.Curves[i];
            const Curve& next = ring.Curves[(i + 1) % ring.Curves.size()];
            if (CurveLength(curve) <= tolerance || Distance(Position(curve, 1), Position(next, 0)) > tolerance)
                return false;
            model.Edges.push_back({&curve, ring.IsInner, side});
        }
        return true;
    };
    if (!append(*outer))
        return std::nullopt;
    model.OuterCount = model.Edges.size();
    for (const Ring& ring : rings) {
        if (ring.IsInner) {
            if (!append(ring))
                return std::nullopt;
            model.HasInner = true;
        }
    }
    return model;
}

std::optional<double> ArcParameter(const Curve& curve, double angle, double tolerance = ParameterTolerance)
{
    double delta = std::fmod((angle - curve.StartAngle) * Sign(curve.Sweep), FullTurn);
    if (delta < 0)
        delta += FullTurn;
    const double parameter = (delta > FullTurn - tolerance ? 0 : delta) / std::abs(curve.Sweep);
    if (parameter > 1 + tolerance)
        return std::nullopt;
    return std::clamp(parameter, 0.0, 1.0);
}

struct Hit
{
    const Edge* Target;
    double Distance;
    double Parameter;
    Point Position;
    double Outward;
};

template<class F>
void VisitIntersections(const Model& model, const Edge& edge, Point point, Point direction, F&& visit)
{
    const Curve& curve = *edge.Shape;
    const auto addHit = [&](double distance, double parameter) {
        visit(Hit{&edge, distance, parameter, point + direction * distance, -Dot(direction, Inward(edge, parameter))});
    };
    if (!curve.IsArc) {
        const Point along = curve.End - curve.Start;
        const double segmentLength = Length(along);
        const double determinant = Cross(direction, along);
        if (std::abs(determinant) <= segmentLength * 1.e-12)
            return;
        const Point offset = curve.Start - point;
        const double parameter = Cross(offset, direction) / determinant;
        const double slack = model.Tolerance / segmentLength;
        if (parameter >= -slack && parameter <= 1 + slack)
            addHit(Cross(offset, along) / determinant, std::clamp(parameter, 0.0, 1.0));
        return;
    }
    const Point offset = point - curve.Center;
    const double projected = Dot(offset, direction);
    const double normal = Cross(offset, direction);
    const double discriminant = curve.Radius * curve.Radius - normal * normal;
    if (discriminant < -curve.Radius * model.Tolerance)
        return;
    const double root = std::sqrt(std::max(0.0, discriminant));
    for (double distance : {-projected - root, -projected + root}) {
        const Point radial = offset + direction * distance;
        const auto parameter = ArcParameter(curve, std::atan2(radial[1], radial[0]),
            model.Tolerance / CurveLength(curve));
        if (parameter)
            addHit(distance, *parameter);
    }
}

std::optional<Hit> FirstExit(const Model& model, Point point, Point inward)
{
    std::vector<Hit> hits;
    for (const Edge& edge : model.Edges)
        VisitIntersections(model, edge, point, inward, [&](const Hit& hit) {
            if (hit.Distance > model.Tolerance)
                hits.push_back(hit);
        });
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.Distance < b.Distance; });
    for (auto begin = hits.begin(); begin != hits.end();) {
        const auto end = std::find_if(begin + 1, hits.end(),
            [&](const Hit& hit) { return hit.Distance - begin->Distance > model.Tolerance; });
        const auto exit = std::find_if(begin, end, [](const Hit& hit) { return hit.Outward > 1.e-9; });
        if (exit != end && std::none_of(begin, end, [](const Hit& hit) { return hit.Outward < -1.e-9; }))
            return *exit;
        begin = end;
    }
    return std::nullopt;
}

// The first-exit branch can change when an endpoint or tangency meets the ray.
// A witness between these analytic events identifies the branch for the interval.
std::vector<double> EventParameters(const Model& model, const Curve& guide)
{
    std::vector<double> events{0, 1};
    const auto add = [&](std::optional<double> parameter) {
        if (parameter && *parameter > ParameterTolerance && *parameter < 1 - ParameterTolerance)
            events.push_back(*parameter);
    };
    if (!guide.IsArc) {
        const Point axis = guide.End - guide.Start;
        const double normSquared = Dot(axis, axis);
        const Point axisUnit = axis * (1 / std::sqrt(normSquared));
        const auto project = [&](Point point) { add(Dot(point - guide.Start, axis) / normSquared); };
        for (const Edge& edge : model.Edges) {
            const Curve& curve = *edge.Shape;
            project(Position(curve, 0));
            project(Position(curve, 1));
            if (curve.IsArc) {
                for (double sign : {-1.0, 1.0}) {
                    const Point radial = axisUnit * sign;
                    if (ArcParameter(curve, std::atan2(radial[1], radial[0])))
                        project(curve.Center + radial * curve.Radius);
                }
            }
        }
    } else {
        const auto addDirection = [&](double angle) {
            add(ArcParameter(guide, angle));
            add(ArcParameter(guide, angle + std::numbers::pi));
        };
        for (const Edge& edge : model.Edges) {
            const Curve& curve = *edge.Shape;
            for (double end : {0.0, 1.0}) {
                const Point radial = Position(curve, end) - guide.Center;
                if (Length(radial) > model.Tolerance)
                    addDirection(std::atan2(radial[1], radial[0]));
            }
            if (!curve.IsArc) {
                const Point axis = curve.End - curve.Start;
                addDirection(std::atan2(axis[1], axis[0]));
            } else {
                const Point radial = curve.Center - guide.Center;
                const double distance = Length(radial);
                if (distance > curve.Radius) {
                    const double angle = std::atan2(radial[1], radial[0]);
                    const double offset = std::asin(curve.Radius / distance);
                    addDirection(angle - offset);
                    addDirection(angle + offset);
                }
            }
        }
    }
    std::sort(events.begin(), events.end());
    std::vector<double> distinct{events.front()};
    for (std::size_t i = 1; i < events.size(); ++i)
        if (events[i] - events[i - 1] > ParameterTolerance)
            distinct.push_back(events[i]);
    return distinct;
}

std::optional<Hit> LimitHit(const Model& model, const Edge& guide, double parameter, const Hit& witness)
{
    std::optional<Hit> nearest;
    VisitIntersections(model, *witness.Target, Position(*guide.Shape, parameter), Inward(guide, parameter),
        [&](const Hit& hit) {
            if (hit.Distance > model.Tolerance && hit.Outward >= -1.e-8 &&
                (!nearest || std::abs(hit.Parameter - witness.Parameter) <
                    std::abs(nearest->Parameter - witness.Parameter)))
                nearest = hit;
        });
    return nearest;
}

struct SweepSpan
{
    const Edge* Guide;
    const Edge* Target;
    double First;
    double Last;
    std::optional<Hit> Start;
    std::optional<Hit> End;
};

using GuideSpans = std::vector<SweepSpan>;

GuideSpans Trace(const Model& model, const Edge& guide)
{
    GuideSpans spans;
    const auto events = EventParameters(model, *guide.Shape);
    for (std::size_t i = 1; i < events.size(); ++i) {
        const double first = events[i - 1], last = events[i], middle = (first + last) / 2;
        const auto hit = FirstExit(model, Position(*guide.Shape, middle), Inward(guide, middle));
        const auto start = hit ? LimitHit(model, guide, first, *hit) : std::nullopt;
        const auto end = hit ? LimitHit(model, guide, last, *hit) : std::nullopt;
        const Edge* target = hit ? hit->Target : nullptr;
        if (!spans.empty() && spans.back().Target == target &&
            (!target || (spans.back().End && start &&
                Distance(spans.back().End->Position, start->Position) <= 2 * model.Tolerance))) {
            spans.back().Last = last;
            spans.back().End = end;
        } else {
            spans.push_back({&guide, target, first, last, start, end});
        }
    }
    return spans;
}

bool IsContinuous(const Model& model, const SweepSpan& span)
{
    return span.Target && span.Start && span.End && (!model.HasInner || span.Target->Inner);
}

bool Joins(const Model& model, const SweepSpan& prior, const SweepSpan& next)
{
    if (!IsContinuous(model, prior) || !IsContinuous(model, next))
        return false;
    const double tolerance = model.Tolerance * 3;
    if (Distance(Position(*prior.Guide->Shape, prior.Last), Position(*next.Guide->Shape, next.First)) > tolerance ||
        Distance(prior.End->Position, next.Start->Position) > tolerance)
        return false;
    const double firstDirection = Sign(prior.End->Parameter - prior.Start->Parameter);
    const double secondDirection = Sign(next.End->Parameter - next.Start->Parameter);
    return Dot(Tangent(*prior.Guide->Shape, prior.Last), Tangent(*next.Guide->Shape, next.First)) > 1 - AngularTolerance &&
        Dot(Tangent(*prior.Target->Shape, prior.End->Parameter) * firstDirection,
            Tangent(*next.Target->Shape, next.Start->Parameter) * secondDirection) > 1 - AngularTolerance;
}

bool Collinear(const Model& model, const Edge& a, const Edge& b)
{
    if (a.Shape->IsArc || b.Shape->IsArc)
        return false;
    const Point tangent = Tangent(*a.Shape, 0);
    return std::abs(Cross(tangent, Tangent(*b.Shape, 0))) < AngularTolerance &&
        std::abs(Cross(tangent, b.Shape->Start - a.Shape->Start)) <= 3 * model.Tolerance;
}

struct LineHit
{
    Point PointOnSupport;
    double Parameter;
};

std::optional<LineHit> IntersectSupport(Point point, Point direction, const Edge& edge)
{
    const Point along = edge.Shape->End - edge.Shape->Start;
    const double determinant = Cross(direction, along);
    if (std::abs(determinant) <= Length(along) * 1.e-12)
        return std::nullopt;
    const Point offset = edge.Shape->Start - point;
    return LineHit{point + direction * (Cross(offset, along) / determinant),
        Cross(offset, direction) / determinant};
}

bool IsOnSupport(const Model& model, const Edge& edge, const std::optional<LineHit>& hit)
{
    const double slack = model.Tolerance * 3 / CurveLength(*edge.Shape);
    return hit && hit->Parameter >= -slack && hit->Parameter <= 1 + slack;
}

bool CoversInner(const Model& model, const std::vector<const SweepSpan*>& spans)
{
    for (std::size_t i = model.OuterCount; i < model.Edges.size(); ++i) {
        const Edge& edge = model.Edges[i];
        std::vector<std::pair<double, double>> intervals;
        for (const SweepSpan* span : spans) {
            if (span->Target == &edge && IsContinuous(model, *span))
                intervals.emplace_back(std::min(span->Start->Parameter, span->End->Parameter),
                    std::max(span->Start->Parameter, span->End->Parameter));
        }
        std::sort(intervals.begin(), intervals.end());
        double cursor = 0;
        const double slack = 3 * model.Tolerance / CurveLength(*edge.Shape);
        for (const auto& [start, end] : intervals) {
            if (std::abs(start - cursor) > slack || end <= start)
                return false;
            cursor = end;
        }
        if (1 - cursor > slack)
            return false;
    }
    return true;
}

std::vector<std::size_t> RoundedWallGuides(const Model& model)
{
    std::vector<std::size_t> guides;
    for (std::size_t i = 0; i < model.OuterCount; ++i) {
        const auto& curve = *model.Edges[i].Shape;
        if (curve.IsArc || curve.IsFacetedTransition)
            continue;
        const auto& following = *model.Edges[(i + 1) % model.OuterCount].Shape;
        if (!following.IsArc && !following.IsFacetedTransition)
            return {};
        guides.push_back(i);
    }
    return guides.size() < 2 ? std::vector<std::size_t>{} : guides;
}

bool CoversOppositeSupport(const GuideSpans& spans, const Edge& target,
    double first, double last, double parameterSlack)
{
    double covered = first;
    for (const auto& span : spans) {
        const double begin = std::max(first, span.First);
        const double end = std::min(last, span.Last);
        if (begin >= end || span.Target != &target || !span.Start || !span.End)
            continue;
        if (begin - covered > parameterSlack)
            return false;
        covered = std::max(covered, end);
    }
    return last - covered <= parameterSlack;
}

std::optional<Wall> BoundedStraightWall(const Model& model, const Edge& guide,
    const GuideSpans& traces, const Edge& target, double first, double last, int wallId)
{
    const double length = CurveLength(*guide.Shape);
    const double slack = 3 * model.Tolerance / length;
    if (last - first <= 2 * slack || !CoversOppositeSupport(traces, target, first, last, slack))
        return std::nullopt;

    const Point start = Position(*guide.Shape, first), end = Position(*guide.Shape, last);
    const Point normal = Inward(guide, first);
    const auto startHit = IntersectSupport(start, normal, target);
    const auto endHit = IntersectSupport(end, normal, target);
    if (!IsOnSupport(model, target, startHit) || !IsOnSupport(model, target, endHit) ||
        Dot(startHit->PointOnSupport - start, normal) <= model.Tolerance ||
        Dot(endHit->PointOnSupport - end, normal) <= model.Tolerance)
        return std::nullopt;

    const double reverseFirst = std::clamp(std::min(startHit->Parameter, endHit->Parameter), 0.0, 1.0);
    const double reverseLast = std::clamp(std::max(startHit->Parameter, endHit->Parameter), 0.0, 1.0);
    const double reverseSlack = 3 * model.Tolerance / CurveLength(*target.Shape);
    if (reverseLast - reverseFirst <= 2 * reverseSlack ||
        !CoversOppositeSupport(Trace(model, target), guide, reverseFirst, reverseLast, reverseSlack))
        return std::nullopt;

    return Wall{"直壁 " + std::to_string(wallId), {guide.Shape->Id},
        {guide.Shape->Id, target.Shape->Id}, {start, end, endHit->PointOnSupport, startHit->PointOnSupport}};
}

bool IsParallelInsideSupport(const Model& model, const SweepSpan& span, Point tangent)
{
    return IsContinuous(model, span) && !span.Target->Shape->IsArc && !span.Target->Shape->IsFacetedTransition &&
        std::abs(Cross(tangent, Tangent(*span.Target->Shape, 0))) <= AngularTolerance;
}

std::optional<Wall> FiniteStraightWall(const Model& model, const Edge& guide,
    const GuideSpans& traces, int wallId)
{
    const Point tangent = Tangent(*guide.Shape, 0);
    const Edge* target = nullptr;
    for (const auto& span : traces) {
        if (!IsParallelInsideSupport(model, span, tangent))
            continue;
        if (target && span.Target != target)
            return std::nullopt;
        target = span.Target;
    }
    if (!target)
        return std::nullopt;

    const double length = CurveLength(*guide.Shape);
    const double a = Dot(target->Shape->Start - guide.Shape->Start, tangent) / length;
    const double b = Dot(target->Shape->End - guide.Shape->Start, tangent) / length;
    return BoundedStraightWall(model, guide, traces, *target,
        std::max(0.0, std::min(a, b)), std::min(1.0, std::max(a, b)), wallId);
}

void AddRoundedPatches(const Model& model, const std::vector<std::size_t>& guides,
    const std::vector<std::array<int, 2>>& bandIds, Plan& plan)
{
    for (std::size_t i = 0; i < guides.size(); ++i) {
        const int wallId = bandIds[i].back();
        const int nextId = bandIds[(i + 1) % guides.size()].front();
        std::vector<int> transition;
        const std::size_t nextGuide = guides[(i + 1) % guides.size()];
        for (std::size_t index = (guides[i] + 1) % model.OuterCount;
             index != nextGuide; index = (index + 1) % model.OuterCount)
            transition.push_back(model.Edges[index].Shape->Id);
        plan.Patches.push_back({transition.front(), {std::min(wallId, nextId), std::max(wallId, nextId)}, plan.Walls.size()});
        plan.Walls.push_back({"圆角区 " + std::to_string(i + 1), transition, transition, {}});
    }
}

std::optional<Plan> RoundedWallCoverage(const Model& model,
    const std::vector<GuideSpans>& traces, const std::vector<std::size_t>& guides)
{
    Plan plan;
    std::vector<std::array<int, 2>> bandIds;
    for (const auto index : guides) {
        const auto& guide = model.Edges[index];
        const int firstWallId = static_cast<int>(plan.Walls.size() + 1);
        if (auto wall = FiniteStraightWall(model, guide, traces[index], firstWallId))
            plan.Walls.push_back(std::move(*wall));
        else {
            // An inside step can change the opposite support without changing
            // the outer support. Keep each reciprocal straight interval and
            // use its exact normal crosscut as the boundary at the step.
            const Point tangent = Tangent(*guide.Shape, 0);
            const double slack = 3 * model.Tolerance / CurveLength(*guide.Shape);
            std::optional<double> previousEnd;
            for (const auto& span : traces[index]) {
                if (!IsParallelInsideSupport(model, span, tangent))
                    continue;
                if (previousEnd && std::abs(span.First - *previousEnd) > slack)
                    return std::nullopt;
                auto band = BoundedStraightWall(model, guide, traces[index], *span.Target,
                    span.First, span.Last, static_cast<int>(plan.Walls.size() + 1));
                if (!band)
                    return std::nullopt;
                plan.Walls.push_back(std::move(*band));
                previousEnd = span.Last;
            }
            if (!previousEnd)
                return std::nullopt;
        }
        bandIds.push_back({firstWallId, static_cast<int>(plan.Walls.size())});
    }
    AddRoundedPatches(model, guides, bandIds, plan);
    return plan;
}

const Curve* CircularSupport(const Model& model, std::size_t first, std::size_t last)
{
    const Curve& reference = *model.Edges[first].Shape;
    double sweep = 0;
    for (std::size_t i = first; i < last; ++i) {
        const Curve& curve = *model.Edges[i].Shape;
        if (!curve.IsArc || Sign(curve.Sweep) != Sign(reference.Sweep) ||
            Distance(curve.Center, reference.Center) > model.Tolerance ||
            std::abs(curve.Radius - reference.Radius) > model.Tolerance)
            return nullptr;
        sweep += curve.Sweep;
    }
    return std::abs(std::abs(sweep) - FullTurn) * reference.Radius <= model.Tolerance ? &reference : nullptr;
}

std::optional<Plan> CircularWallCoverage(const Model& model)
{
    const auto* outer = CircularSupport(model, 0, model.OuterCount);
    const auto* inner = CircularSupport(model, model.OuterCount, model.Edges.size());
    if (!outer || !inner)
        return std::nullopt;
    const double offset = Distance(outer->Center, inner->Center);
    // Every inward normal passes through the outer center. Strict nesting
    // and a center inside the inner circle prove a finite first exit.
    if (offset + model.Tolerance >= inner->Radius ||
        offset + inner->Radius + model.Tolerance >= outer->Radius)
        return std::nullopt;

    Wall wall;
    wall.Label = "连续壁区";
    for (std::size_t i = 0; i < model.Edges.size(); ++i) {
        const int id = model.Edges[i].Shape->Id;
        wall.SupportIds.push_back(id);
        if (i < model.OuterCount)
            wall.GuideIds.push_back(id);
    }
    return Plan{{std::move(wall)}, {}, {}};
}

std::optional<Plan> ClosedCurvedWallCoverage(const Model& model, const std::vector<GuideSpans>& traces)
{
    if (model.OuterCount < 2 || std::ranges::any_of(model.Edges.begin(),
        model.Edges.begin() + model.OuterCount, [](const Edge& edge) { return !edge.Shape->IsArc; }))
        return std::nullopt;

    Plan plan;
    for (std::size_t i = 0; i < model.OuterCount; ++i)
    {
        const auto& guide = model.Edges[i];
        const auto& curve = *guide.Shape;
        const double slack = model.Tolerance / CurveLength(curve);
        Wall wall{"曲壁 " + std::to_string(i + 1), {curve.Id}, {curve.Id}, {}};
        const auto resolveLimit = [&](const SweepSpan& span, bool atEnd) -> std::optional<Hit>
        {
            const auto& limit = atEnd ? span.End : span.Start;
            if (limit)
                return limit;
            const double parameter = atEnd ? span.Last : span.First;
            const auto hit = FirstExit(model, Position(curve, parameter), Inward(guide, parameter));
            if (!hit || !hit->Target->Inner)
                return std::nullopt;
            // Fitting can move a trim seam within its tolerance. Its exact
            // endpoint ray may then leave through the adjacent support.
            const auto& support = *span.Target->Shape;
            if (std::min(Distance(hit->Position, support.Start), Distance(hit->Position, support.End)) >
                3 * model.Tolerance)
                return std::nullopt;
            return hit;
        };
        std::optional<Hit> previousExit;
        double covered = 0;
        for (const auto& span : traces[i])
        {
            if (!span.Target || !span.Target->Inner)
                continue;
            const auto start = resolveLimit(span, false), end = resolveLimit(span, true);
            if (!start || !end)
                return std::nullopt;
            if (span.First > covered + slack || (previousExit &&
                Distance(previousExit->Position, start->Position) > 3 * model.Tolerance))
                return std::nullopt;
            // The normal map of a convex arc becomes singular at its centre.
            // A usable inner exit must precede that point over the whole trim.
            if (curve.Sweep * guide.MaterialSide > 0 &&
                std::max(start->Distance, end->Distance) >= curve.Radius - model.Tolerance)
                return std::nullopt;
            previousExit = end;
            covered = span.Last;
            const int target = span.Target->Shape->Id;
            if (std::ranges::find(wall.SupportIds, target) == wall.SupportIds.end())
                wall.SupportIds.push_back(target);
        }
        if (!previousExit || 1 - covered > slack)
            return std::nullopt;

        // Keep each distinct outer support. Its endpoint normal is the
        // boundary at the next support, including a tangent connection.
        const auto start = Position(curve, 1);
        const auto hit = FirstExit(model, start, Inward(guide, 1));
        if (!hit || !hit->Target->Inner || Distance(hit->Position, previousExit->Position) > 3 * model.Tolerance)
            return std::nullopt;
        plan.Crosscuts.push_back({start, hit->Position});
        plan.Walls.push_back(std::move(wall));
    }
    return plan;
}

std::optional<Plan> ClosedWallCoverage(const Model& model, const std::vector<GuideSpans>& traces)
{
    if (auto circle = CircularWallCoverage(model))
        return circle;
    if (auto curved = ClosedCurvedWallCoverage(model, traces))
        return curved;
    std::vector<const SweepSpan*> spans;
    for (const auto& trace : traces)
        for (const SweepSpan& span : trace)
            spans.push_back(&span);
    if (spans.empty())
        return std::nullopt;
    if (const auto guides = RoundedWallGuides(model); !guides.empty()) {
        // Each straight band is proved by reciprocal first exits on its
        // finite overlap. The original transition may meet a band with a
        // tangent change. Material construction requires its native patch
        // to share a boundary interval with both adjacent proven bands.
        return RoundedWallCoverage(model, traces, guides);
    }
    if (!CoversInner(model, spans))
        return std::nullopt;

    std::size_t first = 0;
    while (first < spans.size() && Joins(model, *spans[(first + spans.size() - 1) % spans.size()], *spans[first]))
        ++first;
    if (first == spans.size())
        return std::nullopt;

    std::vector<std::vector<const SweepSpan*>> groups;
    std::vector<const SweepSpan*> group;
    for (std::size_t i = 0; i < spans.size(); ++i) {
        const auto& span = *spans[(first + i) % spans.size()];
        if (!group.empty() && !Joins(model, *group.back(), span)) {
            groups.push_back(std::move(group));
            group.clear();
        }
        if (IsContinuous(model, span))
            group.push_back(&span);
    }
    if (!group.empty())
        groups.push_back(std::move(group));
    if (groups.size() < 3)
        return std::nullopt;

    // A sharp corner is shared by consecutive orthogonal walls when both
    // opposite skins arrive at the same inside corner.
    std::vector<const Edge*> guides;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const SweepSpan& previous = *groups[i].back();
        const SweepSpan& next = *groups[(i + 1) % groups.size()].front();
        const Edge* following = &model.Edges[(previous.Guide - model.Edges.data() + 1) % model.OuterCount];
        const Point vertex = Position(*previous.Guide->Shape, 1);
        if (next.Guide != following || previous.Guide->Shape->IsArc || next.Guide->Shape->IsArc ||
            previous.Target->Shape->IsArc || next.Target->Shape->IsArc ||
            std::abs(Dot(Inward(*previous.Guide, 1), Inward(*next.Guide, 0))) > AngularTolerance ||
            Distance(vertex, Position(*next.Guide->Shape, 0)) > model.Tolerance ||
            Distance(previous.End->Position, next.Start->Position) > 3 * model.Tolerance)
            return std::nullopt;
        const double area = std::abs(Cross(vertex - previous.End->Position,
            Position(*next.Guide->Shape, next.First) - Position(*previous.Guide->Shape, previous.Last))) / 2;
        if (area <= model.Tolerance * model.Tolerance)
            return std::nullopt;
        guides.push_back(previous.Guide);
    }

    Plan plan;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const Edge& guide = *guides[i];
        const auto& trace = traces[&guide - model.Edges.data()];
        const auto initial = std::find_if(trace.begin(), trace.end(),
            [&](const SweepSpan& span) { return IsContinuous(model, span); });
        const Edge& target = *initial->Target;
        if (std::any_of(groups[i].begin(), groups[i].end(),
                [&](const SweepSpan* span) { return span->Guide != &guide; }) ||
            std::abs(Dot(Tangent(*guide.Shape, 0), Tangent(*target.Shape, 0))) < 1 - AngularTolerance)
            return std::nullopt;

        Wall wall{"直壁 " + std::to_string(i + 1), {guide.Shape->Id}, {guide.Shape->Id}, {}};
        for (const SweepSpan& span : trace) {
            if (!IsContinuous(model, span))
                continue;
            if (!Collinear(model, target, *span.Target))
                return std::nullopt;
            if (std::find(wall.SupportIds.begin(), wall.SupportIds.end(), span.Target->Shape->Id) == wall.SupportIds.end())
                wall.SupportIds.push_back(span.Target->Shape->Id);
        }

        const auto cutAt = [&](double parameter) -> std::optional<Point> {
            const Point point = Position(*guide.Shape, parameter);
            const Point normal = Inward(guide, parameter);
            const auto other = IntersectSupport(point, normal, target);
            if (!other || Dot(other->PointOnSupport - point, normal) <= model.Tolerance)
                return std::nullopt;
            int matches = 0;
            for (const Edge* cap : {guides[(i + guides.size() - 1) % guides.size()], guides[(i + 1) % guides.size()]}) {
                const auto& line = *cap->Shape;
                if ((Distance(line.Start, point) <= model.Tolerance || Distance(line.End, point) <= model.Tolerance) &&
                    std::abs(Cross(Tangent(line, 0), other->PointOnSupport - line.Start)) <= model.Tolerance * 3 &&
                    Dot(other->PointOnSupport - line.Start, other->PointOnSupport - line.End) <=
                        model.Tolerance * CurveLength(line))
                    ++matches;
            }
            return matches == 1 ? std::optional<Point>{other->PointOnSupport} : std::nullopt;
        };
        const auto start = cutAt(0), end = cutAt(1);
        if (!start || !end)
            return std::nullopt;
        wall.Polygon = {guide.Shape->Start, guide.Shape->End, *end, *start};
        plan.Walls.push_back(std::move(wall));
        const int a = static_cast<int>(i + 1), b = static_cast<int>((i + 1) % groups.size() + 1);
        plan.Junctions.push_back({"角部 " + std::to_string(a), {std::min(a, b), std::max(a, b)}});
    }
    return plan;
}

const Edge& AlongOuter(const Model& model, const Edge& edge, std::ptrdiff_t offset)
{
    const auto size = static_cast<std::ptrdiff_t>(model.OuterCount);
    return model.Edges[(&edge - model.Edges.data() + offset + size * 2) % size];
}

struct Attachment
{
    const Edge* Support;
    std::vector<const Edge*> Transition;
};

std::optional<Attachment> WebEndSupport(const Model& model, const Edge& webSide, bool atEnd)
{
    const int step = atEnd ? 1 : -1;
    const Edge& atJoint = AlongOuter(model, webSide, step);
    if (!atJoint.Shape->IsArc)
        return Attachment{&atJoint, {}};

    const Edge& support = AlongOuter(model, webSide, step * 2);
    const double meet = atEnd ? 1 : 0;
    if (support.Shape->IsArc || Sign(atJoint.Shape->Sweep) * atJoint.MaterialSide >= 0 ||
        Dot(Tangent(*webSide.Shape, meet), Tangent(*atJoint.Shape, 1 - meet)) < 1 - AngularTolerance ||
        Dot(Tangent(*atJoint.Shape, meet), Tangent(*support.Shape, 1 - meet)) < 1 - AngularTolerance)
        return std::nullopt;
    return Attachment{&support, {&atJoint}};
}

double MinimumProjection(const Curve& curve, Point origin, Point axis)
{
    const auto height = [&](Point point) { return Dot(point - origin, axis); };
    double minimum = std::min(height(Position(curve, 0)), height(Position(curve, 1)));
    if (curve.IsArc) {
        const auto parameter = ArcParameter(curve, std::atan2(-axis[1], -axis[0]));
        if (parameter)
            minimum = std::min(minimum, height(Position(curve, *parameter)));
    }
    return minimum;
}

std::vector<Attachment> WebEndSupportCandidates(const Model& model, const Edge& side, const Edge& opposing, bool atEnd)
{
    const Point end = Position(*side.Shape, atEnd ? 1 : 0);
    const Point forward = Tangent(*side.Shape, 0) * (atEnd ? 1 : -1);
    const Point outward = Inward(side, 0) * -1;
    const double midline = Dot(opposing.Shape->Start - end, outward) / 2;
    const int step = atEnd ? 1 : -1;
    std::vector<const Edge*> transition;
    std::vector<Attachment> result;
    for (std::size_t i = 1; i < model.OuterCount; ++i) {
        const Edge& edge = AlongOuter(model, side, step * static_cast<std::ptrdiff_t>(i));
        const auto& curve = *edge.Shape;
        // A root transition, including a native step or non-tangent arc,
        // belongs to this end only while it stays beyond the finite web
        // side and on the same half of the web. A convex bend starts the toe.
        if (&edge == &opposing ||
            (curve.IsArc && Sign(curve.Sweep) * edge.MaterialSide >= 0) ||
            MinimumProjection(curve, end, forward) < -model.Tolerance ||
            MinimumProjection(curve, end, outward) < midline - model.Tolerance)
            break;
        if (!curve.IsArc &&
            Dot(curve.End - curve.Start, outward) * step > model.Tolerance)
            result.push_back({&edge, transition});
        transition.push_back(&edge);
    }
    return result;
}

struct Flange
{
    const Edge* Outside;
    std::array<Attachment, 2> Attachments;
    std::vector<Point> Polygon;
};

using WebSides = std::array<const Edge*, 2>;

std::optional<Point> TaperedTip(const Model& model, const Edge& outside, const Edge& rail, bool atStart)
{
    const int step = atStart ? -1 : 1;
    const Edge& first = AlongOuter(model, outside, step);
    const Edge& second = AlongOuter(model, outside, 2 * step);
    if (!first.Shape->IsArc && &second == &rail)
        return Position(*rail.Shape, atStart ? 1 : 0);

    const Edge& flank = first.Shape->IsArc ? outside : first;
    const Edge& round = first.Shape->IsArc ? first : second;
    const Edge& afterRound = first.Shape->IsArc ? second : AlongOuter(model, outside, 3 * step);
    if (&afterRound != &rail || !round.Shape->IsArc ||
        Sign(round.Shape->Sweep) * round.MaterialSide <= 0 || std::abs(round.Shape->Sweep) >= std::numbers::pi)
        return std::nullopt;

    const Curve& before = atStart ? *rail.Shape : *flank.Shape;
    const Curve& after = atStart ? *flank.Shape : *rail.Shape;
    if (Dot(Tangent(before, 1), Tangent(*round.Shape, 0)) < 1 - AngularTolerance ||
        Dot(Tangent(*round.Shape, 1), Tangent(after, 0)) < 1 - AngularTolerance)
        return std::nullopt;

    // The tangents enclose the convex fillet. The material Boolean later
    // trims this corner back to the original arc instead of using its chord.
    const Point tangent = Tangent(*flank.Shape, 0);
    const auto corner = IntersectSupport(flank.Shape->Start, tangent, rail);
    if (!corner)
        return std::nullopt;
    const double alongFlank = Dot(corner->PointOnSupport - flank.Shape->Start, tangent);
    const double railSlack = model.Tolerance / CurveLength(*rail.Shape);
    if (atStart ? alongFlank > model.Tolerance || corner->Parameter < 1 - railSlack
                : alongFlank < CurveLength(*flank.Shape) - model.Tolerance || corner->Parameter > railSlack)
        return std::nullopt;
    return corner->PointOnSupport;
}

std::optional<Flange> FindTaperedFlange(const Model& model, const std::vector<GuideSpans>& traces,
    const WebSides& sides, bool atEnd, const std::array<Attachment, 2>& attachments)
{
    const Point acrossWeb = Inward(*sides[0], 0);
    const Point webSide = sides[0]->Shape->Start;
    const double webWidth = Dot(sides[1]->Shape->Start - webSide, acrossWeb);
    std::optional<Flange> result;
    for (const auto& spans : traces) {
        if (spans.size() < 3 || spans.front().Guide->Shape->IsArc)
            continue;
        const Edge& outside = *spans.front().Guide;
        const Point normal = Inward(outside, 0);
        const auto hitsRail = [&](const SweepSpan& span) {
            return (span.Target == attachments[0].Support || span.Target == attachments[1].Support) &&
                span.Start && span.End &&
                (span.Last - span.First) * CurveLength(*outside.Shape) > model.Tolerance;
        };
        const auto early = std::find_if(spans.begin(), spans.end(), hitsRail);
        const auto late = std::find_if(spans.rbegin(), spans.rend(), hitsRail);
        if (early == spans.end() || late.base() - early <= 2 || early->Target == late->Target)
            continue;
        const Edge& start = *early->Target;
        const Edge& end = *late->Target;
        if (Dot(normal, Inward(start, 0)) >= -AngularTolerance ||
            Dot(normal, Inward(end, 0)) >= -AngularTolerance)
            continue;

        const auto firstTip = TaperedTip(model, outside, start, true);
        const auto lastTip = TaperedTip(model, outside, end, false);
        const auto apex = IntersectSupport(start.Shape->Start, Tangent(*start.Shape, 0), end);
        if (!firstTip || !lastTip || !apex)
            continue;
        const Point buried = apex->PointOnSupport;
        const double across = Dot(buried - webSide, acrossWeb);
        const double depth = Dot(buried - outside.Shape->Start, normal);
        // Both rails must lead beyond their native web ends, and their
        // extension must meet inside the web's lateral and depth bounds.
        if (across <= model.Tolerance || across >= webWidth - model.Tolerance ||
            depth <= model.Tolerance ||
            Dot(buried - start.Shape->Start, Tangent(*start.Shape, 0)) >= -model.Tolerance ||
            Dot(buried - end.Shape->End, Tangent(*end.Shape, 0)) <= model.Tolerance ||
            Dot(Position(*sides[0]->Shape, atEnd ? 1 : 0) - buried, normal) < -model.Tolerance ||
            Dot(Position(*sides[1]->Shape, atEnd ? 0 : 1) - buried, normal) < -model.Tolerance)
            continue;
        const Point along = Tangent(*outside.Shape, 0);
        if (Dot(buried - *firstTip, along) <= model.Tolerance ||
            Dot(*lastTip - buried, along) <= model.Tolerance)
            continue;
        if (result)
            return std::nullopt;
        result = Flange{&outside, attachments,
            {outside.Shape->Start, outside.Shape->End, *lastTip, buried, *firstTip}};
    }
    return result;
}

bool HasDeeperBranchExit(const Model& model, const Edge& support,
    GuideSpans::const_iterator first, GuideSpans::const_iterator last)
{
    const Point away = Inward(support, 0) * -1;
    const auto depth = [&](Point point) { return Dot(point - support.Shape->Start, away); };
    double greatestDepth = 0;
    for (; first != last; ++first) {
        const auto& span = *first;
        if (!span.Target || !span.Start || !span.End)
            return false;
        double minimumDepth = std::min(depth(span.Start->Position), depth(span.End->Position));
        double maximumDepth = std::max(depth(span.Start->Position), depth(span.End->Position));
        const Curve& target = *span.Target->Shape;
        if (target.IsArc) {
            const auto [low, high] = std::minmax(span.Start->Parameter, span.End->Parameter);
            for (const double sign : {-1.0, 1.0}) {
                const Point radial = away * sign;
                const auto extremum = ArcParameter(target, std::atan2(radial[1], radial[0]));
                if (extremum && *extremum >= low && *extremum <= high) {
                    const double limit = depth(Position(target, *extremum));
                    minimumDepth = std::min(minimumDepth, limit);
                    maximumDepth = std::max(maximumDepth, limit);
                }
            }
        }
        if (minimumDepth < -3 * model.Tolerance)
            return false;
        greatestDepth = std::max(greatestDepth, maximumDepth);
    }
    return greatestDepth > 3 * model.Tolerance;
}

std::optional<Flange> FindFlangeForAttachments(const Model& model, const std::vector<GuideSpans>& traces,
    const WebSides& sides, bool atEnd, const Attachment& first, const Attachment& second)
{
    if (first.Support == second.Support)
        return std::nullopt;
    if (!Collinear(model, *first.Support, *second.Support))
        return FindTaperedFlange(model, traces, sides, atEnd, {first, second});
    if (std::abs(Dot(Tangent(*first.Support->Shape, 0), Tangent(*sides[0]->Shape, 0))) > AngularTolerance)
        return std::nullopt;

    std::optional<Flange> result;
    for (const GuideSpans& spans : traces) {
        if (spans.size() < 3)
            continue;
        const Edge& outer = *spans.front().Guide;
        if (outer.Shape->IsArc || Dot(Inward(outer, 0), Inward(*first.Support, 0)) > -1 + AngularTolerance)
            continue;
        const auto hitsRail = [&](const SweepSpan& span) {
            return (span.Target == first.Support || span.Target == second.Support) &&
                span.Start && span.End && (span.Last - span.First) * CurveLength(*outer.Shape) > model.Tolerance;
        };
        const auto early = std::find_if(spans.begin(), spans.end(), hitsRail);
        const auto late = std::find_if(spans.rbegin(), spans.rend(), hitsRail);
        if (early == spans.end() || late.base() - early <= 2 || early->Target == late->Target)
            continue;

        const auto start = TaperedTip(model, outer, *early->Target, true);
        const auto end = TaperedTip(model, outer, *late->Target, false);
        const Point normal = Inward(outer, 0);
        if (!start || !end || Dot(*start - outer.Shape->Start, normal) <= model.Tolerance ||
            Dot(*end - outer.Shape->End, normal) <= model.Tolerance ||
            !HasDeeperBranchExit(model, *early->Target, early + 1, late.base() - 1))
            continue;
        if (result)
            return std::nullopt;
        result = Flange{&outer, {first, second}, {outer.Shape->Start, outer.Shape->End, *end, *start}};
    }
    return result;
}

std::optional<Flange> FindFlangeAtWebEnd(const Model& model, const std::vector<GuideSpans>& traces,
    const WebSides& sides, bool atEnd, bool allowTransitionChains)
{
    // The two reciprocal skins traverse the outline in opposite directions.
    const auto first = WebEndSupport(model, *sides[0], atEnd);
    const auto second = WebEndSupport(model, *sides[1], !atEnd);
    if (first && second)
        if (auto flange = FindFlangeForAttachments(model, traces, sides, atEnd, *first, *second))
            return flange;
    if (!allowTransitionChains)
        return std::nullopt;

    std::optional<Flange> result;
    for (const auto& a : WebEndSupportCandidates(model, *sides[0], *sides[1], atEnd))
        for (const auto& b : WebEndSupportCandidates(model, *sides[1], *sides[0], !atEnd))
            if (auto flange = FindFlangeForAttachments(model, traces, sides, atEnd, a, b)) {
                if (result)
                    return std::nullopt;
                result = std::move(flange);
            }
    return result;
}

const Edge* DirectWebTip(const Model& model, const WebSides& sides, bool atEnd)
{
    const int step = atEnd ? 1 : -1;
    const Edge& cap = AlongOuter(model, *sides[0], step);
    if (&cap != &AlongOuter(model, *sides[1], -step) || cap.Shape->IsArc)
        return nullptr;
    return &cap;
}

bool SweepsIntervalTo(const Model& model, const GuideSpans& spans,
    const Edge& target, double first, double last)
{
    const Edge& guide = *spans.front().Guide;
    const double slack = 3 * model.Tolerance / CurveLength(*guide.Shape);
    const auto hitsBoundedSupport = [&](double parameter) {
        bool exits = false;
        VisitIntersections(model, target, Position(*guide.Shape, parameter), Inward(guide, parameter),
            [&](const Hit& hit) { exits |= hit.Distance > model.Tolerance && hit.Outward > 1.e-9; });
        return exits;
    };
    bool hasSupport = false;
    for (const auto& span : spans) {
        const double clippedFirst = std::max(span.First, first), clippedLast = std::min(span.Last, last);
        if (clippedLast - clippedFirst <= slack)
            continue;
        if (span.Target != &target ||
            (!span.Start && !hitsBoundedSupport(clippedFirst)) ||
            (!span.End && !hitsBoundedSupport(clippedLast)))
            return false;
        hasSupport = true;
    }
    return hasSupport;
}

bool SweepsOverOpposingSide(const Model& model, const Edge& guide, const Edge& target, const GuideSpans& spans)
{
    if (spans.empty())
        return false;
    const Point origin = guide.Shape->Start;
    const Point tangent = Tangent(*guide.Shape, 0);
    const double length = CurveLength(*guide.Shape);
    const double a = Dot(target.Shape->Start - origin, tangent);
    const double b = Dot(target.Shape->End - origin, tangent);
    const double first = std::max(0.0, std::min(a, b));
    const double last = std::min(length, std::max(a, b));
    return last - first > 3 * model.Tolerance &&
        SweepsIntervalTo(model, spans, target, first / length, last / length);
}

bool AcceptsBendArc(const Edge* arc, const Edge& before, const Edge& after, bool convex)
{
    if (!arc)
        return true;
    return arc->Shape->IsArc && std::abs(arc->Shape->Sweep) < std::numbers::pi &&
        Sign(arc->Shape->Sweep) * arc->MaterialSide == (convex ? 1 : -1) &&
        Dot(Tangent(*before.Shape, 1), Tangent(*arc->Shape, 0)) > 1 - AngularTolerance &&
        Dot(Tangent(*arc->Shape, 1), Tangent(*after.Shape, 0)) > 1 - AngularTolerance;
}

struct OpenTip
{
    const Edge* Inner;
    Point OuterCorner;
    Point InnerCorner;
    std::size_t RoundedCorners;
};

std::optional<Point> TipCorner(const Model& model, const Edge& rail, const Edge& cap,
    const Edge* round, bool railAtStart)
{
    const Point endpoint = Position(*rail.Shape, railAtStart ? 0 : 1);
    if (!round)
        return endpoint;
    if (!(railAtStart ? AcceptsBendArc(round, cap, rail, true) : AcceptsBendArc(round, rail, cap, true)))
        return std::nullopt;
    const Point extension = Tangent(*rail.Shape, 0) * (railAtStart ? -1 : 1);
    const auto corner = IntersectSupport(endpoint, extension, cap);
    const double capSlack = model.Tolerance / CurveLength(*cap.Shape);
    if (!corner || Dot(corner->PointOnSupport - endpoint, extension) < -model.Tolerance ||
        (railAtStart ? corner->Parameter < 1 - capSlack : corner->Parameter > capSlack))
        return std::nullopt;
    return corner->PointOnSupport;
}

std::optional<OpenTip> FindOpenTip(const Model& model, const Edge& outside, bool atStart)
{
    const int step = atStart ? -1 : 1;
    const Edge& first = AlongOuter(model, outside, step);
    const Edge* outsideRound = first.Shape->IsArc ? &first : nullptr;
    const Edge& cap = outsideRound ? AlongOuter(model, first, step) : first;
    const Edge& afterCap = AlongOuter(model, cap, step);
    const Edge* insideRound = afterCap.Shape->IsArc ? &afterCap : nullptr;
    const Edge& inner = insideRound ? AlongOuter(model, afterCap, step) : afterCap;
    if (cap.Shape->IsArc || inner.Shape->IsArc)
        return std::nullopt;
    const auto outerCorner = TipCorner(model, outside, cap, outsideRound, atStart);
    const auto innerCorner = TipCorner(model, inner, cap, insideRound, !atStart);
    if (!outerCorner || !innerCorner)
        return std::nullopt;
    return OpenTip{&inner, *outerCorner, *innerCorner,
        static_cast<std::size_t>(outsideRound != nullptr) + static_cast<std::size_t>(insideRound != nullptr)};
}

std::optional<Wall> OpenWallCore(const Model& model, const std::vector<GuideSpans>& traces,
    const Edge& guide, const OpenTip& tip, bool cornerAtEnd, std::string label)
{
    const Edge& inner = *tip.Inner;
    const Point normal = Inward(guide, 0);
    const Point outerTip = Position(*guide.Shape, cornerAtEnd ? 0 : 1);
    const Point innerTip = Position(*inner.Shape, cornerAtEnd ? 1 : 0);
    const Point innerRoot = Position(*inner.Shape, cornerAtEnd ? 0 : 1);
    const Point towardCorner = Tangent(*guide.Shape, 0) * (cornerAtEnd ? 1 : -1);
    const Point innerNormal = Inward(inner, 0);
    if (Dot(normal, innerNormal) >= 0 || Dot(tip.InnerCorner - tip.OuterCorner, normal) <= model.Tolerance)
        return std::nullopt;

    // Trace the common interval of the finite rails. The envelope continues
    // to the cap tangents so that it includes the native rounding at the tip.
    const double length = CurveLength(*guide.Shape);
    const double coreStart = std::max(0.0, Dot(innerTip - outerTip, towardCorner));
    const double coreLength = std::min(length, Dot(innerRoot - outerTip, towardCorner));
    const double provedLength = coreLength - coreStart;
    if (provedLength <= 3 * model.Tolerance ||
        std::abs(Cross(normal, innerNormal)) * provedLength > model.Tolerance)
        return std::nullopt;
    const Point cut = outerTip + towardCorner * coreLength;
    const auto insideCut = IntersectSupport(cut, normal, inner);
    const auto insideStart = IntersectSupport(outerTip + towardCorner * coreStart, normal, inner);
    if (!IsOnSupport(model, inner, insideCut) || !IsOnSupport(model, inner, insideStart) ||
        Dot(insideCut->PointOnSupport - cut, normal) <= model.Tolerance)
        return std::nullopt;
    const double first = coreStart / length, last = coreLength / length;
    const auto innerBounds = std::minmax(insideStart->Parameter, insideCut->Parameter);
    const auto& outsideTrace = traces[&guide - model.Edges.data()];
    const auto& insideTrace = traces[&inner - model.Edges.data()];
    if (!SweepsIntervalTo(model, outsideTrace, inner, cornerAtEnd ? first : 1 - last,
            cornerAtEnd ? last : 1 - first) ||
        !SweepsIntervalTo(model, insideTrace, guide, std::max(0.0, innerBounds.first),
            std::min(1.0, innerBounds.second)))
        return std::nullopt;

    return Wall{std::move(label), {guide.Shape->Id}, {guide.Shape->Id, inner.Shape->Id},
        {tip.OuterCorner, cut, insideCut->PointOnSupport, tip.InnerCorner}};
}

bool IsQuarterTurn(const Model& model, const Edge& before, const Edge& after, bool convex)
{
    const Point u = Tangent(*before.Shape, 1), v = Tangent(*after.Shape, 0);
    const double supportLength = std::min(CurveLength(*before.Shape), CurveLength(*after.Shape));
    return Cross(u, v) * before.MaterialSide * (convex ? 1 : -1) > 0 &&
        std::abs(Dot(u, v)) * supportLength <= model.Tolerance;
}

struct OpenChainTurn
{
    const Edge* After;
    const Edge* Arc;
};

std::optional<OpenChainTurn> NextOpenChainTurn(const Model& model, const Edge& before, bool convex)
{
    const Edge& adjacent = AlongOuter(model, before, 1);
    const Edge* arc = adjacent.Shape->IsArc ? &adjacent : nullptr;
    const Edge& after = arc ? AlongOuter(model, adjacent, 1) : adjacent;
    if (after.Shape->IsArc || &after == &before)
        return std::nullopt;
    const bool innerArc = !convex && arc && std::abs(arc->Shape->Sweep) < std::numbers::pi &&
        Sign(arc->Shape->Sweep) * arc->MaterialSide < 0 &&
        Dot(Tangent(*before.Shape, 1), Tangent(*arc->Shape, 0)) > 0 &&
        Dot(Tangent(*arc->Shape, 1), Tangent(*after.Shape, 0)) > 0;
    // A corner's inside trim is bounded by its chord, which also applies
    // when the actual concave arc and adjoining straight trims meet at an angle.
    if (Cross(Tangent(*before.Shape, 1), Tangent(*after.Shape, 0)) *
            before.MaterialSide * (convex ? 1 : -1) <= AngularTolerance ||
        !(innerArc || AcceptsBendArc(arc, before, after, convex)))
        return std::nullopt;
    return OpenChainTurn{&after, arc};
}

struct SteppedOpenTurn
{
    const Edge* After;
    std::vector<const Edge*> Trims;
};

std::optional<SteppedOpenTurn> NextSteppedOpenTurn(const Model& model, const Edge& before)
{
    const Point start = before.Shape->End;
    const Point incoming = Tangent(*before.Shape, 1);
    const Point inwardBefore = Inward(before, 1);
    std::vector<const Edge*> trims;
    std::optional<SteppedOpenTurn> result;
    for (std::size_t offset = 1; offset < model.OuterCount; ++offset) {
        const Edge& after = AlongOuter(model, before, static_cast<int>(offset));
        if (after.Shape->IsArc)
            break;
        const Point end = after.Shape->Start;
        const Point outgoing = Tangent(*after.Shape, 0);
        const Point inwardAfter = Inward(after, 0);
        if (!trims.empty() && Cross(incoming, outgoing) * before.MaterialSide > AngularTolerance) {
            const auto corner = IntersectSupport(start, incoming, after);
            const bool bounded = corner && Dot(corner->PointOnSupport - start, incoming) >= -model.Tolerance &&
                corner->Parameter * CurveLength(*after.Shape) <= model.Tolerance;
            const auto insideTurn = [&](Point point) {
                return Dot(point - start, inwardBefore) >= -model.Tolerance &&
                    Dot(point - end, inwardAfter) >= -model.Tolerance &&
                    Dot(point - start, incoming) >= -model.Tolerance &&
                    Dot(point - end, outgoing) <= model.Tolerance;
            };
            // A stepped corner is a bounded, monotone connection of the two
            // outer guides. A concave turn inside it distinguishes a return
            // step from a sequence of successive convex wall turns.
            bool hasReturn = false;
            Point previous = incoming;
            const bool connection = std::ranges::all_of(trims, [&](const Edge* trim) {
                const Point tangent = Tangent(*trim->Shape, 0);
                hasReturn |= Cross(previous, tangent) * before.MaterialSide < -AngularTolerance;
                previous = tangent;
                const Point advance = trim->Shape->End - trim->Shape->Start;
                return Dot(advance, incoming) >= -model.Tolerance &&
                    Dot(advance, outgoing) >= -model.Tolerance &&
                    insideTurn(trim->Shape->Start) && insideTurn(trim->Shape->End);
            });
            hasReturn |= Cross(previous, outgoing) * before.MaterialSide < -AngularTolerance;
            if (bounded && connection && hasReturn) {
                if (result)
                    return std::nullopt;
                result = SteppedOpenTurn{&after, trims};
            }
        }
        trims.push_back(&after);
    }
    return result;
}

struct OpenChainRail
{
    const Edge* Guide;
    std::vector<const Edge*> Supports;
    Point OuterFirst, OuterLast, InnerFirst, InnerLast;
};

bool IsOpenChainSupport(const Model& model, const std::vector<GuideSpans>& traces,
    const Edge& guide, const Edge& support)
{
    if (support.Shape->IsArc)
        return false;
    const Point tangent = Tangent(*guide.Shape, 0);
    const Point normal = Inward(guide, 0);
    const Point origin = guide.Shape->Start;
    const double length = CurveLength(*guide.Shape);
    if (Dot(normal, Inward(support, 0)) >= -AngularTolerance)
        return false;
    const Point first = support.Shape->End;
    const Point last = support.Shape->Start;
    const double lo = std::max(0.0, Dot(first - origin, tangent));
    const double hi = std::min(length, Dot(last - origin, tangent));
    if (hi - lo <= 3 * model.Tolerance ||
        Dot(first - origin, normal) <= model.Tolerance || Dot(last - origin, normal) <= model.Tolerance)
        return false;
    return SweepsIntervalTo(model, traces[&guide - model.Edges.data()], support, lo / length, hi / length) &&
        SweepsOverOpposingSide(model, support, guide, traces[&support - model.Edges.data()]);
}

std::optional<OpenChainRail> OpenChainRailFor(const Model& model, const Edge& guide,
    std::vector<const Edge*> supports, const OpenTip* start, const OpenTip* end)
{
    const Point tangent = Tangent(*guide.Shape, 0);
    const Point origin = guide.Shape->Start;
    const double length = CurveLength(*guide.Shape);
    const Point first = supports.back()->Shape->End;
    const Point last = supports.front()->Shape->Start;
    const double projectedFirst = Dot(first - origin, tangent);
    const double projectedLast = Dot(last - origin, tangent);
    if ((!start && projectedFirst < -model.Tolerance) || (!end && projectedLast > length + model.Tolerance))
        return std::nullopt;
    return OpenChainRail{&guide, std::move(supports),
        start ? start->OuterCorner : origin + tangent * std::max(0.0, projectedFirst),
        end ? end->OuterCorner : origin + tangent * std::min(length, projectedLast),
        start ? start->InnerCorner : first, end ? end->InnerCorner : last};
}

std::optional<Plan> ProveOpenWallChain(const Model& model, const std::vector<GuideSpans>& traces,
    const std::vector<const Edge*>& guides, const std::vector<std::vector<const Edge*>>& outsideTransitions,
    const OpenTip& firstTip, const OpenTip& lastTip)
{
    std::vector<std::vector<const Edge*>> supports(guides.size());
    std::vector<const Edge*> insideArcs(guides.size() - 1);
    std::set<const Edge*> used(guides.begin(), guides.end());
    const Edge* nextSupport = lastTip.Inner;
    std::size_t usedSupportCount = 0;
    for (std::size_t i = supports.size(); i-- > 0;) {
        // A finite inner support can change direction and still belong to
        // the same wall when successive source-skin sweeps prove the pairing
        // against the same uninterrupted opposing guide.
        while (!used.contains(nextSupport) &&
            IsOpenChainSupport(model, traces, *guides[i], *nextSupport)) {
            supports[i].push_back(nextSupport);
            used.insert(nextSupport);
            ++usedSupportCount;
            if (nextSupport == firstTip.Inner)
                break;
            nextSupport = &AlongOuter(model, *nextSupport, 1);
        }
        if (supports[i].empty())
            return std::nullopt;
        if (i == 0)
            break;
        const auto turn = NextOpenChainTurn(model, *supports[i].back(), false);
        if (!turn)
            return std::nullopt;
        insideArcs[i - 1] = turn->Arc;
        nextSupport = turn->After;
    }
    const auto arcCount = [](const auto& arcs) {
        return static_cast<std::size_t>(std::ranges::count_if(arcs, [](const Edge* edge) { return edge; }));
    };
    std::size_t outsideCount = 0;
    for (const auto& transition : outsideTransitions)
        outsideCount += transition.size();
    if (supports.front().back() != firstTip.Inner ||
        model.OuterCount != usedSupportCount + guides.size() + 2 + firstTip.RoundedCorners + lastTip.RoundedCorners +
            outsideCount + arcCount(insideArcs))
        return std::nullopt;

    std::vector<OpenChainRail> rails;
    Plan plan;
    for (std::size_t i = 0; i < guides.size(); ++i) {
        auto rail = OpenChainRailFor(model, *guides[i], std::move(supports[i]),
            i == 0 ? &firstTip : nullptr, i + 1 == guides.size() ? &lastTip : nullptr);
        if (!rail)
            return std::nullopt;
        Wall wall{"直壁 " + std::to_string(i + 1), {rail->Guide->Shape->Id}, {rail->Guide->Shape->Id},
            {rail->OuterFirst, rail->OuterLast, rail->InnerLast}};
        for (std::size_t j = 0; j < rail->Supports.size(); ++j) {
            const auto* support = rail->Supports[j];
            wall.SupportIds.push_back(support->Shape->Id);
            if (j + 1 < rail->Supports.size())
                wall.Polygon.push_back(support->Shape->End);
        }
        wall.Polygon.push_back(rail->InnerFirst);
        plan.Walls.push_back(std::move(wall));
        rails.push_back(std::move(*rail));
    }

    for (std::size_t i = 0; i + 1 < rails.size(); ++i) {
        const auto& before = rails[i];
        const auto& after = rails[i + 1];
        const Point tangent = Tangent(*before.Guide->Shape, 1);
        const auto vertex = IntersectSupport(before.Guide->Shape->End, tangent, *after.Guide);
        if (!vertex || Dot(vertex->PointOnSupport - before.Guide->Shape->End, tangent) < -model.Tolerance ||
            vertex->Parameter * CurveLength(*after.Guide->Shape) > model.Tolerance)
            return std::nullopt;
        // The convex support tangents and the chord of the inner concave
        // bend enclose this turn. The material Boolean restores both arcs.
        std::vector<Point> polygon;
        for (Point p : {before.OuterLast, vertex->PointOnSupport, after.OuterFirst,
                after.InnerFirst, before.InnerLast})
            if (polygon.empty() || Distance(polygon.back(), p) > model.Tolerance)
                polygon.push_back(p);
        if (polygon.size() > 1 && Distance(polygon.front(), polygon.back()) <= model.Tolerance)
            polygon.pop_back();
        if (polygon.size() < 3)
            return std::nullopt;
        std::vector<int> sources{before.Guide->Shape->Id, after.Guide->Shape->Id,
            before.Supports.front()->Shape->Id, after.Supports.back()->Shape->Id};
        for (const Edge* trim : outsideTransitions[i])
            sources.push_back(trim->Shape->Id);
        if (insideArcs[i])
            sources.push_back(insideArcs[i]->Shape->Id);
        plan.Walls.push_back({"转角区 " + std::to_string(i + 1), {}, std::move(sources), std::move(polygon)});
    }
    return plan;
}

std::optional<Plan> OpenWallChainCoverage(const Model& model, const std::vector<GuideSpans>& traces, bool followSteps)
{
    if (model.OuterCount < 8)
        return std::nullopt;
    std::optional<Plan> result;
    for (const Edge& outside : model.Edges) {
        if (outside.Shape->IsArc)
            continue;
        const auto start = FindOpenTip(model, outside, true);
        if (!start || Dot(Inward(outside, 0), Inward(*start->Inner, 0)) >= -AngularTolerance)
            continue;
        std::vector<const Edge*> guides{&outside};
        std::vector<std::vector<const Edge*>> transitions;
        while (guides.size() < model.OuterCount / 2) {
            auto stepped = followSteps ? NextSteppedOpenTurn(model, *guides.back()) : std::nullopt;
            const auto next = stepped ? std::nullopt : NextOpenChainTurn(model, *guides.back(), true);
            const auto* after = stepped ? stepped->After : next ? next->After : nullptr;
            if (!after || std::ranges::find(guides, after) != guides.end())
                break;
            guides.push_back(after);
            transitions.emplace_back();
            if (stepped)
                transitions.back() = std::move(stepped->Trims);
            else if (next->Arc)
                transitions.back().push_back(next->Arc);
            if (guides.size() < 3)
                continue;
            const auto end = FindOpenTip(model, *guides.back(), false);
            if (!end)
                continue;
            if (auto plan = ProveOpenWallChain(model, traces, guides, transitions, *start, *end)) {
                if (result)
                    return std::nullopt;
                result = std::move(plan);
            }
        }
    }
    return result;
}

std::optional<Plan> OpenWallChainCoverage(const Model& model, const std::vector<GuideSpans>& traces)
{
    if (auto plan = OpenWallChainCoverage(model, traces, false))
        return plan;
    return OpenWallChainCoverage(model, traces, true);
}

std::optional<Plan> OpenCornerCoverage(const Model& model, const std::vector<GuideSpans>& traces)
{
    if (model.OuterCount < 6 || model.OuterCount > 12)
        return OpenWallChainCoverage(model, traces);

    std::optional<Plan> result;
    for (const Edge& a : model.Edges) {
        if (a.Shape->IsArc)
            continue;
        const Edge& afterA = AlongOuter(model, a, 1);
        const Edge* outerArc = afterA.Shape->IsArc ? &afterA : nullptr;
        const Edge& b = outerArc ? AlongOuter(model, *outerArc, 1) : afterA;
        if (b.Shape->IsArc || !IsQuarterTurn(model, a, b, true))
            continue;
        const auto tipA = FindOpenTip(model, a, true), tipB = FindOpenTip(model, b, false);
        if (!tipA || !tipB)
            continue;
        const Edge& insideA = *tipA->Inner;
        const Edge& insideB = *tipB->Inner;
        const Edge& afterInsideB = AlongOuter(model, insideB, 1);
        const Edge* innerArc = afterInsideB.Shape->IsArc ? &afterInsideB : nullptr;
        if ((innerArc ? &AlongOuter(model, *innerArc, 1) : &afterInsideB) != &insideA ||
            model.OuterCount != 6 + tipA->RoundedCorners + tipB->RoundedCorners
                + static_cast<std::size_t>(outerArc != nullptr)
                + static_cast<std::size_t>(innerArc != nullptr) ||
            !IsQuarterTurn(model, insideB, insideA, false) ||
            !AcceptsBendArc(outerArc, a, b, true) || !AcceptsBendArc(innerArc, insideB, insideA, false))
            continue;
        auto first = OpenWallCore(model, traces, a, *tipA, true, "直壁 1");
        auto second = OpenWallCore(model, traces, b, *tipB, false, "直壁 2");
        if (!first || !second)
            continue;
        if (result)
            return std::nullopt;
        std::vector<int> cornerSupports{a.Shape->Id, b.Shape->Id, insideA.Shape->Id, insideB.Shape->Id};
        for (const Edge* arc : {outerArc, innerArc})
            if (arc)
                cornerSupports.push_back(arc->Shape->Id);
        result = Plan{{std::move(*first), std::move(*second), {"转角区", {}, std::move(cornerSupports), {}}},
            {}, {}, 2};
    }
    return result ? std::move(result) : OpenWallChainCoverage(model, traces);
}

const Edge* ReciprocalWebSide(const Model& model, const std::vector<GuideSpans>& traces, std::size_t index)
{
    const auto& a = model.Edges[index];
    if (a.Shape->IsArc)
        return nullptr;
    std::set<const Edge*> visited;
    const Edge* side = nullptr;
    for (const auto& span : traces[index]) {
        const auto* b = span.Target;
        if (!b || b == &a || b->Shape->IsArc || !visited.insert(b).second ||
            Dot(Inward(a, 0), Inward(*b, 0)) > -1 + AngularTolerance)
            continue;
        // Verify the interval where both finite side skins exist. The longer
        // side of a web with an oblique tip can also sweep into its end cap.
        if (!SweepsOverOpposingSide(model, a, *b, traces[index]) ||
            !SweepsOverOpposingSide(model, *b, a, traces[b - model.Edges.data()]))
            continue;
        if (side)
            return nullptr;
        side = b;
    }
    return side;
}

std::optional<Plan> BranchedCoverageWithAttachments(const Model& model,
    const std::vector<GuideSpans>& traces, bool allowTransitionChains)
{
    std::optional<Plan> result;
    for (std::size_t i = 0; i < traces.size(); ++i) {
        const Edge& a = model.Edges[i];
        const Edge* b = ReciprocalWebSide(model, traces, i);
        if (!b || b <= &a || ReciprocalWebSide(model, traces, b - model.Edges.data()) != &a)
            continue;
        const WebSides sides{&a, b};
        const std::array flanges{FindFlangeAtWebEnd(model, traces, sides, false, allowTransitionChains),
            FindFlangeAtWebEnd(model, traces, sides, true, allowTransitionChains)};
        if (!flanges[0] && !flanges[1])
            continue;

        const std::array caps{
            flanges[0] ? flanges[0]->Outside : DirectWebTip(model, sides, false),
            flanges[1] ? flanges[1]->Outside : DirectWebTip(model, sides, true)};
        if (!caps[0] || !caps[1] || caps[0] == caps[1])
            continue;

        // For inclined attachment supports retain the additional far-cap
        // witness. A coplanar pair already proves the deeper interval against
        // its own continued support, regardless of the tip's orientation.
        bool certified = true;
        for (std::size_t end = 0; end < flanges.size(); ++end)
            if (flanges[end] &&
                !Collinear(model, *flanges[end]->Attachments[0].Support, *flanges[end]->Attachments[1].Support)) {
                const auto& spans = traces[caps[end] - model.Edges.data()];
                if (std::none_of(spans.begin(), spans.end(),
                        [&](const SweepSpan& span) { return span.Target == caps[1 - end]; }))
                    certified = false;
            }
        if (!certified)
            continue;

        Plan plan;
        const int flangeCount = static_cast<int>(flanges[0].has_value()) + static_cast<int>(flanges[1].has_value());
        std::vector<int> webSupports{a.Shape->Id, b->Shape->Id};
        for (std::size_t end = 0; end < flanges.size(); ++end) {
            const auto& flange = flanges[end];
            if (!flange) {
                webSupports.push_back(caps[end]->Shape->Id);
                continue;
            }
            const int id = static_cast<int>(plan.Walls.size()) + 1;
            const int outsideId = flange->Outside->Shape->Id;
            plan.Walls.push_back({flangeCount == 1 ? "翼板区" : "翼板区 " + std::to_string(id),
                {outsideId}, {outsideId, flange->Attachments[0].Support->Shape->Id, flange->Attachments[1].Support->Shape->Id},
                flange->Polygon});
            for (const Attachment& attachment : flange->Attachments) {
                webSupports.push_back(attachment.Support->Shape->Id);
                for (const auto* edge : attachment.Transition)
                    webSupports.push_back(edge->Shape->Id);
            }
        }
        // The inner flange supports establish the split. The original
        // material between the fitted flange bands includes the root fillets.
        plan.RemainderWallIndex = plan.Walls.size();
        plan.Walls.push_back({"腹板区", {a.Shape->Id, b->Shape->Id}, std::move(webSupports), {}});
        if (result)
            return std::nullopt;
        result = std::move(plan);
    }
    return result;
}

std::optional<Plan> BranchedCoverage(const Model& model, const std::vector<GuideSpans>& traces)
{
    if (auto plan = BranchedCoverageWithAttachments(model, traces, false))
        return plan;
    return BranchedCoverageWithAttachments(model, traces, true);
}

#include "SectionRegionInterruptedWall.inc"
#include "SectionRegionOpenBranches.inc"
}

#include "SectionRegionNormalBoundaryCuts.inc"

std::optional<Plan> PlanNormalSweep(const std::vector<Ring>& rings, double tolerance)
{
    if (rings.size() > 2)
        return std::nullopt;
    const auto model = Prepare(rings, tolerance);
    if (!model)
        return std::nullopt;
    // A full circle has a coincident parameter seam. Treat concentric full
    // circles directly so seam endpoint ambiguity cannot break an annulus.
    if (model->OuterCount == 1 && model->HasInner && model->Edges.size() == 2) {
        const Curve& outer = *model->Edges[0].Shape;
        const Curve& inner = *model->Edges[1].Shape;
        if (outer.IsArc && inner.IsArc && std::abs(std::abs(outer.Sweep) - FullTurn) < ParameterTolerance &&
            std::abs(std::abs(inner.Sweep) - FullTurn) < ParameterTolerance &&
            Distance(outer.Center, inner.Center) <= tolerance && outer.Radius - inner.Radius > tolerance)
            return Plan{{Wall{"连续壁区", {outer.Id}, {outer.Id, inner.Id}, {}}}, {}, {}};
    }
    std::vector<GuideSpans> traces;
    traces.reserve(model->OuterCount);
    for (std::size_t i = 0; i < model->OuterCount; ++i)
        traces.push_back(Trace(*model, model->Edges[i]));
    if (model->HasInner)
        return ClosedWallCoverage(*model, traces);
    if (auto plan = OpenCornerCoverage(*model, traces))
        return plan;
    if (auto plan = BranchedCoverage(*model, traces))
        return plan;
    if (auto plan = InterruptedOpenWallCoverage(rings.front(), *model, traces))
        return plan;
    return ProtrudingOpenWallCoverage(rings.front(), *model, traces);
}

}
