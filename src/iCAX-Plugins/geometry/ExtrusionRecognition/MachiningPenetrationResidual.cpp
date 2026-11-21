#include "pch.h"
#include "MachiningPenetrationResidual.h"

#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <NCollection_IndexedMap.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Solid.hxx>
#include <gp_Ax3.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <numeric>
#include <stdexcept>

namespace iCAX::ExtrusionRecognition::penetration_detail
{
namespace
{
    constexpr double DirectionRoundoff = 128 * std::numeric_limits<double>::epsilon();
    using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;

    struct SectionPoint
    {
        double U;
        double Depth;
    };

    struct SweepFrame
    {
        gp_Pnt Start;
        gp_Vec Along;
        gp_Vec Axis;
        gp_Vec CrossAxis;
        gp_Vec RemovalNormal;
        double UResolution;
        double Tolerance;
        double DepthLimit;

        SectionPoint Project(const gp_Pnt& point) const
        {
            const gp_Vec delta(Start, point);
            const double u = delta.Dot(CrossAxis) / CrossAxis.SquareMagnitude();
            return {u, delta.Dot(Axis) - u * Along.Dot(Axis)};
        }

        gp_Pnt Point(double u, double depth) const
        {
            return Start.Translated(Along * u + Axis * depth);
        }

        double Side(const gp_Pnt& point) const { return gp_Vec(Start, point).Dot(RemovalNormal); }
    };

    struct SweepBounds
    {
        double First = std::numeric_limits<double>::infinity();
        double Last = -std::numeric_limits<double>::infinity();
        double MinDepth = std::numeric_limits<double>::infinity();
        double MaxDepth = -std::numeric_limits<double>::infinity();
        double MinSide = std::numeric_limits<double>::infinity();
        double MaxSide = -std::numeric_limits<double>::infinity();
    };

    Bnd_Box ExactBounds(const TopoDS_Shape& shape)
    {
        Bnd_Box box;
        BRepBndLib::AddOptimal(shape, box, false, true);
        if (box.IsVoid() || box.IsOpen())
            throw std::runtime_error("Retained boundary has no finite exact bounds");
        return box;
    }

    SweepBounds Bounds(const SweepFrame& frame, const Bnd_Box& box)
    {
        double x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        SweepBounds bounds;
        for (const auto x : {x0, x1})
            for (const auto y : {y0, y1})
                for (const auto z : {z0, z1})
                {
                    const gp_Pnt point(x, y, z);
                    const auto p = frame.Project(point);
                    const auto side = frame.Side(point);
                    bounds.First = std::min(bounds.First, p.U);
                    bounds.Last = std::max(bounds.Last, p.U);
                    bounds.MinDepth = std::min(bounds.MinDepth, p.Depth);
                    bounds.MaxDepth = std::max(bounds.MaxDepth, p.Depth);
                    bounds.MinSide = std::min(bounds.MinSide, side);
                    bounds.MaxSide = std::max(bounds.MaxSide, side);
                }
        return bounds;
    }

    gp_Vec OutwardPlaneNormal(const TopoDS_Face& face)
    {
        BRepAdaptor_Surface surface(face, true);
        gp_Pnt unused;
        gp_Vec du, dv;
        surface.D1(0, 0, unused, du, dv);
        gp_Vec normal = du.Crossed(dv).Normalized();
        if (face.Orientation() == TopAbs_REVERSED)
            normal.Reverse();
        return normal;
    }

    struct NativeFace
    {
        std::uint64_t Id;
        TopoDS_Face Face;
        Bnd_Box Bounds;
    };

    struct NativeSolid
    {
        std::unique_ptr<BRepClass3d_SolidClassifier> Classifier;
        std::vector<NativeFace> Faces;
    };

