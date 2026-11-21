#pragma once
#include "core.hpp"
#include "recovered_shaker_constraints.hpp"
namespace study {
// 43e8a0/43e4e0 -> 43dad0 -> 70e870/715db0. Mapping is in output placement
// order; an empty mapping uses that order directly as Shaker part indices.
std::vector<recovered::ShakerPairConstraint> ordinaryShakerPointConstraints(
    const Input &,const Output &,std::span<const size_t> shakerPartIndices={});
}
