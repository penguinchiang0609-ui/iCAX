#include "pch.h"
#include "MachiningPenetrationStock.h"

#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_GTransform.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <BndLib_Add2dCurve.hxx>
#include <Bnd_Box.hxx>
#include <Bnd_Box2d.hxx>
#include <GProp_GProps.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_GTrsf.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Vec.hxx>

namespace iCAX::ExtrusionRecognition::penetration_detail
{
namespace
{
    struct Point
    {
        double Y, Z;
        Point operator+(Point other) const { return {Y + other.Y, Z + other.Z}; }
        Point operator-(Point other) const { return {Y - other.Y, Z - other.Z}; }
        Point operator*(double scale) const { return {Y * scale, Z * scale}; }
    };

    double Dot(Point a, Point b) { return a.Y * b.Y + a.Z * b.Z; }
    double Cross(Point a, Point b) { return a.Y * b.Z - a.Z * b.Y; }
    double Norm(Point a) { return std::hypot(a.Y, a.Z); }
    Point Left(Point a) { return {-a.Z, a.Y}; }
    Point Unit(Point a) { return a * (1 / Norm(a)); }
    bool IsFinite(Point a) { return std::isfinite(a.Y) && std::isfinite(a.Z); }

    struct Trim
    {
        Point Start, End;
        double MaterialSide = 0;
        std::size_t Previous = 0, Next = 0;
        std::vector<std::uint64_t> FaceIds;
        std::optional<std::array<Point, 2>> CurvedBounds;
    };

    using Contour = std::vector<Trim>;

    Point At(Point start, Point end, double parameter)
    {
        return start + (end - start) * parameter;
    }

    void IncludeFaceIds(std::vector<std::uint64_t>& ids, const std::vector<std::uint64_t>& added)
    {
        for (const auto id : added)
            if (std::find(ids.begin(), ids.end(), id) == ids.end())
                ids.push_back(id);
    }

    bool SameDirectedLine(const Trim& a, const Trim& b, double tolerance)
    {
        if (a.CurvedBounds || b.CurvedBounds)
            return false;
        const auto first = Unit(a.End - a.Start);
        const auto second = Unit(b.End - b.Start);
        return a.MaterialSide == b.MaterialSide && Dot(first, second) > 0 &&
            std::abs(Cross(first, second)) <= Precision::Angular() &&
            std::abs(Cross(first, b.Start - a.Start)) <= tolerance &&
            std::abs(Cross(first, b.End - a.Start)) <= tolerance;
    }

    bool BoundCurvedTrim(Trim& trim, const Geom2dAdaptor_Curve& curve, double tolerance)
    {
        switch (curve.GetType())
        {
            case GeomAbs_Circle:
            case GeomAbs_Ellipse:
            case GeomAbs_BezierCurve:
            case GeomAbs_BSplineCurve:
                break;
            default:
                return false;
        }
        // OCCT encloses conics analytically and bounds splines by their poles.
        Bnd_Box2d box;
        BndLib_Add2dCurve::Add(curve, tolerance, box);
        if (box.IsVoid() || box.IsWhole())
            return false;
        Point minimum, maximum;
        box.Get(minimum.Y, minimum.Z, maximum.Y, maximum.Z);
        if (!IsFinite(minimum) || !IsFinite(maximum))
            return false;
        trim.CurvedBounds = std::array{minimum, maximum};
        return true;
    }

