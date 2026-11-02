#include "NestingExecution.h"
#include <algorithm>
#include <chrono>

namespace iCAX::SheetNesting::Detail
{
    void AssessCandidate(const Problem& input, JobResult& candidate)
    {
        candidate.Remnants = {};
        const auto verifyStarted = std::chrono::steady_clock::now();
        candidate.Checked = Validate(input, candidate.Output);
        candidate.Output.Stats.VerificationSeconds = std::max(0.0, candidate.Output.Stats.VerificationSeconds) +
            std::chrono::duration<double>(std::chrono::steady_clock::now() - verifyStarted).count();
        candidate.Output.GeometryValidated = candidate.Checked.Valid;
        if (!candidate.Checked.Valid)
        {
            candidate.Output.State = Status::Failed;
            candidate.Output.Diagnostics.insert(candidate.Output.Diagnostics.end(), candidate.Checked.Errors.begin(), candidate.Checked.Errors.end());
        }
        else
        {
            const auto evaluateStarted = std::chrono::steady_clock::now();
            try { candidate.Remnants = EvaluateRemnants(input, candidate.Output); }
            catch (...) { candidate.Remnants = {}; }
            candidate.Output.Stats.EvaluationSeconds = std::max(0.0, candidate.Output.Stats.EvaluationSeconds) +
                std::chrono::duration<double>(std::chrono::steady_clock::now() - evaluateStarted).count();
            if (!candidate.Remnants.Measured) candidate.Output.Diagnostics.push_back("Reusable-remnant score unavailable; valid geometry is retained.");
        }
    }
}
