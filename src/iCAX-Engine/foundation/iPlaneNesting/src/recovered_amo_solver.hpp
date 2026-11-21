#pragma once
#include "recovered_amo_map.hpp"
namespace study::recovered {
struct AmoTilingSolveRequest {
    AmoTilingAngleRequest angles;
    AmoTilingCriterion criterion;
    bool fixedTranslation=false;
    Point translation{};
    std::array<std::array<bool,2>,2> sourceFlags{}; // original shape +59/+5a
    std::function<void()> check; // host cancellation, never changes native search ordering
};
// The two actual floating NFP construction stages belong to geometry. All
// angle sources, map/family preparation, Y-then-X period queries, evaluation,
// bisection, phase correction and final ordering are connected here.
// Returned extra-member identities refer to resolved job.memberSources (or
// its owned members); retain those source objects until descriptor conversion.
std::vector<AmoTilingCandidatePtr> amoTilingSolve(AmoTilingJob &,const AmoTilingSolveRequest &,
    const AmoTilingMapGeometry &); //8bf7b0 / 8bd380 / 8b61d0
std::vector<AmoTilingCandidatePtr> amoTilingSolve(AmoTilingJob &,const AmoTilingSolveRequest &);
// 492420 runs the unconstrained-quantity and required/2 criteria in that
// order, retaining each search's first sorted result. Input dimensions here
// already include the caller's original geometry offset.
std::vector<AmoTilingDescriptor> amoTilingProduceDescriptors(AmoTilingJob &,AmoTilingSolveRequest,
    int32_t required,const AmoTilingMapGeometry &); //492420 / 8b4a50 / 491e80
std::vector<AmoTilingDescriptor> amoTilingProduceDescriptors(AmoTilingJob &,AmoTilingSolveRequest,int32_t required);
}
