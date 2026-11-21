#pragma once
#include "ordinary_multiheads_clustering.hpp"
namespace study {
struct OrdinaryState;
struct MultiheadsManagerProgram {
    recovered::MultiheadsHardware hardware;
    recovered::MultiheadsSpecialInput admission;
    int32_t trial=0;
    double selection458=2,reporting488=0;
    MultiheadsGeometryProgram division;
    MultiheadsClusteringProgram clustering;
};
struct MultiheadsManagerTrace {
    std::vector<recovered::MultiheadsChildKind> children;
    std::vector<size_t> placed;
    std::vector<recovered::FinalizeKey> keys;
    std::optional<size_t> selected;
    MultiheadsGeometryTrace division;
    MultiheadsClusteringTrace clustering;
};
//44dcb0 ->3ec880/3ecb20 with both actual specialized geometric children.
//This adapter currently admits empty rectangular layouts; warm group replay,
//original composed source/cache order and extra cost/constraint families are
//explicit remaining boundaries, not aliases to another manager.
MultiheadsGeometryResult ordinaryMultiheadsManagerResult(const Input &,Budget &,
    const MultiheadsManagerProgram &,MultiheadsManagerTrace * =nullptr);
MultiheadsManagerProgram multiheadsProgram(const Input &,const recovered::MultiheadsHardware &,int32_t trial,
    const recovered::Random *initialRandom=nullptr,MultiheadsGeometryChild divisionFactories={});
// Existing ordinary catalog, geometry and committed owners remain authoritative.
// Warm groups are admitted only when complete. New results commit through the
//ordinary lifecycle, then copy their actual sister/process associations.
int ordinaryMultiheadsContinue(const Input &,const MultiheadsManagerProgram &,Budget &,OrdinaryState &,
    MultiheadsManagerTrace * =nullptr);
} // namespace study
