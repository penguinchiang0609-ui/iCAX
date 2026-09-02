#include "etp/Pipeline.hxx"

#include "PipelineInternals.hxx"

#include <BRepBuilderAPI_Sewing.hxx>
#include <Precision.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace etp
{
    namespace
    {
        void AppendUniqueFaces(
            const TopoDS_Shape& shape,
            std::vector<TopoDS_Face>& destination)
        {
            if (shape.IsNull())
                return;

            const auto append = [&](const TopoDS_Face& candidate)
            {
                const auto duplicate = std::any_of(
                    destination.begin(), destination.end(),
                    [&](const TopoDS_Face& existing)
                    { return existing.IsSame(candidate); });
                if (!duplicate)
                    destination.push_back(candidate);
            };

            // TopExp_Explorer normally also accepts a Face as its root, but
            // handling that case explicitly makes this helper independent of
            // explorer-root semantics across OCCT versions.
            if (shape.ShapeType() == TopAbs_FACE)
            {
                append(TopoDS::Face(shape));
                return;
            }

            for (TopExp_Explorer explorer(shape, TopAbs_FACE);
                 explorer.More(); explorer.Next())
            {
                append(TopoDS::Face(explorer.Current()));
            }
        }

        void AppendSewedComponents(
            const TopoDS_Shape& shape,
            std::vector<TopoDS_Shape>& destination)
        {
            if (shape.IsNull())
                return;

            switch (shape.ShapeType())
            {
            case TopAbs_FACE:
            case TopAbs_SHELL:
            case TopAbs_SOLID:
                // Sewing represents one connected result as a Face, Shell or
                // (less commonly here) Solid. That object is one feature
                // group and its shared Edges are already canonicalized.
                destination.push_back(shape);
                return;

            case TopAbs_COMPOUND:
            case TopAbs_COMPSOLID:
                // Multiple disconnected results are returned in a container.
                // Each independent Shell/Face below it is one feature group.
                for (TopoDS_Iterator iterator(shape); iterator.More(); iterator.Next())
                    AppendSewedComponents(iterator.Value(), destination);
                return;

            default:
                // Floating Edges and Vertices are not machining-face groups.
                return;
            }
        }

        bool ContainsSameFace(
            const std::vector<TopoDS_Face>& faces,
            const TopoDS_Face& candidate)
        {
            return std::any_of(
                faces.begin(), faces.end(),
                [&](const TopoDS_Face& face)
                { return face.IsSame(candidate); });
        }
    }

    std::string_view FeatureGroupingWork::Name() const noexcept
    {
        return "feature-grouping";
    }

    void FeatureGroupingWork::Execute(AnalysisJob& job) const
    {
        if (job.FacePatches.empty())
            throw std::logic_error("FaceSplitClassifyWork must run before FeatureGroupingWork");

        std::vector<const FacePatch*> machiningSources;
        for (const auto& patch : job.FacePatches)
        {
            if (patch.Role == FaceRole::Machining)
                machiningSources.push_back(&patch);
        }

        job.FeatureGroups.clear();
        if (machiningSources.empty())
            return;

        // Sewing is intentionally the only geometry-proximity operation in
        // this Work. It turns geometrically coincident boundaries into one
        // shared TopoDS_Edge; later stages therefore need only IsSame topology.
        BRepBuilderAPI_Sewing sewing(
            job.Options.DistanceTolerance,
            true,   // sew faces
            true,   // analyze degenerate shapes
            true,   // cut coincident free boundaries when necessary
            false); // machining surfaces are expected to be manifold
        sewing.SetSameParameterMode(true);
        sewing.SetLocalTolerancesMode(false);
        sewing.SetMinTolerance(std::max(
            Precision::Confusion(), job.Options.DistanceTolerance * 0.1));
        sewing.SetMaxTolerance(job.Options.DistanceTolerance);

        for (const auto* patch : machiningSources)
            sewing.Add(patch->Face);
        sewing.Perform();

        const auto sewedShape = sewing.SewedShape();
        if (sewedShape.IsNull())
        {
            job.Diagnostics.push_back({
                DiagnosticSeverity::Warning,
                "feature.sewing-empty",
                "OCCT Sewing produced no shape; machining Faces were retained as independent feature groups.",
                std::nullopt });
        }

        // Rebuild FacePatches with the actual Faces returned by Sewing. This
        // is the critical source-to-result mapping: group Shapes and patches
        // must refer to the same replacement Edges, not visually coincident
        // copies. Contour Faces never enter Sewing and remain unchanged.
        std::vector<FacePatch> sewedPatches;
        sewedPatches.reserve(job.FacePatches.size());
        for (const auto& source : job.FacePatches)
        {
            if (source.Role != FaceRole::Machining)
            {
                auto retained = source;
                retained.Id = sewedPatches.size();
                sewedPatches.push_back(std::move(retained));
                continue;
            }

            std::vector<TopoDS_Face> replacements;
            AppendUniqueFaces(sewing.Modified(source.Face), replacements);
            if (replacements.empty())
            {
                // Do not silently erase a Face if an importer exposes missing
                // Sewing history. It becomes a singleton group below.
                replacements.push_back(source.Face);
                job.Diagnostics.push_back({
                    DiagnosticSeverity::Warning,
                    "feature.sewing-history-missing",
                    "A machining Face could not be mapped through OCCT Sewing and was retained unchanged.",
                    source.Id });
            }

            for (const auto& replacement : replacements)
            {
                auto patch = source;
                patch.Id = sewedPatches.size();
                patch.Face = replacement;
                patch.MaterializedSplit = source.MaterializedSplit
                    || replacements.size() > 1;
                sewedPatches.push_back(std::move(patch));
            }
        }

        job.FacePatches = std::move(sewedPatches);
        job.Result.ContourFaceIds.clear();
        for (std::size_t index = 0; index < job.FacePatches.size(); ++index)
        {
            job.FacePatches[index].Id = index;
            if (job.FacePatches[index].Role == FaceRole::Contour)
                job.Result.ContourFaceIds.push_back(index);
        }

        // The container structure returned by Sewing already is the grouping
        // answer. No second geometric adjacency search is performed here.
        std::vector<TopoDS_Shape> components;
        AppendSewedComponents(sewedShape, components);
        std::vector<bool> assigned(job.FacePatches.size(), false);
        for (const auto& component : components)
        {
            std::vector<TopoDS_Face> componentFaces;
            AppendUniqueFaces(component, componentFaces);

            FeatureGroupJob group;
            group.Id = job.FeatureGroups.size();
            group.Shape = component;
            for (const auto& patch : job.FacePatches)
            {
                if (patch.Role != FaceRole::Machining || assigned[patch.Id])
                    continue;
                if (ContainsSameFace(componentFaces, patch.Face))
                {
                    group.FacePatchIds.push_back(patch.Id);
                    assigned[patch.Id] = true;
                }
            }
            if (!group.FacePatchIds.empty())
                job.FeatureGroups.push_back(std::move(group));
        }

        // Defensive fallback for a missing history/component association. It
        // preserves all machining geometry without weakening normal grouping:
        // only an unmapped Face becomes its own group.
        for (const auto& patch : job.FacePatches)
        {
            if (patch.Role != FaceRole::Machining || assigned[patch.Id])
                continue;

            FeatureGroupJob group;
            group.Id = job.FeatureGroups.size();
            group.FacePatchIds = { patch.Id };
            group.Shape = pipeline_detail::MakeFaceGroup(
                job.FacePatches, group.FacePatchIds);
            job.FeatureGroups.push_back(std::move(group));
            assigned[patch.Id] = true;
        }
    }
}
