#pragma once
#include "NestingInternal.h"
#include <tuple>

namespace iCAX::SheetNesting::Detail
{
    // Only independently checked, usable results enter selection.
    using QualityKey = std::tuple<std::size_t, std::size_t, double, double, double>;
    bool Eligible(const JobResult& job);
    QualityKey Quality(const JobResult& job, const Problem& input, RankingPolicy policy);
    void AssessCandidate(const Problem& input, JobResult& candidate);
}
