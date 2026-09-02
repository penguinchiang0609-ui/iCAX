#include "PipelineInternals.hxx"

#include "../GeometryUtils.hxx"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <gp.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

namespace etp::pipeline_detail
{
    namespace
    {
        struct AxisCandidate final
        {
            gp_Dir Direction;
            double SeedWeight = 0.0;
            double Score = 0.0;
        };

        gp_Dir CanonicalDirection(gp_Dir direction)
        {
            constexpr double tolerance = 1.0e-12;
            if (direction.X() < -tolerance
                || (std::abs(direction.X()) <= tolerance && direction.Y() < -tolerance)
                || (std::abs(direction.X()) <= tolerance
                    && std::abs(direction.Y()) <= tolerance && direction.Z() < 0.0))
            {
                direction.Reverse();
            }
            return direction;
        }

        void AddCandidate(
            std::vector<AxisCandidate>& candidates,
            const gp_Dir& rawDirection,
            const double weight,
            const double angularTolerance)
        {
            const auto direction = CanonicalDirection(rawDirection);
            const auto threshold = std::cos(std::max(angularTolerance * 5.0, 1.0e-3));
            for (auto& candidate : candidates)
            {
                if (detail::DirectionAbsDot(candidate.Direction, direction) >= threshold)
                {
                    candidate.SeedWeight += weight;
                    return;
                }
            }
            candidates.push_back({ direction, weight, 0.0 });
        }

        std::size_t WireCount(const TopoDS_Face& face)
        {
            std::size_t count = 0;
            for (TopExp_Explorer explorer(face, TopAbs_WIRE); explorer.More(); explorer.Next())
                ++count;
            return count;
        }

        std::optional<gp_Dir> LinearExtrusionDirection(const TopoDS_Face& face)
        {
            const auto surface = BRep_Tool::Surface(face);
            const auto extrusion = occ::down_cast<Geom_SurfaceOfLinearExtrusion>(surface);
            if (extrusion.IsNull())
                return std::nullopt;
            return extrusion->Direction();
        }

        bool EquivalentProjectedEdges(
            const TopoDS_Edge& left,
            const TopoDS_Edge& right,
            const AnalyzerOptions& options)
        {
            const auto leftSamples = detail::ResamplePolyline(
                detail::SampleEdge(left, 9, options.SamplingDeflection, false), 9);
            const auto rightSamples = detail::ResamplePolyline(
                detail::SampleEdge(right, 9, options.SamplingDeflection, false), 9);
            if (leftSamples.size() != rightSamples.size() || leftSamples.empty())
                return false;

            bool same = true;
            bool reversed = true;
            for (std::size_t index = 0; index < leftSamples.size(); ++index)
            {
                same = same && leftSamples[index].Distance(rightSamples[index])
                    <= options.DistanceTolerance;
                reversed = reversed && leftSamples[index].Distance(
                    rightSamples[rightSamples.size() - 1 - index])
                    <= options.DistanceTolerance;
            }
            return same || reversed;
        }