    std::optional<Contour> ReadContour(const SSectionWiresResult& section, double tolerance)
    {
        Contour contour;
        for (const auto& wire : section.Wires)
        {
            if (!wire.bClosed || wire.Edges.empty())
                return std::nullopt;
            Contour ring;
            for (const auto& edge : wire.Edges)
            {
                const auto curve = OpenCascade::BuildOpenCascadeCurve2(edge.Curve);
                if (curve.IsNull() || !std::isfinite(edge.Range.First) || !std::isfinite(edge.Range.Last) ||
                    edge.Range.First >= edge.Range.Last)
                    return std::nullopt;
                const auto a = curve->Value(edge.bReversed ? edge.Range.Last : edge.Range.First);
                const auto b = curve->Value(edge.bReversed ? edge.Range.First : edge.Range.Last);
                const Point start{a.X(), a.Y()}, end{b.X(), b.Y()};
                if (!IsFinite(start) || !IsFinite(end))
                    return std::nullopt;
                Trim trim{start, end};
                const Geom2dAdaptor_Curve adapted(curve, edge.Range.First, edge.Range.Last);
                if (adapted.GetType() == GeomAbs_Line)
                {
                    if (Norm(end - start) <= Precision::Confusion())
                        return std::nullopt;
                }
                else if (!BoundCurvedTrim(trim, adapted, tolerance))
                    return std::nullopt;
                if (edge.SourceFaceId != 0)
                    trim.FaceIds.push_back(edge.SourceFaceId);
                if (!ring.empty() && Norm(ring.back().End - start) > tolerance)
                    return std::nullopt;
                if (ring.empty() || !SameDirectedLine(ring.back(), trim, tolerance))
                    ring.push_back(std::move(trim));
                else
                {
                    ring.back().End = end;
                    IncludeFaceIds(ring.back().FaceIds, trim.FaceIds);
                }
            }
            if (Norm(ring.back().End - ring.front().Start) > tolerance)
                return std::nullopt;
            if (ring.size() > 1 && SameDirectedLine(ring.back(), ring.front(), tolerance))
            {
                ring.front().Start = ring.back().Start;
                IncludeFaceIds(ring.front().FaceIds, ring.back().FaceIds);
                ring.pop_back();
            }
            const auto origin = ring.front().Start;
            double signedArea = wire.dSignedArea;
            if (std::none_of(ring.begin(), ring.end(), [](const Trim& trim) { return trim.CurvedBounds.has_value(); }))
            {
                if (ring.size() < 3)
                    return std::nullopt;
                signedArea = 0;
                for (const auto& trim : ring)
                    signedArea += Cross(trim.Start - origin, trim.End - origin);
            }
            if (!std::isfinite(signedArea) || std::abs(signedArea) <= 2 * tolerance * tolerance)
                return std::nullopt;
            const auto base = contour.size();
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                auto& trim = ring[i];
                trim.MaterialSide = (signedArea > 0 ? 1 : -1) * (wire.bInner ? -1 : 1);
                trim.Previous = base + (i + ring.size() - 1) % ring.size();
                trim.Next = base + (i + 1) % ring.size();
                contour.push_back(std::move(trim));
            }
        }
        return contour;
    }

    struct Hit
    {
        std::size_t Index;
        double Depth, Parameter;
        bool Entering;
    };

    std::optional<Hit> Intersect(const Trim& trim, std::size_t index, Point origin, Point direction)
    {
        const auto along = trim.End - trim.Start;
        const double determinant = Cross(direction, along);
        if (std::abs(determinant) <= Precision::Angular() * Norm(along))
            return std::nullopt;
        const auto offset = trim.Start - origin;
        return Hit{index, Cross(offset, along) / determinant, Cross(offset, direction) / determinant,
            determinant * trim.MaterialSide < 0};
    }

    struct HitPair
    {
        std::size_t Entry, Exit;
        bool operator==(const HitPair&) const = default;
    };

    std::optional<HitPair> FirstStockInterval(const Contour& contour, Point source, Point direction, double tolerance)
    {
        std::optional<Hit> entry, exit;
        std::optional<double> boundaryStart;
        bool ambiguousExit = false;
        for (std::size_t i = 0; i < contour.size(); ++i)
        {
            const auto& trim = contour[i];
            if (trim.CurvedBounds)
                continue;
            const auto hit = Intersect(trim, i, source, direction);
            if (!hit)
            {
                if (std::abs(Cross(direction, trim.Start - source)) <= Precision::Confusion() &&
                    std::abs(Cross(direction, trim.End - source)) <= Precision::Confusion())
                {
                    const double first = Dot(trim.Start - source, direction);
                    const double last = Dot(trim.End - source, direction);
                    if (std::max(first, last) > tolerance)
                    {
                        const double begin = std::min(first, last);
                        boundaryStart = boundaryStart ? std::min(*boundaryStart, begin) : begin;
                    }
                }
                continue;
            }
            const double slack = Precision::Confusion() / Norm(trim.End - trim.Start);
            if (hit->Parameter < -slack || hit->Parameter > 1 + slack || hit->Depth < -tolerance)
                continue;
            if (hit->Depth <= tolerance)
            {
                if (!hit->Entering || entry)
                    return std::nullopt;
                entry = hit;
            }
            else if (!exit || hit->Depth < exit->Depth - tolerance)
            {
                exit = hit;
                ambiguousExit = false;
            }
            else if (std::abs(hit->Depth - exit->Depth) <= tolerance)
                ambiguousExit = true;
        }
        if (!entry || !exit || exit->Entering || ambiguousExit ||
            (boundaryStart && *boundaryStart < exit->Depth - tolerance))
            return std::nullopt;
        return HitPair{entry->Index, exit->Index};
    }

