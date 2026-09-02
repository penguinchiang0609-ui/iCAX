#include "etp/Pipeline.hxx"

#include "PipelineInternals.hxx"
#include "../GeometryUtils.hxx"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepFeat_SplitShape.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace etp
{
    namespace
    {
        std::vector<gp_Pnt2d> ApplicableBreaks(
            const TopoDS_Face& face,
            const AnalysisJob& job)
        {
            std::vector<gp_Pnt2d> existingVertices;
            for (TopExp_Explorer explorer(face, TopAbs_VERTEX);
                 explorer.More(); explorer.Next())
            {
                existingVertices.push_back(detail::ToSection2d(
                    BRep_Tool::Pnt(TopoDS::Vertex(explorer.Current())),
                    *job.SectionFrame));
            }

            std::vector<std::vector<gp_Pnt2d>> projectedRails;
            for (TopExp_Explorer explorer(face, TopAbs_EDGE);
                 explorer.More(); explorer.Next())
            {
                const auto samples = detail::SampleEdge(
                    TopoDS::Edge(explorer.Current()),
                    job.Options.MinimumEdgeSamples,
                    job.Options.SamplingDeflection,
                    false);
                if (samples.size() < 2)
                    continue;

                auto minimumStation = (std::numeric_limits<double>::max)();
                auto maximumStation = -(std::numeric_limits<double>::max)();
                for (const auto& point : samples)
                {
                    const auto station = detail::AxialStation(point, *job.SectionFrame);
                    minimumStation = std::min(minimumStation, station);
                    maximumStation = std::max(maximumStation, station);
                }
                // A transverse rail projects to a section curve. Axis-parallel
                // generators collapse to one point and are not split targets.
                if (maximumStation - minimumStation
                    > job.Options.DistanceTolerance * 5.0)
                {
                    continue;
                }
                std::vector<gp_Pnt2d> projected;
                projected.reserve(samples.size());
                for (const auto& point : samples)
                    projected.push_back(detail::ToSection2d(point, *job.SectionFrame));
                projectedRails.push_back(std::move(projected));
            }

            std::vector<gp_Pnt2d> result;
            for (const auto& sectionBreak : job.SectionBreakPoints)
            {
                const auto alreadyVertex = std::any_of(
                    existingVertices.begin(), existingVertices.end(),
                    [&](const gp_Pnt2d& vertex)
                    { return vertex.Distance(sectionBreak) <= job.Options.DistanceTolerance; });
                if (alreadyVertex)
                    continue;

                const auto liesOnRail = std::any_of(
                    projectedRails.begin(), projectedRails.end(),
                    [&](const std::vector<gp_Pnt2d>& rail)
                    {
                        return detail::PointPolylineDistance2d(sectionBreak, rail)
                            <= job.Options.DistanceTolerance * 2.0;
                    });
                if (liesOnRail)
                    result.push_back(sectionBreak);
            }
            return result;
        }

        std::pair<double, double> FaceAxialRange(
            const TopoDS_Face& face,
            const gp_Ax3& frame)
        {
            auto first = (std::numeric_limits<double>::max)();
            auto last = -(std::numeric_limits<double>::max)();
            for (TopExp_Explorer explorer(face, TopAbs_VERTEX);
                 explorer.More(); explorer.Next())
            {
                const auto station = detail::AxialStation(
                    BRep_Tool::Pnt(TopoDS::Vertex(explorer.Current())), frame);
                first = std::min(first, station);
                last = std::max(last, station);
            }
            return first <= last ? std::pair(first, last) : std::pair(0.0, 0.0);
        }

        std::vector<TopoDS_Face> SplitFace(
            const TopoDS_Face& face,
            const std::vector<gp_Pnt2d>& breaks,
            const AnalysisJob& job)
        {
            if (breaks.empty() || !job.Options.MaterializeFaceSplits)
                return { face };

            const auto [first, last] = FaceAxialRange(face, *job.SectionFrame);
            if (last - first <= job.Options.DistanceTolerance)
                return { face };

            BRepFeat_SplitShape splitter(face);
            splitter.SetCheckInterior(false);
            for (const auto& sectionBreak : breaks)
            {
                const auto start = detail::FromSection2d(
                    sectionBreak, first, *job.SectionFrame);
                const auto finish = detail::FromSection2d(
                    sectionBreak, last, *job.SectionFrame);
                const auto edge = BRepBuilderAPI_MakeEdge(start, finish).Edge();
                splitter.Add(edge, face);
            }
            splitter.Build();
            if (!splitter.IsDone())
                return { face };

            std::vector<TopoDS_Face> result;
            for (TopExp_Explorer explorer(splitter.Shape(), TopAbs_FACE);
                 explorer.More(); explorer.Next())
            {
                const auto candidate = TopoDS::Face(explorer.Current());
                const auto duplicate = std::any_of(
                    result.begin(), result.end(),
                    [&](const TopoDS_Face& existing) { return existing.IsSame(candidate); });
                if (!duplicate)
                    result.push_back(candidate);
            }
            return result.size() > 1 ? result : std::vector<TopoDS_Face>{ face };
        }
    }

    std::string_view FaceSplitClassifyWork::Name() const noexcept
    {
        return "face-split-classify";
    }

    void FaceSplitClassifyWork::Execute(AnalysisJob& job) const
    {
        if (!job.SectionFrame || !job.ExtrusionAxis || job.SectionWires.empty())
            throw std::logic_error("SectionProfileWork must run before FaceSplitClassifyWork");

        const detail::SectionRegion section(
            job.SectionWires, *job.SectionFrame, job.Options);
        TopTools_IndexedMapOfShape sourceFaces;
        TopExp::MapShapes(job.WorkingShape, TopAbs_FACE, sourceFaces);

        job.FacePatches.clear();
        job.Result.ContourFaceIds.clear();
        std::size_t nextPatchId = 0;
        for (Standard_Integer index = 1; index <= sourceFaces.Extent(); ++index)
        {
            const auto sourceFaceId = static_cast<std::size_t>(index - 1);
            const auto face = TopoDS::Face(sourceFaces(index));
            const auto axisParallel = pipeline_detail::IsAxisInvariantSurface(
                face, *job.ExtrusionAxis, job.Options.AngularToleranceRadians);
            const auto breaks = axisParallel ? ApplicableBreaks(face, job)
                                             : std::vector<gp_Pnt2d>{};

            std::vector<TopoDS_Face> fragments;
            try
            {
                fragments = SplitFace(face, breaks, job);
            }
            catch (const Standard_Failure&)
            {
                fragments = { face };
                job.Diagnostics.push_back({
                    DiagnosticSeverity::Warning,
                    "face.split-failed",
                    "A section-driven face split failed; the original face is retained as one logical patch.",
                    sourceFaceId });
            }

            const auto materialized = fragments.size() > 1;
            if (!breaks.empty() && !materialized && job.Options.MaterializeFaceSplits)
            {
                job.Diagnostics.push_back({
                    DiagnosticSeverity::Warning,
                    "face.split-not-materialized",
                    "Section breakpoints were found on a wide face, but OCCT did not materialize the split.",
                    sourceFaceId });
            }

            for (const auto& fragment : fragments)
            {
                const auto stockSurface = pipeline_detail::IsCapFace(
                        fragment, *job.ExtrusionAxis, job.Options.AngularToleranceRadians)
                    || pipeline_detail::IsAxisInvariantSurface(
                        fragment, *job.ExtrusionAxis, job.Options.AngularToleranceRadians);
                const auto contour = stockSurface
                    && pipeline_detail::EveryBoundaryPointOnSection(
                        fragment, section, job.Options);

                FacePatch patch;
                patch.Id = nextPatchId++;
                patch.SourceFaceId = sourceFaceId;
                patch.Face = fragment;
                patch.Role = contour ? FaceRole::Contour : FaceRole::Machining;
                patch.AxisParallel = axisParallel;
                patch.MaterializedSplit = materialized;
                patch.AppliedSectionBreaks = breaks;
                if (contour)
                    job.Result.ContourFaceIds.push_back(patch.Id);
                job.FacePatches.push_back(std::move(patch));
            }
        }
    }
}