        std::vector<TopoDS_Wire> RecoverFromAxialFaceEdges(
            const std::vector<TopoDS_Face>& axialFaces,
            const gp_Ax3& frame,
            const AnalyzerOptions& options)
        {
            std::vector<TopoDS_Edge> projectedEdges;
            for (const auto& face : axialFaces)
            {
                for (TopExp_Explorer explorer(face, TopAbs_EDGE); explorer.More(); explorer.Next())
                {
                    const auto edge = TopoDS::Edge(explorer.Current());
                    const auto samples = detail::SampleEdge(
                        edge, options.MinimumEdgeSamples, options.SamplingDeflection, false);
                    if (samples.size() < 2)
                        continue;

                    auto minimumStation = (std::numeric_limits<double>::max)();
                    auto maximumStation = -(std::numeric_limits<double>::max)();
                    double stationSum = 0.0;
                    for (const auto& point : samples)
                    {
                        const auto station = detail::AxialStation(point, frame);
                        minimumStation = std::min(minimumStation, station);
                        maximumStation = std::max(maximumStation, station);
                        stationSum += station;
                    }
                    if (maximumStation - minimumStation > options.DistanceTolerance * 5.0)
                        continue;

                    const auto station = stationSum / static_cast<double>(samples.size());
                    gp_Trsf projection;
                    projection.SetTranslation(gp_Vec(frame.Direction()) * -station);
                    const auto projected = TopoDS::Edge(
                        BRepBuilderAPI_Transform(edge, projection, true).Shape());
                    const auto projectedSamples = detail::SampleEdge(
                        projected, options.MinimumEdgeSamples,
                        options.SamplingDeflection, false);
                    if (projectedSamples.size() < 2)
                        continue;
                    const auto sectionLength = detail::EdgeLength(projected);
                    if (sectionLength <= options.DistanceTolerance)
                        continue;

                    const auto duplicate = std::any_of(
                        projectedEdges.begin(), projectedEdges.end(),
                        [&](const TopoDS_Edge& existing)
                        { return EquivalentProjectedEdges(existing, projected, options); });
                    if (!duplicate)
                        projectedEdges.push_back(projected);
                }
            }

            if (projectedEdges.empty())
                return {};

            std::vector<TopoDS_Wire> result;
            std::vector<bool> used(projectedEdges.size(), false);
            for (std::size_t seed = 0; seed < projectedEdges.size(); ++seed)
            {
                if (used[seed])
                    continue;

                BRepBuilderAPI_MakeWire wireBuilder;
                auto seedSamples = detail::SampleEdge(
                    projectedEdges[seed], 3, options.SamplingDeflection, true);
                if (seedSamples.size() < 2)
                    continue;
                const auto loopStart = seedSamples.front();
                auto currentEnd = seedSamples.back();
                wireBuilder.Add(projectedEdges[seed]);
                used[seed] = true;

                for (std::size_t step = 1; step <= projectedEdges.size(); ++step)
                {
                    if (currentEnd.Distance(loopStart) <= options.DistanceTolerance)
                        break;

                    std::optional<std::size_t> next;
                    bool reverse = false;
                    for (std::size_t index = 0; index < projectedEdges.size(); ++index)
                    {
                        if (used[index])
                            continue;
                        const auto samples = detail::SampleEdge(
                            projectedEdges[index], 3,
                            options.SamplingDeflection, true);
                        if (samples.size() < 2)
                            continue;
                        if (currentEnd.Distance(samples.front()) <= options.DistanceTolerance)
                        {
                            next = index;
                            break;
                        }
                        if (currentEnd.Distance(samples.back()) <= options.DistanceTolerance)
                        {
                            next = index;
                            reverse = true;
                            break;
                        }
                    }
                    if (!next)
                        break;

                    auto edge = projectedEdges[*next];
                    if (reverse)
                        edge.Reverse();
                    wireBuilder.Add(edge);
                    const auto samples = detail::SampleEdge(
                        edge, 3, options.SamplingDeflection, true);
                    currentEnd = samples.back();
                    used[*next] = true;
                }

                if (wireBuilder.IsDone()
                    && currentEnd.Distance(loopStart) <= options.DistanceTolerance)
                {
                    result.push_back(wireBuilder.Wire());
                }
            }
            return result;
        }

        std::vector<TopoDS_Wire> RecoverFromBestCap(
            const TopoDS_Shape& shape,
            const gp_Dir& axis,
            const AnalyzerOptions& options)
        {
            std::optional<TopoDS_Face> bestCap;
            double bestScore = -1.0;
            for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next())
            {
                const auto face = TopoDS::Face(explorer.Current());
                if (!IsCapFace(face, axis, options.AngularToleranceRadians))
                    continue;
                const auto loops = WireCount(face);
                const auto score = detail::FaceArea(face) * (loops > 1 ? 4.0 : 1.0);
                if (score > bestScore)
                {
                    bestCap = face;
                    bestScore = score;
                }
            }

