#include "etp/Pipeline.hxx"

#include "../GeometryUtils.hxx"
#include "PipelineInternals.hxx"

#include <BRep_Builder.hxx>
#include <BRepTools_History.hxx>
#include <ShapeBuild_ReShape.hxx>
#include <ShapeUpgrade_ShapeDivideContinuity.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace etp
{
    namespace
    {
        void AppendUniqueFaces(
            const TopoDS_Shape& shape,
            const AnalyzerOptions& options,
            std::vector<TopoDS_Face>& destination)
        {
            if (shape.IsNull())
                return;

            for (TopExp_Explorer explorer(shape, TopAbs_FACE);
                 explorer.More(); explorer.Next())
            {
                const auto candidate = TopoDS::Face(explorer.Current());
                const auto duplicate = std::any_of(
                    destination.begin(), destination.end(),
                    [&](const TopoDS_Face& existing)
                    { return existing.IsSame(candidate); });

                // A tolerance-sized sliver does not represent a machinable
                // sweep unit and destabilizes the later edge-adjacency graph.
                if (!duplicate
                    && detail::FaceArea(candidate)
                        > options.DistanceTolerance * options.DistanceTolerance)
                {
                    destination.push_back(candidate);
                }
            }
        }

        std::vector<TopoDS_Face> UnifiedFacesForSource(
            const TopoDS_Face& source,
            const occ::handle<BRepTools_History>& history,
            const AnalyzerOptions& options)
        {
            std::vector<TopoDS_Face> result;
            if (!history.IsNull())
            {
                const auto& modified = history->Modified(source);
                for (TopTools_ListOfShape::Iterator iterator(modified);
                     iterator.More(); iterator.Next())
                {
                    AppendUniqueFaces(iterator.Value(), options, result);
                }

            }

            // UnifySameDomain records only changed Faces.  An absent history
            // entry normally means that this exact source Face survives.  We
            // also retain it defensively if an importer produced incomplete
            // history: preprocessing is never allowed to erase a Face merely
            // because its replacement could not be mapped.
            if (result.empty())
                result.push_back(source);
            return result;
        }

        std::vector<std::vector<TopoDS_Face>> DivideAtSemanticContinuityBreaks(
            const std::vector<FacePatch>& patches,
            const AnalyzerOptions& options)
        {
            std::vector<std::vector<TopoDS_Face>> result(patches.size());
            if (!options.MaterializeFaceSplits)
            {
                for (std::size_t index = 0; index < patches.size(); ++index)
                    result[index] = { patches[index].Face };
                return result;
            }

            // Every Face in one already-sewn feature group is put through both
            // operations together. This is essential: if neighboring Faces
            // were repaired separately, each could receive its own replacement
            // Edge and the group would lose the topology established by Sewing.
            BRep_Builder builder;
            TopoDS_Compound group;
            builder.MakeCompound(group);
            for (const auto& patch : patches)
                builder.Add(group, patch.Face);

            // Imported models also exhibit the opposite defect: one real line
            // or circle is fragmented into several same-domain micro-edges.
            // Merge only edges here, never Faces and never merely C1 B-splines;
            // otherwise this cleanup could erase the semantic spans that the
            // continuity divider below is meant to expose.
            ShapeUpgrade_UnifySameDomain unifier(
                group,
                true,   // unify exact same-domain edge fragments
                false,  // never merge neighboring Faces
                false); // do not concatenate arbitrary C1 B-splines
            unifier.SetLinearTolerance(options.DistanceTolerance);
            unifier.SetAngularTolerance(options.AngularToleranceRadians);
            unifier.Build();
            const auto edgeNormalized = unifier.Shape().IsNull()
                ? TopoDS_Shape(group)
                : unifier.Shape();

            std::vector<std::vector<TopoDS_Face>> unifiedBySource(patches.size());
            for (std::size_t index = 0; index < patches.size(); ++index)
            {
                unifiedBySource[index] = UnifiedFacesForSource(
                    patches[index].Face, unifier.History(), options);
            }

            ShapeUpgrade_ShapeDivideContinuity divider(edgeNormalized);
            divider.SetPrecision(options.DistanceTolerance);
            divider.SetMinTolerance(options.DistanceTolerance * 0.1);
            divider.SetMaxTolerance(options.DistanceTolerance * 10.0);
            divider.SetEdgeMode(2);
            divider.SetSurfaceSegmentMode(true);

            // C1 is not enough for manufacturing semantics.  A stadium joins
            // a line (zero curvature) to an arc (non-zero curvature)
            // tangentially, so the join is normally G1/C1 but not C2.  Using
            // C2 here exposes that join as a real coedge and also divides a
            // composite extrusion surface at the corresponding ruling.
            divider.SetBoundaryCriterion(GeomAbs_C2);
            divider.SetPCurveCriterion(GeomAbs_C2);
            divider.SetSurfaceCriterion(GeomAbs_C2);

            if (!divider.Perform() || divider.Result().IsNull())
                return unifiedBySource;

            const auto context = divider.GetContext();
            if (context.IsNull())
                return unifiedBySource;

            // Query the single shared replacement context per source Face.
            // Calling Apply() on an intermediate Face returns either that Face
            // rebuilt with split boundary Edges, or a compound of child Faces
            // when a surface continuity break materialized a real split.
            for (std::size_t index = 0; index < unifiedBySource.size(); ++index)
            {
                for (const auto& unifiedFace : unifiedBySource[index])
                {
                    AppendUniqueFaces(
                        context->Apply(unifiedFace), options, result[index]);
                }

                // A failed history lookup must not silently delete machining
                // geometry.  Retain the globally edge-normalized Face and let
                // the downstream compiler decide whether it is supported.
                if (result[index].empty())
                    result[index] = unifiedBySource[index];
            }
            return result;
        }
    }

    std::string_view SweepFaceDecomposeWork::Name() const noexcept
    {
        return "sweep-face-decompose";
    }

    void SweepFaceDecomposeWork::Execute(AnalysisJob& job) const
    {
        if (job.FacePatches.empty())
            throw std::logic_error(
                "FaceSplitClassifyWork must run before SweepFaceDecomposeWork");

        const auto hasMachiningFaces = std::any_of(
            job.FacePatches.begin(), job.FacePatches.end(),
            [](const FacePatch& patch)
            { return patch.Role == FaceRole::Machining; });
        if (!hasMachiningFaces)
            return;
        if (job.FeatureGroups.empty())
            throw std::logic_error(
                "FeatureGroupingWork must run before SweepFaceDecomposeWork");

        // Contour Faces are outside the machining-feature graph. Preserve them
        // exactly and only decompose Faces belonging to each sewn group.
        std::vector<FacePatch> normalized;
        normalized.reserve(job.FacePatches.size());
        for (const auto& patch : job.FacePatches)
        {
            if (patch.Role == FaceRole::Machining)
                continue;
            auto retained = patch;
            retained.Id = normalized.size();
            normalized.push_back(std::move(retained));
        }

        std::vector<bool> consumed(job.FacePatches.size(), false);
        for (auto& group : job.FeatureGroups)
        {
            std::vector<FacePatch> sources;
            sources.reserve(group.FacePatchIds.size());
            for (const auto sourceId : group.FacePatchIds)
            {
                const auto found = std::find_if(
                    job.FacePatches.begin(), job.FacePatches.end(),
                    [&](const FacePatch& patch) { return patch.Id == sourceId; });
                if (found == job.FacePatches.end()
                    || found->Role != FaceRole::Machining)
                {
                    throw std::logic_error(
                        "a FeatureGroup contains an invalid machining FacePatch id");
                }
                if (sourceId >= consumed.size() || consumed[sourceId])
                    throw std::logic_error(
                        "a machining FacePatch belongs to more than one FeatureGroup");
                consumed[sourceId] = true;
                sources.push_back(*found);
            }

            std::vector<std::vector<TopoDS_Face>> fragmentsBySource;
            try
            {
                fragmentsBySource = DivideAtSemanticContinuityBreaks(
                    sources, job.Options);
            }
            catch (const Standard_Failure&)
            {
                fragmentsBySource.resize(sources.size());
                for (std::size_t index = 0; index < sources.size(); ++index)
                    fragmentsBySource[index] = { sources[index].Face };

                job.Diagnostics.push_back({
                    DiagnosticSeverity::Warning,
                    "face.semantic-split-failed",
                    "The C2 semantic boundary split failed for one sewn feature group; its Faces were retained.",
                    std::nullopt });
            }

            group.FacePatchIds.clear();
            for (std::size_t sourceIndex = 0;
                 sourceIndex < sources.size(); ++sourceIndex)
            {
                const auto& source = sources[sourceIndex];
                const auto& fragments = fragmentsBySource[sourceIndex];
                const bool faceWasDivided = fragments.size() > 1;
                for (const auto& fragment : fragments)
                {
                    FacePatch patch = source;
                    patch.Id = normalized.size();
                    patch.Face = fragment;
                    patch.MaterializedSplit = source.MaterializedSplit
                        || faceWasDivided;
                    group.FacePatchIds.push_back(patch.Id);
                    normalized.push_back(std::move(patch));
                }
            }
            group.Shape = pipeline_detail::MakeFaceGroup(
                normalized, group.FacePatchIds);
        }

        // Grouping must be a lossless partition of all machining Faces.
        for (const auto& patch : job.FacePatches)
        {
            if (patch.Role == FaceRole::Machining
                && (patch.Id >= consumed.size() || !consumed[patch.Id]))
            {
                throw std::logic_error(
                    "FeatureGroupingWork did not assign every machining FacePatch");
            }
        }

        // Rebuild the contour list after inserting per-group semantic children.
        job.FacePatches = std::move(normalized);
        job.Result.ContourFaceIds.clear();
        for (std::size_t index = 0; index < job.FacePatches.size(); ++index)
        {
            job.FacePatches[index].Id = index;
            if (job.FacePatches[index].Role == FaceRole::Contour)
                job.Result.ContourFaceIds.push_back(index);
        }
    }
}