    std::optional<std::vector<NativeSolid>> LoadSolids(
        const iCAX::OpenCascade::SOpenCascadeBRepBuildResult& built)
    {
        ShapeMap identities;
        std::map<int, std::uint64_t> ids;
        for (const auto& [id, face] : built.Faces)
            ids.emplace(identities.Add(face), id);
        ShapeMap solids;
        TopExp::MapShapes(built.Shape, TopAbs_SOLID, solids);
        ShapeMap anchoredFaces;
        std::vector<NativeSolid> result;
        for (int i = 1; i <= solids.Extent(); ++i)
        {
            auto solid = TopoDS::Solid(solids(i));
            std::size_t shells = 0;
            for (TopExp_Explorer iter(solid, TopAbs_SHELL); iter.More(); iter.Next())
            {
                if (!BRep_Tool::IsClosed(iter.Current()))
                    return std::nullopt;
                ++shells;
            }
            if (shells == 0 || !BRepCheck_Analyzer(solid, false).IsValid())
                return std::nullopt;

            auto classifier = std::make_unique<BRepClass3d_SolidClassifier>(solid);
            classifier->PerformInfinitePoint(Precision::Confusion());
            if (classifier->State() == TopAbs_IN)
            {
                solid.Reverse();
                classifier->Load(solid);
                classifier->PerformInfinitePoint(Precision::Confusion());
            }
            if (classifier->State() != TopAbs_OUT)
                return std::nullopt;

            NativeSolid native{std::move(classifier), {}};
            for (TopExp_Explorer iter(solid, TopAbs_FACE); iter.More(); iter.Next())
            {
                const auto face = TopoDS::Face(iter.Current());
                const int index = identities.FindIndex(face);
                if (!index || (face.Orientation() != TopAbs_FORWARD && face.Orientation() != TopAbs_REVERSED))
                    return std::nullopt;
                anchoredFaces.Add(face);
                native.Faces.push_back({ids.at(index), face, ExactBounds(face)});
            }
            result.push_back(std::move(native));
        }
        if (result.empty() || anchoredFaces.Extent() != identities.Extent())
            return std::nullopt;
        return result;
    }

    struct SectionSegment
    {
        SectionPoint First;
        SectionPoint Last;
        std::uint64_t FaceId;

        double DepthAt(double u) const
        {
            return std::lerp(First.Depth, Last.Depth, (u - First.U) / (Last.U - First.U));
        }
    };

    struct CoplanarFace
    {
        std::size_t Solid;
        TopoDS_Face Face;
        gp_Vec Outward;
    };

    struct SweepSection
    {
        std::vector<SectionSegment> Segments;
        std::vector<CoplanarFace> Coplanar;
        std::vector<SweepBounds> Unproven;
    };

    SweepBounds TrimBounds(const SweepFrame& frame, const TopoDS_Edge& edge)
    {
        const gp_Ax3 coordinates(frame.Start, gp_Dir(frame.CrossAxis.Crossed(frame.Axis)), gp_Dir(frame.CrossAxis));
        gp_Trsf worldToLocal;
        worldToLocal.SetTransformation(coordinates);
        const auto box = ExactBounds(edge.Moved(TopLoc_Location(worldToLocal)));
        double x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        const double length = frame.CrossAxis.Magnitude();
        const double skew = frame.Along.Dot(frame.Axis) / length;
        SweepBounds bounds;
        bounds.First = x0 / length;
        bounds.Last = x1 / length;
        bounds.MinDepth = y0 - std::max(skew * x0, skew * x1);
        bounds.MaxDepth = y1 - std::min(skew * x0, skew * x1);
        bounds.MinSide = z0;
        bounds.MaxSide = z1;
        return bounds;
    }

    bool ReachesSweep(const SweepFrame& frame, const SweepBounds& bounds)
    {
        return bounds.Last >= -frame.UResolution && bounds.First <= 1 + frame.UResolution &&
            bounds.MinSide <= frame.Tolerance && bounds.MaxSide >= -frame.Tolerance &&
            bounds.MaxDepth >= -frame.Tolerance && bounds.MinDepth <= frame.DepthLimit + frame.Tolerance;
    }

    void AddSegment(const SweepFrame& frame, SweepSection& section,
        const gp_Pnt& first, const gp_Pnt& last, std::uint64_t id)
    {
        auto a = frame.Project(first);
        auto b = frame.Project(last);
        if (a.U > b.U)
            std::swap(a, b);
        if (b.U < 0 || a.U > 1 || std::max(a.Depth, b.Depth) < 0 ||
            std::min(a.Depth, b.Depth) > frame.DepthLimit)
            return;
        section.Segments.push_back({a, b, id});
    }

    void AddTrim(const SweepFrame& frame, SweepSection& section, const TopoDS_Edge& edge, std::uint64_t id)
    {
        if (BRep_Tool::Degenerated(edge))
            return;
        const BRepAdaptor_Curve curve(edge);
        if (curve.GetType() == GeomAbs_Line)
        {
            AddSegment(frame, section, curve.Value(curve.FirstParameter()), curve.Value(curve.LastParameter()), id);
            return;
        }
        const auto bounds = TrimBounds(frame, edge);
        if (ReachesSweep(frame, bounds))
            section.Unproven.push_back(bounds);
    }

