#pragma once
#include "recovered_multiheads.hpp"
namespace study::recovered {
struct MultiheadsAllocatedInstance {size_t identity=0;int64_t leftBound=0;};
class MultiheadsInstantiationHost {
public:
    virtual ~MultiheadsInstantiationHost()=default;
    virtual std::optional<size_t> resolveVariant(uintptr_t,PlacementOrientation)=0; //54ec10
    virtual std::vector<std::optional<MultiheadsAllocatedInstance>> allocateInstances(std::span<const size_t>)=0; //393310
    virtual size_t createPlacement(size_t instance,MultiheadsReplicaPosition,bool offGrid,int32_t kind)=0; //3c5220
    virtual void commonCut(size_t first,size_t second,double)=0; //3c5a00
    virtual void createSisterGroup(std::span<const size_t>)=0; //372390/3c5d00
};
struct MultiheadsInstantiationInput {
    std::span<const TilingMember> members;
    int32_t kind=4;
    MultiheadsReplicaPosition origin;
    int64_t gridPitch=1,gridOrigin=0;
    std::span<const TilingCommonCut> commonCuts;
};
//546270: resolve source variants in source order, allocate actual job
//instances as one batch, round coordinates to native1e6/1e3 grid, preserve
//the native signed32 grid quotient and compressed source-slot mapping, copy
//actual common cuts, and attach one shared sister owner only for kind4.
std::vector<size_t> instantiateMultiheadsMembers(const MultiheadsInstantiationInput &,MultiheadsInstantiationHost &);
struct MultiheadsMemberModule {
    MultiheadsModuleDescriptor descriptor;
    int32_t kind=4;
    std::vector<TilingMember> members;
    std::vector<TilingCommonCut> commonCuts;
};
struct MultiheadsMaterializationInput {
    MultiheadsReplicaPosition origin;
    int64_t gridPitch=1,gridOrigin=0;
    bool hasPreNest=false,preNestCompatible=true;
};
//546b60: instantiation is followed by temporary admission before the
//3eef90 observer, independently from the second admission after the observer.
std::vector<size_t> expandMultiheadsModule(const MultiheadsMemberModule &,const MultiheadsMaterializationInput &,
    MultiheadsInstantiationHost &,MultiheadsMemberAdmissionHost &);
//3eee70/3eed60: first geometry identity match, then generic module expansion.
std::vector<size_t> materializeMultiheadsModule(std::span<const MultiheadsMemberModule>,uintptr_t selectedGeometry,
    const MultiheadsMaterializationInput &,MultiheadsInstantiationHost &,MultiheadsMemberAdmissionHost &,
    const std::function<void(bool)> &requireMultiple);
} // namespace study::recovered
