#include "RulingToolpath.hxx"

#include "GeometryUtils.hxx"

#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_CompCurve.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomLProp_SLProps.hxx>
#include <GCPnts_UniformAbscissa.hxx>
#include <Precision.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace etp::detail
{
    namespace
    {
        enum class DirectionLawKind
        {
            Constant,
            ConeGenerator,
            PlaneSideInterpolation,
            BSplineU,
            BSplineV
        };

        struct DirectionLaw final
        {
            DirectionLawKind Kind = DirectionLawKind::Constant;
            gp_Vec First;
            gp_Vec Last;
        };

        struct PathCandidate final
        {
            Toolpath Path;
            double Score = -(std::numeric_limits<double>::max)();
        };

        std::vector<TopoDS_Edge> WireEdges(const TopoDS_Wire& wire)
        {
            std::vector<TopoDS_Edge> result;
            for (BRepTools_WireExplorer explorer(wire);
                 explorer.More(); explorer.Next())
            {
                result.push_back(TopoDS::Edge(explorer.Current()));
            }
            return result;
        }

        std::vector<gp_Pnt> SampleTrajectoryExactly(
            const TopoDS_Wire& wire,
            const std::size_t count,
            const double tolerance)
        {
            if (count < 2)
                return {};

            // ResamplePolyline is appropriate for classification polylines but
            // not for a drive curve: interpolating two circle samples creates
            // a chord whose XYZ points no longer lie on the cylindrical Face.
            // Evaluate the OCC composite curve at uniform arc-length stations
            // so every emitted tool position remains on the exact trajectory.
            BRepAdaptor_CompCurve curve(wire, true);
            const GCPnts_UniformAbscissa abscissa(
                curve, static_cast<int>(count), tolerance);
            if (!abscissa.IsDone()
                || abscissa.NbPoints() != static_cast<int>(count))
            {
                return {};
            }

            std::vector<gp_Pnt> result;
            result.reserve(count);
            for (int index = 1; index <= abscissa.NbPoints(); ++index)
                result.push_back(curve.Value(abscissa.Parameter(index)));
            return result;
        }

        bool IsTrajectoryEdge(
            const TopoDS_Edge& edge,
            const std::vector<TopoDS_Edge>& trajectoryEdges)
        {
            return std::any_of(
                trajectoryEdges.begin(), trajectoryEdges.end(),
                [&](const TopoDS_Edge& candidate)
                { return edge.IsSame(candidate); });
        }

        std::optional<gp_Vec> StraightEdgeDirectionFrom(
            const TopoDS_Edge& edge,
            const gp_Pnt& endpoint,
            const AnalyzerOptions& options)
        {
            const auto samples = SampleEdge(
                edge,
                options.MinimumEdgeSamples,
                options.SamplingDeflection,
                false);
            if (!IsGeometricallyStraight(
                    samples,
                    options.DistanceTolerance,
                    options.AngularToleranceRadians)
                || samples.size() < 2)
            {
                return std::nullopt;
            }

            const auto tolerance = options.DistanceTolerance * 5.0;
            if (endpoint.Distance(samples.front()) <= tolerance)
                return gp_Vec(endpoint, samples.back());
            if (endpoint.Distance(samples.back()) <= tolerance)
                return gp_Vec(endpoint, samples.front());
            return std::nullopt;
        }

        std::vector<DirectionLaw> PlaneDirectionLaws(
            const TopoDS_Face& face,
            const std::vector<TopoDS_Edge>& trajectoryEdges,
            const std::vector<gp_Pnt>& trajectorySamples,
            const AnalyzerOptions& options)
        {
            std::vector<gp_Vec> atStart;
            std::vector<gp_Vec> atFinish;
            if (trajectorySamples.size() < 2)
                return {};

            for (TopExp_Explorer explorer(face, TopAbs_EDGE);
                 explorer.More(); explorer.Next())
            {
                const auto edge = TopoDS::Edge(explorer.Current());
                if (IsTrajectoryEdge(edge, trajectoryEdges)
                    || BRep_Tool::Degenerated(edge)
                    || BRep_Tool::IsClosed(edge, face))
                {
                    continue;
                }

                if (const auto direction = StraightEdgeDirectionFrom(
                        edge, trajectorySamples.front(), options))
                {
                    atStart.push_back(*direction);
                }
                if (const auto direction = StraightEdgeDirectionFrom(
                        edge, trajectorySamples.back(), options))
                {
                    atFinish.push_back(*direction);
                }
            }

            std::vector<DirectionLaw> result;
            for (const auto& first : atStart)
            {
                for (const auto& last : atFinish)
                {
                    if (first.SquareMagnitude()
                            <= options.DistanceTolerance * options.DistanceTolerance
                        || last.SquareMagnitude()
                            <= options.DistanceTolerance * options.DistanceTolerance)
                    {
                        continue;
                    }
                    result.push_back({
                        DirectionLawKind::PlaneSideInterpolation,
                        first,
                        last });
                }
            }
            return result;
        }

        std::vector<DirectionLaw> DirectionLaws(
            const TopoDS_Face& face,
            const std::vector<TopoDS_Edge>& trajectoryEdges,
            const std::vector<gp_Pnt>& trajectorySamples,
            const AnalyzerOptions& options)
        {
            BRepAdaptor_Surface surface(face, true);
            switch (surface.GetType())
            {
            case GeomAbs_Cylinder:
                return {{
                    DirectionLawKind::Constant,
                    gp_Vec(surface.Cylinder().Axis().Direction()),
                    gp_Vec(surface.Cylinder().Axis().Direction()) }};

            case GeomAbs_SurfaceOfExtrusion:
                return {{
                    DirectionLawKind::Constant,
                    gp_Vec(surface.Direction()),
                    gp_Vec(surface.Direction()) }};

            case GeomAbs_Cone:
                return {{ DirectionLawKind::ConeGenerator, {}, {} }};

            case GeomAbs_Plane:
                // A plane has infinitely many ruling families.  The two
                // straight coedges adjacent to the selected trajectory remove
                // that ambiguity and define the first/last cutting rays.
                return PlaneDirectionLaws(
                    face, trajectoryEdges, trajectorySamples, options);

            case GeomAbs_BSplineSurface:
            case GeomAbs_BezierSurface:
            {
                std::vector<DirectionLaw> result;
                // Degree one is a structural, exact ruled direction.  Knot
                // spans are divided by SweepFaceDecomposeWork before this
                // function, so a degree-one direction is not treated as an
                // arbitrary polyline across several semantic patches.
                if (surface.UDegree() == 1)
                    result.push_back({ DirectionLawKind::BSplineU, {}, {} });
                if (surface.VDegree() == 1)
                    result.push_back({ DirectionLawKind::BSplineV, {}, {} });
                return result;
            }

            default:
                return {};
            }
        }

        std::optional<gp_Dir> DirectionAt(
            const DirectionLaw& law,
            BRepAdaptor_Surface& surface,
            const TopoDS_Face& face,
            const gp_Pnt& point,
            const double parameter,
            const AnalyzerOptions& options)
        {
            gp_Vec direction;
            switch (law.Kind)
            {
            case DirectionLawKind::Constant:
                direction = law.First;
                break;

            case DirectionLawKind::ConeGenerator:
                direction = gp_Vec(point, surface.Cone().Apex());
                break;

            case DirectionLawKind::PlaneSideInterpolation:
                direction = law.First * (1.0 - parameter) + law.Last * parameter;
                break;

            case DirectionLawKind::BSplineU:
            case DirectionLawKind::BSplineV:
            {
                const auto geometry = BRep_Tool::Surface(face);
                if (geometry.IsNull())
                    return std::nullopt;
                GeomAPI_ProjectPointOnSurf projector(point, geometry);
                if (projector.NbPoints() < 1)
                    return std::nullopt;
                Standard_Real u = 0.0;
                Standard_Real v = 0.0;
                projector.LowerDistanceParameters(u, v);
                gp_Pnt evaluated;
                gp_Vec du;
                gp_Vec dv;
                surface.D1(u, v, evaluated, du, dv);
                direction = law.Kind == DirectionLawKind::BSplineU ? du : dv;
                break;
            }
            }

            if (direction.SquareMagnitude()
                <= options.DistanceTolerance * options.DistanceTolerance)
            {
                return std::nullopt;
            }
            return gp_Dir(direction);
        }

        double FaceSearchExtent(const TopoDS_Face& face)
        {
            Bnd_Box box;
            BRepBndLib::Add(face, box);
            if (box.IsVoid())
                return 1.0;

            Standard_Real xmin = 0.0;
            Standard_Real ymin = 0.0;
            Standard_Real zmin = 0.0;
            Standard_Real xmax = 0.0;
            Standard_Real ymax = 0.0;
            Standard_Real zmax = 0.0;
            box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
            return std::max(
                gp_Pnt(xmin, ymin, zmin).Distance(gp_Pnt(xmax, ymax, zmax)) * 4.0,
                1.0);
        }

        std::optional<gp_Pnt> FindExitPoint(
            const gp_Pnt& entry,
            const gp_Dir& direction,
            const double searchExtent,
            const std::vector<TopoDS_Edge>& boundary,
            const AnalyzerOptions& options)
        {
            const gp_Vec axis(direction);
            const auto start = entry.Translated(axis * -searchExtent);
            const auto finish = entry.Translated(axis * searchExtent);
            const auto probe = BRepBuilderAPI_MakeEdge(start, finish).Edge();
            const auto intersectionTolerance = std::max(
                options.DistanceTolerance * 10.0,
                Precision::Confusion() * 10.0);

            double bestParameter = (std::numeric_limits<double>::max)();
            std::optional<gp_Pnt> result;
            const auto consider = [&](const gp_Pnt& point)
            {
                const gp_Vec offset(entry, point);
                const auto along = offset.Dot(axis);
                const auto transverse = (offset - axis * along).Magnitude();
                if (along <= options.DistanceTolerance
                    || along >= bestParameter
                    || transverse > intersectionTolerance)
                {
                    return;
                }
                bestParameter = along;
                result = point;
            };

            for (const auto& edge : boundary)
            {
                BRepExtrema_DistShapeShape extrema(probe, edge);
                extrema.Perform();
                if (extrema.IsDone() && extrema.Value() <= intersectionTolerance)
                {
                    for (Standard_Integer index = 1;
                         index <= extrema.NbSolution(); ++index)
                    {
                        consider(extrema.PointOnShape2(index));
                    }
                }

                // When a side coedge is itself a ruling, distance extrema are
                // non-unique and OCCT may report only the shared entry point.
                // Its far vertex is the real exit at the first/last path pose.
                const auto firstVertex = TopExp::FirstVertex(edge, true);
                const auto lastVertex = TopExp::LastVertex(edge, true);
                if (!firstVertex.IsNull())
                    consider(BRep_Tool::Pnt(firstVertex));
                if (!lastVertex.IsNull())
                    consider(BRep_Tool::Pnt(lastVertex));
            }
            return result;
        }

        bool BoundaryContainsPoint(
            const gp_Pnt& point,
            const std::vector<TopoDS_Edge>& boundary,
            const AnalyzerOptions& options)
        {
            const auto tolerance = options.DistanceTolerance * 5.0;
            return std::any_of(
                boundary.begin(), boundary.end(),
                [&](const TopoDS_Edge& edge)
                {
                    const auto first = TopExp::FirstVertex(edge, true);
                    const auto last = TopExp::LastVertex(edge, true);
                    return (!first.IsNull()
                            && BRep_Tool::Pnt(first).Distance(point) <= tolerance)
                        || (!last.IsNull()
                            && BRep_Tool::Pnt(last).Distance(point) <= tolerance);
                });
        }

        bool IsInsideTrimmedFace(
            const TopoDS_Face& face,
            const gp_Pnt& point,
            const AnalyzerOptions& options)
        {
            const BRepClass_FaceClassifier classifier(
                face, point, options.DistanceTolerance * 5.0, true);
            return classifier.State() == TopAbs_IN
                || classifier.State() == TopAbs_ON;
        }

        std::optional<gp_Dir> SurfaceNormalAt(
            const TopoDS_Face& face,
            const gp_Pnt& point,
            const AnalyzerOptions& options)
        {
            const auto geometry = BRep_Tool::Surface(face);
            if (geometry.IsNull())
                return std::nullopt;

            GeomAPI_ProjectPointOnSurf projector(point, geometry);
            if (projector.NbPoints() < 1
                || projector.LowerDistance()
                    > std::max(options.DistanceTolerance * 10.0,
                        options.SamplingDeflection * 2.0))
            {
                return std::nullopt;
            }

            Standard_Real u = 0.0;
            Standard_Real v = 0.0;
            projector.LowerDistanceParameters(u, v);
            GeomLProp_SLProps properties(
                geometry, u, v, 1, options.DistanceTolerance);
            if (!properties.IsNormalDefined())
                return std::nullopt;
            auto normal = properties.Normal();
            if (face.Orientation() == TopAbs_REVERSED)
                normal.Reverse();
            return normal;
        }

        bool TrajectoryLiesOnBoundary(
            const TopoDS_Face& face,
            const std::vector<gp_Pnt>& samples,
            const AnalyzerOptions& options)
        {
            return !samples.empty() && std::all_of(
                samples.begin(), samples.end(),
                [&](const gp_Pnt& point)
                {
                    const BRepClass_FaceClassifier classifier(
                        face, point, options.DistanceTolerance * 5.0, true);
                    return classifier.State() == TopAbs_ON;
                });
        }

        std::optional<PathCandidate> EvaluateLaw(
            const TopoDS_Face& face,
            const DirectionLaw& law,
            const int sign,
            const std::vector<gp_Pnt>& trajectory,
            const bool closed,
            const std::vector<TopoDS_Edge>& boundary,
            const SectionRegion& section,
            const AnalyzerOptions& options)
        {
            BRepAdaptor_Surface surface(face, true);
            const auto searchExtent = FaceSearchExtent(face);
            std::vector<ToolPose> poses;
            poses.reserve(trajectory.size());
            double materialScore = 0.0;

            for (std::size_t index = 0; index < trajectory.size(); ++index)
            {
                const auto parameter = trajectory.size() <= 1
                    ? 0.0
                    : static_cast<double>(index)
                        / static_cast<double>(trajectory.size() - 1);
                auto direction = DirectionAt(
                    law, surface, face, trajectory[index], parameter, options);
                if (!direction)
                    return std::nullopt;
                if (sign < 0)
                    direction->Reverse();

                const auto feed = EstimateFeedDirection(
                    trajectory, index, section.Frame().Direction());
                // A drive rail must cross the ruling family.  If it follows a
                // ruling (the axial-line-on-cylinder case), all beam positions
                // describe the same infinite line and sweep zero area.
                const auto transverse = gp_Vec(feed).Crossed(gp_Vec(*direction)).Magnitude();
                if (transverse <= std::sin(options.AngularToleranceRadians))
                    return std::nullopt;

                const auto exit = FindExitPoint(
                    trajectory[index], *direction, searchExtent, boundary, options);
                if (!exit)
                {
                    const bool pathEndpoint = index == 0
                        || index + 1 == trajectory.size();
                    if (!pathEndpoint
                        || !BoundaryContainsPoint(
                            trajectory[index], boundary, options))
                    {
                        return std::nullopt;
                    }

                    // Two trim rails may meet at a pinched endpoint.  The
                    // material depth is exactly zero there, so P->Q cannot
                    // define IJK.  The already-recognized surface ruling is the
                    // correct limiting tool direction; keep it and let the
                    // caller classify the pair of zero-depth ends as
                    // DoublePinched.
                    ToolPose pose;
                    pose.Position = trajectory[index];
                    pose.ToolDirection = *direction;
                    pose.FeedDirection = feed;
                    pose.SurfaceNormal = SurfaceNormalAt(
                            face, trajectory[index], options)
                        .value_or(gp_Dir(
                            gp_Vec(feed).Crossed(gp_Vec(*direction))));
                    pose.PathParameter = parameter;
                    pose.MaterialDepth = 0.0;
                    poses.push_back(pose);
                    continue;
                }

                const gp_Vec ruling(trajectory[index], *exit);
                if (ruling.SquareMagnitude()
                    <= options.DistanceTolerance * options.DistanceTolerance)
                {
                    return std::nullopt;
                }

                // Checking several interior points proves coverage of the
                // trimmed Face; merely fitting the infinite supporting surface
                // would incorrectly accept rays crossing a concavity or hole.
                for (const double ratio : { 0.25, 0.5, 0.75 })
                {
                    const auto interior = trajectory[index].Translated(ruling * ratio);
                    if (!IsInsideTrimmedFace(face, interior, options))
                        return std::nullopt;
                    if (section.IsValid())
                    {
                        const auto state = section.Classify(interior).State;
                        if (state == SectionPointState::InVoid)
                            return std::nullopt;
                        materialScore += state == SectionPointState::InMaterial ? 1.0 : 0.25;
                    }
                }

                ToolPose pose;
                pose.Position = trajectory[index];
                pose.ToolDirection = gp_Dir(ruling);
                pose.FeedDirection = feed;
                pose.SurfaceNormal = SurfaceNormalAt(
                        face,
                        trajectory[index].Translated(ruling * 0.5),
                        options)
                    .value_or(gp_Dir(gp_Vec(feed).Crossed(ruling)));
                pose.PathParameter = parameter;
                pose.MaterialDepth = ruling.Magnitude();
                poses.push_back(pose);
            }

            PathCandidate candidate;
            candidate.Path.Id = "face-toolpath";
            candidate.Path.SweepUnitId = "face-sweep";
            candidate.Path.Closed = closed;
            candidate.Path.Poses = std::move(poses);
            candidate.Score = materialScore;
            return candidate;
        }
    }

    bool TryBuildRulingToolpath(
        const TopoDS_Face& face,
        const TopoDS_Wire& cuttingTrajectory,
        const SectionRegion& section,
        const AnalyzerOptions& options,
        Toolpath& outToolpath)
    {
        // Never leak a partially generated path through the bool API.
        outToolpath = {};
        if (face.IsNull() || cuttingTrajectory.IsNull())
            return false;

        std::size_t wireCount = 0;
        for (TopExp_Explorer explorer(face, TopAbs_WIRE);
             explorer.More(); explorer.Next())
        {
            ++wireCount;
        }
        // Inner wires create more than one material interval on at least some
        // rulings.  SweepFaceDecomposeWork must turn them into elementary Faces
        // before this single-toolpath function is called.
        if (wireCount != 1)
            return false;

        auto rawTrajectory = SampleTrajectoryExactly(
            cuttingTrajectory,
            std::max<std::size_t>(options.MinimumEdgeSamples, 17),
            options.DistanceTolerance);
        if (rawTrajectory.size() < 2
            || !TrajectoryLiesOnBoundary(face, rawTrajectory, options))
        {
            return false;
        }

        // Do not trust the inherited TopoDS closed flag on a temporary
        // one-edge wire; MakeWire can preserve a source edge flag that does not
        // describe geometric endpoint closure.  Toolpath closure is geometric.
        const bool closed = rawTrajectory.front().Distance(rawTrajectory.back())
            <= options.DistanceTolerance;
        auto trajectory = SampleTrajectoryExactly(
            cuttingTrajectory,
            options.ToolpathSamples,
            options.DistanceTolerance);
        if (trajectory.size() != options.ToolpathSamples)
            return false;

        const auto trajectoryEdges = WireEdges(cuttingTrajectory);
        std::vector<TopoDS_Edge> boundary;
        for (TopExp_Explorer explorer(face, TopAbs_EDGE);
             explorer.More(); explorer.Next())
        {
            const auto edge = TopoDS::Edge(explorer.Current());
            if (BRep_Tool::Degenerated(edge)
                || BRep_Tool::IsClosed(edge, face)
                || IsTrajectoryEdge(edge, trajectoryEdges))
            {
                continue;
            }
            boundary.push_back(edge);
        }
        if (boundary.empty())
            return false;

        const auto laws = DirectionLaws(
            face, trajectoryEdges, rawTrajectory, options);
        std::optional<PathCandidate> best;
        for (const auto& law : laws)
        {
            for (const int sign : { 1, -1 })
            {
                auto candidate = EvaluateLaw(
                    face,
                    law,
                    sign,
                    trajectory,
                    closed,
                    boundary,
                    section,
                    options);
                if (candidate && (!best || candidate->Score > best->Score))
                    best = std::move(candidate);
            }
        }

        if (!best)
            return false;
        outToolpath = std::move(best->Path);
        return true;
    }
}