    bool AddNativeSection(const SweepFrame& frame, const TopoDS_Face& face,
        std::uint64_t id, SweepSection& section)
    {
        try
        {
            const gp_Pln plane(frame.Start, gp_Dir(frame.RemovalNormal));
            BRepAlgoAPI_Section intersection(face, plane, false);
            intersection.SetNonDestructive(true);
            intersection.Approximation(false);
            intersection.Build();
            if (!intersection.IsDone() || intersection.HasErrors() || intersection.HasWarnings() ||
                intersection.Shape().IsNull())
                return false;
            for (TopExp_Explorer item(intersection.Shape(), TopAbs_EDGE); item.More(); item.Next())
                AddTrim(frame, section, TopoDS::Edge(item.Current()), id);
            return true;
        }
        catch (const Standard_Failure&)
        {
            return false;
        }
    }

    bool AddPlanarFace(const SweepFrame& frame, const TopoDS_Face& face, std::uint64_t id,
        std::size_t solid, SweepSection& section)
    {
        BRepAdaptor_Surface surface(face, true);
        if (surface.GetType() != GeomAbs_Plane)
            return false;
        const auto plane = surface.Plane();
        const auto normal = OutwardPlaneNormal(face);
        const auto transverse = normal.Crossed(frame.RemovalNormal);
        const bool parallel = transverse.Magnitude() <= DirectionRoundoff;
        if (parallel && plane.Distance(frame.Start) > frame.Tolerance)
            return true;
        // A transverse plane intersection is a line. The only changes of
        // membership on it are intersections with the actual finite trim.
        ShapeMap edgeMap;
        TopExp::MapShapes(face, TopAbs_EDGE, edgeMap);
        if (parallel)
        {
            section.Coplanar.push_back({solid, face, normal});
            for (int i = 1; i <= edgeMap.Extent(); ++i)
                AddTrim(frame, section, TopoDS::Edge(edgeMap(i)), id);
            return true;
        }

        std::vector<std::pair<gp_Pnt, gp_Pnt>> edges;
        for (int i = 1; i <= edgeMap.Extent(); ++i)
        {
            const auto edge = TopoDS::Edge(edgeMap(i));
            if (BRep_Tool::Degenerated(edge))
                continue;
            const BRepAdaptor_Curve curve(edge);
            const auto first = curve.FirstParameter(), last = curve.LastParameter();
            if (curve.GetType() != GeomAbs_Line || !std::isfinite(first) || !std::isfinite(last))
                return AddNativeSection(frame, face, id, section);
            edges.emplace_back(curve.Value(first), curve.Value(last));
        }
        if (edges.empty())
            return false;

        std::vector<gp_Pnt> intersections;
        for (const auto& [a, b] : edges)
        {
            const auto aSide = frame.Side(a), bSide = frame.Side(b);
            if (std::abs(aSide) <= frame.Tolerance) intersections.push_back(a);
            if (std::abs(bSide) <= frame.Tolerance) intersections.push_back(b);
            if ((aSide < -frame.Tolerance && bSide > frame.Tolerance) ||
                (bSide < -frame.Tolerance && aSide > frame.Tolerance))
                intersections.push_back(a.Translated(gp_Vec(a, b) * (aSide / (aSide - bSide))));
        }
        std::sort(intersections.begin(), intersections.end(), [&](const auto& a, const auto& b)
        {
            return gp_Vec(frame.Start, a).Dot(transverse) < gp_Vec(frame.Start, b).Dot(transverse);
        });
        const auto last = std::unique(intersections.begin(), intersections.end(), [&](const auto& a, const auto& b)
        {
            return a.Distance(b) <= frame.Tolerance;
        });
        intersections.erase(last, intersections.end());
        for (std::size_t i = 1; i < intersections.size(); ++i)
        {
            const auto& a = intersections[i - 1];
            const auto& b = intersections[i];
            const auto middle = a.Translated(gp_Vec(a, b) * 0.5);
            const auto state = BRepClass_FaceClassifier(face, middle, frame.Tolerance).State();
            if (state == TopAbs_UNKNOWN)
                return false;
            if (state == TopAbs_IN || state == TopAbs_ON)
                AddSegment(frame, section, a, b, id);
        }
        return true;
    }

