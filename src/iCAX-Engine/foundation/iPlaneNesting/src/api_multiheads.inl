// Include after api.cpp's TU-private Session/Result and publication helpers.
extern "C++" {
#include "recovered_multiheads_sdk.hpp"
#include "recovered_multiheads_query.hpp"
#include "recovered_multiheads_infer.hpp"
namespace {
[[maybe_unused]] void apiMultiheadsConfigureSupervisor(const Session &session,SupervisorProgram &program){
    program.multiheads=session.multiheads.hardware;
    program.materialUnitCost10=session.multiheads.materialUnitCost10;
    program.masterCuttingUnitCost18=session.multiheads.masterCuttingUnitCost18;
    program.additionalHeadCuttingUnitCost20=session.multiheads.additionalHeadCuttingUnitCost20;
}

[[maybe_unused]] void apiMultiheadsValidateSisters(const Result &result,
    const std::vector<std::vector<Id>> &groups){
    std::set<Id> placed,linked;
    for(const auto &part:result.placed)require(placed.insert(part.part).second,PN2S_INTERNAL,
        "A multi-head result repeats a concrete SDK instance");
    for(const auto &group:groups)for(const auto part:group){
        require(placed.contains(part),PN2S_INTERNAL,"A serialized sister is absent from its actual SDK result");
        require(linked.insert(part).second,PN2S_INTERNAL,"A serialized sister has multiple native group owners");
    }
}

[[maybe_unused]] void apiMultiheadsStoreTrace(Result &result,const SupervisorTrace &trace,
    const std::vector<std::vector<Id>> &instances){
    std::vector<std::vector<Id>> groups;groups.reserve(trace.sisterGroups.size());
    for(const auto &source:trace.sisterGroups){
        std::vector<Id> group;group.reserve(source.size());
        for(const auto [type,ordinal]:source){require(type<instances.size()&&ordinal<instances[type].size(),
            PN2S_INTERNAL,"Native sister trace exceeds the current concrete SDK mapping");group.push_back(instances[type][ordinal]);}
        if(!group.empty())groups.push_back(std::move(group));
    }
    apiMultiheadsValidateSisters(result,groups);result.multiheadsSisters=std::move(groups);
}

// The caller provides the actual instance replacement map. A partially rebound
// native sister group cannot be silently truncated or split into single heads.
[[maybe_unused]] void apiMultiheadsRebindSisters(const Result &source,Result &destination,
    const std::map<Id,Id> &mapping){
    std::set<Id> placed;for(const auto &part:destination.placed)placed.insert(part.part);
    std::vector<std::vector<Id>> groups;
    for(const auto &original:source.multiheadsSisters){
        std::vector<Id> group;group.reserve(original.size());
        for(const auto part:original)if(const auto found=mapping.find(part);found!=mapping.end()){
            require(placed.contains(found->second),PN2S_INTERNAL,"Sister rebind points outside the destination SDK result");
            group.push_back(found->second);
        }
        require(group.empty()||group.size()==original.size(),PN2S_UNSUPPORTED,
                "A native multi-head sister group must be rebound or removed as a whole");
        if(!group.empty())groups.push_back(std::move(group));
    }
    for(const auto &existing:destination.multiheadsSisters){
        const auto available=std::count_if(existing.begin(),existing.end(),[&](Id part){return placed.contains(part);});
        require(available==0||size_t(available)==existing.size(),PN2S_UNSUPPORTED,
                "A retained native sister group must remain complete");
        if(available&&std::find(groups.begin(),groups.end(),existing)==groups.end())groups.push_back(existing);
    }
    apiMultiheadsValidateSisters(destination,groups);destination.multiheadsSisters=std::move(groups);
}

[[maybe_unused]] void apiMultiheadsRetainSisters(const Result &source,Result &destination){
    std::set<Id> placed;for(const auto &part:destination.placed)placed.insert(part.part);
    std::map<Id,Id> mapping;for(const auto &part:source.placed)if(placed.contains(part.part))mapping.emplace(part.part,part.part);
    apiMultiheadsRebindSisters(source,destination,mapping);
}

// Read the real committed native owners once at serialization, retaining their
// group58 member order. Geometry similarity is never used to reconstruct links.
[[maybe_unused]] void apiMultiheadsCaptureSisters(Result &result,
    std::span<const recovered::WarmPlacementPtr> owners){
    require(owners.size()==result.placed.size(),PN2S_INTERNAL,"Native multi-head owner/output counts differ");
    std::map<const recovered::ProcessPlacement *,Id> identities;
    for(size_t n=0;n<owners.size();++n){require(bool(owners[n]),PN2S_INTERNAL,"Missing native multi-head placement owner");
        require(identities.emplace(owners[n].get(),result.placed[n].part).second,PN2S_INTERNAL,
            "A native multi-head placement owner occurs twice");}
    std::set<const recovered::PlacementGroup *> visited;std::vector<std::vector<Id>> groups;
    for(const auto &owner:owners){const auto group=owner->group58;if(!group||group->members.empty()||!visited.insert(group.get()).second)continue;
        std::vector<Id> ids;ids.reserve(group->members.size());
        for(const auto &weak:group->members){const auto member=weak.lock();
            require(member&&member->group58==group&&identities.contains(member.get()),PN2S_UNSUPPORTED,
                "Native sister serialization requires the complete live committed group");
            ids.push_back(identities.at(member.get()));}
        groups.push_back(std::move(ids));
    }
    apiMultiheadsValidateSisters(result,groups);result.multiheadsSisters=std::move(groups);
}

// Call only at a recovered layout-import boundary (3b9de0), with real geometry
// module+D0 keys in output order. This is not an ordinary commit/sync hook.
[[maybe_unused]] void apiMultiheadsInferImportedResult(Result &result,
    const recovered::MultiheadsHardware &hardware,std::span<const int64_t> moduleKeys){
    require(moduleKeys.size()==result.placed.size(),PN2S_INVALID,"Imported native module keys must match the actual SDK output");
    apiMultiheadsValidateSisters(result,result.multiheadsSisters);
    std::set<Id> linked;for(const auto &group:result.multiheadsSisters)linked.insert(group.begin(),group.end());
    std::vector<recovered::MultiheadsInferPlacement> placements;placements.reserve(result.placed.size());
    for(size_t n=0;n<result.placed.size();++n){const auto &part=result.placed[n];
        require(grid_exact(part.position.x,1e6)&&grid_exact(part.position.y,1e3),PN2S_UNSUPPORTED,
            "Imported sister inference requires exact native integer placement coordinates");
        const auto position=recovered::shakerSdkUnchangedPosition(part.position);
        placements.push_back({position.x,moduleKeys[n],position.y,linked.contains(part.part)});
    }
    auto groups=result.multiheadsSisters;
    for(const auto &source:recovered::multiheadsInferGroups(placements,hardware)){
        std::vector<Id> group;group.reserve(source.size());for(const auto index:source)group.push_back(result.placed.at(index).part);
        if(!group.empty())groups.push_back(std::move(group));
    }
    apiMultiheadsValidateSisters(result,groups);result.multiheadsSisters=std::move(groups);
}
}
}

