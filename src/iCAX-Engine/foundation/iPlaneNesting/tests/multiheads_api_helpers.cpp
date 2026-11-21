// Independent integration executable: reuse the real API TU and exercise its
// private serialization/rebind helpers on actual native sister owners.
#define ALMA_STUDY_BUILD
#include "../src/api.cpp"
#include "ordinary_multiheads_manager.hpp"
#include <iostream>

namespace {
size_t helperChecks=0;
void verify(bool value,const char *message){++helperChecks;if(!value)throw std::runtime_error(message);}
void expectIncomplete(auto &&function){try{function();throw std::runtime_error("Expected incomplete native sister group rejection");}catch(const Failure &failure){verify(failure.code==PN2S_UNSUPPORTED,"Partial native sister group has explicit unsupported status");}}
}
int main(){PN2_Session h=nullptr;try{
    h=PN2_CreateSession();verify(!PN2_IsObjectError(h),"Create real SDK session for native sister serialization");
    const PN2_Point polygon[]{{0,0},{30,0},{30,20},{0,20}};const auto shape=PN2_AddShape(h,polygon,4,{0,0});const auto pose=PN2_CreateOrientation(0);
    std::array<PN2_Part,12> originalIds{},replacementIds{};verify(PN2_AddSeveralParts(h,shape,&pose,1,12,originalIds.data())==0&&PN2_AddSeveralParts(h,shape,&pose,1,12,replacementIds.data())==0,"Allocate actual distinct SDK IDs for both concrete catalogs");const auto sheet=PN2_AddSheet(h,100,160);
    Input input;input.width=160;input.height=100;input.pitch=1;input.parts={{{{0,0},{30,0},{30,20},{0,20}},{},{{0,false}},12}};
    Budget time(20.);const auto program=multiheadsProgram(input,{3,-1,20,48},0);const auto native=ordinaryMultiheadsManagerResult(input,time,program);
    verify(native.output.placements.size()==12&&valid_layout(input,native.output),"Actual specialized geometric children produce valid complete native material");
    Result source;source.sheet=id(sheet);for(const auto &part:native.output.placements){source.placed.push_back({id(originalIds.at(part.ordinal)),{double(part.x)/1e6,double(part.y)/1000.},pose});}
    apiMultiheadsCaptureSisters(source,native.placements);verify(!source.multiheadsSisters.empty(),"Capture actual committed group58 owners into stable concrete SDK IDs");const auto saved=source.multiheadsSisters;
    for(const auto &group:saved)for(size_t n=0;n<group.size();++n){verify(recovered::multiheadsLinkedCount(saved,group[n])==int32_t(group.size()),"Every real sister exposes its actual saved native count");uintptr_t linked=0;verify(recovered::multiheadsLinkedPart(saved,group[n],int32_t(n),linked)==0&&linked==group[n],"Captured SDK sister order matches actual native owner order");}
    Result rebound=source;rebound.multiheadsSisters.clear();std::map<Id,Id> mapping;for(size_t n=0;n<originalIds.size();++n)mapping.emplace(id(originalIds[n]),id(replacementIds[n]));for(auto &part:rebound.placed)part.part=mapping.at(part.part);apiMultiheadsRebindSisters(source,rebound,mapping);
    verify(rebound.multiheadsSisters.size()==saved.size(),"Whole native groups survive actual catalog ID replacement");for(size_t n=0;n<saved.size();++n)for(size_t k=0;k<saved[n].size();++k)verify(rebound.multiheadsSisters[n][k]==mapping.at(saved[n][k]),"Every sister slot uses the new concrete SDK identity without stale template IDs");
    const auto &removed=saved.front();const std::set<Id> removedIds(removed.begin(),removed.end());Result trimmed=source;std::erase_if(trimmed.placed,[&](const auto &part){return removedIds.contains(part.part);});apiMultiheadsRetainSisters(source,trimmed);verify(trimmed.multiheadsSisters.size()+1==saved.size(),"Copy followed by whole-group removal drops its old serialized metadata");for(const auto &group:trimmed.multiheadsSisters)for(const auto part:group)verify(!removedIds.contains(part),"No removed SDK ID remains in serialized groups");
    Result partial=source;std::erase_if(partial.placed,[&](const auto &part){return part.part==removed.front();});const auto before=partial.multiheadsSisters;expectIncomplete([&]{apiMultiheadsRetainSisters(source,partial);});verify(partial.multiheadsSisters==before,"Partial retention failure does not mutate destination metadata");
    auto incompleteMapping=mapping;incompleteMapping.erase(removed.front());Result failedRebind=rebound;failedRebind.multiheadsSisters.clear();const auto beforeRebind=failedRebind.multiheadsSisters;expectIncomplete([&]{apiMultiheadsRebindSisters(source,failedRebind,incompleteMapping);});verify(failedRebind.multiheadsSisters==beforeRebind,"Partial rebind failure does not publish half a native group");verify(source.multiheadsSisters==saved,"All copies and rejected rebinds keep the actual original source unchanged");
    // Infer only through an explicit imported-layout service and actual native
    // Catalog module keys, preserving preexisting native groups untouched.
    OrdinaryState prepared;OrdinaryControl initialize;initialize.placementLimit=0;initialize.values={material_area(input.parts[0])};ordinaryRun(input,time,prepared,&initialize);std::vector<int64_t> modules;for(const auto &part:native.output.placements){const auto record=prepared.catalog->groups.at(part.group).at(part.poseIndex);modules.push_back(int64_t(prepared.catalog->records.at(record).module));}
    Result imported=source;imported.multiheadsSisters.clear();apiMultiheadsInferImportedResult(imported,{3,-1,20,48},modules);verify(!imported.multiheadsSisters.empty(),"Explicit import inference consumes real native Catalog module identities");const auto importedBefore=imported.multiheadsSisters;apiMultiheadsInferImportedResult(imported,{3,-1,20,48},modules);verify(imported.multiheadsSisters.size()>=importedBefore.size()&&std::equal(importedBefore.begin(),importedBefore.end(),imported.multiheadsSisters.begin()),"Repeated explicit inference retains existing complete native groups without rewriting their members");std::set<Id> alreadyLinked;for(const auto &group:importedBefore)alreadyLinked.insert(group.begin(),group.end());for(size_t n=importedBefore.size();n<imported.multiheadsSisters.size();++n)for(const auto part:imported.multiheadsSisters[n])verify(!alreadyLinked.contains(part),"Only previously unlinked material can enter a later explicit inferred group");
    PN2_DeleteSession(h);std::cout<<"{\"pass\":true,\"nativePlacements\":"<<source.placed.size()<<",\"nativeGroups\":"<<saved.size()<<",\"inferredGroups\":"<<imported.multiheadsSisters.size()<<",\"checks\":"<<helperChecks<<"}\n";return 0;
}catch(const std::exception &error){if(h&&!PN2_IsObjectError(h))PN2_DeleteSession(h);std::cerr<<error.what()<<'\n';return 1;}}