    SweepSection IntersectBoundary(const SweepFrame& frame, const std::vector<NativeSolid>& solids)
    {
        SweepSection section;
        for (std::size_t solid = 0; solid < solids.size(); ++solid)
            for (const auto& [id, face, box] : solids[solid].Faces)
            {
                const auto bounds = Bounds(frame, box);
                if (!ReachesSweep(frame, bounds))
                    continue;
                const BRepAdaptor_Surface surface(face, true);
                const auto proven = surface.GetType() == GeomAbs_Plane
                    ? AddPlanarFace(frame, face, id, solid, section)
                    : AddNativeSection(frame, face, id, section);
                if (!proven)
                    section.Unproven.push_back(bounds);
            }
        return section;
    }

    std::vector<double> EventParameters(const SweepFrame& frame, const SweepSection& section)
    {
        std::vector<double> events{0, 1};
        const auto append = [&](double u)
        {
            if (u > frame.UResolution && u < 1 - frame.UResolution)
                events.push_back(u);
        };
        const auto swapEvent = [&](const SectionSegment& a, const SectionSegment& b)
        {
            const double first = std::max({0.0, a.First.U, b.First.U});
            const double last = std::min({1.0, a.Last.U, b.Last.U});
            if (last - first <= frame.UResolution)
                return;
            const double d0 = a.DepthAt(first) - b.DepthAt(first);
            const double d1 = a.DepthAt(last) - b.DepthAt(last);
            if ((d0 < 0 && d1 > 0) || (d0 > 0 && d1 < 0))
                append(std::lerp(first, last, d0 / (d0 - d1)));
        };
        const SectionSegment entry{{0, 0}, {1, 0}, 0};
        const SectionSegment end{{0, frame.DepthLimit}, {1, frame.DepthLimit}, 0};
        for (std::size_t i = 0; i < section.Segments.size(); ++i)
        {
            const auto& segment = section.Segments[i];
            append(segment.First.U);
            append(segment.Last.U);
            swapEvent(segment, entry);
            swapEvent(segment, end);
            for (std::size_t j = 0; j < i; ++j)
                swapEvent(segment, section.Segments[j]);
        }
        for (const auto& unknown : section.Unproven)
        {
            append(unknown.First);
            append(unknown.Last);
        }
        std::sort(events.begin(), events.end());
        std::vector<double> unique{0};
        for (const double u : events)
            if (u - unique.back() > frame.UResolution)
                unique.push_back(u);
        if (unique.back() < 1)
            unique.back() = 1;
        return unique;
    }

    // For a regular point on a coplanar face the outward normal settles the
    // one-sided limit without translating the ray by an arbitrary distance.
    TopAbs_State RetainedSide(const SweepFrame& frame, const SweepSection& section,
        std::vector<NativeSolid>& solids, const gp_Pnt& point)
    {
        bool uncertain = false;
        for (std::size_t i = 0; i < solids.size(); ++i)
        {
            auto& classifier = *solids[i].Classifier;
            classifier.Perform(point, frame.Tolerance);
            auto state = classifier.State();
            if (state == TopAbs_ON)
            {
                state = TopAbs_UNKNOWN;
                for (const auto& candidate : section.Coplanar)
                {
                    if (candidate.Solid != i ||
                        BRepClass_FaceClassifier(candidate.Face, point, frame.Tolerance).State() != TopAbs_IN)
                        continue;
                    const auto here = candidate.Outward.Dot(frame.RemovalNormal) > 0 ? TopAbs_OUT : TopAbs_IN;
                    if (state != TopAbs_UNKNOWN && state != here)
                        return TopAbs_UNKNOWN;
                    state = here;
                }
            }
            if (state == TopAbs_IN)
                return TopAbs_IN;
            uncertain |= state != TopAbs_OUT;
        }
        return uncertain ? TopAbs_UNKNOWN : TopAbs_OUT;
    }

