#pragma once
#include "ordinary_runtime.hpp"
#include "recovered_chris6.hpp"

namespace study {
OffsetProgram chris6Program(const Input &, const recovered::UserEvaluation &,
                           recovered::OffsetFactoryInput = {}, const recovered::Random * = nullptr);
Output ordinaryChris6(const Input &, const recovered::UserEvaluation &, Budget &,
                      recovered::OffsetFactoryInput = {}, OffsetTrace * = nullptr);
int ordinaryChris6Continue(const Input &, const OffsetProgram &, Budget &, OrdinaryState &,
                          OffsetTrace * = nullptr);
Output ordinaryReverseChris6(const Input &, const recovered::UserEvaluation &, Budget &,
                            recovered::OffsetFactoryInput = {}, OrdinaryState *existing = nullptr,
                            ReverseTrace * = nullptr);
}
