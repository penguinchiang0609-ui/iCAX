#include "etp/Pipeline.hxx"

#include "../SectionRegion.hxx"
#include "../SweepCompiler.hxx"

#include <algorithm>
#include <stdexcept>

namespace etp
{
    std::string_view ToolpathWork::Name() const noexcept
    {
        return "toolpath-analysis";
    }

    void ToolpathWork::Execute(AnalysisJob& job) const
    {
        if (!job.SectionFrame || job.SectionWires.empty())
            throw std::logic_error("SectionProfileWork must run before ToolpathWork");

        const detail::SectionRegion section(
            job.SectionWires, *job.SectionFrame, job.Options);
        job.Result.Features.clear();
        job.Result.Features.reserve(job.FeatureGroups.size());
        for (const auto& group : job.FeatureGroups)
        {
            auto feature = detail::CompileSweepFeature(
                group.Shape, section, job.Options, group.Id);
            feature.FaceIds = group.FacePatchIds;

            for (auto& unit : feature.SweepUnits)
            {
                unit.SourceFaceIds.clear();
                for (const auto& sourceFace : unit.SourceFaces)
                {
                    const auto patch = std::find_if(
                        job.FacePatches.begin(), job.FacePatches.end(),
                        [&](const FacePatch& candidate)
                        { return candidate.Face.IsSame(sourceFace); });
                    if (patch != job.FacePatches.end())
                        unit.SourceFaceIds.push_back(patch->Id);
                }
            }
            job.Result.Features.push_back(std::move(feature));
        }

        job.Result.Id = job.Id;
        job.Result.Shape = job.WorkingShape;
        job.Result.SourceToNormalized = job.SourceToNormalized;
        job.Result.NormalizedToSource = job.NormalizedToSource;
        job.Result.LengthScale = job.LengthScale;
        job.Result.SectionFrame = *job.SectionFrame;
        job.Result.AxialFirst = job.AxialFirst;
        job.Result.AxialLast = job.AxialLast;
        job.Result.SectionWires = job.SectionWires;
    }
}