    RetainedSpan EvaluateSpan(const SweepFrame& frame, const SweepSection& section,
        std::vector<NativeSolid>& solids, std::uint64_t machiningFaceId, double first, double last)
    {
        const double u = std::midpoint(first, last);
        struct Witness
        {
            double Depth;
            const SectionSegment* Limit;
        };
        std::vector<Witness> bounds{{0, nullptr}, {frame.DepthLimit, nullptr}};
        for (const auto& segment : section.Segments)
        {
            if (u <= segment.First.U || u >= segment.Last.U)
                continue;
            const auto depth = segment.DepthAt(u);
            if (depth >= -frame.Tolerance && depth <= frame.DepthLimit + frame.Tolerance)
                bounds.push_back({std::clamp(depth, 0.0, frame.DepthLimit), &segment});
        }
        std::sort(bounds.begin(), bounds.end(), [](const auto& a, const auto& b) { return a.Depth < b.Depth; });

        double unknownDepth = std::numeric_limits<double>::infinity();
        for (const auto& unknown : section.Unproven)
            if (last > unknown.First && first < unknown.Last)
                unknownDepth = std::min(unknownDepth, unknown.MinDepth);

        for (std::size_t i = 1; i < bounds.size(); ++i)
        {
            const double start = bounds[i - 1].Depth, end = bounds[i].Depth;
            if (end - start <= frame.Tolerance)
                continue;
            if (unknownDepth < end - frame.Tolerance)
                return {first, last, false, {}, {}, 0, "nonlinear-finished-boundary-intersects-swept-cell"};
            const auto state = RetainedSide(frame, section, solids, frame.Point(u, std::midpoint(start, end)));
            if (state == TopAbs_UNKNOWN)
                return {first, last, false, {}, {}, 0, "finished-material-side-is-undetermined"};
            if (state == TopAbs_OUT)
                continue;

            const SectionSegment* boundary = nullptr;
            for (std::size_t j = i; j > 0 && start - bounds[j - 1].Depth <= frame.Tolerance; --j)
            {
                const auto* limit = bounds[j - 1].Limit;
                if (limit && (!boundary || boundary->FaceId == machiningFaceId))
                    boundary = limit;
            }
            if (!boundary && start > frame.Tolerance)
                return {first, last, false, {}, {}, 0, "retained-material-boundary-is-undetermined"};
            const double a = boundary ? std::max(0.0, boundary->DepthAt(first)) : 0.0;
            const double b = boundary ? std::max(0.0, boundary->DepthAt(last)) : 0.0;
            return {first, last, true, a, b, boundary ? boundary->FaceId : 0, "first-retained-material-on-removal-side"};
        }
        return {first, last, true, {}, {}, 0, "clear-on-removal-side-through-requested-depth"};
    }

