#pragma once
#include "GeometryServices/MachineMotionPlanningService.h"
#include <algorithm>
#include <limits>
#include <string>

namespace iCAX::FiveAxisCAM::MachinePlanPolicy {
struct Budget {
    double MaximumSeconds=180;
    std::size_t MaximumVerificationSamples=100000;
};
// Product task allowance, not a geometry tolerance or an execution time claim.
// Keep a finite ceiling while budgeting for all source stations and body pairs.
inline Budget ForWorkload(std::size_t stations,std::size_t collisionPairs){
    Budget budget;
    budget.MaximumSeconds=std::clamp(60.+static_cast<double>(stations)*(.025+.0005*static_cast<double>(collisionPairs)),180.,600.);
    budget.MaximumVerificationSamples=std::max<std::size_t>(100000,std::min<std::size_t>(stations,2000000/64)*64);
    return budget;
}
inline bool IsAuthoritativeCollision(const iCAX::GeometryServices::MachineMotion::Result& result,
    const std::string& witnessStatus,const std::string& witnessReason,const std::string& witnessStation){
    namespace MM=iCAX::GeometryServices::MachineMotion;
    if(result.State!=MM::Status::Rejected||witnessStatus!="Blocked"||result.Diagnostic.empty()||result.Diagnostic!=witnessReason)return false;
    const auto absent=std::numeric_limits<std::size_t>::max();
    if(witnessStation=="Initial")return result.FailedStation==absent&&result.FailedEdge==absent;
    return (result.FailedStation!=absent&&witnessStation==std::to_string(result.FailedStation))||
        (result.FailedEdge!=absent&&witnessStation==std::to_string(result.FailedEdge));
}
}
