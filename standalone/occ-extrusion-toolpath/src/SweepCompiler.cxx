#include "SweepCompiler.hxx"

#include "GeometryUtils.hxx"
#include "RulingToolpath.hxx"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <Precision.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <optional>
#include <sstream>
#include <utility>

namespace etp::detail
{
    namespace
    {
        struct BoundaryEdge final
        {
            CoedgeSegment Segment;
            SectionHit Start;
            SectionHit End;
            std::vector<SectionHit> Hits;
        };

        std::vector<gp_Pnt> ConcatenateSamples(
            const std::vector<BoundaryEdge>& boundary,
            const std::vector<std::size_t>& indices,
            const bool reverse)
        {
            std::vector<gp_Pnt> result;
            for (const auto index : indices)
            {
                auto samples = boundary[index].Segment.Samples;
                if (!result.empty() && !samples.empty()
                    && result.back().Distance(samples.front()) <= Precision::Confusion())
                {
                    samples.erase(samples.begin());
                }
                result.insert(result.end(), samples.begin(), samples.end());
            }
            if (reverse)
                std::reverse(result.begin(), result.end());
            return result;
        }

        Rail MakeCurveRail(
            const std::vector<BoundaryEdge>& boundary,
            const std::vector<std::size_t>& indices,
            const bool reverse)
        {
            Rail rail;
            rail.Kind = RailKind::Curve;
            rail.Samples = ConcatenateSamples(boundary, indices, reverse);
            rail.Segments.reserve(indices.size());
            if (!reverse)
            {
                for (const auto index : indices)
                    rail.Segments.push_back(boundary[index].Segment);
            }
            else
            {
                for (auto it = indices.rbegin(); it != indices.rend(); ++it)
                {
                    auto segment = boundary[*it].Segment;
                    segment.Reversed = !segment.Reversed;
                    std::reverse(segment.Samples.begin(), segment.Samples.end());
                    rail.Segments.push_back(std::move(segment));
                }
            }
            return rail;
        }

        Rail MakePointRail(const gp_Pnt& point)
        {
            Rail rail;
            rail.Kind = RailKind::Point;
            rail.Samples.push_back(point);
            return rail;
        }

        SweepModel ClassifyModel(
            const TopoDS_Face& face,
            const double fitError,
            const double tolerance)
        {
            BRepAdaptor_Surface surface(face, true);
            switch (surface.GetType())
            {
            case GeomAbs_Cylinder: return SweepModel::AnalyticCylinder;
            case GeomAbs_Cone: return SweepModel::AnalyticCone;
            default:
                return fitError <= tolerance
                    ? SweepModel::LinearRuling
                    : SweepModel::ApproximateLinearRuling;
            }
        }

        BoundaryEdge ClassifyBoundaryEdge(
            const TopoDS_Edge& edge,
            const TopoDS_Face& face,
            const SectionRegion& section,
            const AnalyzerOptions& options,
            const TopTools_IndexedDataMapOfShapeListOfShape& edgeFaces)
        {
            BoundaryEdge result;
            result.Segment.Edge = edge;
            result.Segment.Reversed = edge.Orientation() == TopAbs_REVERSED;
            BRepAdaptor_Curve curve(edge);
            result.Segment.First = curve.FirstParameter();
            result.Segment.Last = curve.LastParameter();
            result.Segment.Samples = SampleEdge(
                edge, options.MinimumEdgeSamples, options.SamplingDeflection, true);
            result.Segment.GeometricallyStraight = IsGeometricallyStraight(
                result.Segment.Samples,
                options.DistanceTolerance,
                options.AngularToleranceRadians);

            if (result.Segment.Samples.empty())
                return result;
            result.Hits.reserve(result.Segment.Samples.size());
            for (const auto& point : result.Segment.Samples)
                result.Hits.push_back(section.Classify(point));
            result.Start = result.Hits.front();
            result.End = result.Hits.back();
            result.Segment.StartSkin = result.Start.Boundary;
            result.Segment.EndSkin = result.End.Boundary;

            const auto allOnSkin = std::all_of(
                result.Hits.begin(), result.Hits.end(), [](const SectionHit& hit)
                { return hit.State == SectionPointState::OnBoundary; });
            const auto anyMaterial = std::any_of(
                result.Hits.begin(), result.Hits.end(), [](const SectionHit& hit)
                { return hit.State == SectionPointState::InMaterial; });
            const auto anyVoid = std::any_of(
                result.Hits.begin(), result.Hits.end(), [](const SectionHit& hit)
                { return hit.State == SectionPointState::InVoid; });
            const auto hasSkinEnd = result.Start.State == SectionPointState::OnBoundary
                || result.End.State == SectionPointState::OnBoundary;

            if (BRep_Tool::IsClosed(edge, face))
            {
                result.Segment.Role = SegmentRole::SurfaceSeam;
            }
            else if (edgeFaces.Contains(edge) && edgeFaces.FindFromKey(edge).Extent() > 1)
            {
                result.Segment.Role = SegmentRole::FeatureInternal;
            }
            else if (allOnSkin)
            {
                result.Segment.Role = SegmentRole::OnSkin;
            }
            else if (result.Segment.GeometricallyStraight
                && hasSkinEnd && anyMaterial && !anyVoid)
            {
                result.Segment.Role = SegmentRole::CrossMaterial;
            }
            return result;
        }

