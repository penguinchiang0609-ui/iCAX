#include "etp/Pipeline.hxx"

#include <Standard_Failure.hxx>

#include <chrono>
#include <stdexcept>
#include <utility>

namespace etp
{
    bool AnalysisJob::HasErrors() const noexcept
    {
        for (const auto& diagnostic : Diagnostics)
        {
            if (diagnostic.Severity == DiagnosticSeverity::Error)
                return true;
        }
        return false;
    }

    Pipe& Pipe::Add(std::unique_ptr<Work> work)
    {
        if (!work)
            throw std::invalid_argument("cannot add a null Work to the Pipe");
        m_Works.push_back(std::move(work));
        return *this;
    }

    PartAnalysis Pipe::Run(AnalysisJob& job) const
    {
        job.Reports.clear();
        job.Diagnostics.clear();
        job.Result = {};
        job.WorkingShape = job.SourceShape;
        job.Result.Id = job.Id;
        job.Result.Shape = job.SourceShape;

        for (const auto& work : m_Works)
        {
            WorkReport report;
            report.Name = std::string(work->Name());
            report.DiagnosticsBefore = job.Diagnostics.size();
            const auto started = std::chrono::steady_clock::now();
            try
            {
                work->Execute(job);
                report.Succeeded = !job.HasErrors();
            }
            catch (const Standard_Failure& failure)
            {
                job.Diagnostics.push_back({
                    DiagnosticSeverity::Error,
                    "pipe.occ-failure",
                    std::string(work->Name()) + ": " + failure.what(),
                    std::nullopt });
            }
            catch (const std::exception& exception)
            {
                job.Diagnostics.push_back({
                    DiagnosticSeverity::Error,
                    "pipe.work-failure",
                    std::string(work->Name()) + ": " + exception.what(),
                    std::nullopt });
            }
            const auto finished = std::chrono::steady_clock::now();
            report.ElapsedMilliseconds = std::chrono::duration<double, std::milli>(
                finished - started).count();
            report.DiagnosticsAfter = job.Diagnostics.size();
            report.Succeeded = report.Succeeded && !job.HasErrors();
            job.Reports.push_back(std::move(report));
            if (job.HasErrors())
                break;
        }

        job.Result.Diagnostics = job.Diagnostics;
        return std::move(job.Result);
    }

    const std::vector<std::unique_ptr<Work>>& Pipe::Works() const noexcept
    {
        return m_Works;
    }

    Pipe MakeDefaultPipe()
    {
        Pipe pipe;
        pipe.Add(std::make_unique<PreprocessWork>())
            .Add(std::make_unique<NormalizeWork>())
            .Add(std::make_unique<SectionProfileWork>())
            .Add(std::make_unique<FaceSplitClassifyWork>())
            .Add(std::make_unique<FeatureGroupingWork>())
            .Add(std::make_unique<SweepFaceDecomposeWork>())
            .Add(std::make_unique<ToolpathWork>());
        return pipe;
    }
}
