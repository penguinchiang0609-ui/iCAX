#pragma once
#include "recovered_multiheads.hpp"
#include "recovered_warm.hpp"
#include "recovered_shaker_constraints.hpp"
namespace study {
struct MultiheadsGeometryTrace {
    int32_t divisions=0;
    recovered::MultiheadsDivisionPlan plan;
    std::vector<int32_t> childMethods;
    size_t seedPlacements=0,replicaPlacements=0,rolledBack=0;
    std::vector<std::vector<size_t>> simultaneousParts;
};
// A supplied geometric worker receives the actual narrow sheet and reduced
// quantities; the continuation receives the restored original input and warm
// placements. Methods 1/2/3 mean the original Offset/Rectangle/Pattern factories.
// The supplied worker must preserve warm ordinal/pose identities in its output.
using MultiheadsGeometryChild=std::function<Output(const Input &,const Output &,int32_t,Budget &)>;
struct MultiheadsGeometryProgram {
    recovered::MultiheadsChild child;
    recovered::Random random{1};
    double maximumPartGap=0;
    int32_t forcedMethod=1;
    std::vector<recovered::MultiheadsMethodGroup> methodGroups;
    bool hasPatternProvider=false;
    MultiheadsGeometryChild geometry;
};
struct MultiheadsGeometryResult {
    Output output;
    // These owners have exactly the output order. Concurrent sisters share
    // the recovered ProcessPlacement::group58 owner and ordered weak members;
    // warm copying must use copyPlacementRelations/clonePlacement, not copy
    // Output records alone. instance40 is the stable zero-based catalog index.
    std::vector<recovered::WarmPlacementPtr> placements;
};
// 6506e1..650a15: the first member emits X and Y equality constraints to
// every other sister in group order. The optional map translates output
// indexes to Shaker indexes. A partial, expired or inconsistent group is
// rejected so warm selection cannot silently detach a concurrent head.
std::vector<recovered::ShakerPairConstraint> multiheadsShakerConstraints(
    std::span<const recovered::WarmPlacementPtr>,std::span<const size_t> shakerIndexes={});
MultiheadsGeometryResult ordinaryMultiheadsDivisionResult(const Input &,Budget &,
    const MultiheadsGeometryProgram &,MultiheadsGeometryTrace * =nullptr);
// Native division and replication controllers operating on real input shapes.
// Hardware/pre-nest admission belongs to the caller's manager 9 factory.
Output ordinaryMultiheadsDivision(const Input &,Budget &,const MultiheadsGeometryProgram &,
                                  MultiheadsGeometryTrace * =nullptr);
} // namespace study