    struct TraceSpan
    {
        double First, Last;
        std::optional<HitPair> Pair;
        bool CurvedBoundary = false;
    };

    struct Range
    {
        double First, Last;
    };

    Range Project(const std::array<Point, 2>& bounds, Point origin, Point direction)
    {
        const auto& [a, b] = bounds;
        const Point minimum{direction.Y >= 0 ? a.Y : b.Y, direction.Z >= 0 ? a.Z : b.Z};
        const Point maximum{direction.Y >= 0 ? b.Y : a.Y, direction.Z >= 0 ? b.Z : a.Z};
        return {Dot(minimum - origin, direction), Dot(maximum - origin, direction)};
    }

    struct CurvedTrace
    {
        Range Parameter;
        Range Depth;
    };

    std::vector<TraceSpan> Trace(const Contour& contour, Point start, Point end, Point direction, double tolerance)
    {
        std::vector<double> events{0, 1};
        std::vector<CurvedTrace> curved;
        const double sweep = Cross(direction, end - start);
        const bool movingAcross = std::abs(sweep) > Precision::Confusion();
        const auto addEvent = [&](double parameter) {
            if (parameter > 0 && parameter < 1)
                events.push_back(parameter);
        };
        for (const auto& trim : contour)
        {
            if (movingAcross)
                for (const auto point : {trim.Start, trim.End})
                    addEvent(Cross(direction, point - start) / sweep);
            if (!trim.CurvedBounds)
                continue;
            const auto across = Project(*trim.CurvedBounds, start, Left(direction));
            if (across.Last < std::min(0.0, sweep) || across.First > std::max(0.0, sweep))
                continue;
            Range parameter{0, 1};
            if (movingAcross)
            {
                const double a = across.First / sweep, b = across.Last / sweep;
                const auto [first, last] = std::minmax(a, b);
                parameter = {first, last};
                addEvent(first);
                addEvent(last);
            }
            curved.push_back({parameter, Project(*trim.CurvedBounds, start, direction)});
        }
        std::sort(events.begin(), events.end());
        events.erase(std::unique(events.begin(), events.end(), [](double a, double b) {
            return std::abs(a - b) <= 16 * std::numeric_limits<double>::epsilon();
        }), events.end());
        std::vector<TraceSpan> spans;
        for (std::size_t i = 1; i < events.size(); ++i)
        {
            // A support switch in a linear contour can only occur at a
            // projected trim endpoint. A witness selects that event interval.
            const double first = events[i - 1], last = events[i];
            const double middle = (first + last) / 2;
            auto pair = FirstStockInterval(contour, At(start, end, middle), direction, tolerance);
            const Point a = At(start, end, first), b = At(start, end, last);
            const double earliest = std::min(Dot(a - start, direction), Dot(b - start, direction)) - tolerance;
            double latest = std::numeric_limits<double>::infinity();
            if (pair)
            {
                const auto hitA = Intersect(contour[pair->Exit], pair->Exit, a, direction);
                const auto hitB = Intersect(contour[pair->Exit], pair->Exit, b, direction);
                if (hitA && hitB)
                    latest = std::max(Dot(a - start, direction) + hitA->Depth,
                        Dot(b - start, direction) + hitB->Depth) + tolerance;
            }
            const bool curvedBoundary = std::any_of(curved.begin(), curved.end(), [&](const CurvedTrace& trim) {
                return trim.Parameter.First < last && trim.Parameter.Last > first &&
                    trim.Depth.First <= latest && trim.Depth.Last >= earliest;
            });
            if (curvedBoundary)
                pair.reset();
            if (!spans.empty() && spans.back().Pair == pair && spans.back().CurvedBoundary == curvedBoundary)
                spans.back().Last = last;
            else
                spans.push_back({first, last, pair, curvedBoundary});
        }
        return spans;
    }

