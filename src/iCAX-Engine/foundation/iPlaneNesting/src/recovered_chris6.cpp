#include "recovered_chris6.hpp"

namespace study::recovered {
double UserEvaluation::value(size_t nativeIndex) const {
    require(nativeIndex < values.size(), PN2S_INVALID, "UserEvaluation part index outside supplied problem");
    return values[nativeIndex];
}
std::optional<UserEvaluation> userEvaluation(size_t nativePartCount,
                                            std::span<const UserEvaluationPart> parts) {
    UserEvaluation result;
    result.values.resize(nativePartCount, 0.);
    bool positive = true;
    for (const auto &part : parts) {
        if (!(part.profit > 0.)) positive = false;
        else {
            require(part.nativeIndex < nativePartCount, PN2S_INVALID,
                    "SDK evaluation record references an unknown normalized part");
            result.values[part.nativeIndex] = part.profit;
        }
    }
    if (!positive) return std::nullopt;
    return result;
}
std::vector<OffsetChildPlan> chris6Factory(Random &random, const UserEvaluation &evaluation,
                                         const OffsetFactoryInput &settings) {
    OffsetValuePlan value;
    value.values = evaluation.values;
    // The UserEvaluation constructor leaves both reference owners empty.
    // 44c170 binds an empty primary owner to the context normal reference when
    // the right-to-left candidater prepares it; the standard Offset adapter
    // represents that owner as ReferenceKind::Normal.
    return offsetChildren(random, settings, std::span<const OffsetValuePlan>(&value, 1));
}
}
