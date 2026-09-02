#include "etp/Pipeline.hxx"

#include "PipelineInternals.hxx"

#include <BRepBuilderAPI_Transform.hxx>
#include <gp.hxx>

#include <cmath>
#include <stdexcept>

namespace etp
{
    std::string_view NormalizeWork::Name() const noexcept
    {
        return "normalize";
    }

    void NormalizeWork::Execute(AnalysisJob& job) const
    {
        if (job.Options.InputLengthScale <= 0.0)
            throw std::invalid_argument("InputLengthScale must be positive");

        const auto sourceAxis = pipeline_detail::RecognizeAxis(
            job.WorkingShape, job.Options, job.Diagnostics);
        const auto sourceFrame = pipeline_detail::BuildSourceFrame(
            job.WorkingShape, sourceAxis, job.Options.DistanceTolerance);
        const gp_Ax3 targetFrame(gp::Origin(), gp::DY(), gp::DX());

        gp_Trsf alignment;
        alignment.SetDisplacement(sourceFrame, targetFrame);
        gp_Trsf scaling;
        scaling.SetScale(gp::Origin(), job.Options.InputLengthScale);
        gp_Trsf sourceToNormalized = scaling;
        sourceToNormalized.Multiply(alignment);

        const auto normalized = BRepBuilderAPI_Transform(
            job.WorkingShape, sourceToNormalized, true).Shape();
        if (normalized.IsNull())
            throw std::runtime_error("normalization transform produced a null shape");

        job.WorkingShape = normalized;
        job.SourceToNormalized = sourceToNormalized;
        job.NormalizedToSource = sourceToNormalized.Inverted();
        job.LengthScale = job.Options.InputLengthScale;
        job.ExtrusionAxis = gp::DY();
        job.SectionFrame = targetFrame;
        const auto range = pipeline_detail::AxialRange(normalized, targetFrame);
        job.AxialFirst = range.first;
        job.AxialLast = range.second;

        job.Result.Id = job.Id;
        job.Result.Shape = job.WorkingShape;
        job.Result.SourceToNormalized = job.SourceToNormalized;
        job.Result.NormalizedToSource = job.NormalizedToSource;
        job.Result.LengthScale = job.LengthScale;
        job.Result.SectionFrame = *job.SectionFrame;
        job.Result.AxialFirst = job.AxialFirst;
        job.Result.AxialLast = job.AxialLast;
    }
}
