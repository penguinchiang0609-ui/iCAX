#pragma once
#include "recovered_factory.hpp"

namespace study::recovered {
// SDK part-record +18 -> normalized Part +258. 592300 initializes +18 to
// -1; only an explicit pricing input makes the ordinary SDK create this owner.
struct UserEvaluationPart {
    size_t nativeIndex = 0;
    double profit = -1;
};
struct UserEvaluation {
    std::vector<double> values;
    double value(size_t nativeIndex) const; // 365b50
};
// The 5ce6d0 producer preserves record order, zero-initializes uncovered slots,
// and installs no evaluator unless every profit is strictly positive. The
// COMISD/JBE pair also rejects unordered (NaN) inputs.
std::optional<UserEvaluation> userEvaluation(size_t nativePartCount,
                                            std::span<const UserEvaluationPart>);
// 44f260 -> 44ce90(false,false,true): one supplied evaluator, no value-factory
// draw and no auxiliary reference. Candidater and inspection draws are shared
// with the original Offset child factory.
std::vector<OffsetChildPlan> chris6Factory(Random &, const UserEvaluation &,
                                         const OffsetFactoryInput & = {});
}