extern "C" PN2S_API PN2_ErrorCode PN2S_CALL PN2_SetCuttingHeadsNumber(PN2_Session h,int count){
    return invoke<int>([&]{return recovered::multiheadsSetNumber(session(h).multiheads,count);});
}
extern "C" PN2S_API PN2_ErrorCode PN2S_CALL PN2_SetCuttingHeadsMinimumGap(PN2_Session h,double gap){
    return invoke<int>([&]{return recovered::multiheadsSetMinimumGap(session(h).multiheads,gap);});
}
extern "C" PN2S_API PN2_ErrorCode PN2S_CALL PN2_SetCuttingHeadsMaximumGap(PN2_Session h,double gap){
    return invoke<int>([&]{return recovered::multiheadsSetMaximumGap(session(h).multiheads,gap);});
}
extern "C" PN2S_API PN2_ErrorCode PN2S_CALL PN2_SetAdditionnalHeadCuttingUnitCost(PN2_Session h,double cost){
    return invoke<int>([&]{return recovered::multiheadsSetAdditionalCuttingCost(session(h).multiheads,cost);});
}
extern "C" PN2S_API PN2_ErrorCode PN2S_CALL PN2_GetPartMultipleHeadsNumber(
    PN2_Session h,PN2_Result r,PN2_Part part,int *count){
    return invoke<int>([&]{const auto &result=lookup(session(h).results,id(r));
        require(count,PN2S_INVALID,"Null multi-head count output");
        *count=recovered::multiheadsLinkedCount(result.multiheadsSisters,id(part));return 0;});
}
extern "C" PN2S_API PN2_ErrorCode PN2S_CALL PN2_GetPartMultipleHeadsLinkedPart(
    PN2_Session h,PN2_Result r,PN2_Part part,int index,PN2_Part *linked){
    return invoke<int>([&]{const auto &result=lookup(session(h).results,id(r));
        require(linked,PN2S_INVALID,"Null linked multi-head part output");
        uintptr_t value=0;const auto status=recovered::multiheadsLinkedPart(result.multiheadsSisters,id(part),index,value);
        if(status==0)*linked=handle<PN2_Part>(value);return status;});
}