    std::array<Point, 2> ClipSide(const Trim& trim, Point origin, Point along, double first, double last)
    {
        const double start = Dot(trim.Start - origin, along);
        const double distance = Dot(trim.End - trim.Start, along);
        return {At(trim.Start, trim.End, (first - start) / distance),
            At(trim.Start, trim.End, (last - start) / distance)};
    }

    bool ReciprocalWeb(const Contour& contour, std::size_t firstIndex, std::size_t secondIndex,
        Point firstRoot, Point secondRoot, Point direction, double tolerance)
    {
        if (firstIndex == secondIndex)
            return false;
        const auto& first = contour[firstIndex];
        const auto& second = contour[secondIndex];
        if (first.CurvedBounds || second.CurvedBounds)
            return false;
        const double firstStartGap = Norm(first.Start - firstRoot), firstEndGap = Norm(first.End - firstRoot);
        const double secondStartGap = Norm(second.Start - secondRoot), secondEndGap = Norm(second.End - secondRoot);
        if (std::min(firstStartGap, firstEndGap) > tolerance || std::max(firstStartGap, firstEndGap) <= tolerance ||
            std::min(secondStartGap, secondEndGap) > tolerance || std::max(secondStartGap, secondEndGap) <= tolerance)
            return false;
        const Point firstEnd = firstStartGap < firstEndGap ? first.End : first.Start;
        const Point secondEnd = secondStartGap < secondEndGap ? second.End : second.Start;
        const Point along = Unit(firstEnd - firstRoot);
        const Point otherAlong = Unit(secondEnd - secondRoot);
        const Point across = Left(Unit(first.End - first.Start)) * first.MaterialSide;
        const Point towardFirst = Left(Unit(second.End - second.Start)) * second.MaterialSide;
        if (Dot(along, direction) <= Precision::Angular() ||
            Dot(otherAlong, direction) <= Precision::Angular() ||
            Dot(otherAlong, along) <= 0 || std::abs(Cross(otherAlong, along)) > Precision::Angular() ||
            Dot(across, towardFirst) > -1 + Precision::Angular() ||
            Dot(secondRoot - firstRoot, across) <= tolerance)
            return false;

        const double begin = std::max(0.0, Dot(secondRoot - firstRoot, along));
        const double end = std::min(Dot(firstEnd - firstRoot, along), Dot(secondEnd - firstRoot, along));
        if (end - begin <= tolerance)
            return false;
        const auto a = ClipSide(first, firstRoot, along, begin, end);
        const auto b = ClipSide(second, firstRoot, along, begin, end);
        const auto covers = [&](const std::array<Point, 2>& segment, Point inward,
            std::size_t entry, std::size_t exit) {
            const auto spans = Trace(contour, segment[0], segment[1], inward, tolerance);
            return std::all_of(spans.begin(), spans.end(), [&](const TraceSpan& span) {
                return span.Pair == HitPair{entry, exit};
            });
        };
        return covers(a, across, firstIndex, secondIndex) && covers(b, towardFirst, secondIndex, firstIndex);
    }

    struct Flank
    {
        Point PointOnTrim;
        std::size_t WebSide;
    };

    std::optional<Flank> FindFlank(const Contour& contour, std::size_t index,
        Point start, Point direction, double tolerance)
    {
        const auto& trim = contour[index];
        const auto hit = Intersect(trim, index, start, direction);
        if (!hit)
            return std::nullopt;
        const Point point = start + direction * hit->Depth;
        if (Norm(point - trim.Start) <= tolerance)
            return Flank{trim.Start, trim.Previous};
        if (Norm(point - trim.End) <= tolerance)
            return Flank{trim.End, trim.Next};
        return std::nullopt;
    }

    struct Continuation
    {
        double First, Last;
        std::size_t Left, Right;
        bool Proven;
    };

