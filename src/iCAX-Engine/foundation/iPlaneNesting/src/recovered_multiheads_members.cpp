#include "recovered_multiheads_members.hpp"
#include <bit>
namespace study::recovered {
namespace {
int64_t add64(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)+uint64_t(b));}
int64_t subtract64(int64_t a,int64_t b){return std::bit_cast<int64_t>(uint64_t(a)-uint64_t(b));}
int64_t rounded64(double value){value+=value<0?-.5:.5;return value>=-0x1p63&&value<0x1p63?int64_t(value):INT64_MIN;}
int32_t rounded32(double value){return truncateInt32(value<0?value-.5:value+.5);}
}
std::vector<size_t> instantiateMultiheadsMembers(const MultiheadsInstantiationInput &input,MultiheadsInstantiationHost &host){
    require(input.gridPitch!=0,PN2S_INVALID,"Multiheads module grid pitch is zero");
    std::vector<int32_t> sourceSlots(input.members.size(),-1);std::vector<size_t> variants,filtered;
    for(size_t n=0;n<input.members.size();++n){const auto &member=input.members[n];if(const auto variant=host.resolveVariant(member.part,member.orientation)){variants.push_back(*variant);filtered.push_back(n);}}
    const auto instances=host.allocateInstances(variants);
    require(instances.size()==variants.size(),PN2S_INVALID,"Multiheads module allocation returned an inconsistent instance count");
    std::vector<size_t> result;for(size_t n=0;n<filtered.size();++n){if(!instances[n])continue;const auto &member=input.members[filtered[n]];
        MultiheadsReplicaPosition position{add64(rounded64(member.position.x*1e6),input.origin.x),
            std::bit_cast<int32_t>(uint32_t(rounded32(member.position.y*1000.))+uint32_t(input.origin.y))};
        const auto left=add64(instances[n]->leftBound,position.x),relative=subtract64(left,input.gridOrigin);
        require(!(relative==INT64_MIN&&input.gridPitch==-1),PN2S_INVALID,"Multiheads module grid quotient overflows");
        auto quotient=relative/input.gridPitch;if(relative<0&&relative%input.gridPitch!=0)--quotient;
        const auto index=std::bit_cast<int32_t>(uint32_t(quotient));
        const auto grid=add64(std::bit_cast<int64_t>(uint64_t(int64_t(index))*uint64_t(input.gridPitch)),input.gridOrigin);
        result.push_back(host.createPlacement(instances[n]->identity,position,grid!=left,input.kind));
        // Native546270 uses the compressed resolved-member index here, not
        // the original source index retained in `filtered`.
        sourceSlots[n]=int32_t(result.size()-1);}
    for(const auto &link:input.commonCuts){require(link.first>=0&&link.second>=0&&size_t(link.first)<sourceSlots.size()&&size_t(link.second)<sourceSlots.size(),PN2S_INVALID,"Multiheads module common-cut slot is invalid");
        const auto first=sourceSlots[size_t(link.first)],second=sourceSlots[size_t(link.second)];
        if(first>=0&&second>=0)host.commonCut(result[size_t(second)],result[size_t(first)],link.value);}
    if(input.kind==4)host.createSisterGroup(result);return result;
}
std::vector<size_t> expandMultiheadsModule(const MultiheadsMemberModule &module,const MultiheadsMaterializationInput &input,
    MultiheadsInstantiationHost &host,MultiheadsMemberAdmissionHost &admission){
    auto members=instantiateMultiheadsMembers({module.members,module.kind,input.origin,input.gridPitch,input.gridOrigin,module.commonCuts},host);
    if((!input.hasPreNest||input.preNestCompatible)&&!multiheadsAdmitMembers(members,admission))members.clear();return members;
}
std::vector<size_t> materializeMultiheadsModule(std::span<const MultiheadsMemberModule> modules,uintptr_t selectedGeometry,
    const MultiheadsMaterializationInput &input,MultiheadsInstantiationHost &host,MultiheadsMemberAdmissionHost &admission,
    const std::function<void(bool)> &requireMultiple){
    const auto selected=std::find_if(modules.begin(),modules.end(),[&](const auto &module){return module.descriptor.geometry==selectedGeometry;});
    require(selected!=modules.end(),PN2S_INVALID,"Multiheads selected compound module was not found");
    auto result=expandMultiheadsModule(*selected,input,host,admission);if(requireMultiple)requireMultiple(result.size()>1);return result;
}
} // namespace study::recovered