    std::vector<RetainedSpan> Pending(std::string reason)
    {
        return {{0, 1, false, {}, {}, 0, std::move(reason)}};
    }
}

struct RetainedMaterialQuery::Model
{
    std::vector<NativeSolid> Solids;
    Bnd_Box Bounds;
    std::map<std::uint64_t, TopoDS_Face> MachiningFaces;
    std::string PendingReason;
};

RetainedMaterialQuery::RetainedMaterialQuery(
    const iCAX::OpenCascade::SOpenCascadeBRepBuildResult& built,
    const SSectionFaceClassificationResult& classification)
    : m_Model(std::make_unique<Model>())
{
    try
    {
        auto solids = built.bOK ? LoadSolids(built) : std::nullopt;
        if (!solids)
        {
            m_Model->PendingReason = "finished-brep-does-not-certify-closed-material";
            return;
        }
        std::unordered_set<std::uint64_t> machiningIds(classification.MachiningFaceIds.begin(),
            classification.MachiningFaceIds.end());
        for (const auto& solid : *solids)
            for (const auto& [id, face, bounds] : solid.Faces)
            {
                m_Model->Bounds.Add(bounds);
                if (machiningIds.contains(id))
                {
                    const auto [found, inserted] = m_Model->MachiningFaces.try_emplace(id, face);
                    if (!inserted && found->second.Orientation() != face.Orientation())
                        found->second.Nullify();
                }
            }
        m_Model->Solids = std::move(*solids);
    }
    catch (const std::exception& error)
    {
        m_Model->PendingReason = error.what()[0] ? error.what() : "native-finished-material-load-failed";
    }
}

RetainedMaterialQuery::~RetainedMaterialQuery() = default;

std::vector<RetainedSpan> RetainedMaterialQuery::Resolve(std::uint64_t machiningFaceId,
    const gp_Pnt& start, const gp_Pnt& end, const gp_Dir& axis, double tolerance, double maximumDepth)
{
    if (!std::isfinite(tolerance) || tolerance <= 0 ||
        std::isnan(maximumDepth) || maximumDepth <= tolerance)
        return Pending("invalid-retained-material-query");
    if (!m_Model->PendingReason.empty())
        return Pending(m_Model->PendingReason);
    const auto machining = m_Model->MachiningFaces.find(machiningFaceId);
    if (machining == m_Model->MachiningFaces.end())
        return Pending("owning-machining-face-is-unavailable");
    if (machining->second.IsNull())
        return Pending("owning-machining-face-has-conflicting-material-sides");
    try
    {
        const BRepAdaptor_Surface support(machining->second, true);
        if (support.GetType() != GeomAbs_Plane)
            return Pending("owning-face-is-not-an-analytic-plane");

        const gp_Vec along(start, end), depth(axis);
        const gp_Vec crossAxis = along - depth * along.Dot(depth);
        const auto normal = OutwardPlaneNormal(machining->second);
        if (crossAxis.Magnitude() <= tolerance || std::abs(normal.Dot(depth)) > DirectionRoundoff ||
            std::abs(normal.Dot(along)) > DirectionRoundoff * along.Magnitude() ||
            support.Plane().Distance(start) > tolerance)
            return Pending("source-and-axis-do-not-certify-a-planar-sweep");

        SweepFrame frame{start, along, depth, crossAxis, normal,
            tolerance / crossAxis.Magnitude(), tolerance, maximumDepth};
        const auto bounds = Bounds(frame, m_Model->Bounds);
        frame.DepthLimit = std::min(maximumDepth, std::max(bounds.MaxDepth, 0.0) + 2 * tolerance);
        if (frame.UResolution >= 0.5)
            return Pending("source-span-is-below-geometric-resolution");
        const auto section = IntersectBoundary(frame, m_Model->Solids);
        const auto events = EventParameters(frame, section);
        std::vector<RetainedSpan> spans;
        for (std::size_t i = 1; i < events.size(); ++i)
            spans.push_back(EvaluateSpan(frame, section, m_Model->Solids, machiningFaceId, events[i - 1], events[i]));
        return spans;
    }
    catch (const std::exception& error)
    {
        return Pending(error.what()[0] ? error.what() : "native-retained-material-query-failed");
    }
}

bool MachiningFaceCoversStrip(const TopoDS_Face& face, const gp_Pnt& start, const gp_Pnt& end,
    const gp_Dir& axis, double firstDepth, double lastDepth, double tolerance)
{
    if (face.IsNull() || !std::isfinite(firstDepth) || !std::isfinite(lastDepth) ||
        !std::isfinite(tolerance) || tolerance <= 0 || firstDepth <= tolerance || lastDepth <= tolerance)
        return false;
    const BRepAdaptor_Surface surface(face, true);
    if (surface.GetType() != GeomAbs_Plane)
        return false;
    const auto plane = surface.Plane();
    if (plane.Distance(start) > tolerance || plane.Distance(end) > tolerance ||
        !axis.IsNormal(plane.Axis().Direction(), Precision::Angular()))
        return false;
    const gp_Vec normal(plane.Axis().Direction());
    const auto onPlane = [&](const gp_Pnt& point)
    {
        return point.Translated(normal * -gp_Vec(plane.Location(), point).Dot(normal));
    };
    const auto a = onPlane(start), b = onPlane(end);
    const gp_Vec ruling(axis);
    if (gp_Vec(a, b).Crossed(ruling).Magnitude() <= tolerance)
        return false;

    BRepBuilderAPI_MakePolygon boundary;
    boundary.Add(a);
    boundary.Add(b);
    boundary.Add(onPlane(b.Translated(ruling * lastDepth)));
    boundary.Add(onPlane(a.Translated(ruling * firstDepth)));
    boundary.Close();
    if (!boundary.IsDone())
        return false;
    BRepBuilderAPI_MakeFace patch(plane, boundary.Wire());
    if (!patch.IsDone() || !BRepCheck_Analyzer(patch.Face()).IsValid())
        return false;

    // A classified machining face that contains the complete continuous
    // stroke up to this wall's exit directly witnesses that the cut reaches it.
    TopTools_ListOfShape strokes, faces;
    strokes.Append(patch.Face());
    faces.Append(face.Oriented(TopAbs_FORWARD));
    BRepAlgoAPI_Cut difference;
    difference.SetArguments(strokes);
    difference.SetTools(faces);
    difference.SetNonDestructive(true);
    difference.Build();
    return difference.IsDone() && !difference.HasErrors() && !difference.HasWarnings() &&
        !difference.Shape().IsNull() && !TopExp_Explorer(difference.Shape(), TopAbs_FACE).More();
}
}
