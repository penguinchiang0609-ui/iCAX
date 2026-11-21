#pragma once
#include "recovered_direction.hpp"
namespace study::recovered {
struct ComplexityConfig {
    double perimeterProbability=.5, perimeterScale=.05, alphaLimit=.3, gammaLimit=.3;
};
Complexity drawComplexity(Random &, const ComplexityConfig &);
int32_t truncateInt32(double); // x64 cvttsd2si, including indefinite result.
struct MultiFactoryInput {
    int32_t contextBatch238=1;
    ComplexityConfig config;
    ReferenceKind currentReference=ReferenceKind::Normal;
    double currentHeight=0, normalHeight=0;
    double config700=0, config710=0;
    int managerId=0;
};
struct MultiFactoryResult {
    int32_t count10=5, flag14=5, limit18=0, count1c=5, batch=0;
    Complexity complexity;
    std::vector<double> evaluationValues;
    ReferenceKind reference=ReferenceKind::Normal, currentReference=ReferenceKind::Normal;
    bool updateCurrentReference=false, flagD0=false, flagD8=false;
    int managerId=0;
};
// 44e880 -> 44f940 -> 3660f0. No manager selection or worker scheduling here.
MultiFactoryResult multiFactory(Random &, const MultiFactoryInput &, const std::vector<ValuePart> &, double totalArea);
struct AliasEntry { double threshold=1; uint32_t alias=0; };
static_assert(sizeof(AliasEntry)==16);
// 6533f0/653270: ordered work lists; active aliases only are defined.
std::vector<AliasEntry> aliasTable(std::span<const double>);
uint32_t aliasDraw(Random &, std::span<const double>);
}