    std::vector<Continuation> FindContinuations(const Contour& contour,
        std::size_t guideIndex, Point direction, double tolerance)
    {
        const auto& guide = contour[guideIndex];
        const auto spans = Trace(contour, guide.Start, guide.End, direction, tolerance);
        std::vector<Continuation> result;
        for (std::size_t left = 0; left + 2 < spans.size(); ++left)
        {
            if (!spans[left].Pair || spans[left].Pair->Entry != guideIndex)
                continue;
            const auto leftIndex = spans[left].Pair->Exit;
            const auto& leftTrim = contour[leftIndex];
            for (std::size_t right = left + 2; right < spans.size(); ++right)
            {
                if (!spans[right].Pair || spans[right].Pair->Entry != guideIndex)
                    continue;
                const auto rightIndex = spans[right].Pair->Exit;
                if (rightIndex == leftIndex || !SameDirectedLine(leftTrim, contour[rightIndex], tolerance))
                    continue;
                bool curvedBoundary = false;
                const auto deeper = [&](const TraceSpan& span) {
                    if (span.CurvedBoundary)
                    {
                        curvedBoundary = true;
                        return true;
                    }
                    if (!span.Pair || span.Pair->Entry != guideIndex)
                        return false;
                    const auto difference = [&](double parameter) {
                        const Point origin = At(guide.Start, guide.End, parameter);
                        const auto before = Intersect(leftTrim, leftIndex, origin, direction);
                        const auto after = Intersect(contour[span.Pair->Exit], span.Pair->Exit, origin, direction);
                        return before && after && before->Depth > tolerance ?
                            after->Depth - before->Depth : -2 * tolerance;
                    };
                    return difference((span.First + span.Last) / 2) > tolerance &&
                        difference(span.First) >= -tolerance && difference(span.Last) >= -tolerance;
                };
                if (!std::all_of(spans.begin() + left + 1, spans.begin() + right, deeper))
                    continue;
                const double first = spans[left].Last, last = spans[right].First;
                const auto a = FindFlank(contour, leftIndex, At(guide.Start, guide.End, first), direction, tolerance);
                const auto b = FindFlank(contour, rightIndex, At(guide.Start, guide.End, last), direction, tolerance);
                const bool proven = !curvedBoundary && a && b &&
                    ReciprocalWeb(contour, a->WebSide, b->WebSide, a->PointOnTrim, b->PointOnTrim,
                        direction, tolerance);
                result.push_back({first, last, leftIndex, rightIndex, proven});
                break;
            }
        }
        return result;
    }

    std::vector<StockExitSpan> Unknown(std::string reason)
    {
        StockExitSpan span;
        span.Reason = std::move(reason);
        return {std::move(span)};
    }

    bool CoversSourceWithBoundary(const TopoDS_Shape& stockFaces, const gp_Pnt& start,
        const gp_Pnt& end, double tolerance)
    {
        const gp_Vec travel(start, end);
        const double length = travel.Magnitude();
        const gp_Vec tangent = travel / length;
        const gp_Lin source(start, gp_Dir(tangent));
        std::vector<Range> intervals;
        for (TopExp_Explorer face(stockFaces, TopAbs_FACE); face.More(); face.Next())
        {
            for (TopExp_Explorer item(face.Current(), TopAbs_EDGE); item.More(); item.Next())
            {
                const auto edge = TopoDS::Edge(item.Current());
                if (BRep_Tool::Degenerated(edge))
                    continue;
                const BRepAdaptor_Curve curve(edge);
                if (curve.GetType() != GeomAbs_Line)
                    continue;
                const auto a = curve.Value(curve.FirstParameter()), b = curve.Value(curve.LastParameter());
                if (source.Distance(a) > tolerance || source.Distance(b) > tolerance)
                    continue;
                const double first = gp_Vec(start, a).Dot(tangent);
                const double last = gp_Vec(start, b).Dot(tangent);
                intervals.push_back({std::min(first, last), std::max(first, last)});
            }
        }
        std::ranges::sort(intervals, {}, &Range::First);
        double reached = 0;
        for (const auto& interval : intervals)
        {
            if (interval.First > reached + tolerance)
                return false;
            reached = std::max(reached, interval.Last);
            if (reached >= length - tolerance)
                return true;
        }
        return false;
    }
}

