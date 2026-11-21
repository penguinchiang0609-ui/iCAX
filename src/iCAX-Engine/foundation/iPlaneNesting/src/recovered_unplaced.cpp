#include "recovered_unplaced.hpp"

namespace study::recovered {
bool unplacedModuleActive(const UnplacedModule &module){return module.nestedCount==0&&!module.constraintBlocksProbe;}
std::vector<UnplacedModule> unplacedModules(std::span<const std::vector<UnplacedModule>> groups){
    std::map<uint64_t,UnplacedModule> unique;
    for(const auto &group:groups)for(const auto &module:group)
        if(unplacedModuleActive(module))unique.try_emplace(module.identity,module);
    std::vector<UnplacedModule> result;for(const auto &[identity,module]:unique)result.push_back(module);return result;
}
double unplacedPenalty(double baseScore,uint64_t count){
    const double converted=count<=uint64_t(INT64_MAX)?double(int64_t(count)):double(int64_t((count>>1)|(count&1)))*2.;
    return (converted*10000000000.+1.)*baseScore;
}
std::array<int64_t,2> proximityAnchorBand(double coordinate,double tolerance,double scale){
    return {rounded((coordinate-tolerance)*scale),rounded((coordinate+tolerance)*scale)};
}
std::array<int64_t,2> proximityTargetBand(std::array<int64_t,2> band,double origin,double target,double scale){
    const int64_t offset=rounded((origin-target)*scale);band[0]+=offset;band[1]+=offset;return band;
}
double evaluateUnplacedCandidate(UnplacedPenaltyHost &host){
    const double score=host.baseScore();
    uint64_t blocked;
    try{host.commitCandidate();blocked=host.blockedModules();}catch(...){host.restoreSnapshot();throw;}
    host.restoreSnapshot();
    return unplacedPenalty(score,blocked);
}
OffsetChildPlan unplacedFactory(Random &random,const OffsetFactoryInput &settings,
                                const std::vector<ValuePart> &parts,double totalArea){
    const auto evaluation=offsetValue(random,settings,parts,totalArea);
    auto child=offsetChildren(random,settings,std::span<const OffsetValuePlan>(&evaluation,1)).front();
    // 44bf10 prepares the supplied evaluator but does not perform 44c170's
    // empty-reference assignment to context+88. 402e50 therefore takes its
    // ordinary context+78 fallback for a Normal evaluator, even right-to-left.
    child.evaluationReferenceScale=1.;
    return child;
}
}
