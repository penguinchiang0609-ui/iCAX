#pragma once
#include "recovered_escher_period.hpp"
#include "recovered_escher_nfp.hpp"
namespace study::recovered {
struct EscherCandidateLists {std::vector<EscherPeriodCandidate> ordinary,special;};
std::array<EscherIntegerPoint,2> escherTrialSupportExtrema(std::span<const EscherSupportRecord>,EscherIntegerPoint &); // 7ff2e0
void escherAppendNfpCandidates(std::span<const EscherSupportTable>,const EscherNfpBoundary &,const ConvolutionEdgeIndex &,
    EscherIntegerPoint,std::span<const EscherIntegerPoint>,EscherCandidateLists &,int64_t &,
    const EscherSupportTable *aggregateSupport=nullptr); // 77abc0 / 78c870
EscherPeriodCandidate escherSwapCandidateBasis(const EscherPeriodCandidate &,std::span<const EscherSupportTable>); // 78ca00
void escherNormalizeCandidatePhase(EscherPeriodCandidate &); // 78d430 / 77d470
bool escherCandidateSlopeNear(double,double,EscherIntegerPoint,EscherIntegerPoint,double); // 77cd80
}
