#pragma once
#include "ordinary_runtime.hpp"
#include "recovered_chris.hpp"

namespace study {
struct ChrisProgram {
    MultiProgram common;
    // Original debug fields retain CVTTSD2SI semantics. Both vendor defaults
    // are zero (P_DEBUG_TRACE_MULTI_P01 / P_DEBUG_BT_NESTER_GUI).
    double config700 = 0, config710 = 0;
    int32_t managerId = 5;
};
BtProgram chrisProgram(const Input &, const ChrisProgram &, recovered::ChrisFactoryResult * = nullptr);
Output ordinaryChris(const Input &, const ChrisProgram &, Budget &, BtTrace * = nullptr);
int ordinaryChrisContinue(const Input &, const ChrisProgram &, Budget &, OrdinaryState &, BtTrace * = nullptr);
Output ordinaryReverseChris(const Input &, const ChrisProgram &, Budget &,
                            OrdinaryState *existing = nullptr, ReverseTrace * = nullptr);
}
