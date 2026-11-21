#pragma once
#include "recovered_amo_solver.hpp"
#include "recovered_amo_orientation.hpp"
namespace study::recovered {
struct AmoTilingPatternResult {
    std::shared_ptr<TilingPattern> pattern;
    AmoTilingDescriptor descriptor;
};
// 4a9e50: sort the associated pattern and repetition descriptor together by
// original cell area. The original sorting policy is retained for ties.
void amoTilingSortPatternResults(std::vector<AmoTilingPatternResult> &,size_t depth=SIZE_MAX);
// 4a4e00 / 4992a0 / 492420, for the supplied actual canonical member variants.
// The caller owns the variant lookup and the original angle/quantity criteria.
std::vector<AmoTilingPatternResult> amoTilingPrepareOrientationPatterns(AmoTilingJob &,
    AmoTilingSolveRequest,int32_t required,const AmoTilingOrientationGeometry &);
}
