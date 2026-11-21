#include "ordinary_pricing.hpp"

namespace study {
SupervisorProgram pricedProgram(size_t count, std::span<const recovered::PricedSdkPart> parts,
                                SupervisorProgram inherited) {
    require(recovered::pricedEntryValid(parts), PN2S_INVALID,
            "PricedNest requires at least one SDK part and strictly positive integer prices");
    for (const auto &part : parts)
        require(part.nativeIndex < count, PN2S_INVALID, "PricedNest normalized part index");
    auto evaluation = recovered::pricedProblem(count, parts);
    require(evaluation.has_value(), PN2S_INVALID, "PricedNest problem evaluator");
    const auto policy = recovered::pricedEntryPolicy(0., 0.);
    inherited.initialPreset = policy.initialPreset;
    inherited.reversePreset = policy.reversePreset;
    inherited.engineBudget.usePostop9b0 = policy.usePostop9b0;
    inherited.usePostOptimization = false;
    inherited.problemEvaluation = std::move(evaluation);
    return inherited;
}
Output ordinaryPricedNest(const Input &input, Budget &budget,
                         std::span<const recovered::PricedSdkPart> parts,
                         SupervisorProgram inherited, SupervisorTrace *trace) {
    return ordinaryDefault(input, budget, pricedProgram(input.parts.size(), parts, std::move(inherited)), trace);
}
}
