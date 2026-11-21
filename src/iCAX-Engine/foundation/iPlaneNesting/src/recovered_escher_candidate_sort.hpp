#pragma once
#include "recovered_escher_period.hpp"
namespace study::recovered {
struct EscherCandidateAreaIndex {
    int64_t area=0;
    int32_t index=0;
    int32_t padding=0;
    bool operator==(const EscherCandidateAreaIndex &)const=default;
};
static_assert(sizeof(EscherCandidateAreaIndex)==16);
bool escherCandidateSlopeLess(const EscherPeriodCandidate &,const EscherPeriodCandidate &); //77d250
void escherSortCandidateAreas(std::vector<EscherCandidateAreaIndex> &,size_t depth=SIZE_MAX); //781260
void escherSortCandidateSlopes(std::vector<EscherPeriodCandidate> &,size_t depth=SIZE_MAX); //780080
}
