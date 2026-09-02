#include "etp/Pipeline.hxx"

#include <BRepCheck_Analyzer.hxx>
#include <BRepLib.hxx>
#include <ShapeFix_Shape.hxx>

#include <stdexcept>

namespace etp
{
    std::string_view PreprocessWork::Name() const noexcept
    {
        return "preprocess";
    }

    void PreprocessWork::Execute(AnalysisJob& job) const
    {
        if (job.SourceShape.IsNull())
            throw std::invalid_argument("input part is null");

        // ShapeFix_Shape repairs wire ordering, small topological gaps,
        // missing/incorrect pcurves, face-wire orientation and common imported
        // STEP defects.  It deliberately runs before semantic splitting: a
        // splitter must never be asked to operate on an invalid trimming wire.
        ShapeFix_Shape fixer(job.SourceShape);
        fixer.SetPrecision(job.Options.DistanceTolerance);
        fixer.Perform();
        job.WorkingShape = fixer.Shape();
        if (job.WorkingShape.IsNull())
            throw std::runtime_error("topology healing produced a null shape");

        // A toolpath point is evaluated on the 3D edge while trimming and
        // intersection are evaluated on its face pcurve.  SameParameter makes
        // those parameterizations agree and prevents a valid ruling from
        // apparently missing the UV boundary after data exchange.
        BRepLib::SameParameter(
            job.WorkingShape, job.Options.DistanceTolerance, true);

        const BRepCheck_Analyzer validator(job.WorkingShape, true);
        if (!validator.IsValid())
            throw std::runtime_error(
                "topology healing did not produce a valid BRep shape");
    }
}
