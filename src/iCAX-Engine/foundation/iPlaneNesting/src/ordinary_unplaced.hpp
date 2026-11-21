#pragma once
#include "ordinary_runtime.hpp"
#include "recovered_unplaced.hpp"

namespace study {
// The actual TwoPartsPointsProximityConstraintsGroup<X/Y> inputs: two concrete
// module instances, one local point per module, an axial tolerance, and +48/+49
// selecting the modules whose future placement is protected by manager 10.
struct UnplacedProgram {
    recovered::Random random{1};
    recovered::OffsetChildPlan child;
    std::vector<PointsProximityGroup> groups;
};
struct UnplacedTrace {size_t evaluations=0,moduleProbes=0,blocked=0,restores=0,committed=0;};
// A partial candidate may omit an endpoint; every pair that is present must
// satisfy the same original quantized interval used during placement.
bool validPointsProximity(const Input &,const Output &,std::span<const PointsProximityGroup>);
UnplacedProgram unplacedProgram(const Input &,std::span<const PointsProximityGroup>,
                               recovered::OffsetFactoryInput={},const recovered::Random * =nullptr);
int ordinaryUnplacedContinue(const Input &,const UnplacedProgram &,Budget &,OrdinaryState &,UnplacedTrace * =nullptr);
Output ordinaryUnplaced(const Input &,const UnplacedProgram &,Budget &,UnplacedTrace * =nullptr);
Output ordinaryReverseUnplaced(const Input &,std::span<const PointsProximityGroup>,Budget &,
                              recovered::OffsetFactoryInput={},OrdinaryState *existing=nullptr,ReverseTrace * =nullptr);
}
