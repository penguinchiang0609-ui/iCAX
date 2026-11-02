#include "pch.h"
#include "EditableToolpathClone.h"
#include "EditableToolpathInternal.h"

namespace iCAX::CAM::Intent::Editable
{
namespace
{
std::string RewriteText(std::string value,const std::map<IntentId,IntentId>& ids)
{
    // One pass over the original string prevents an assigned UUID from being
    // rewritten again when it happens to contain another old token.
    std::string result;for(std::size_t i=0;i<value.size();)
    {bool found=false;for(const auto& [oldId,newId]:ids){const auto old=Key(oldId);if(value.compare(i,old.size(),old)==0){result+=Key(newId);i+=old.size();found=true;break;}}if(!found)result+=value[i++];}return result;
}
Variant Remap(const Variant& value,const std::map<IntentId,IntentId>& ids)
{
    if(value.Is<IntentId>()){const auto id=ID(value);const auto found=ids.find(id);return found==ids.end()?value:Variant(found->second);}
    if(value.Is<std::string>())return RewriteText(value.To<std::string>(),ids);
    if(value.Is<Array>()){Array result;for(const auto& item:value.To<Array>())result.push_back(Remap(item,ids));return result;}
    if(value.Is<Map>()){Map result;for(const auto& [key,item]:value.To<Map>())Require(result.emplace(RewriteText(key,ids),Remap(item,ids)).second,"TOOLPATH_CLONE_KEY_COLLISION");return result;}
    return value;
}
Variant Reframe(const Variant& value,const std::map<std::string,std::string>& frames)
{
    if(value.Is<Array>()){Array result;for(const auto& item:value.To<Array>())result.push_back(Reframe(item,frames));return result;}
    if(value.Is<Map>()){Map result;for(const auto& [key,item]:value.To<Map>())
        if((key=="frameId"||key=="frame")&&item.Is<std::string>()&&frames.contains(item.To<std::string>()))result[key]=frames.at(item.To<std::string>());else result[key]=Reframe(item,frames);return result;}
    return value;
}
Variant Resign(const Variant& value,const std::map<std::string,std::string>& fingerprints)
{
    if(value.Is<Array>()){Array result;for(const auto& item:value.To<Array>())result.push_back(Resign(item,fingerprints));return result;}
    if(value.Is<Map>()){Map result;for(const auto& [key,item]:value.To<Map>())
        if((key=="observedGeometryFingerprint"||key=="sourceGeometryFingerprint"||key=="geometryFingerprint")&&item.Is<std::string>()&&fingerprints.contains(item.To<std::string>()))result[key]=fingerprints.at(item.To<std::string>());else result[key]=Resign(item,fingerprints);return result;}
    return value;
}
}
IntentMap PrepareToolpathPlanClone(const IntentMap& snapshot,IntentId command,IntentId destination,
    const std::string& labelScope,const SPlanCloneTransform& transform,const IToolpathGeometryServices& geometry)
{
    const auto definitions=MapAt(snapshot,"definitions"),records=MapAt(snapshot,"records"),policies=MapAt(snapshot,"pathPolicies");
    auto ids=transform.ExternalIdentityRemap;for(const auto& [before,after]:ids)Require(!before.is_nil()&&!after.is_nil(),"TOOLPATH_CLONE_EXTERNAL_IDENTITY_INVALID");
    Require(ids.emplace(ID(At(snapshot,"planId")),destination).second,"TOOLPATH_CLONE_EXTERNAL_IDENTITY_CONFLICT");Map operationIds,pathIds,portRemap;
    for(const auto& [key,item]:definitions){const auto old=ID(Variant(key));const auto next=NamedID(Key(command)+"/operation/"+key);Require(ids.emplace(old,next).second,"TOOLPATH_CLONE_EXTERNAL_IDENTITY_CONFLICT");operationIds[key]=next;}
    for(const auto& [key,item]:policies){const auto old=ID(Variant(key));const auto next=NamedID(Key(command)+"/path/"+key);Require(ids.emplace(old,next).second,"TOOLPATH_CLONE_EXTERNAL_IDENTITY_CONFLICT");pathIds[key]=next;}
    for(const auto& [key,item]:MapAt(MapAt(snapshot,"plan"),"groups"))for(const auto& member:ArrayAt(item.To<Map>(),"members"))Require(pathIds.contains(Key(ID(member))),"TOOLPATH_CLONE_GROUP_MEMBER_OUTSIDE_GRAPH");
    const auto declare=[&](IntentId old,const std::string& role,IntentId port)
    {const auto mappedRole=RewriteText(role,ids);const auto found=ids.find(port);const auto next=found==ids.end()?NamedID(Key(ids.at(old))+"/port/"+mappedRole):found->second;ids[port]=next;portRemap[Key(SPortRef{old,port})]=ToData(SPortRef{ids.at(old),next});};
    for(const auto& value:ArrayAt(snapshot,"order"))
    {
        const auto old=ID(value);const auto definition=definitions.at(Key(old)).To<Map>();
        if(definition.contains("outputDeclarations"))for(const auto& [role,port]:MapAt(definition,"outputDeclarations"))declare(old,role,PortFromData(port.To<Map>()).PortId);
        const auto record=records.at(Key(old)).To<Map>();if(record.contains("identities"))for(const auto& [role,identity]:MapAt(record,"identities"))declare(old,role,ID(At(identity.To<Map>(),"portId")));
    }
    std::map<IntentId,SToolpathValue> sources;std::map<std::string,std::string> frames;bool identityPreserved=true;
    for(const auto& [key,item]:definitions)
    {
        const auto definition=item.To<Map>();if(Text(definition,"typeId")!="icax.source")continue;const auto old=ID(Variant(key));
        const auto before=ToolpathFromData(Remap(At(MapAt(definition,"parameters"),"value"),ids).To<Map>());auto after=before;
        if(transform.TransformSource)
        {auto result=transform.TransformSource(old,ids.at(old),before);after=std::move(result.Value);auto left=before.Geometry,right=after.Geometry;right.FrameId=left.FrameId;
         const bool same=ToData(SToolpathValue{left,{}})==ToData(SToolpathValue{right,{}});if(result.ReframeOnly)Require(same,"TOOLPATH_CLONE_REFRAME_CHANGED_GEOMETRY");
         identityPreserved&=same;if(same&&before.Geometry.FrameId!=after.Geometry.FrameId)
         {Require(result.ReframeOnly,"TOOLPATH_CLONE_REFRAME_CERTIFICATE_REQUIRED");const auto [found,added]=frames.emplace(before.Geometry.FrameId,after.Geometry.FrameId);Require(added||found->second==after.Geometry.FrameId,"TOOLPATH_CLONE_FRAME_REMAP_CONFLICT");}}
        geometry.Validate(after.Geometry);sources[old]=std::move(after);
    }
    std::map<std::string,std::string> fingerprints;
    if(identityPreserved)for(const auto& [key,item]:records)
    {const auto record=item.To<Map>();if(!record.contains("outputs"))continue;for(const auto& value:ArrayAt(record,"outputs"))
     {const auto output=value.To<Map>();const auto before=ToolpathFromData(MapAt(output,"value"));const auto after=ToolpathFromData(Reframe(Remap(At(output,"value"),ids),frames).To<Map>());
      const auto previous=ToolpathFingerprint(before,{"geometry","topology","traversal","frame"}),next=ToolpathFingerprint(after,{"geometry","topology","traversal","frame"});const auto [found,added]=fingerprints.emplace(previous,next);Require(added||found->second==next,"TOOLPATH_CLONE_FINGERPRINT_REMAP_CONFLICT");}}
    Map clonedDefinitions,seeds,identities,labels,initialLayers;
    for(const auto& [key,item]:definitions)
    {
        const auto old=ID(Variant(key)),next=ids.at(old);auto definition=Resign(Reframe(Remap(item,ids),frames),fingerprints).To<Map>();
        auto inputs=ArrayAt(definition,"inputs");for(auto& input:inputs){auto binding=input.To<Map>();binding.erase("deferred");binding.erase("producerFingerprint");input=std::move(binding);}definition["inputs"]=std::move(inputs);
        if(Text(definition,"typeId")=="icax.source")definition["parameters"]=Map{{"value",Resign(ToData(sources.at(old)),fingerprints)}};
        if(transform.TransformContext)definition["context"]=transform.TransformContext(old,next,Text(definition,"typeId"),MapAt(definition,"context"),ids);
        if(!labelScope.empty()&&definition.contains("outputLabelPolicy")){auto policy=MapAt(definition,"outputLabelPolicy");policy["numberScope"]=labelScope;definition["outputLabelPolicy"]=std::move(policy);}
        auto hashedDefinition=definition;hashedDefinition.erase("name");const auto definitionFingerprint=Hash(hashedDefinition);clonedDefinitions[Key(next)]=std::move(definition);const auto record=records.at(key).To<Map>();
        if(record.contains("identities"))
        {auto table=Remap(At(record,"identities"),ids).To<Map>();for(auto& [role,item]:table){auto identity=item.To<Map>();auto boundary=ArrayAt(identity,"boundaries");std::sort(boundary.begin(),boundary.end(),[](const auto& a,const auto& b){return a.template To<std::string>()<b.template To<std::string>();});identity["boundaries"]=std::move(boundary);item=std::move(identity);}
         Map organization;Array pendingPorts;if(record.contains("outputs"))for(const auto& item:ArrayAt(record,"outputs")){const auto output=item.To<Map>();const auto port=ids.at(ID(At(output,"portId")));organization[Key(port)]=Text(output,"organizationInputSlot");pendingPorts.emplace_back(ToData(SPortRef{next,port}));}
         seeds[Key(next)]=Map{{"kind",std::string("operationResult")},{"operationId",next},{"state",std::string("Blocked")},{"diagnostic",std::string("TOOLPATH_CLONE_NOT_GENERATED")},{"definitionFingerprint",definitionFingerprint},{"identities",std::move(table)},{"outputs",Array{}},{"retiredOutputs",Map{}},{"excludedRanges",Array{}},{"constraints",Map{}},{"outputOrganization",std::move(organization)},{"pendingPorts",std::move(pendingPorts)}};}
    }
    for(const auto& [key,item]:policies)
    {const auto policy=item.To<Map>();auto identity=Remap(At(policy,"identity"),ids).To<Map>();if(!labelScope.empty())identity["labelScope"]=labelScope;const auto next=ids.at(ID(Variant(key)));identities[Key(next)]=std::move(identity);labels[Key(next)]=At(policy,"labels");if(policy.contains("initialLayers"))initialLayers[Key(next)]=At(policy,"initialLayers");}
    auto plan=Resign(Reframe(Remap(At(snapshot,"plan"),ids),frames),fingerprints).To<Map>();
    Map externalIds;for(const auto& [before,after]:transform.ExternalIdentityRemap)externalIds[Key(before)]=after;
    return {{"plan",std::move(plan)},{"definitions",std::move(clonedDefinitions)},{"seeds",std::move(seeds)},{"identities",std::move(identities)},{"labels",std::move(labels)},{"externalIdentityRemap",std::move(externalIds)},
        {"operationIds",std::move(operationIds)},{"pathIds",std::move(pathIds)},{"portRemap",std::move(portRemap)},{"initialPathLayers",std::move(initialLayers)},{"evidence",transform.Evidence}};
}
}