std::vector<StockExitSpan> ResolveStockExit(const SSectionWiresResult& section,
    const gp_Pnt& start, const gp_Pnt& end, const gp_Dir& axis, double tolerance)
{
    if (!section.bSuccess || section.Wires.empty() || !std::isfinite(tolerance) || tolerance <= 0)
        return Unknown("stock-section-unavailable");
    std::optional<Contour> contour;
    try { contour = ReadContour(section, tolerance); }
    catch (const Standard_Failure&) { return Unknown("stock-section-curve-evaluation-failed"); }
    if (!contour || contour->empty())
        return Unknown("stock-section-contour-or-curve-bounds-unproved");

    const Point projected{axis.Y(), axis.Z()};
    const double scale = Norm(projected);
    if (scale <= Precision::Angular())
        return Unknown("stock-axis-parallel-to-extrusion");
    const Point direction = projected * (1 / scale);
    const Point first{start.Y(), start.Z()}, last{end.Y(), end.Z()};
    if (!IsFinite(first) || !IsFinite(last))
        return Unknown("stock-source-geometry-unavailable");
    if (std::abs(Cross(direction, last - first)) <= Precision::Confusion() &&
        Norm(last - first) > Precision::Confusion())
        return Unknown("stock-source-not-transverse-to-axis");

    std::map<std::size_t, std::vector<Continuation>> continuations;
    std::vector<StockExitSpan> result;
    for (const auto& traced : Trace(*contour, first, last, direction, tolerance))
    {
        auto& span = result.emplace_back();
        span.First = traced.First;
        span.Last = traced.Last;
        if (!traced.Pair)
        {
            span.Reason = traced.CurvedBoundary ? "stock-curved-entry-or-exit-unproved" :
                "stock-entry-or-first-exit-not-unique";
            continue;
        }
        const auto [entryIndex, exitIndex] = *traced.Pair;
        const auto& entry = (*contour)[entryIndex];
        const Point a = At(first, last, traced.First), b = At(first, last, traced.Last);
        const auto entersAt = [&](Point point) {
            const auto hit = Intersect(entry, entryIndex, point, direction);
            const double length = Norm(entry.End - entry.Start);
            return hit && std::abs(hit->Depth) <= tolerance &&
                hit->Parameter >= -tolerance / length && hit->Parameter <= 1 + tolerance / length;
        };
        if (!entersAt(a) || !entersAt(b))
        {
            span.Reason = "stock-source-does-not-follow-entry-trim";
            continue;
        }

        auto [cached, inserted] = continuations.try_emplace(entryIndex);
        if (inserted)
            cached->second = FindContinuations(*contour, entryIndex, direction, tolerance);
        const auto projectToEntry = [&](Point point) {
            return Cross(direction, point - entry.Start) / Cross(direction, entry.End - entry.Start);
        };
        const double middle = (projectToEntry(a) + projectToEntry(b)) / 2;
        const Continuation* active = nullptr;
        bool ambiguous = false;
        for (const auto& continuation : cached->second)
        {
            if (middle <= continuation.First || middle >= continuation.Last)
                continue;
            if (active || !continuation.Proven)
                ambiguous = true;
            active = &continuation;
        }
        if (ambiguous)
        {
            span.Reason = "stock-interrupted-exit-has-no-unique-web-proof";
            continue;
        }

        const std::size_t support = active ? active->Left : exitIndex;
        const auto& exit = (*contour)[support];
        const auto firstHit = Intersect(exit, support, a, direction);
        const auto lastHit = Intersect(exit, support, b, direction);
        if (!firstHit || !lastHit || std::min(firstHit->Depth, lastHit->Depth) <= tolerance)
        {
            span.Reason = "stock-exit-limit-not-proven";
            continue;
        }
        span.FirstDepth = firstHit->Depth / scale;
        span.LastDepth = lastHit->Depth / scale;
        span.SupportFaceIds = exit.FaceIds;
        if (active)
            IncludeFaceIds(span.SupportFaceIds, (*contour)[active->Right].FaceIds);
        span.Continued = active != nullptr;
        span.Reason = active ? "stock-continued-wall-exit" : "stock-first-exit";
    }
    return result;
}