        std::vector<BoundaryEdge> ReadWireBoundary(
            const TopoDS_Wire& wire,
            const TopoDS_Face& face,
            const SectionRegion& section,
            const AnalyzerOptions& options,
            const TopTools_IndexedDataMapOfShapeListOfShape& edgeFaces)
        {
            std::vector<BoundaryEdge> result;
            for (BRepTools_WireExplorer explorer(wire, face); explorer.More(); explorer.Next())
            {
                result.push_back(ClassifyBoundaryEdge(
                    TopoDS::Edge(explorer.Current()), face, section, options, edgeFaces));
            }
            return result;
        }

        std::optional<SweepUnit> CompileFace(
            const TopoDS_Face& face,
            const std::size_t faceId,
            const std::size_t featureIndex,
            const SectionRegion& section,
            const AnalyzerOptions& options,
            const TopTools_IndexedDataMapOfShapeListOfShape& edgeFaces)
        {
            for (TopExp_Explorer wireExplorer(face, TopAbs_WIRE);
                 wireExplorer.More(); wireExplorer.Next())
            {
                const auto wire = TopoDS::Wire(wireExplorer.Current());
                const auto boundary = ReadWireBoundary(
                    wire, face, section, options, edgeFaces);
                if (boundary.size() < 3)
                    continue;

                // The semantic decomposition stage guarantees that a real
                // manufacturing span is represented by an explicit coedge.
                // Remove only unambiguous access sides and parametric seams;
                // try each remaining span as the caller-visible drive rail.
                // This is deliberately trajectory-first: the selected span
                // defines XYZ, and TryBuildRulingToolpath proves whether its
                // straight rays cover the complete trimmed Face.
                for (std::size_t candidateIndex = 0;
                     candidateIndex < boundary.size(); ++candidateIndex)
                {
                    const auto& candidateSegment = boundary[candidateIndex].Segment;
                    if (candidateSegment.Role == SegmentRole::CrossMaterial
                        || candidateSegment.Role == SegmentRole::FeatureInternal
                        || candidateSegment.Role == SegmentRole::SurfaceSeam)
                    {
                        continue;
                    }

                    BRepBuilderAPI_MakeWire wireBuilder(candidateSegment.Edge);
                    if (!wireBuilder.IsDone())
                        continue;

                    Toolpath path;
                    if (!TryBuildRulingToolpath(
                            face,
                            wireBuilder.Wire(),
                            section,
                            options,
                            path))
                    {
                        continue;
                    }

                    SweepUnit unit;
                    std::ostringstream id;
                    id << "feature-" << featureIndex << "-face-" << faceId;
                    unit.Id = id.str();
                    unit.FeatureIndex = featureIndex;
                    unit.SourceFaceIds.push_back(faceId);
                    unit.SourceFaces.push_back(face);
                    unit.EntryRail = MakeCurveRail(
                        boundary, { candidateIndex }, false);
                    unit.Poses = path.Poses;
                    unit.Closed = path.Closed;
                    unit.FitError = 0.0;
                    unit.Confidence = 1.0;
                    unit.Model = ClassifyModel(
                        face, 0.0, options.DistanceTolerance);

                    std::vector<gp_Pnt> exitSamples;
                    exitSamples.reserve(unit.Poses.size());
                    for (const auto& pose : unit.Poses)
                    {
                        exitSamples.push_back(pose.Position.Translated(
                            gp_Vec(pose.ToolDirection) * pose.MaterialDepth));
                    }
                    const bool collapsedExit = !exitSamples.empty()
                        && std::all_of(
                            exitSamples.begin(), exitSamples.end(),
                            [&](const gp_Pnt& point)
                            {
                                return point.Distance(exitSamples.front())
                                    <= options.DistanceTolerance * 5.0;
                            });
                    if (collapsedExit)
                    {
                        unit.ExitRail = MakePointRail(exitSamples.front());
                        unit.Topology = SweepTopology::FanToPoint;
                    }
                    else
                    {
                        unit.ExitRail.Kind = RailKind::Curve;
                        unit.ExitRail.Samples = std::move(exitSamples);
                        const bool doublePinched = !path.Closed
                            && unit.Poses.size() > 2
                            && unit.Poses.front().MaterialDepth
                                <= options.DistanceTolerance
                            && unit.Poses.back().MaterialDepth
                                <= options.DistanceTolerance
                            && std::any_of(
                                unit.Poses.begin() + 1,
                                unit.Poses.end() - 1,
                                [&](const ToolPose& pose)
                                {
                                    return pose.MaterialDepth
                                        > options.DistanceTolerance;
                                });
                        unit.Topology = path.Closed
                            ? SweepTopology::Periodic
                            : doublePinched
                                ? SweepTopology::DoublePinched
                                : SweepTopology::Strip;
                    }

                    // In a normalized one-span Face the coedges immediately
                    // before and after the drive rail are its two access sides.
                    // Keep only geometrically straight sides; curved rails are
                    // never promoted to sides merely because they are adjacent.
                    const auto previous =
                        (candidateIndex + boundary.size() - 1) % boundary.size();
                    const auto next = (candidateIndex + 1) % boundary.size();
                    const auto isSide = [](const CoedgeSegment& segment)
                    {
                        return segment.GeometricallyStraight
                            && (segment.Role == SegmentRole::CrossMaterial
                                || segment.Role == SegmentRole::OnSkin);
                    };
                    if (isSide(boundary[previous].Segment))
                        unit.StartSide = boundary[previous].Segment;
                    if (isSide(boundary[next].Segment))
                        unit.EndSide = boundary[next].Segment;

                    return unit;
                }
            }
            return std::nullopt;
        }
    }

