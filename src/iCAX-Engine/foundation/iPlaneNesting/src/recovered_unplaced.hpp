#pragma once
#include "recovered_factory.hpp"

namespace study::recovered {
struct UnplacedModule {
    uint64_t identity=0;
    int32_t nestedCount=0;
    bool constraintBlocksProbe=false;
};
bool unplacedModuleActive(const UnplacedModule &); // 38b7c0(module,true)
// 403470: watched modules from constraint-group virtual +78, ascending module
// identity, first owner retained for duplicates, and active modules only.
std::vector<UnplacedModule> unplacedModules(std::span<const std::vector<UnplacedModule>>);
double unplacedPenalty(double baseScore,uint64_t blockedModules); // 403e10
std::array<int64_t,2> proximityAnchorBand(double coordinate,double tolerance,double scale); // 43e680/43e2b0
std::array<int64_t,2> proximityTargetBand(std::array<int64_t,2>,double origin,double target,double scale); // 43a940/43b600
class UnplacedPenaltyHost {
public:
    virtual ~UnplacedPenaltyHost()=default;
    virtual double baseScore()=0;
    virtual void commitCandidate()=0;
    virtual uint64_t blockedModules()=0;
    virtual void restoreSnapshot()=0;
};
// 403e10, also shared by 403c10 after it materializes its candidate placement.
double evaluateUnplacedCandidate(UnplacedPenaltyHost &);
OffsetChildPlan unplacedFactory(Random &,const OffsetFactoryInput &,
                                const std::vector<ValuePart> &,double totalArea); // 44d620/44bf10
}