            std::vector<TopoDS_Wire> result;
            if (bestCap)
            {
                for (TopExp_Explorer explorer(*bestCap, TopAbs_WIRE);
                     explorer.More(); explorer.Next())
                {
                    result.push_back(TopoDS::Wire(explorer.Current()));
                }
            }
            return result;
        }
    }

    bool IsCapFace(
        const TopoDS_Face& face,
        const gp_Dir& axis,
        const double angularTolerance)
    {
        BRepAdaptor_Surface surface(face, true);
        return surface.GetType() == GeomAbs_Plane
            && detail::DirectionAbsDot(surface.Plane().Axis().Direction(), axis)
                >= std::cos(angularTolerance * 5.0);
    }

    bool IsAxisInvariantSurface(
        const TopoDS_Face& face,
        const gp_Dir& axis,
        const double angularTolerance)
    {
        BRepAdaptor_Surface surface(face, true);
        const auto parallelThreshold = std::cos(angularTolerance * 5.0);
        const auto perpendicularThreshold = std::sin(angularTolerance * 5.0);
        switch (surface.GetType())
        {
        case GeomAbs_Plane:
            return std::abs(surface.Plane().Axis().Direction().Dot(axis))
                <= perpendicularThreshold;
        case GeomAbs_Cylinder:
            return detail::DirectionAbsDot(surface.Cylinder().Axis().Direction(), axis)
                >= parallelThreshold;
        case GeomAbs_Cone:
            return detail::DirectionAbsDot(surface.Cone().Axis().Direction(), axis)
                >= parallelThreshold;
        case GeomAbs_SurfaceOfExtrusion:
            if (const auto direction = LinearExtrusionDirection(face))
                return detail::DirectionAbsDot(*direction, axis) >= parallelThreshold;
            return false;
        default:
            return false;
        }
    }

    gp_Dir RecognizeAxis(
        const TopoDS_Shape& shape,
        const AnalyzerOptions& options,
        std::vector<Diagnostic>& diagnostics)
    {
        std::vector<AxisCandidate> candidates;
        for (TopExp_Explorer explorer(shape, TopAbs_EDGE); explorer.More(); explorer.Next())
        {
            const auto edge = TopoDS::Edge(explorer.Current());
            BRepAdaptor_Curve curve(edge);
            if (curve.GetType() == GeomAbs_Line)
            {
                AddCandidate(candidates, curve.Line().Direction(),
                    std::max(detail::EdgeLength(edge), options.DistanceTolerance),
                    options.AngularToleranceRadians);
            }
        }

        for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next())
        {
            const auto face = TopoDS::Face(explorer.Current());
            const auto area = std::max(detail::FaceArea(face), options.DistanceTolerance);
            BRepAdaptor_Surface surface(face, true);
            switch (surface.GetType())
            {
            case GeomAbs_Plane:
                AddCandidate(candidates, surface.Plane().Axis().Direction(),
                    std::sqrt(area), options.AngularToleranceRadians);
                break;
            case GeomAbs_Cylinder:
                AddCandidate(candidates, surface.Cylinder().Axis().Direction(),
                    area, options.AngularToleranceRadians);
                break;
            case GeomAbs_Cone:
                AddCandidate(candidates, surface.Cone().Axis().Direction(),
                    area, options.AngularToleranceRadians);
                break;
            case GeomAbs_SurfaceOfExtrusion:
                if (const auto direction = LinearExtrusionDirection(face))
                {
                    AddCandidate(candidates, *direction,
                        area * 2.0, options.AngularToleranceRadians);
                }
                break;
            default:
                break;
            }
        }
        if (candidates.empty())
            throw std::runtime_error("no extrusion-axis candidate can be obtained from the shape");

        for (auto& candidate : candidates)
        {
            candidate.Score = candidate.SeedWeight;
            for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next())
            {
                const auto face = TopoDS::Face(explorer.Current());
                const auto area = detail::FaceArea(face);
                if (IsAxisInvariantSurface(
                    face, candidate.Direction, options.AngularToleranceRadians))
                {
                    candidate.Score += area;
                }
                if (IsCapFace(face, candidate.Direction, options.AngularToleranceRadians))
                {
                    candidate.Score += area * (WireCount(face) > 1 ? 6.0 : 0.15);
                }
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right)
        { return left.Score > right.Score; });
        if (candidates.size() > 1 && candidates[1].Score >= candidates[0].Score * 0.95)
        {
            diagnostics.push_back({
                DiagnosticSeverity::Warning,
                "axis.ambiguous",
                "The two best extrusion-axis candidates have nearly equal scores; the highest score was selected.",
                std::nullopt });
        }
        return candidates.front().Direction;
    }

    gp_Ax3 BuildSourceFrame(
        const TopoDS_Shape& shape,
        const gp_Dir& axis,
        const double tolerance)
    {
        TopTools_IndexedMapOfShape vertices;
        TopExp::MapShapes(shape, TopAbs_VERTEX, vertices);
        if (vertices.IsEmpty())
            throw std::runtime_error("the shape contains no vertex");

        gp_XYZ sum(0.0, 0.0, 0.0);
        for (Standard_Integer index = 1; index <= vertices.Extent(); ++index)
            sum += BRep_Tool::Pnt(TopoDS::Vertex(vertices(index))).XYZ();
        const gp_Pnt origin(sum / static_cast<double>(vertices.Extent()));

        gp_Dir reference = gp::DX();
        if (detail::DirectionAbsDot(reference, axis) > 0.9)
            reference = gp::DY();
        if (detail::DirectionAbsDot(reference, axis) > 0.9)
            reference = gp::DZ();
        const auto projected = gp_Vec(reference)
            - gp_Vec(axis) * gp_Vec(reference).Dot(gp_Vec(axis));
        if (projected.SquareMagnitude() <= tolerance * tolerance)
            throw std::runtime_error("failed to construct a section frame");
        return gp_Ax3(origin, axis, gp_Dir(projected));
    }

    std::pair<double, double> AxialRange(
        const TopoDS_Shape& shape,
        const gp_Ax3& frame)
    {
        auto first = (std::numeric_limits<double>::max)();
        auto last = -(std::numeric_limits<double>::max)();
        for (TopExp_Explorer explorer(shape, TopAbs_VERTEX); explorer.More(); explorer.Next())
        {
            const auto station = detail::AxialStation(
                BRep_Tool::Pnt(TopoDS::Vertex(explorer.Current())), frame);
            first = std::min(first, station);
            last = std::max(last, station);
        }
        return first <= last ? std::pair(first, last) : std::pair(0.0, 0.0);
    }

    std::vector<TopoDS_Wire> RecoverSectionWires(
        const TopoDS_Shape& shape,
        const std::vector<TopoDS_Face>& axialFaces,
        const gp_Ax3& frame,
        const AnalyzerOptions& options,
        std::vector<Diagnostic>& diagnostics)
    {
        auto result = RecoverFromAxialFaceEdges(axialFaces, frame, options);
        if (!result.empty())
            return result;

        result = RecoverFromBestCap(shape, frame.Direction(), options);
        if (!result.empty())
        {
            diagnostics.push_back({
                DiagnosticSeverity::Info,
                "section.cap-fallback",
                "Axial-face edge projection did not close; section wires were recovered from the best complete cap.",
                std::nullopt });
            return result;
        }

        diagnostics.push_back({
            DiagnosticSeverity::Error,
            "section.not-closed",
            "Neither projected boundaries of axis-parallel faces nor a complete cap produced a closed section.",
            std::nullopt });
        return result;
    }

    std::vector<gp_Pnt2d> CollectSectionBreakPoints(
        const std::vector<TopoDS_Wire>& wires,
        const gp_Ax3& frame,
        const double tolerance)
    {
        std::vector<gp_Pnt2d> result;
        for (const auto& wire : wires)
        {
            for (BRepTools_WireExplorer explorer(wire); explorer.More(); explorer.Next())
            {
                const auto samples = detail::SampleEdge(
                    TopoDS::Edge(explorer.Current()), 3, tolerance, true);
                if (samples.empty())
                    continue;
                const auto point = detail::ToSection2d(samples.front(), frame);
                const auto duplicate = std::any_of(
                    result.begin(), result.end(), [&](const gp_Pnt2d& existing)
                    { return existing.Distance(point) <= tolerance; });
                if (!duplicate)
                    result.push_back(point);
            }
        }
        return result;
    }

    bool EveryBoundaryPointOnSection(
        const TopoDS_Face& face,
        const detail::SectionRegion& section,
        const AnalyzerOptions& options)
    {
        bool sampled = false;
        for (TopExp_Explorer explorer(face, TopAbs_EDGE); explorer.More(); explorer.Next())
        {
            const auto points = detail::SampleEdge(
                TopoDS::Edge(explorer.Current()),
                options.MinimumEdgeSamples,
                options.SamplingDeflection,
                false);
            for (const auto& point : points)
            {
                sampled = true;
                if (section.Classify(point).State != SectionPointState::OnBoundary)
                    return false;
            }
        }
        return sampled;
    }

    TopoDS_Shape MakeFaceGroup(
        const std::vector<FacePatch>& patches,
        const std::vector<std::size_t>& patchIds)
    {
        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        for (const auto patchId : patchIds)
        {
            const auto found = std::find_if(patches.begin(), patches.end(),
                [&](const FacePatch& patch) { return patch.Id == patchId; });
            if (found != patches.end())
                builder.Add(compound, found->Face);
        }
        return compound;
    }
}
