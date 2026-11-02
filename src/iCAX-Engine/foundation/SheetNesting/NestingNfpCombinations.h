#pragma once
#include "NestingInternal.h"
#include "SheetNestingNfp.h"

namespace iCAX::SheetNesting::Detail
{
    struct NfpCombinationOptions
    {
        std::size_t MaxMembers=4,MaxResults=64,MaxStates=96,MaxAttempts=12000;
        double MinimumIntermediateFill=.55,MinimumFinalFill=.85;
        std::function<bool()> Stop;
    };
    // Returns real original members and poses, never a merged substitute.
    // Search is bounded and a missing pattern proves nothing about feasibility.
    std::vector<Pattern> BuildNfpCombinationPatterns(const Problem& problem,NfpCache& cache,
        const std::vector<NfpPreparedShape>& prepared,const NfpCombinationOptions& options,
        std::size_t& attempts);
}
