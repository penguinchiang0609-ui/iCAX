#pragma once
#include "ordinary_multiheads.hpp"
#include "recovered_multiheads_members.hpp"
namespace study {
struct MultiheadsClusteringProgram {
    recovered::MultiheadsChild child;
    recovered::Random random{1};
    recovered::OffsetFactoryInput valueFactory;
    int32_t trial=0;
    std::shared_ptr<const MultiheadsGeometryResult> warm;
};
struct MultiheadsClusteringTrace {
    std::vector<recovered::MultiheadsClusterKey> keys;
    size_t modules=0,candidates=0,compoundCommits=0,singleCommits=0,temporaryValidations=0,temporaryRemovals=0;
    bool cardinalOnly=false;
};
// Real compound geometry bridge: all sisters participate in the free map and
//in the reference-overlap integral. The recovered Greedy/next/count/instance/
//admission controllers retain whole shared groups. Original composed cache,
//full source-order ties and all specialized constraint/evaluation families
//remain separate equivalence boundaries; this is not full SDK equivalence.
MultiheadsGeometryResult ordinaryMultiheadsClusteringResult(const Input &,Budget &,
    const MultiheadsClusteringProgram &,MultiheadsClusteringTrace * =nullptr);
} // namespace study
