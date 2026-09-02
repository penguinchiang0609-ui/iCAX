#include "etp/Pipeline.hxx"

#include "PipelineInternals.hxx"

#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <stdexcept>

namespace etp
{
    std::string_view SectionProfileWork::Name() const noexcept
    {
        return "section-profile";
    }

    void SectionProfileWork::Execute(AnalysisJob& job) const
    {
        if (!job.SectionFrame || !job.ExtrusionAxis)
            throw std::logic_error("NormalizeWork must run before SectionProfileWork");

        job.AxisParallelFaces.clear();
        for (TopExp_Explorer explorer(job.WorkingShape, TopAbs_FACE);
             explorer.More(); explorer.Next())
        {
            const auto face = TopoDS::Face(explorer.Current());
            if (pipeline_detail::IsAxisInvariantSurface(
                face, *job.ExtrusionAxis, job.Options.AngularToleranceRadians))
            {
                job.AxisParallelFaces.push_back(face);
            }
        }
        if (job.AxisParallelFaces.empty())
            throw std::runtime_error("no face parallel to the extrusion axis was found");

        job.SectionWires = pipeline_detail::RecoverSectionWires(
            job.WorkingShape,
            job.AxisParallelFaces,
            *job.SectionFrame,
            job.Options,
            job.Diagnostics);
        if (job.SectionWires.empty())
            return;

        job.SectionBreakPoints = pipeline_detail::CollectSectionBreakPoints(
            job.SectionWires, *job.SectionFrame, job.Options.DistanceTolerance);
        // Published by ToolpathWork after all section-dependent stages finish.
    }
}
