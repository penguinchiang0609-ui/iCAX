#pragma once
#include "rectangle_runtime.hpp"

namespace study {
struct OrdinaryGeneralTilingTrace {
    size_t generalGroups=0,providerGroups=0,models=0,expanded=0;
    uint32_t candidateLimit=0;
    int32_t toroidalRefinementLevel=0;
    size_t toroidalCandidates=0,toroidalSolves=0,toroidalImproved=0;
    std::set<std::string> missingProviders;
};
// contextSeconds is the source Context+150 requested run time, not an
// independently chosen per-part timeout. Budget retains the actual deadline.
std::vector<RectangleTilingModel> ordinaryGeneralTilingModelsForInput(
    const Input &,Budget &,double contextSeconds,OrdinaryGeneralTilingTrace * = nullptr);
}
