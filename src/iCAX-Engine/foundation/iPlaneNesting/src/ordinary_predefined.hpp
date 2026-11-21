#pragma once
#include "ordinary_runtime.hpp"
#include "recovered_predefined.hpp"

namespace study {
struct OrdinaryPredefinedRecord {
    size_t group = 0, ordinal = 0;
    recovered::PredefinedSdkRecord sdk;
};
struct OrdinaryPredefinedProgram {
    std::vector<OrdinaryPredefinedRecord> records;
    Point sheetOrigin{};
    bool moveDefault = false;
    double xWeight778 = 2, yWeight780 = .5;
    bool checkConstraints4e0 = false;
};
struct OrdinaryPredefinedTrace {
    size_t difficultyQueries = 0, variantQueries = 0, xQueries = 0, yQueries = 0;
    size_t conflicting = 0, unavailable = 0;
    std::vector<Placement> committed;
};
struct OrdinaryPredefinedLock {size_t placementIndex = 0; bool fixed = true;};
// This is the actual mandatory-constraint phase, invoked after candidate reset
// and before the main strategy. It neither checks material collisions nor
// samples/truncates the source using warm-start heuristics.
uint32_t ordinaryApplyPredefined(const Input &, const OrdinaryPredefinedProgram &, Budget &,
                                OrdinaryState &, OrdinaryPredefinedTrace * = nullptr);
Output ordinaryPreNest(const Input &, const OrdinaryPredefinedProgram &, Budget &,
                       OrdinaryPredefinedTrace * = nullptr);
// A continuation callback must run against the supplied initialized real state.
// Existing accepted placements remain strong owners in the same lifecycle.
Output ordinaryNestMore(const Input &, const OrdinaryPredefinedProgram &, Budget &, const ReverseRunner &,
                        OrdinaryPredefinedTrace * = nullptr);
std::vector<OrdinaryPredefinedLock> ordinaryPredefinedLocks(const Output &,
    const OrdinaryPredefinedProgram &, bool moveDefault);
}
