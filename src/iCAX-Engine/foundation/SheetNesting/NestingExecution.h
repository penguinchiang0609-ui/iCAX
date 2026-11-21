#pragma once
#include "NestingInternal.h"
#include <tuple>

namespace iCAX::SheetNesting::Detail
{
    // Only independently checked, usable results enter selection.
    // Utilization: incomplete tier, reserved count, partial net-material
    // progress, reserved flag, negative stock utilization, negative full-width
    // section utilization, empty vector, consumed stock area, used section
    // area, occupied part-envelope area, top/right and centroid ties. Older
    // policies retain their own slot semantics; traces must name the policy.
    using QualityKey = std::tuple<std::size_t, std::size_t, double, bool, double,
        double, std::vector<double>, double, double, double, double, double, double, double>;
    bool Eligible(const JobResult& job);
    QualityKey Quality(const JobResult& job, const Problem& input, RankingPolicy policy);
    void AssessCandidate(const Problem& input, JobResult& candidate);
    std::unique_ptr<IStrategy> MakeRefinementExecutor(std::unique_ptr<IStrategy> constructor);
}
