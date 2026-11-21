#pragma once
#include "recovered_shaker.hpp"
namespace study::recovered {
struct ShakerPairConstraint {
    size_t first=0,second=0;
    int32_t type=0;
    double value=0;
    // Only types with axis 2 consume these immutable creation-time angles.
    std::array<double,2> referenceAngles{};
};
int32_t shakerPairConstraintAxis(int32_t type); // 723e60
// 7242b0: first/second sparse order, fixed-axis substitution, exact bound
// selection and row ownership; these are hard rows without collision slack.
size_t shakerPairConstraintRows(const ShakerPairConstraint &,bool rotation,
                               std::span<ShakerPart>,LinearModel &);
}
