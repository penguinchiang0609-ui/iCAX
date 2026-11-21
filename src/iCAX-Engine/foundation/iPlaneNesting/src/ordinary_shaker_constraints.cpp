#include "ordinary_shaker_constraints.hpp"
#include <bit>
#include <immintrin.h>
namespace study {
namespace {
double sub(double a,double b){return _mm_cvtsd_f64(_mm_sub_sd(_mm_set_sd(a),_mm_set_sd(b)));}
double negate(double value){return std::bit_cast<double>(std::bit_cast<uint64_t>(value)^0x8000000000000000ULL);}
Point worldPoint(const Input &input,const ProximityEndpoint &endpoint,const Placement &placement){
    const auto &part=input.parts.at(endpoint.group);auto pose=part.poses.at(placement.poseIndex);
    if(std::isfinite(placement.solvedAngle))pose.angle=placement.solvedAngle;
    auto point=transform(Polygon{endpoint.point},part.pivot,pose).front();
    point.x+=(std::isfinite(placement.exactX)?placement.exactX:double(placement.x)/1e6);
    point.y+=(std::isfinite(placement.exactY)?placement.exactY:double(placement.y)/1000.);return point;
}
}
std::vector<recovered::ShakerPairConstraint> ordinaryShakerPointConstraints(
    const Input &input,const Output &output,std::span<const size_t> mapping){
    require(mapping.empty()||mapping.size()==output.placements.size(),PN2S_INVALID,"Shaker point constraint placement mapping");
    std::vector<recovered::ShakerPairConstraint> result;
    for(const auto &group:input.pointGroups){
        std::array<std::optional<size_t>,2> placements;
        for(size_t side=0;side<2;++side)for(size_t index=0;index<output.placements.size();++index){const auto &p=output.placements[index];const auto &endpoint=group.endpoints[side];if(p.group==endpoint.group&&p.ordinal==endpoint.ordinal){placements[side]=index;break;}}
        if(!placements[0]||!placements[1])continue;
        const auto first=worldPoint(input,group.endpoints[0],output.placements[*placements[0]]),second=worldPoint(input,group.endpoints[1],output.placements[*placements[1]]);
        const double difference=sub(group.axis==ProximityAxis::X?first.x:first.y,group.axis==ProximityAxis::X?second.x:second.y);
        const size_t a=mapping.empty()?*placements[0]:mapping[*placements[0]],b=mapping.empty()?*placements[1]:mapping[*placements[1]];
        const int32_t upper=group.axis==ProximityAxis::X?1:5;
        result.push_back({a,b,upper,sub(group.tolerance,difference)});
        result.push_back({a,b,upper+1,sub(negate(group.tolerance),difference)});
    }
    return result;
}
}