bool ProjectedStockStripIsCovered(const TopoDS_Face& stockSection, const TopoDS_Face& machiningFace,
    const gp_Pnt& start, const gp_Pnt& end, const gp_Dir& axis, double tolerance)
{
    if (stockSection.IsNull() || machiningFace.IsNull() || !std::isfinite(tolerance) || tolerance <= 0)
        return false;
    const BRepAdaptor_Surface surface(machiningFace, true);
    const BRepAdaptor_Surface stockSurface(stockSection, true);
    const gp_Dir extrusion(1, 0, 0);
    if (surface.GetType() != GeomAbs_Plane || stockSurface.GetType() != GeomAbs_Plane ||
        !stockSurface.Plane().Axis().Direction().IsParallel(extrusion, Precision::Angular()))
        return false;

    const auto plane = surface.Plane();
    auto normal = plane.Axis().Direction();
    if (std::abs(normal.X()) <= Precision::Angular() || !axis.IsNormal(normal, Precision::Angular()) ||
        plane.Distance(start) > tolerance || plane.Distance(end) > tolerance)
        return false;
    if (normal.X() < 0)
        normal.Reverse();

    const auto stockPlane = stockSurface.Plane();
    const double sectionX = stockPlane.Location().X();
    const gp_Pnt a(sectionX, start.Y(), start.Z()), b(sectionX, end.Y(), end.Z());
    const gp_Vec along(a, b), ruling(0, axis.Y(), axis.Z());
    if (along.Magnitude() <= tolerance || ruling.Magnitude() <= Precision::Angular() ||
        !CoversSourceWithBoundary(stockSection, a, b, tolerance))
        return false;
    auto across = gp_Vec(extrusion).Crossed(along).Normalized();
    if (across.Dot(ruling) < 0)
        across.Reverse();
    const double advance = across.Dot(ruling);
    if (advance <= Precision::Angular())
        return false;

    Bnd_Box bounds;
    BRepBndLib::AddOptimal(stockSection, bounds, false, false);
    if (bounds.IsVoid() || bounds.IsOpen())
        return false;
    const auto lo = bounds.CornerMin(), hi = bounds.CornerMax();
    const gp_Pnt furthestCorner(sectionX, across.Y() >= 0 ? hi.Y() : lo.Y(), across.Z() >= 0 ? hi.Z() : lo.Z());
    const double farthest = gp_Vec(a, furthestCorner).Dot(across);
    if (!std::isfinite(farthest) || farthest <= tolerance)
        return false;
    const gp_Vec shift = ruling * ((farthest + tolerance) / advance);
    BRepBuilderAPI_MakePolygon wire(a, b, b.Translated(shift), a.Translated(shift), true);
    if (!wire.IsDone())
        return false;
    BRepBuilderAPI_MakeFace strip(stockPlane, wire.Wire());
    if (!strip.IsDone())
        return false;

    NCollection_List<TopoDS_Shape> arguments, tools;
    arguments.Append(stockSection.Oriented(TopAbs_FORWARD));
    tools.Append(strip.Face().Oriented(TopAbs_FORWARD));
    BRepAlgoAPI_Common common;
    common.SetArguments(arguments);
    common.SetTools(tools);
    common.SetNonDestructive(true);
    common.Build();
    if (!common.IsDone() || common.HasErrors() || common.HasWarnings() || common.Shape().IsNull() ||
        !CoversSourceWithBoundary(common.Shape(), a, b, tolerance))
        return false;
    GProp_GProps material;
    BRepGProp::SurfaceProperties(common.Shape(), material);
    if (!(std::abs(material.Mass()) > tolerance * tolerance))
        return false;

    // Flatten the machining plane onto the section by an invertible affine
    // map that leaves Y and Z unchanged. This preserves the actual trim,
    // including the conic and spline boundaries of the stock intersection.
    gp_GTrsf project;
    project.SetVectorialPart(gp_Mat(normal.X(), normal.Y(), normal.Z(), 0, 1, 0, 0, 0, 1));
    project.SetTranslationPart(gp_XYZ(sectionX - plane.Location().XYZ().Dot(normal.XYZ()), 0, 0));
    BRepBuilderAPI_GTransform footprint(machiningFace.Oriented(TopAbs_FORWARD), project, true);
    if (!footprint.IsDone() || footprint.Shape().IsNull() || !BRepCheck_Analyzer(footprint.Shape()).IsValid())
        return false;

    arguments.Clear();
    tools.Clear();
    arguments.Append(common.Shape());
    tools.Append(footprint.Shape());
    BRepAlgoAPI_Cut difference;
    difference.SetArguments(arguments);
    difference.SetTools(tools);
    difference.SetNonDestructive(true);
    difference.Build();
    return difference.IsDone() && !difference.HasErrors() && !difference.HasWarnings() &&
        !difference.Shape().IsNull() && !TopExp_Explorer(difference.Shape(), TopAbs_FACE).More();
}
}