    FeatureAnalysis CompileSweepFeature(
        const TopoDS_Shape& featureGroup,
        const SectionRegion& section,
        const AnalyzerOptions& options,
        const std::size_t featureIndex)
    {
        FeatureAnalysis result;
        result.Id = "feature-" + std::to_string(featureIndex);
        result.Shape = featureGroup;

        if (featureGroup.IsNull())
        {
            result.Diagnostics.push_back({
                DiagnosticSeverity::Error,
                "feature.null",
                "The feature group is null.",
                std::nullopt });
            return result;
        }
        if (!section.IsValid())
        {
            result.Diagnostics.push_back({
                DiagnosticSeverity::Error,
                "section.invalid",
                "No valid closed section loop is available for material classification.",
                std::nullopt });
            return result;
        }

        TopTools_IndexedMapOfShape faces;
        TopExp::MapShapes(featureGroup, TopAbs_FACE, faces);
        TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
        TopExp::MapShapesAndAncestors(
            featureGroup, TopAbs_EDGE, TopAbs_FACE, edgeFaces);

        for (Standard_Integer index = 1; index <= faces.Extent(); ++index)
        {
            const auto faceId = static_cast<std::size_t>(index - 1);
            result.FaceIds.push_back(faceId);
            const auto face = TopoDS::Face(faces(index));
            auto unit = CompileFace(
                face, faceId, featureIndex, section, options, edgeFaces);
            if (!unit)
            {
                result.Diagnostics.push_back({
                    DiagnosticSeverity::Warning,
                    "face.not-sweepable",
                    "No boundary trajectory spans the Face with a valid straight-ruling family.",
                    faceId });
                continue;
            }

            Toolpath path;
            path.Id = unit->Id + "-path";
            path.SweepUnitId = unit->Id;
            path.Closed = unit->Closed;
            path.Poses = unit->Poses;
            result.Toolpaths.push_back(std::move(path));
            result.SweepUnits.push_back(std::move(*unit));
        }

        if (result.SweepUnits.empty())
        {
            result.Diagnostics.push_back({
                DiagnosticSeverity::Warning,
                "feature.no-sweep-unit",
                "The feature contains no face that satisfies the material and ruled-surface tests.",
                std::nullopt });
        }
        return result;
    }
}
