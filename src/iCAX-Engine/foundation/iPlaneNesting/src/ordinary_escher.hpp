#pragma once
#include "ordinary_runtime.hpp"
#include "recovered_escher_nester.hpp"
namespace study {
struct EscherProgram {
    recovered::Random random{1};
    recovered::OffsetValuePlan evaluation;
    OffsetProgram fallback;
    double contextSeconds=0;
    int32_t trial13bc=0;
    bool fallback90=true;
};
struct OrdinaryEscherTrace {
    size_t orderedParts=0,patternRequests=0,providers=0,rawPatterns=0,committed=0,rejectedPatterns=0,greedyCalls=0,fallbackCalls=0;
    size_t toroidalCandidates=0,toroidalSolves=0,toroidalImproved=0;
    size_t integerOuterContours=0,integerHoleContours=0,boundaryHoleOwners=0;
    std::vector<size_t> partOrder;
    std::set<std::string> missingProviders;
};
EscherProgram escherProgram(const Input &,double contextSeconds,
    recovered::OffsetFactoryInput = {},const recovered::Random * = nullptr);
int ordinaryEscherContinue(const Input &,const EscherProgram &,Budget &,OrdinaryState &,OrdinaryEscherTrace * = nullptr);
Output ordinaryEscher(const Input &,const EscherProgram &,Budget &,OrdinaryEscherTrace * = nullptr);
}
