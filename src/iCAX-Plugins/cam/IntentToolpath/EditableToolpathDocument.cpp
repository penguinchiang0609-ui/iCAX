#include "pch.h"
#include "EditableToolpathInternal.h"
#include "EditableToolpathHostEdit.h"
#include "EditableToolpathLabels.h"
#include "EditableToolpathClone.h"
#include "EditableToolpathLayers.h"
#include "Database/OperationLog.h"
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include <tuple>
#include <unordered_set>

namespace iCAX::CAM::Intent::Editable
{
using namespace iCAX::Database;
struct SEditValidationMemo final
{
    std::shared_ptr<const SOperationType> Type;
    std::uint64_t DefinitionRevision=0,RepositoryRevision=0;
    std::string DefinitionFingerprint,Key,Diagnostic;
    bool Current=false;
};
struct SReadValidationCache final
{
    struct SParsedOutputs final
    {
        std::uint64_t DefinitionRevision=0,ResultRevision=0;
        bool HasOutputs=false;
        std::shared_ptr<const std::vector<SResolvedPath>> Values;
    };
    std::uint64_t Serial=std::numeric_limits<std::uint64_t>::max();
    std::map<IntentId,SEditValidationMemo> Entries;
    std::map<IntentId,SParsedOutputs> ParsedOutputs;
    std::function<void()> RequireUnchangedRepository;
    std::function<std::uint64_t(IntentId)> Revision;
};
struct SEditOverlay final
{
    const IRepository* Repository=nullptr;
    IntentId Document;
    std::map<std::pair<IntentId,std::string>,Map> Definitions;
    std::map<IntentId,Map> Records,Identities,Labels,Receipts;
    std::map<IntentId,Map> CloneInputs,CloneProjections,SeedRecords;
    std::vector<Map> NewReceipts;
    std::vector<IntentId> ReceiptIds;
    std::map<IntentId,std::uint64_t> DefinitionRevisions,RecordRevisions;
    std::map<IntentId,SEditValidationMemo> ValidationMemo;
    std::function<std::uint64_t(IntentId)> RepositoryRevision;
    std::function<void()> RequireUnchangedRepository;
};
thread_local SEditOverlay* ActiveEditOverlay=nullptr;
bool HasStagedEntity(IntentId id)
{if(!ActiveEditOverlay)return false;if(ActiveEditOverlay->Identities.contains(id)||ActiveEditOverlay->Receipts.contains(id))return true;
 for(const auto& [key,value]:ActiveEditOverlay->Definitions)if(key.first==id)return true;return false;}
struct CEditOverlayScope final
{
    explicit CEditOverlayScope(SEditOverlay& overlay)
    {Require(ActiveEditOverlay==nullptr,"TOOLPATH_REENTRANT_EDIT");ActiveEditOverlay=&overlay;}
    ~CEditOverlayScope(){ActiveEditOverlay=nullptr;}
};
template<class C> std::shared_ptr<C> Component(const IRepository& repository,IntentId id)
{const auto entity=repository.GetEntity(id);return entity?std::dynamic_pointer_cast<C>(entity->GetComponent(C::S_ClassName)):nullptr;}
template<class C> Map Definition(const IRepository& repository,IntentId id)
{if(ActiveEditOverlay&&ActiveEditOverlay->Repository==&repository){const auto found=ActiveEditOverlay->Definitions.find({id,C::S_ClassName});if(found!=ActiveEditOverlay->Definitions.end())return found->second;}
 const auto component=Component<C>(repository,id);Require(component!=nullptr,"TOOLPATH_DEFINITION_MISSING: "+Key(id));return component->GetDefinition();}
IntentId ResultId(IntentId document,IntentId operation){return NamedID(Key(document)+"/result/"+Key(operation));}
Map Result(const IRepository& repository,IntentId document,IntentId operation)
{if(ActiveEditOverlay&&ActiveEditOverlay->Repository==&repository&&ActiveEditOverlay->Document==document){const auto found=ActiveEditOverlay->Records.find(operation);if(found!=ActiveEditOverlay->Records.end())return found->second;}
 const auto component=Component<CEditableDerivedComponent>(repository,ResultId(document,operation));return component?component->GetData():Map{};}
// Definition-only edits need output identity and organization for lock checks,
// but never need to copy or validate the saved geometry/interpolation payload.
Map ResultIdentity(const IRepository& repository,IntentId document,IntentId operation)
{
 const Map* data=nullptr;
 if(ActiveEditOverlay&&ActiveEditOverlay->Repository==&repository&&ActiveEditOverlay->Document==document)
  if(const auto found=ActiveEditOverlay->Records.find(operation);found!=ActiveEditOverlay->Records.end())data=&found->second;
 const auto component=data?nullptr:Component<CEditableDerivedComponent>(repository,ResultId(document,operation));
 if(!data&&component)data=&component->GetData();if(!data)return {};
 Map result;for(const auto* key:{"state","identities","pendingPorts","outputOrganization"})if(data->contains(key))result[key]=data->at(key);
 if(data->contains("outputs")){Array outputs;for(const auto& item:std::get<Array>(data->at("outputs").m_Value)){
  Map output;for(const auto& [key,value]:std::get<Map>(item.m_Value))if(key!="value")output[key]=value;outputs.emplace_back(std::move(output));}result["outputs"]=std::move(outputs);}
 return result;
}
void Create(ITransaction& transaction,IntentId id,const std::string& type,const Map& properties)
{transaction.CreateEntity(id);transaction.AttachComponent(id,type,properties);}
template<class C> void WriteDefinition(ITransaction& transaction,const IRepository& repository,IntentId id,const Map& data)
{if(repository.GetEntity(id))transaction.ModifyComponent(id,C::S_ClassName,{{"Definition",data}});else Create(transaction,id,C::S_ClassName,{{"Definition",data}});}
void WriteDerived(ITransaction& transaction,const IRepository& repository,IntentId id,IntentId owner,const Map& data)
{const Map fields{{"Owner",owner},{"Data",data}};if(Component<CEditableDerivedComponent>(repository,id))transaction.ModifyComponent(id,CEditableDerivedComponent::S_ClassName,fields);else if(repository.GetEntity(id))transaction.AttachComponent(id,CEditableDerivedComponent::S_ClassName,fields);else Create(transaction,id,CEditableDerivedComponent::S_ClassName,fields);}
void Commit(IRepository& repository,ITransaction& transaction)
{std::string error;if(!repository.CommitTransaction(transaction,error))throw std::runtime_error("TOOLPATH_TRANSACTION_FAILED: "+error);}
std::string DefinitionHash(const Map& definition)
{auto d=definition;d.erase("name");return Hash(d);}
Map DeclareOutputs(IntentId operation,const SOperationType& type)
{Map ports;for(const auto& role:type.DeclaredOutputRoles)ports[role]=ToData(SPortRef{operation,NamedID(Key(operation)+"/port/"+role)});return ports;}
Map DeclareOutputIdentities(const SOperationType& type)
{Map result;for(const auto& [role,index]:type.DeclaredOutputIdentityInputs)result[role]=static_cast<unsigned long long>(index);return result;}
bool DeclaresPort(const Map& definition,const SPortRef& port)
{if(!definition.contains("outputDeclarations"))return Text(definition,"typeId")=="icax.source"&&port==SourceToolpathPort(port.OperationId);
 for(const auto& [role,value]:MapAt(definition,"outputDeclarations"))if(PortFromData(value.To<Map>())==port)return true;return false;}
std::string ReadHash(const std::vector<SFrozenPathInput>& inputs,const SOperationType& type,bool algorithmOnly=false)
{
    auto readable=type.ReadChannels;readable.insert({"geometry","topology","traversal","provenance","frame"});
    Array data;for(const auto& input:inputs)
    {
        // Frozen geometry is exposed as one exact value to algorithms, so all
        // of its intrinsic fields belong to their read set. Extension channels
        // are projected separately before evaluation.
        Map item{{"slot",input.Binding.Slot},{"port",ToData(input.Binding.Source)},{"pathId",input.PathId},{"value",ToolpathFingerprint(input.Value,readable)}};
        if(!algorithmOnly&&type.PropagateUnwrittenChannels)
        {Map channels;if(!type.WriteChannels.contains("*"))for(const auto& [key,value]:input.Value.Channels)if(!type.WriteChannels.contains(key))channels.emplace(key,value);
         item["propagation"]=Map{{"channels",std::move(channels)},{"mapping",ToolpathFingerprint(input.Value,{"topology","traversal","provenance","frame"})}};}
        data.emplace_back(std::move(item));
    }
    return Hash(data);
}
std::vector<SResolvedPath> Outputs(IntentId operation,const Map& record)
{
    std::vector<SResolvedPath> result;if(!record.contains("outputs"))return result;
    for(const auto& item:std::get<Array>(At(record,"outputs").m_Value)){const auto& m=std::get<Map>(item.m_Value);result.push_back({ID(At(m,"pathId")),{operation,ID(At(m,"portId"))},{},{},Text(m,"fingerprint"),ToolpathFromData(std::get<Map>(At(m,"value").m_Value))});}return result;
}
const Map* OutputData(const Map& record,IntentId port)
{if(record.contains("outputs"))for(const auto& item:std::get<Array>(At(record,"outputs").m_Value)){const auto& data=std::get<Map>(item.m_Value);if(ID(At(data,"portId"))==port)return &data;}return nullptr;}
SResolvedPath FindOutput(IntentId operation,IntentId port,const Map& record)
{if(const auto* output=OutputData(record,port))return {ID(At(*output,"pathId")),{operation,port},{},{},Text(*output,"fingerprint"),ToolpathFromData(std::get<Map>(At(*output,"value").m_Value))};throw std::invalid_argument("TOOLPATH_NEEDS_REBIND_OUTPUT: "+Key(port));}
std::shared_ptr<const std::vector<SResolvedPath>> ParsedReadOutputs(IntentId document,IntentId operation,const Map& record,SReadValidationCache* cache)
{
    if(!cache)return std::make_shared<const std::vector<SResolvedPath>>(Outputs(operation,record));
    cache->RequireUnchangedRepository();
    const auto definitionRevision=cache->Revision(operation),resultRevision=cache->Revision(ResultId(document,operation));
    const bool hasOutputs=record.contains("outputs");
    if(const auto found=cache->ParsedOutputs.find(operation);found!=cache->ParsedOutputs.end())
    {
        const auto& previous=found->second;
        if(previous.DefinitionRevision==definitionRevision&&previous.ResultRevision==resultRevision&&previous.HasOutputs==hasOutputs)return previous.Values;
    }
    // Parsing depends only on owned persisted bytes. Currentness still passes
    // ValidateType, fresh external context and the read-serial guard on every
    // read. Shared ownership keeps nested reads/eviction from invalidating a
    // geometry validation in progress; callers receive independent value copies.
    auto values=std::make_shared<const std::vector<SResolvedPath>>(Outputs(operation,record));
    cache->RequireUnchangedRepository();
    if(!cache->ParsedOutputs.contains(operation)&&cache->ParsedOutputs.size()>=4096)cache->ParsedOutputs.erase(cache->ParsedOutputs.begin());
    cache->ParsedOutputs[operation]={definitionRevision,resultRevision,hasOutputs,values};return values;
}
SResolvedPath FindReadOutput(IntentId document,IntentId operation,IntentId port,const Map& record,SReadValidationCache* cache)
{
    if(!cache)return FindOutput(operation,port,record);
    const auto outputs=ParsedReadOutputs(document,operation,record,cache);
    for(const auto& output:*outputs)if(output.Port.PortId==port)return output;
    throw std::invalid_argument("TOOLPATH_NEEDS_REBIND_OUTPUT: "+Key(port));
}
Array IdsData(const std::vector<IntentId>& ids){Array result;for(auto id:ids)result.emplace_back(id);return result;}
std::vector<IntentId> Ids(const Array& data){std::vector<IntentId> ids;for(const auto& value:data)ids.push_back(ID(value));return ids;}
bool Contains(const Array& values,IntentId id){for(const auto& v:values)if(ID(v)==id)return true;return false;}

struct SGraph final
{
    IntentId Document,Plan;
    Map PlanDefinition;
    std::map<IntentId,Map> Definitions,Records;
    std::map<SPortRef,IntentId> Replacements;
    std::vector<IntentId> Order;
    std::set<IntentId> Selected;
};
void CheckGraph(SGraph& graph)
{
    graph.Replacements.clear();graph.Order.clear();std::map<IntentId,int> marks;
    const auto visit=[&](auto&& self,IntentId id)->void
    {
        Require(marks[id]!=1,"TOOLPATH_DEPENDENCY_CYCLE");if(marks[id]==2)return;marks[id]=1;
        const auto found=graph.Definitions.find(id);Require(found!=graph.Definitions.end(),"TOOLPATH_SOURCE_DEFINITION_MISSING");
        const auto& d=found->second;Require(ID(At(d,"documentId"))==graph.Document,"TOOLPATH_CROSS_DOCUMENT_REFERENCE");
        const auto plan=ID(At(d,"planId"));Require(plan.is_nil()||plan==graph.Plan,"TOOLPATH_CROSS_PLAN_REFERENCE");
        std::set<std::string> slots;std::set<SPortRef> sources;
        for(const auto& input:Inputs(d))
        {
            Require(!input.Slot.empty()&&slots.insert(input.Slot).second&&sources.insert(input.Source).second,"TOOLPATH_INPUT_SLOT_OR_SOURCE_DUPLICATE");
            self(self,input.Source.OperationId);
            if(input.Replace&&Flag(d,"enabled",true)&&graph.Selected.contains(id))
                Require(graph.Replacements.emplace(input.Source,id).second,"TOOLPATH_REPLACEMENT_CONFLICT");
        }
        marks[id]=2;graph.Order.push_back(id);
    };
    for(auto id:graph.Selected)visit(visit,id);
}
SGraph LoadGraph(const IRepository& repository,IntentId document,IntentId plan,bool identityOnly=false)
{
    SGraph graph;graph.Document=document;graph.Plan=plan;graph.PlanDefinition=Definition<CEditablePlanComponent>(repository,plan);
    Require(ID(At(graph.PlanDefinition,"documentId"))==document,"TOOLPATH_PLAN_DOCUMENT_MISMATCH");
    for(const auto& root:ArrayAt(graph.PlanDefinition,"roots"))graph.Selected.insert(PortFromData(root.To<Map>()).OperationId);
    for(const auto& id:ArrayAt(graph.PlanDefinition,"operations"))graph.Selected.insert(ID(id));
    for(const auto& id:ArrayAt(graph.PlanDefinition,"outputOperations"))graph.Selected.insert(ID(id));
    const auto load=[&](auto&& self,IntentId id)->void
    {if(graph.Definitions.contains(id))return;const auto d=Definition<CEditableOperationComponent>(repository,id);graph.Definitions[id]=d;graph.Records[id]=identityOnly?ResultIdentity(repository,document,id):Result(repository,document,id);for(const auto& input:Inputs(d))self(self,input.Source.OperationId);};
    for(auto id:graph.Selected)load(load,id);CheckGraph(graph);return graph;
}
bool IsActive(const SGraph& graph,IntentId id)
{
    const auto& d=graph.Definitions.at(id);if(!Flag(d,"enabled",true))return false;
    return ID(At(d,"planId")).is_nil()||graph.Selected.contains(id);
}
void ValidateType(const Map& definition,const SOperationType& type)
{
    if(definition.contains("sourceBinding"))
    {const auto binding=MapAt(definition,"sourceBinding");if(Text(binding,"mode")=="LiveLinked")Require(Text(binding,"state","Current")=="Current","TOOLPATH_LIVE_SOURCE_NOT_CURRENT");}
    Require(UnsignedValue(At(definition,"schemaVersion"))==type.SchemaVersion
        &&Text(definition,"algorithmVersion")==type.AlgorithmVersion,"TOOLPATH_OPERATION_VERSION_UNSUPPORTED");
    const auto inputs=Inputs(definition);Require(inputs.size()>=type.MinimumInputs&&inputs.size()<=type.MaximumInputs,"TOOLPATH_INPUT_ARITY");
    const auto& parameters=std::get<Map>(At(definition,"parameters").m_Value);ValidateParameterSchema(type.ParameterSchema,parameters);if(type.ValidateParameters)type.ValidateParameters(parameters);
    if(definition.contains("outputDeclarations"))
    {std::set<std::string> roles;for(const auto& [role,value]:MapAt(definition,"outputDeclarations"))roles.insert(role);Require(roles==type.DeclaredOutputRoles,"TOOLPATH_OUTPUT_DECLARATION_VERSION_MISMATCH");}
    if(definition.contains("outputIdentityInputs"))Require(MapAt(definition,"outputIdentityInputs")==DeclareOutputIdentities(type),"TOOLPATH_OUTPUT_IDENTITY_DECLARATION_VERSION_MISMATCH");
}
struct SFrozenContext final
{
    std::shared_ptr<const IToolpathOperationContext> Value;
    std::string Fingerprint,Error;
};
SFrozenContext FreezeOperationContext(const SOperationType* type,const Map& persistent)
{
    SFrozenContext frozen;if(!type||!type->FreezeContext)return frozen;
    try
    {frozen.Value=type->FreezeContext(persistent);Require(frozen.Value!=nullptr,"TOOLPATH_CONTEXT_SNAPSHOT_REQUIRED");frozen.Fingerprint=frozen.Value->Fingerprint();Require(!frozen.Fingerprint.empty(),"TOOLPATH_CONTEXT_FINGERPRINT_REQUIRED");}
    catch(const std::exception& error){frozen.Value.reset();frozen.Error=error.what();frozen.Fingerprint="error/"+Hash(frozen.Error);}
    return frozen;
}
std::vector<SFrozenPathInput> FrozenInputs(const Map& definition,const std::map<IntentId,Map>& records)
{
    std::vector<SFrozenPathInput> inputs;
    for(const auto& binding:Inputs(definition))
    {
        const auto r=records.find(binding.Source.OperationId);Require(r!=records.end(),"TOOLPATH_INPUT_NOT_CURRENT");
        if(Text(r->second,"state")!="Current")
        {
            auto cause=Text(r->second,"diagnostic");
            // Preserve the first actionable failure through arbitrarily many
            // blocked dependants without accumulating wrapper prefixes.
            const std::string inputPrefix="TOOLPATH_INPUT_NOT_CURRENT: ",resultPrefix="TOOLPATH_RESULT_NOT_CURRENT: ";
            for(;;){if(cause.starts_with(inputPrefix))cause.erase(0,inputPrefix.size());else if(cause.starts_with(resultPrefix))cause.erase(0,resultPrefix.size());else break;}
            if(cause=="TOOLPATH_INPUT_NOT_CURRENT")cause.clear();
            if(cause.size()>4096){std::size_t end=4096;while(end&&(static_cast<unsigned char>(cause[end])&0xc0)==0x80)--end;cause.resize(end);cause+="...";}
            throw std::invalid_argument(cause.empty()?std::string("TOOLPATH_INPUT_NOT_CURRENT"):inputPrefix+cause);
        }
        auto output=FindOutput(binding.Source.OperationId,binding.Source.PortId,r->second);
        inputs.push_back({binding,output.PathId,std::move(output.Value),output.Fingerprint});
    }
    return inputs;
}
Array BoundaryData(const std::vector<std::string>& values){Array data;for(const auto& v:values)data.emplace_back(v);return data;}
Map PrepareRecord(const Map& definition,const Map& previous,const SOperationEvaluation& in,
    const SOperationCandidate& candidate,const SOperationType& type,const std::string& contextFingerprint={})
{
    auto identities=previous.contains("identities")?MapAt(previous,"identities"):Map{};Array outputs;
    if(!type.DeclaredOutputRoles.empty())
    {std::set<std::string> roles;for(const auto& output:candidate.Outputs)roles.insert(output.RoleKey);Require(roles==type.DeclaredOutputRoles,"TOOLPATH_DECLARED_OUTPUTS_MISMATCH");}
    std::set<IntentId> activePorts;
    for(const auto& output:candidate.Outputs)
    {
        const auto boundary=BoundaryData(output.BoundaryKeys);Map identity;
        if(identities.contains(output.RoleKey))
        {identity=identities.at(output.RoleKey).To<Map>();Require(At(identity,"boundaries")==Variant(boundary),"TOOLPATH_NEEDS_REBIND_ROLE_SEMANTICS");}
        else
        {
            std::map<IntentId,Map> matches;
            if(!boundary.empty())for(const auto& [role,old]:identities)
            {const auto entry=old.To<Map>();if(At(entry,"boundaries")==Variant(boundary))
             {const auto port=ID(At(entry,"portId"));const auto [found,added]=matches.emplace(port,entry);Require(added||found->second==entry,"TOOLPATH_NEEDS_REBIND_ROLE_AMBIGUITY");}}
            Require(matches.size()<=1,"TOOLPATH_NEEDS_REBIND_ROLE_AMBIGUITY");
            if(matches.size()==1)identity=matches.begin()->second;
            else identity={{"portId",NamedID(Key(in.OperationId)+"/port/"+output.RoleKey)},
                {"pathId",output.InheritInput?in.Inputs.at(*output.InheritInput).PathId:
                    definition.contains("sourcePathId")?ID(At(definition,"sourcePathId")):NamedID(Key(in.OperationId)+"/path/"+output.RoleKey)},
                {"boundaries",boundary}};
            identities[output.RoleKey]=identity;
        }
        Require(activePorts.insert(ID(At(identity,"portId"))).second,"TOOLPATH_OUTPUT_PORT_DUPLICATE");
        if(definition.contains("outputDeclarations"))Require(DeclaresPort(definition,{in.OperationId,ID(At(identity,"portId"))}),"TOOLPATH_DECLARED_OUTPUT_PORT_MISMATCH");
        if(output.InheritInput)Require(ID(At(identity,"pathId"))==in.Inputs.at(*output.InheritInput).PathId,"TOOLPATH_NEEDS_REBIND_IDENTITY");
        if(const auto declared=type.DeclaredOutputIdentityInputs.find(output.RoleKey);declared!=type.DeclaredOutputIdentityInputs.end())
            Require(output.InheritInput&&*output.InheritInput==declared->second,"TOOLPATH_DECLARED_OUTPUT_IDENTITY_MISMATCH");
        auto value=output.Value;RebindChannelPorts(value.Channels,{in.OperationId,NamedID(Key(in.OperationId)+"/port/"+output.RoleKey)},
            {in.OperationId,ID(At(identity,"portId"))});
        Map data{{"portId",At(identity,"portId")},{"pathId",At(identity,"pathId")},{"role",output.RoleKey},
            {"boundaries",boundary},{"value",ToData(value)},{"fingerprint",ToolpathFingerprint(value)}};
        if(output.InheritInput)data["inheritInput"]=static_cast<unsigned long long>(*output.InheritInput);
        data["organizationInputSlot"]=!output.OrganizationInputSlot.empty()?output.OrganizationInputSlot:
            output.InheritInput?in.Inputs.at(*output.InheritInput).Binding.Slot:std::string();
        outputs.emplace_back(std::move(data));
    }
    auto retired=previous.contains("retiredOutputs")?MapAt(previous,"retiredOutputs"):Map{};
    if(previous.contains("outputs"))for(const auto& old:std::get<Array>(At(previous,"outputs").m_Value))
    {const auto& data=std::get<Map>(old.m_Value);if(!activePorts.contains(ID(At(data,"portId"))))retired[Key(ID(At(data,"portId")))]=data;}
    for(auto id:activePorts)retired.erase(Key(id));
    Array excluded;for(const auto& r:candidate.ExcludedRanges)excluded.emplace_back(ToData(r));
    auto constraints=candidate.Constraints;
    if(constraints.contains("executionBlock"))
    {auto block=MapAt(constraints,"executionBlock");Array ports;for(const auto& role:ArrayAt(block,"outputRoles"))
        {bool found=false;for(const auto& value:outputs){const auto& output=std::get<Map>(value.m_Value);if(Text(output,"role")==role.To<std::string>()){ports.emplace_back(ToData(SPortRef{in.OperationId,ID(At(output,"portId"))}));found=true;break;}}Require(found,"TOOLPATH_BLOCK_OUTPUT_ROLE_MISSING");}
     block["ports"]=ports;constraints["executionBlock"]=block;}
    return {{"kind",std::string("operationResult")},{"operationId",in.OperationId},{"state",std::string("Current")},
        {"definitionFingerprint",DefinitionHash(definition)},{"readFingerprint",ReadHash(in.Inputs,type)},{"contextFingerprint",contextFingerprint},
        {"algorithmReadFingerprint",ReadHash(in.Inputs,type,true)},
        {"algorithmVersion",type.AlgorithmVersion},{"evaluationRevision",type.EvaluationRevision},{"outputs",std::move(outputs)},{"identities",std::move(identities)},
        {"intentionalEmpty",candidate.IntentionalEmpty},{"excludedRanges",std::move(excluded)},{"constraints",std::move(constraints)},{"retiredOutputs",std::move(retired)},{"allowRepeatedCoverage",candidate.AllowRepeatedCoverage},{"inheritExecutionBlock",type.InheritExecutionBlock}};
}
struct SPrepared final
{
    IntentId Operation;
    Map Definition,Record;
    std::uint64_t Epoch=0;
    std::shared_ptr<const SOperationType> Type;
    std::string InputStateFingerprint;
};
std::string InputStateHash(const Map& definition,const std::map<IntentId,Map>& records)
{
    Array states;for(const auto& input:Inputs(definition))
    {const auto i=records.find(input.Source.OperationId);if(i==records.end()){states.emplace_back(Map{{"source",ToData(input.Source)},{"state",std::string("Missing")}});continue;}
     Map state{{"source",ToData(input.Source)},{"state",Text(i->second,"state")},{"definition",Text(i->second,"definitionFingerprint")},{"read",Text(i->second,"readFingerprint")}};
     if(i->second.contains("outputs"))for(const auto& item:std::get<Array>(At(i->second,"outputs").m_Value)){const auto& output=std::get<Map>(item.m_Value);if(ID(At(output,"portId"))==input.Source.PortId)state["value"]=Text(output,"fingerprint");}states.emplace_back(std::move(state));}
    return Hash(states);
}
struct SGenerationState final
{
    IntentId Session,Document,Plan;
    std::shared_ptr<std::atomic_bool> Cancelled=std::make_shared<std::atomic_bool>(false);
    std::future<std::vector<SPrepared>> Future;
    std::optional<std::vector<SPrepared>> Prepared;
    bool Published=false;
    std::uint64_t Ticket=0;
};
std::vector<SPrepared> EvaluateGraph(SGraph graph,
    const std::map<IntentId,std::uint64_t>& epochs,
    const std::map<IntentId,std::shared_ptr<const SOperationType>>& types,
    const std::map<IntentId,SFrozenContext>& contexts,
    const std::shared_ptr<const IToolpathGeometryServices>& geometry,
    const std::shared_ptr<std::atomic_bool>& cancelled,std::size_t concurrency,const std::set<IntentId>& force)
{
    const auto evaluate=[&](IntentId id)->SPrepared
    {
        const auto& definition=graph.Definitions.at(id);SPrepared prepared{id,definition,graph.Records.at(id),epochs.at(id),types.at(id),InputStateHash(definition,graph.Records)};
        try
        {
            Require(!cancelled->load(),"TOOLPATH_CANCELLED");Require(IsActive(graph,id),"TOOLPATH_SOURCE_INACTIVE");
            Require(prepared.Type!=nullptr,"TOOLPATH_OPERATION_UNSUPPORTED");const auto& type=*prepared.Type;ValidateType(definition,type);
            const auto& context=contexts.at(id);Require(context.Error.empty(),context.Error);auto inputs=FrozenInputs(definition,graph.Records);
            SOperationEvaluation in{id,std::move(inputs),MapAt(definition,"parameters"),MapAt(definition,"context"),cancelled};
            in.RuntimeContext=context.Value;
            if(type.ValidateInput)type.ValidateInput(in);SOperationCandidate candidate;bool unchanged=false;
            if(!force.contains(id)&&Text(prepared.Record,"state")=="Current"&&Text(prepared.Record,"definitionFingerprint")==DefinitionHash(definition)
                &&Text(prepared.Record,"evaluationRevision")==type.EvaluationRevision&&Text(prepared.Record,"algorithmReadFingerprint")==ReadHash(in.Inputs,type,true)&&Text(prepared.Record,"contextFingerprint")==context.Fingerprint)
            {
                for(const auto& item:std::get<Array>(At(prepared.Record,"outputs").m_Value))
                {const auto& output=std::get<Map>(item.m_Value);SOutputCandidate value;value.RoleKey=Text(output,"role");value.Value=ToolpathFromData(std::get<Map>(At(output,"value").m_Value));
                 for(const auto& boundary:std::get<Array>(At(output,"boundaries").m_Value))value.BoundaryKeys.push_back(boundary.To<std::string>());
                 if(output.contains("inheritInput"))value.InheritInput=At(output,"inheritInput").To<unsigned long long>();value.OrganizationInputSlot=Text(output,"organizationInputSlot");candidate.Outputs.push_back(std::move(value));}
                for(const auto& range:ArrayAt(prepared.Record,"excludedRanges"))candidate.ExcludedRanges.push_back(RangeFromData(range.To<Map>()));
                candidate.IntentionalEmpty=Flag(prepared.Record,"intentionalEmpty");candidate.Constraints=MapAt(prepared.Record,"constraints");
                candidate.AllowRepeatedCoverage=Flag(prepared.Record,"allowRepeatedCoverage");
                unchanged=Text(prepared.Record,"readFingerprint")==ReadHash(in.Inputs,type);
            }
            else
            {
                auto algorithmInput=in;
                if(!type.ReadChannels.contains("*"))for(auto& input:algorithmInput.Inputs)
                    std::erase_if(input.Value.Channels,[&](const auto& item){return !type.ReadChannels.contains(item.first);});
                candidate=type.Evaluate(algorithmInput,*geometry);
            }
            if(!unchanged)PropagateChannels(in,candidate,type);
            ValidateCandidate(in,candidate,*geometry);if(type.ValidateResult)type.ValidateResult(in,candidate);
            // A complete input match also covers propagated channels and exact
            // provenance. Keep the validated record instead of serializing and
            // hashing every unchanged ancestor again for a local edit.
            if(!unchanged)prepared.Record=PrepareRecord(definition,prepared.Record,in,candidate,type,context.Fingerprint);
        }
        catch(const std::exception& error)
        {prepared.Record["kind"]=std::string("operationResult");prepared.Record["operationId"]=id;prepared.Record["state"]=std::string("Blocked");prepared.Record["diagnostic"]=std::string(error.what());prepared.Record["definitionFingerprint"]=DefinitionHash(definition);prepared.Record["contextFingerprint"]=contexts.at(id).Fingerprint;prepared.Record["evaluationRevision"]=prepared.Type?prepared.Type->EvaluationRevision:std::string();}
        return prepared;
    };
    std::set<IntentId> completed;std::vector<SPrepared> results;
    while(completed.size()<graph.Order.size())
    {
        std::vector<IntentId> ready;
        for(auto id:graph.Order)if(!completed.contains(id))
        {bool available=true;for(const auto& input:Inputs(graph.Definitions.at(id)))available&=completed.contains(input.Source.OperationId);if(available)ready.push_back(id);}
        Require(!ready.empty(),"TOOLPATH_DEPENDENCY_CYCLE");
        for(std::size_t begin=0;begin<ready.size();begin+=concurrency)
        {
            std::vector<std::future<SPrepared>> workers;
            const auto end=std::min(ready.size(),begin+concurrency);
            for(std::size_t i=begin;i<end;++i)workers.push_back(std::async(std::launch::async,evaluate,ready[i]));
            for(auto& worker:workers){auto result=worker.get();graph.Records[result.Operation]=result.Record;completed.insert(result.Operation);results.push_back(std::move(result));}
        }
    }
    return results;
}
}

namespace iCAX::CAM::Intent
{
using namespace Editable;
struct CToolpathGeneration::Impl{std::shared_ptr<SGenerationState> State;};
CToolpathGeneration::CToolpathGeneration(std::shared_ptr<Impl> impl):m_Impl(std::move(impl)){}
CToolpathGeneration::~CToolpathGeneration()=default;
void CToolpathGeneration::Cancel(){m_Impl->State->Cancelled->store(true);}
bool CToolpathGeneration::IsReady()const{return m_Impl->State->Published||m_Impl->State->Prepared.has_value()||m_Impl->State->Future.wait_for(std::chrono::milliseconds(0))==std::future_status::ready;}

namespace
{
struct CEpochObserver final:iCAX::Database::IRepositoryEventListener
{
    std::map<IntentId,std::uint64_t> Epochs,Changes;
    std::uint64_t Serial=0;
    void OnRepositoryChanging(void*,const iCAX::Database::RepositoryEventArgs&)override{}
    void OnRepositoryChanged(void*,const iCAX::Database::RepositoryEventArgs& args)override
    {
        ++Serial;
        const auto change=[&](IntentId id,const std::string& type){++Changes[id];if(type==CEditableOperationComponent::S_ClassName||type.empty())++Epochs[id];};
        if(args.pBatch)for(const auto& record:args.pBatch->Records)change(record.EntityID,record.strClassName);
        else change(args.EntityID,args.strClassName);
    }
};
std::map<IntentId,Map> ValidRecords(const SGraph& graph,const CToolpathOperationRegistry& registry,const IToolpathGeometryServices& geometry,SReadValidationCache* readCache=nullptr)
{
    auto records=graph.Records;auto* overlay=ActiveEditOverlay;
    const auto readSerial=readCache?readCache->Serial:0;
    const auto requireReadUnchanged=[&]{if(readCache){Require(readCache->Serial==readSerial,"TOOLPATH_REPOSITORY_CHANGED_DURING_PATH_READ");readCache->RequireUnchangedRepository();}};
    if(overlay)overlay->RequireUnchangedRepository();
    for(auto id:graph.Order)
    {
        auto& record=records[id];SEditValidationMemo memo;bool cacheable=false;
        try
        {
            Require(IsActive(graph,id),"TOOLPATH_SOURCE_INACTIVE");const auto type=registry.Find(Text(graph.Definitions.at(id),"typeId"));
            Require(type!=nullptr,"TOOLPATH_OPERATION_UNSUPPORTED");const auto& definition=graph.Definitions.at(id);
            // Only this synchronous edit batch may reuse a prior full check.
            // Every visit still observes the current descriptor and freezes
            // external context before deciding whether a memo is reusable.
            SFrozenContext context;
            if(!overlay&&readCache)
            {
                // Recheck the exact definition, result and input revisions.
                // Presentation/unrelated edits do not change this evidence.
                // Descriptors, parameter validators and external contexts are
                // still observed on every read.
                requireReadUnchanged();ValidateType(definition,*type);
                memo.Type=type;memo.DefinitionRevision=readCache->Revision(id);memo.RepositoryRevision=readCache->Revision(ResultId(graph.Document,id));
                std::optional<SEditValidationMemo> previous;if(const auto found=readCache->Entries.find(id);found!=readCache->Entries.end())previous=found->second;
                memo.DefinitionFingerprint=previous&&previous->DefinitionRevision==memo.DefinitionRevision?previous->DefinitionFingerprint:DefinitionHash(definition);
                context=FreezeOperationContext(type.get(),MapAt(definition,"context"));requireReadUnchanged();
                Array revisions;for(const auto& binding:Inputs(definition))revisions.emplace_back(static_cast<unsigned long long>(readCache->Revision(ResultId(graph.Document,binding.Source.OperationId))));
                memo.Key=Hash(Map{{"definition",memo.DefinitionFingerprint},{"resultRevision",static_cast<unsigned long long>(memo.RepositoryRevision)},
                    {"context",context.Fingerprint},{"inputResultRevisions",std::move(revisions)},{"inputs",InputStateHash(definition,records)}});cacheable=true;
                if(previous&&previous->Type==type&&previous->Key==memo.Key)
                {
                    if(!previous->Current){record["state"]=std::string("Blocked");record["diagnostic"]=previous->Diagnostic;}
                    continue;
                }
            }
            if(overlay)
            {
                memo.Type=type;memo.DefinitionRevision=overlay->DefinitionRevisions[id];memo.RepositoryRevision=overlay->RepositoryRevision(id);
                const auto previous=overlay->ValidationMemo.find(id);
                if(previous!=overlay->ValidationMemo.end()&&previous->second.DefinitionRevision==memo.DefinitionRevision&&previous->second.RepositoryRevision==memo.RepositoryRevision)
                    memo.DefinitionFingerprint=previous->second.DefinitionFingerprint;
                else memo.DefinitionFingerprint=DefinitionHash(definition);
                context=FreezeOperationContext(type.get(),MapAt(definition,"context"));overlay->RequireUnchangedRepository();
                memo.Key=Hash(Map{{"definition",memo.DefinitionFingerprint},{"evaluation",type->EvaluationRevision},{"context",context.Fingerprint},
                    {"resultRevision",static_cast<unsigned long long>(overlay->RepositoryRevision(ResultId(graph.Document,id)))},
                    {"stagedResultRevision",static_cast<unsigned long long>(overlay->RecordRevisions[id])},{"inputs",InputStateHash(definition,records)}});
                cacheable=true;
                if(previous!=overlay->ValidationMemo.end()&&previous->second.Type==type&&previous->second.Key==memo.Key)
                {
                    if(!previous->second.Current){record["state"]=std::string("Blocked");record["diagnostic"]=previous->second.Diagnostic;}
                    continue;
                }
            }
            if(!readCache||overlay)ValidateType(definition,*type);
            Require(Text(record,"state")=="Current","TOOLPATH_RESULT_NOT_CURRENT: "+Text(record,"diagnostic"));Require(Text(record,"evaluationRevision")==type->EvaluationRevision,"TOOLPATH_RESULT_STALE_EVALUATOR");
            Require(Text(record,"definitionFingerprint")== (overlay||readCache?memo.DefinitionFingerprint:DefinitionHash(definition)),"TOOLPATH_RESULT_STALE_DEFINITION");
            if(!overlay&&!readCache)context=FreezeOperationContext(type.get(),MapAt(definition,"context"));Require(context.Error.empty(),context.Error);
            Require(Text(record,"contextFingerprint")==context.Fingerprint,"TOOLPATH_RESULT_STALE_CONTEXT");
            const auto inputs=FrozenInputs(definition,records);
            Require(Text(record,"readFingerprint")==ReadHash(inputs,*type),"TOOLPATH_RESULT_STALE_INPUT");
            const auto outputs=ParsedReadOutputs(graph.Document,id,record,readCache);
            for(const auto& output:*outputs)geometry.Validate(output.Value.Geometry);
            memo.Current=true;
        }
        catch(const std::exception& error){record["state"]=std::string("Blocked");record["diagnostic"]=std::string(error.what());}
        if(overlay)
        {
            overlay->RequireUnchangedRepository();
            if(cacheable){memo.Diagnostic=Text(record,"diagnostic");overlay->ValidationMemo[id]=std::move(memo);}
        }
        else if(readCache)
        {
            requireReadUnchanged();
            if(cacheable){memo.Diagnostic=Text(record,"diagnostic");if(!readCache->Entries.contains(id)&&readCache->Entries.size()>=4096)readCache->Entries.erase(readCache->Entries.begin());readCache->Entries[id]=std::move(memo);}
        }
    }
    return records;
}
bool RetiredOverlaps(const Map& record,IntentId port)
{
    if(!record.contains("retiredOutputs"))return true;const auto retired=MapAt(record,"retiredOutputs");const auto i=retired.find(Key(port));if(i==retired.end())return true;
    const auto old=ToolpathFromData(MapAt(i->second.To<Map>(),"value"));
    for(const auto& item:ArrayAt(record,"outputs"))for(const auto& segment:ToolpathFromData(MapAt(item.To<Map>(),"value")).Geometry.Segments)
        for(const auto& source:segment.Sources)for(const auto& oldSegment:old.Geometry.Segments)for(const auto& previous:oldSegment.Sources)
            if(source.Source==previous.Source&&source.SourceSegmentId==previous.SourceSegmentId
                &&std::min(source.SourceRange.Last,previous.SourceRange.Last)>std::max(source.SourceRange.First,previous.SourceRange.First)+1e-12)return true;
    return false;
}
Map PathLabels(const IRepository& repository,IntentId path)
{
    Map labels{{"Name",std::string()},{"UserNumber",std::string()},{"OwnLocked",false}};
    if(const auto identity=Component<CEditableDerivedComponent>(repository,path))
    {const auto data=identity->GetData();labels["Name"]=Text(data,"initialName");labels["UserNumber"]=Text(data,"initialUserNumber");}
    if(const auto current=Component<CEditablePathLabelsComponent>(repository,path))labels=current->GetProperties();
    if(ActiveEditOverlay&&ActiveEditOverlay->Repository==&repository)
        if(const auto staged=ActiveEditOverlay->Labels.find(path);staged!=ActiveEditOverlay->Labels.end())
            for(const auto& [key,value]:staged->second)labels[key]=value;
    return labels;
}
Map LayerDocument(const IRepository& repository,IntentId document)
{return Definition<CEditableDocumentComponent>(repository,document);}
std::vector<SPortRef> InspectionPorts(const SGraph& graph,const std::map<IntentId,Map>& records,IntentId operation)
{
    std::vector<SPortRef> ports;
    if(const auto record=records.find(operation);record!=records.end()&&record->second.contains("outputs"))for(const auto& item:std::get<Array>(At(record->second,"outputs").m_Value))ports.push_back({operation,ID(At(std::get<Map>(item.m_Value),"portId"))});
    if(ports.empty())if(const auto record=records.find(operation);record!=records.end()&&record->second.contains("pendingPorts"))
    {for(const auto& item:ArrayAt(record->second,"pendingPorts"))ports.push_back(PortFromData(item.To<Map>()));return ports;}
    if(ports.empty())if(const auto definition=graph.Definitions.find(operation);definition!=graph.Definitions.end()&&definition->second.contains("outputDeclarations"))
        for(const auto& [role,value]:MapAt(definition->second,"outputDeclarations"))ports.push_back(PortFromData(value.To<Map>()));
    if(ports.empty())if(const auto record=records.find(operation);record!=records.end()&&record->second.contains("identities"))
        for(const auto& [role,value]:MapAt(record->second,"identities")){const SPortRef port{operation,ID(At(value.To<Map>(),"portId"))};if(std::find(ports.begin(),ports.end(),port)==ports.end())ports.push_back(port);}
    return ports;
}
IntentId InspectionPathId(const SGraph& graph,const std::map<IntentId,Map>& records,SPortRef port)
{
    if(const auto record=records.find(port.OperationId);record!=records.end())
    {
        if(const auto* output=OutputData(record->second,port.PortId))return ID(At(*output,"pathId"));
        if(record->second.contains("identities"))for(const auto& [role,item]:std::get<Map>(At(record->second,"identities").m_Value))
        {const auto& identity=std::get<Map>(item.m_Value);if(ID(At(identity,"portId"))==port.PortId)return ID(At(identity,"pathId"));}
    }
    const auto& definition=graph.Definitions.at(port.OperationId);Require(DeclaresPort(definition,port),"TOOLPATH_NEEDS_REBIND_OUTPUT: "+Key(port.PortId));
    if(definition.contains("sourcePathId"))return ID(At(definition,"sourcePathId"));
    for(const auto& [role,item]:std::get<Map>(At(definition,"outputDeclarations").m_Value))if(PortFromData(std::get<Map>(item.m_Value))==port)
    {if(definition.contains("outputIdentityInputs")){const auto& inherited=std::get<Map>(At(definition,"outputIdentityInputs").m_Value);if(inherited.contains(role))return InspectionPathId(graph,records,Inputs(definition).at(At(inherited,role).To<unsigned long long>()).Source);}return NamedID(Key(port.OperationId)+"/path/"+role);}
    throw std::invalid_argument("TOOLPATH_NEEDS_REBIND_OUTPUT: "+Key(port.PortId));
}
SResolvedPath InspectionOutput(const SGraph& graph,const std::map<IntentId,Map>& records,SPortRef port,SReadValidationCache* readCache=nullptr)
{
    if(const auto record=records.find(port.OperationId);record!=records.end())
    {
        if(OutputData(record->second,port.PortId))
        {
            // Stale inspection exposes the requested port even if another
            // stale output cannot be decoded. Current records have already
            // passed whole-operation validation and may share their parse.
            if(Text(record->second,"state")!="Current")return FindOutput(port.OperationId,port.PortId,record->second);
            return FindReadOutput(graph.Document,port.OperationId,port.PortId,record->second,readCache);
        }
    }
    SResolvedPath path;path.Port=port;path.PathId=InspectionPathId(graph,records,port);return path;
}
bool PendingIdentityPort(const Map& record,IntentId port)
{if(Text(record,"state")=="Current"||!record.contains("identities"))return false;for(const auto& [role,item]:MapAt(record,"identities"))if(ID(At(item.To<Map>(),"portId"))==port)return true;return false;}
void ValidateConstraints(const Map& constraints)
{
    const auto names=[](const Variant& value)
    {std::vector<std::string> result;const auto one=[&](const Variant& item){result.push_back(item.Is<Map>()?Key(PortFromData(item.To<Map>())):Key(ID(item)));};if(value.Is<Array>())for(const auto& item:value.To<Array>())one(item);else one(value);return result;};
    std::map<std::string,std::vector<std::string>> edges;std::vector<std::set<std::string>> atomicBlocks;std::set<std::string> blockIds;
    if(constraints.contains("precedence"))for(const auto& item:ArrayAt(constraints,"precedence"))
    {const auto m=item.To<Map>();const auto before=names(At(m,"before")),after=names(At(m,"after"));const auto join="constraint/"+Hash(m);for(const auto& id:before)edges[id].push_back(join);for(const auto& id:after)edges[join].push_back(id);}
    if(constraints.contains("blocks"))for(const auto& item:ArrayAt(constraints,"blocks"))
    {const auto m=item.To<Map>();Require(!Text(m,"id").empty()&&blockIds.insert(Text(m,"id")).second,"TOOLPATH_EXECUTION_BLOCK_ID_CONFLICT");std::string previous;std::set<std::string> members;
     for(const auto& member:ArrayAt(m,"members")){const auto id=names(member).at(0);Require(members.insert(id).second,"TOOLPATH_EXECUTION_BLOCK_MEMBER_DUPLICATE");if(!previous.empty())edges[previous].push_back(id);previous=id;}if(Flag(m,"atomic",true))atomicBlocks.push_back(std::move(members));}
    for(std::size_t i=0;i<atomicBlocks.size();++i)for(std::size_t j=i+1;j<atomicBlocks.size();++j)
    {const auto& a=atomicBlocks[i];const auto& b=atomicBlocks[j];bool overlaps=false;for(const auto& member:a)overlaps|=b.contains(member);
     Require(!overlaps||std::includes(a.begin(),a.end(),b.begin(),b.end())||std::includes(b.begin(),b.end(),a.begin(),a.end()),"TOOLPATH_EXECUTION_BLOCK_CONFLICT");}
    std::map<std::string,int> marks;const auto visit=[&](auto&& self,const std::string& id)->void{Require(marks[id]!=1,"TOOLPATH_EXECUTION_CONSTRAINT_CYCLE");if(marks[id]==2)return;marks[id]=1;for(const auto& next:edges[id])self(self,next);marks[id]=2;};
    std::vector<std::string> keys;for(const auto& [key,values]:edges)keys.push_back(key);for(const auto& key:keys)visit(visit,key);
}
Map ResolveConstraints(const SGraph& graph,const std::map<IntentId,Map>& records,const std::vector<SResolvedPath>& paths,
    const Map& exclusions,const std::function<void(IntentId,const std::string&)>& diagnose,bool inspection=false)
{
    std::map<SPortRef,IntentId> terminals;for(const auto& path:paths)terminals[path.Port]=path.PathId;
    std::map<SPortRef,std::vector<IntentId>> memo;
    const auto expand=[&](auto&& self,SPortRef port)->std::vector<IntentId>
    {
        if(const auto found=memo.find(port);found!=memo.end())return found->second;
        std::vector<IntentId> result;if(exclusions.contains(Key(port)))return result;
        const auto record=records.find(port.OperationId);if(record==records.end()||(!inspection&&Text(record->second,"state")!="Current"))return result;
        const auto next=graph.Replacements.find(port);
        if(next!=graph.Replacements.end())
        {
            const auto& replacement=records.at(next->second);if((!inspection&&Text(replacement,"state")!="Current")||!Flag(replacement,"inheritExecutionBlock",true))return result;
            std::vector<SPortRef> outputs;const auto constraints=replacement.contains("constraints")?MapAt(replacement,"constraints"):Map{};
            if(constraints.contains("executionBlock"))for(const auto& item:ArrayAt(MapAt(constraints,"executionBlock"),"ports"))outputs.push_back(PortFromData(item.To<Map>()));
            else outputs=InspectionPorts(graph,records,next->second);
            for(const auto& output:outputs){const auto members=self(self,output);for(auto id:members)if(std::find(result.begin(),result.end(),id)==result.end())result.push_back(id);}
        }
        else if(terminals.contains(port))result.push_back(terminals.at(port));
        else if(!DeclaresPort(graph.Definitions.at(port.OperationId),port)&&!PendingIdentityPort(record->second,port.PortId)&&RetiredOverlaps(record->second,port.PortId))diagnose(port.OperationId,"TOOLPATH_CONSTRAINT_NEEDS_REBIND");
        memo[port]=result;return result;
    };
    const auto members=[&](const Variant& value)
    {
        if(value.Is<Map>())return expand(expand,PortFromData(value.To<Map>()));
        const auto id=ID(value);for(const auto& path:paths)if(path.PathId==id)return std::vector<IntentId>{id};
        diagnose({},"TOOLPATH_CONSTRAINT_PATH_NOT_EFFECTIVE");return std::vector<IntentId>{};
    };
    const auto plan=MapAt(graph.PlanDefinition,"executionConstraints");Array blocks;
    for(auto operation:graph.Order)
    {
        const auto& record=records.at(operation);if((!inspection&&Text(record,"state")!="Current")||!IsActive(graph,operation)||!record.contains("constraints"))continue;
        const auto constraints=MapAt(record,"constraints");if(!constraints.contains("executionBlock"))continue;
        auto block=MapAt(constraints,"executionBlock");std::vector<IntentId> ids;
        for(const auto& port:ArrayAt(block,"ports")){const auto paths=expand(expand,PortFromData(port.To<Map>()));for(auto id:paths)if(std::find(ids.begin(),ids.end(),id)==ids.end())ids.push_back(id);}
        if(!ids.empty()){block["members"]=IdsData(ids);blocks.emplace_back(std::move(block));}
    }
    if(plan.contains("blocks"))for(const auto& item:ArrayAt(plan,"blocks"))
    {auto block=item.To<Map>();std::vector<IntentId> ids;for(const auto& member:ArrayAt(block,"members"))for(auto id:members(member))if(std::find(ids.begin(),ids.end(),id)==ids.end())ids.push_back(id);if(!ids.empty()){block["members"]=IdsData(ids);blocks.emplace_back(std::move(block));}}
    if(plan.contains("blockOverrides"))
    {const auto overrides=MapAt(plan,"blockOverrides");Array filtered;for(const auto& item:blocks){auto block=item.To<Map>();if(overrides.contains(Text(block,"id"))){const auto patch=overrides.at(Text(block,"id")).To<Map>();if(!Flag(patch,"enabled",true))continue;if(patch.contains("atomic"))block["atomic"]=Flag(patch,"atomic");}filtered.emplace_back(std::move(block));}blocks=std::move(filtered);}
    Array precedence;if(plan.contains("precedence"))for(const auto& item:ArrayAt(plan,"precedence"))
    {const auto edge=item.To<Map>();const auto before=members(At(edge,"before")),after=members(At(edge,"after"));if(!before.empty()&&!after.empty())precedence.emplace_back(Map{{"before",IdsData(before)},{"after",IdsData(after)}});}
    Map resolved{{"blocks",std::move(blocks)},{"precedence",std::move(precedence)}};ValidateConstraints(resolved);return resolved;
}
Map ResolveOrganization(const SGraph& graph,const std::map<IntentId,Map>& records,std::vector<SResolvedPath>& paths)
{
    const auto groups=MapAt(graph.PlanDefinition,"groups");std::map<IntentId,std::string> direct;std::map<SPortRef,std::string> memo;
    if(groups.empty()){for(auto& path:paths)path.GroupId.clear();return {};}
    for(const auto& [id,value]:groups)
    {std::set<std::string> visited;auto cursor=id;while(!cursor.empty()){Require(groups.contains(cursor)&&visited.insert(cursor).second,"TOOLPATH_GROUP_PARENT_MISSING_OR_CYCLE");cursor=Text(groups.at(cursor).To<Map>(),"parentGroupId");}}
    for(const auto& [id,value]:groups)for(const auto& path:ArrayAt(value.To<Map>(),"members"))Require(direct.emplace(ID(path),id).second,"TOOLPATH_GROUP_OWNERSHIP_CONFLICT");
    const auto owner=[&](auto&& self,SPortRef port)->std::string
    {
        if(memo.contains(port))return memo.at(port);const auto pathId=InspectionPathId(graph,records,port);
        if(direct.contains(pathId))return memo[port]=direct.at(pathId);
        std::string slot;const auto record=records.find(port.OperationId);if(record!=records.end())if(const auto* output=OutputData(record->second,port.PortId))slot=Text(*output,"organizationInputSlot");
        if(slot.empty()&&record!=records.end()&&record->second.contains("outputOrganization")){const auto inherited=MapAt(record->second,"outputOrganization");if(inherited.contains(Key(port.PortId)))slot=Text(inherited,Key(port.PortId));}
        if(!slot.empty())for(const auto& input:Inputs(graph.Definitions.at(port.OperationId)))if(input.Slot==slot)return memo[port]=self(self,input.Source);
        if((record==records.end()||!record->second.contains("outputs")||std::get<Array>(At(record->second,"outputs").m_Value).empty())&&DeclaresPort(graph.Definitions.at(port.OperationId),port))
        {std::set<std::string> inherited;for(const auto& input:Inputs(graph.Definitions.at(port.OperationId)))if(input.Replace)inherited.insert(self(self,input.Source));if(inherited.size()==1)return memo[port]=*inherited.begin();}
        return memo[port]=std::string();
    };
    Map result;for(const auto& [id,value]:groups){auto group=value.To<Map>();group["members"]=Array{};result[id]=std::move(group);}
    for(auto& path:paths){path.GroupId=owner(owner,path.Port);if(!path.GroupId.empty()){auto group=result.at(path.GroupId).To<Map>();auto members=ArrayAt(group,"members");members.emplace_back(path.PathId);group["members"]=members;result[path.GroupId]=std::move(group);}}
    return result;
}
std::pair<bool,bool> GroupState(const Map& plan,std::string id)
{
    const auto groups=MapAt(plan,"groups");bool enabled=true,locked=false;std::set<std::string> visited;
    while(!id.empty()){Require(groups.contains(id)&&visited.insert(id).second,"TOOLPATH_GROUP_PARENT_MISSING_OR_CYCLE");const auto group=groups.at(id).To<Map>();enabled&=Flag(group,"enabled",true);locked|=Flag(group,"locked");id=Text(group,"parentGroupId");}
    return {enabled,locked};
}
std::pair<bool,bool> PortGroupState(const IRepository& repository,const SGraph& graph,const std::map<IntentId,Map>& records,const SPortRef& port)
{
    SResolvedPath path;path.PathId=InspectionPathId(graph,records,port);path.Port=port;std::vector<SResolvedPath> paths{std::move(path)};ResolveOrganization(graph,records,paths);auto state=GroupState(graph.PlanDefinition,paths.front().GroupId);
    state.second|=Flag(PathLabels(repository,paths.front().PathId),"OwnLocked");
    const auto layers=ResolveToolpathLayer(LayerDocument(repository,graph.Document),graph.PlanDefinition,graph.Definitions,records,port,[&](SPortRef address){return InspectionPathId(graph,records,address);});
    state.second|=Flag(layers,"locked");return state;
}
Map CapturePlanCloneInput(const IRepository& repository,const SGraph& graph)
{
    Map definitions,records,policies;
    const auto includePath=[&](IntentId path,IntentId operation)
    {
        if(policies.contains(Key(path)))return;Map identity{{"kind",std::string("pathIdentity")},{"originOperation",operation}};
        if(const auto component=Component<CEditableDerivedComponent>(repository,path))
        {Require(component->GetOwner()==graph.Document&&Text(component->GetData(),"kind")=="pathIdentity","TOOLPATH_CLONE_PATH_NOT_OWNED");identity=component->GetData();}
        if(ActiveEditOverlay&&ActiveEditOverlay->Repository==&repository)if(const auto staged=ActiveEditOverlay->Identities.find(path);staged!=ActiveEditOverlay->Identities.end())identity=staged->second;
        auto policy=Map{{"identity",std::move(identity)},{"labels",PathLabels(repository,path)}};const auto doc=LayerDocument(repository,graph.Document);
        if(doc.contains("initialPathLayers")){const auto initial=MapAt(doc,"initialPathLayers");if(initial.contains(Key(path)))policy["initialLayers"]=initial.at(Key(path));}policies[Key(path)]=std::move(policy);
    };
    for(auto id:graph.Order)
    {
        const auto definition=graph.Definitions.at(id);definitions[Key(id)]=definition;const auto record=graph.Records.at(id);records[Key(id)]=record;
        if(definition.contains("sourcePathId"))includePath(ID(At(definition,"sourcePathId")),id);
        if(record.contains("identities"))for(const auto& [role,value]:MapAt(record,"identities"))includePath(ID(At(value.To<Map>(),"pathId")),id);
        for(const auto& path:Outputs(id,record))includePath(path.PathId,id);
    }
    return {{"planId",graph.Plan},{"plan",graph.PlanDefinition},{"definitions",std::move(definitions)},{"records",std::move(records)},
        {"pathPolicies",std::move(policies)},{"order",IdsData(graph.Order)}};
}
SEffectiveToolpaths ResolveGraph(const IRepository& repository,const SGraph& graph,const CToolpathOperationRegistry& registry,const IToolpathGeometryServices& geometry,bool inspect=false,SReadValidationCache* readCache=nullptr)
{
    SEffectiveToolpaths result;auto records=ValidRecords(graph,registry,geometry,readCache);const auto exclusions=MapAt(graph.PlanDefinition,"exclusions");
    const auto layerDocument=LayerDocument(repository,graph.Document);result.Layers=ToolpathLayerCatalog(layerDocument);
    if(inspect)for(const auto& [id,definition]:graph.Definitions)if(Text(definition,"typeId")=="icax.source"&&!records[id].contains("outputs"))
    {const auto value=ToolpathFromData(MapAt(MapAt(definition,"parameters"),"value"));records[id]["outputs"]=Array{Map{{"pathId",At(definition,"sourcePathId")},{"portId",SourceToolpathPort(id).PortId},{"fingerprint",ToolpathFingerprint(value)},{"value",ToData(value)}}};}
    std::set<SPortRef> visited;std::set<IntentId> paths,diagnosed,reachedOperations;Array operationConstraints;
    const auto diagnose=[&](IntentId id,const std::string& message){if(diagnosed.insert(id).second)result.Diagnostics.push_back({id,"TOOLPATH_NOT_READY",message});result.Status=EOutputCompleteness::Incomplete;};
    const auto append=[&](SPortRef port,const Map& record,const std::string& state)
    {
        if(exclusions.contains(Key(port))){result.Exclusions.push_back(port);if(!inspect||Text(exclusions.at(Key(port)).To<Map>(),"reason")=="Detached")return;}
        auto path=InspectionOutput(graph,records,port,readCache);Require(paths.insert(path.PathId).second,"TOOLPATH_DUPLICATE_EFFECTIVE_IDENTITY");path.ResultState=state;path.OwnParticipating=!exclusions.contains(Key(port));path.Participating=path.OwnParticipating&&IsActive(graph,port.OperationId);
        if(!path.OwnParticipating)path.ParticipationReason=Text(exclusions.at(Key(port)).To<Map>(),"reason");
        const auto labels=PathLabels(repository,path.PathId);path.Name=Text(labels,"Name");path.UserNumber=Text(labels,"UserNumber");path.OwnLocked=Flag(labels,"OwnLocked");
        if(const auto component=Component<CEditablePathLabelsComponent>(repository,path.PathId))path.LabelsVersion=component->Version();
        result.Paths.push_back(std::move(path));
    };
    const auto walk=[&](auto&& self,SPortRef port)->void
    {
        if(!visited.insert(port).second)return;
        reachedOperations.insert(port.OperationId);
        const auto record=records.find(port.OperationId);if(record==records.end()){diagnose(port.OperationId,"TOOLPATH_SOURCE_MISSING");return;}
        const auto replacement=graph.Replacements.find(port);
        if(replacement!=graph.Replacements.end())
        {
            reachedOperations.insert(replacement->second);
            const auto& next=records.at(replacement->second);const auto ports=InspectionPorts(graph,records,replacement->second);
            if(ports.empty()){diagnose(replacement->second,Text(next,"diagnostic"));if(inspect&&record->second.contains("outputs"))append(port,record->second,"PendingReplacement");return;}
            for(const auto& output:ports)self(self,output);
            if(Text(next,"state")=="Current"&&next.contains("constraints"))operationConstraints.emplace_back(Map{{"operationId",replacement->second},{"constraints",MapAt(next,"constraints")}});return;
        }
        const bool current=Text(record->second,"state")=="Current";const bool available=OutputData(record->second,port.PortId)!=nullptr;
        if(!current&&!exclusions.contains(Key(port)))diagnose(port.OperationId,Text(record->second,"diagnostic"));
        if(!inspect&&(!current||!IsActive(graph,port.OperationId)))return;
        append(port,record->second,current?"Current":!IsActive(graph,port.OperationId)?"Disabled":available?"Stale":"Pending");
    };
    for(const auto& root:ArrayAt(graph.PlanDefinition,"roots"))walk(walk,PortFromData(root.To<Map>()));
    for(auto id:Ids(ArrayAt(graph.PlanDefinition,"outputOperations")))
    {if(!inspect&&!IsActive(graph,id))continue;for(const auto& port:InspectionPorts(graph,records,id))walk(walk,port);}
    for(auto id:graph.Selected)if(IsActive(graph,id)&&!reachedOperations.contains(id))
    {bool replaces=false;for(const auto& input:Inputs(graph.Definitions.at(id)))replaces|=input.Replace;if(replaces)diagnose(id,"TOOLPATH_REPLACEMENT_BRANCH_NOT_REACHABLE");}
    for(const auto& [key,item]:exclusions)
    {
        const auto port=PortFromData(MapAt(item.To<Map>(),"port"));const auto record=records.find(port.OperationId);if(record==records.end())continue;
        bool exists=false;const auto outputs=ParsedReadOutputs(graph.Document,port.OperationId,record->second,readCache);for(const auto& output:*outputs)exists|=output.Port==port;
        if(!exists&&!DeclaresPort(graph.Definitions.at(port.OperationId),port)&&!PendingIdentityPort(record->second,port.PortId)&&RetiredOverlaps(record->second,port.PortId))diagnose(port.OperationId,"TOOLPATH_NEEDS_REBIND_EXCLUSION");
    }
    result.Organization=ResolveOrganization(graph,records,result.Paths);
    auto effectiveExclusions=exclusions;std::vector<SResolvedPath> effective;
    for(auto& path:result.Paths)
    {const auto [enabled,locked]=GroupState(graph.PlanDefinition,path.GroupId);path.Locked=path.OwnLocked||locked;path.Participating&=enabled;
     try{path.Layers=ResolveToolpathLayer(layerDocument,graph.PlanDefinition,graph.Definitions,records,path.Port,[&](SPortRef address){return InspectionPathId(graph,records,address);});path.Locked|=Flag(path.Layers,"locked");path.Participating&=Flag(path.Layers,"participating",true);}
     catch(const std::exception& error){diagnose(path.Port.OperationId,error.what());path.ResultState="NeedsDecision";}
     if(path.Participating&&path.ResultState=="Current"){SResolvedPath identity;identity.PathId=path.PathId;identity.Port=path.Port;effective.push_back(std::move(identity));}else{effectiveExclusions[Key(path.Port)]=Map{{"port",ToData(path.Port)}};if(!path.Participating&&std::find(result.Exclusions.begin(),result.Exclusions.end(),path.Port)==result.Exclusions.end())result.Exclusions.push_back(path.Port);}}
    result.ExecutionConstraints=ResolveConstraints(graph,records,effective,effectiveExclusions,diagnose);
    if(inspect)result.DisplayConstraints=ResolveConstraints(graph,records,result.Paths,{},diagnose,true);
    result.ExecutionConstraints["operations"]=std::move(operationConstraints);
    result.IntentionalEmpty=result.Status==EOutputCompleteness::Complete&&effective.empty();
    if(!inspect)std::erase_if(result.Paths,[](const auto& path){return !path.Participating||path.ResultState!="Current";});
    Array signature;for(const auto& path:result.Paths)signature.emplace_back(Map{{"port",ToData(path.Port)},{"fingerprint",path.Fingerprint},{"name",path.Name},{"number",path.UserNumber},{"participating",path.Participating},{"locked",path.OwnLocked||GroupState(graph.PlanDefinition,path.GroupId).second},{"ownLocked",path.OwnLocked},{"ownParticipating",path.OwnParticipating},{"participationReason",path.ParticipationReason},{"resultState",path.ResultState},
        {"processingLayer",path.Layers.contains("processingLayerId")?At(path.Layers,"processingLayerId"):Variant(IntentId{})},{"processBinding",path.Layers.contains("processBinding")?At(path.Layers,"processBinding"):Variant(Map{})}});
    Array presentation;for(const auto& path:result.Paths)presentation.emplace_back(Map{{"pathId",path.PathId},{"layers",path.Layers}});
    result.PresentationFingerprint=Hash(Map{{"catalog",result.Layers},{"paths",std::move(presentation)}});
    result.Fingerprint=Hash(Map{{"outputs",std::move(signature)},{"exclusions",exclusions},{"constraints",result.ExecutionConstraints},{"displayConstraints",result.DisplayConstraints},{"organization",result.Organization},{"status",static_cast<int>(result.Status)}});return result;
}
}

struct CEditableToolpathDocument::Impl final
{
    std::shared_ptr<IRepository> Repository;
    IntentId Document,Session=iCAX::Data::GenerateNewUUID();
    std::shared_ptr<CToolpathOperationRegistry> Registry;
    std::shared_ptr<const IToolpathGeometryServices> Geometry;
    std::shared_ptr<CEpochObserver> Observer=std::make_shared<CEpochObserver>();
    std::thread::id OwnerThread=std::this_thread::get_id();
    std::vector<std::weak_ptr<SGenerationState>> Generations;
    std::uint64_t NextTicket=0;
    std::map<IntentId,std::uint64_t> PublishedTickets;
    bool Editing=false;
    Map PendingDocument;
    SReadValidationCache ReadValidation;
    SReadValidationCache* ReadCache()
    {
        if(ActiveEditOverlay)return nullptr;
        const auto serial=Observer->Serial;
        ReadValidation.Serial=serial;
        ReadValidation.RequireUnchangedRepository=[this,serial]{Require(Observer->Serial==serial,"TOOLPATH_REPOSITORY_CHANGED_DURING_PATH_READ");};
        ReadValidation.Revision=[this](IntentId id){const auto found=Observer->Changes.find(id);return found==Observer->Changes.end()?std::uint64_t(0):found->second;};
        return &ReadValidation;
    }
    IntentMap StageCommand(const IntentMap&,const std::string&,const SEditableToolpathHostEdit&);
    void Owner()const{Require(std::this_thread::get_id()==OwnerThread,"TOOLPATH_REPOSITORY_OWNER_THREAD_REQUIRED");}
    Map DocumentDefinition()const
    {if(ActiveEditOverlay&&ActiveEditOverlay->Repository==Repository.get())if(const auto found=ActiveEditOverlay->Definitions.find({Document,CEditableDocumentComponent::S_ClassName});found!=ActiveEditOverlay->Definitions.end())return found->second;
     if(!Component<CEditableDocumentComponent>(*Repository,Document)&&!PendingDocument.empty())return PendingDocument;
     return Definition<CEditableDocumentComponent>(*Repository,Document);}
    Map Operation(IntentId id)const
    {const auto data=Definition<CEditableOperationComponent>(*Repository,id);Require(ID(At(data,"documentId"))==Document,"TOOLPATH_OPERATION_DOCUMENT_MISMATCH");return data;}
};

CEditableToolpathDocument::CEditableToolpathDocument(std::shared_ptr<IRepository> repository,IntentId id,
    std::shared_ptr<CToolpathOperationRegistry> registry,std::shared_ptr<const IToolpathGeometryServices> geometry)
    :CEditableToolpathDocument(std::move(repository),id,std::move(registry),std::move(geometry),false){}
std::unique_ptr<CEditableToolpathDocument> CEditableToolpathDocument::PrepareNewDocument(
    std::shared_ptr<IRepository> repository,IntentId id,std::shared_ptr<CToolpathOperationRegistry> registry,std::shared_ptr<const IToolpathGeometryServices> geometry)
{return std::unique_ptr<CEditableToolpathDocument>(new CEditableToolpathDocument(std::move(repository),id,std::move(registry),std::move(geometry),true));}
CEditableToolpathDocument::CEditableToolpathDocument(std::shared_ptr<IRepository> repository,IntentId id,
    std::shared_ptr<CToolpathOperationRegistry> registry,std::shared_ptr<const IToolpathGeometryServices> geometry,bool deferred):m_Impl(std::make_unique<Impl>())
{
    Require(repository&&registry,"TOOLPATH_HOST_REQUIRED");auto& impl=*m_Impl;impl.Repository=std::move(repository);impl.Registry=std::move(registry);impl.Geometry=geometry?std::move(geometry):CreateToolpathGeometryServices();
    impl.Document=id.is_nil()?iCAX::Data::GenerateNewUUID():id;
    if(deferred)
    {
        Require(!impl.Repository->GetEntity(impl.Document),"TOOLPATH_DOCUMENT_ID_COLLISION");
        impl.PendingDocument={{"schemaVersion",3u},{"documentId",impl.Document},{"plans",Array{}},{"sources",Array{}},{"requestEpoch",iCAX::Data::GenerateNewUUID()},{"layers",InitialToolpathLayers(impl.Document)},{"initialPathLayers",Map{}}};
    }
    else if(id.is_nil())
    {auto undo=impl.Repository->BeginUndoCommand("创建刀路文档");auto& tx=impl.Repository->BeginTransaction("Create editable toolpath document");
     Create(tx,impl.Document,CEditableDocumentComponent::S_ClassName,{{"Definition",Map{{"schemaVersion",3u},{"documentId",impl.Document},{"plans",Array{}},{"sources",Array{}},{"requestEpoch",iCAX::Data::GenerateNewUUID()},{"layers",InitialToolpathLayers(impl.Document)},{"initialPathLayers",Map{}}}}});Commit(*impl.Repository,tx);undo->End();}
    else Require(At(impl.DocumentDefinition(),"schemaVersion").To<unsigned int>()==3,"TOOLPATH_DOCUMENT_VERSION_UNSUPPORTED");
    impl.Repository->AddObserver(impl.Observer);
}
CEditableToolpathDocument::~CEditableToolpathDocument()
{for(const auto& weak:m_Impl->Generations)if(const auto task=weak.lock())task->Cancelled->store(true);m_Impl->Repository->RemoveObserver(m_Impl->Observer);}
IntentId CEditableToolpathDocument::Id()const{return m_Impl->Document;}
IntentId CEditableToolpathDocument::CreatePlan(const std::string& name)
{
    const auto id=iCAX::Data::GenerateNewUUID();Execute({{"kind",std::string("CreatePlan")},{"commandId",iCAX::Data::GenerateNewUUID()},{"planId",id},{"name",name}});return id;
}
IntentMap CEditableToolpathDocument::ReadOperation(IntentId id)const
{auto data=ReadOperationDefinition(id);data["result"]=Result(*m_Impl->Repository,m_Impl->Document,id);return data;}
IntentMap CEditableToolpathDocument::ReadOperationDefinition(IntentId id)const
{m_Impl->Owner();auto data=m_Impl->Operation(id);data["definitionFingerprint"]=DefinitionHash(data);return data;}
IntentMap CEditableToolpathDocument::ReadPlan(IntentId id)const
{auto data=ReadPlanDefinition(id);data["graphFingerprint"]=Hash(CapturePlanCloneInput(*m_Impl->Repository,LoadGraph(*m_Impl->Repository,m_Impl->Document,id)));return data;}
IntentMap CEditableToolpathDocument::ReadPlanDefinition(IntentId id)const
{m_Impl->Owner();auto data=Definition<CEditablePlanComponent>(*m_Impl->Repository,id);Require(ID(At(data,"documentId"))==m_Impl->Document,"TOOLPATH_PLAN_DOCUMENT_MISMATCH");data["definitionFingerprint"]=DefinitionHash(data);return data;}
IntentMap CEditableToolpathDocument::QueryLayers()const
{m_Impl->Owner();auto layers=ToolpathLayerCatalog(m_Impl->DocumentDefinition());layers["fingerprint"]=Hash(layers);return layers;}
IntentMap CEditableToolpathDocument::QueryLayerAssignments(IntentId plan)const
{
    auto& impl=*m_Impl;impl.Owner();const auto graph=LoadGraph(*impl.Repository,impl.Document,plan,true);const auto doc=impl.DocumentDefinition();
    const auto exclusions=MapAt(graph.PlanDefinition,"exclusions");std::set<SPortRef> visited;std::vector<SResolvedPath> paths;
    const auto walk=[&](auto&& self,SPortRef port)->void
    {
        if(!visited.insert(port).second)return;
        if(const auto next=graph.Replacements.find(port);next!=graph.Replacements.end()){
            const auto outputs=InspectionPorts(graph,graph.Records,next->second);
            if(!outputs.empty()){for(const auto& output:outputs)self(self,output);return;}
        }
        if(exclusions.contains(Key(port))){const auto reason=Text(exclusions.at(Key(port)).To<Map>(),"reason");if(reason=="Deleted"||reason=="Detached")return;}
        SResolvedPath path;path.Port=port;path.PathId=InspectionPathId(graph,graph.Records,port);paths.push_back(std::move(path));
    };
    for(const auto& root:ArrayAt(graph.PlanDefinition,"roots"))walk(walk,PortFromData(root.To<Map>()));
    for(const auto& id:ArrayAt(graph.PlanDefinition,"outputOperations"))for(const auto& port:InspectionPorts(graph,graph.Records,ID(id)))walk(walk,port);
    ResolveOrganization(graph,graph.Records,paths);Map result;
    for(const auto& path:paths){
        auto layers=ResolveToolpathLayer(doc,graph.PlanDefinition,graph.Definitions,graph.Records,path.Port,[&](SPortRef port){return InspectionPathId(graph,graph.Records,port);});
        const bool locked=Flag(PathLabels(*impl.Repository,path.PathId),"OwnLocked")||GroupState(graph.PlanDefinition,path.GroupId).second||Flag(layers,"locked");
        Require(!result.contains(Key(path.PathId)),"TOOLPATH_DUPLICATE_EFFECTIVE_IDENTITY");
        result[Key(path.PathId)]=Map{{"port",ToData(path.Port)},{"layers",std::move(layers)},{"locked",locked}};
    }
    return result;
}
SEffectiveToolpaths CEditableToolpathDocument::Resolve(IntentId plan)const
{
    m_Impl->Owner();auto* readCache=m_Impl->ReadCache();try{return ResolveGraph(*m_Impl->Repository,LoadGraph(*m_Impl->Repository,m_Impl->Document,plan),*m_Impl->Registry,*m_Impl->Geometry,false,readCache);}
    catch(const std::exception& error){SEffectiveToolpaths result;result.Status=EOutputCompleteness::Invalid;result.Diagnostics.push_back({{},"TOOLPATH_GRAPH_INVALID",error.what()});return result;}
}
SEffectiveToolpaths CEditableToolpathDocument::InspectPlan(IntentId plan)const
{
    m_Impl->Owner();auto* readCache=m_Impl->ReadCache();try{return ResolveGraph(*m_Impl->Repository,LoadGraph(*m_Impl->Repository,m_Impl->Document,plan),*m_Impl->Registry,*m_Impl->Geometry,true,readCache);}
    catch(const std::exception& error){SEffectiveToolpaths result;result.Status=EOutputCompleteness::Invalid;result.Diagnostics.push_back({{},"TOOLPATH_GRAPH_INVALID",error.what()});return result;}
}
SResolvedPath CEditableToolpathDocument::ReadPath(IntentId plan,SPortRef port)const
{auto paths=ReadPaths(plan,{port});return std::move(paths.front());}
std::vector<SResolvedPath> CEditableToolpathDocument::ReadPaths(IntentId plan,const std::vector<SPortRef>& ports)const
{
    auto& impl=*m_Impl;impl.Owner();const auto serial=impl.Observer->Serial;auto* readCache=impl.ReadCache();const auto graph=LoadGraph(*impl.Repository,impl.Document,plan);
    for(const auto& port:ports)Require(graph.Definitions.contains(port.OperationId),"TOOLPATH_PATH_OUTSIDE_PLAN");
    const auto records=ValidRecords(graph,*impl.Registry,*impl.Geometry,readCache);std::vector<SResolvedPath> paths,identities;paths.reserve(ports.size());identities.reserve(ports.size());
    Require(impl.Observer->Serial==serial,"TOOLPATH_REPOSITORY_CHANGED_DURING_PATH_READ");
    for(const auto& port:ports)
    {
        const auto record=records.find(port.OperationId);Require(record!=records.end()&&Text(record->second,"state")=="Current","TOOLPATH_PATH_NOT_CURRENT");
        auto path=FindReadOutput(impl.Document,port.OperationId,port.PortId,record->second,readCache);SResolvedPath identity;identity.PathId=path.PathId;identity.Port=path.Port;
        identities.push_back(std::move(identity));paths.push_back(std::move(path));
    }
    ResolveOrganization(graph,records,identities);const auto exclusions=MapAt(graph.PlanDefinition,"exclusions");
    for(std::size_t index=0;index<paths.size();++index)
    {
        auto& path=paths[index];path.GroupId=identities[index].GroupId;
        const auto labels=PathLabels(*impl.Repository,path.PathId);path.Name=Text(labels,"Name");path.UserNumber=Text(labels,"UserNumber");path.OwnLocked=Flag(labels,"OwnLocked");
        if(const auto component=Component<CEditablePathLabelsComponent>(*impl.Repository,path.PathId))path.LabelsVersion=component->Version();
        path.OwnParticipating=!exclusions.contains(Key(path.Port));
        if(!path.OwnParticipating)path.ParticipationReason=Text(exclusions.at(Key(path.Port)).To<Map>(),"reason");
        const auto [enabled,locked]=GroupState(graph.PlanDefinition,path.GroupId);path.Participating=path.OwnParticipating&&enabled;path.Locked=path.OwnLocked||locked;
        path.Layers=ResolveToolpathLayer(impl.DocumentDefinition(),graph.PlanDefinition,graph.Definitions,records,path.Port,[&](SPortRef address){return InspectionPathId(graph,records,address);});path.Locked|=Flag(path.Layers,"locked");path.Participating&=Flag(path.Layers,"participating",true);
    }
    Require(impl.Observer->Serial==serial,"TOOLPATH_REPOSITORY_CHANGED_DURING_PATH_READ");return paths;
}
std::vector<SResolvedPath> CEditableToolpathDocument::QueryPaths(IntentId plan,std::size_t offset,std::size_t limit,const std::string& expected)const
{Require(limit>0&&limit<=1000,"TOOLPATH_PAGE_LIMIT");const auto result=Resolve(plan);const auto fingerprint=Hash(Map{{"output",result.Fingerprint},{"presentation",result.PresentationFingerprint}});Require(expected.empty()||expected==fingerprint,"TOOLPATH_CURSOR_EXPIRED");if(offset>=result.Paths.size())return {};const auto end=std::min(result.Paths.size(),offset+limit);return {result.Paths.begin()+offset,result.Paths.begin()+end};}
IntentMap CEditableToolpathDocument::QuerySummary(IntentId plan)const
{
    const auto result=Resolve(plan);return {{"documentId",m_Impl->Document},{"planId",plan},{"status",static_cast<int>(result.Status)},
        {"intentionalEmpty",result.IntentionalEmpty},{"pathCount",static_cast<unsigned long long>(result.Paths.size())},
        {"diagnosticCount",static_cast<unsigned long long>(result.Diagnostics.size())},{"fingerprint",Hash(Map{{"output",result.Fingerprint},{"presentation",result.PresentationFingerprint}})}};
}
IntentMap CEditableToolpathDocument::QueryPathSummaries(IntentId plan,std::size_t offset,std::size_t limit,const std::string& expected)const
{
    Require(limit>0&&limit<=1000,"TOOLPATH_PAGE_LIMIT");const auto result=Resolve(plan);const auto fingerprint=Hash(Map{{"output",result.Fingerprint},{"presentation",result.PresentationFingerprint}});Require(expected.empty()||expected==fingerprint,"TOOLPATH_CURSOR_EXPIRED");Array items;
    if(offset<result.Paths.size())for(std::size_t i=offset;i<std::min(result.Paths.size(),offset+limit);++i)
    {const auto& p=result.Paths[i];items.emplace_back(Map{{"pathId",p.PathId},{"port",ToData(p.Port)},{"name",p.Name},{"userNumber",p.UserNumber},
        {"labelsVersion",static_cast<unsigned long long>(p.LabelsVersion)},{"fingerprint",p.Fingerprint},{"segmentCount",static_cast<unsigned long long>(p.Value.Geometry.Segments.size())},
        {"closed",p.Value.Geometry.Closed},{"frameId",p.Value.Geometry.FrameId},{"groupId",p.GroupId},{"layers",p.Layers}});}
    return {{"items",std::move(items)},{"offset",static_cast<unsigned long long>(offset)},{"total",static_cast<unsigned long long>(result.Paths.size())},{"fingerprint",fingerprint},{"status",static_cast<int>(result.Status)}};
}
SOperationCandidate CEditableToolpathDocument::Preview(const std::string& typeId,const IntentMap& parameters,const std::vector<SPortRef>& ports,const IntentMap& context)const
{
    auto& impl=*m_Impl;impl.Owner();const auto type=impl.Registry->Find(typeId);Require(type!=nullptr,"TOOLPATH_OPERATION_UNSUPPORTED");ValidateParameterSchema(type->ParameterSchema,parameters);if(type->ValidateParameters)type->ValidateParameters(parameters);
    SOperationEvaluation in;in.OperationId=iCAX::Data::GenerateNewUUID();in.Parameters=parameters;in.Context=context;in.Cancelled=std::make_shared<std::atomic_bool>(false);
    for(std::size_t i=0;i<ports.size();++i)
    {
        const auto definition=impl.Operation(ports[i].OperationId);const auto plan=ID(At(definition,"planId"));Map record;
        if(plan.is_nil())
        {
            const auto sourceType=impl.Registry->Find(Text(definition,"typeId"));Require(sourceType&&Flag(definition,"enabled",true),"TOOLPATH_INPUT_NOT_CURRENT");ValidateType(definition,*sourceType);
            record=Result(*impl.Repository,impl.Document,ports[i].OperationId);
            Require(Text(record,"state")=="Current"&&Text(record,"definitionFingerprint")==DefinitionHash(definition)
                &&Text(record,"evaluationRevision")==sourceType->EvaluationRevision,"TOOLPATH_INPUT_NOT_CURRENT");
        }
        else
        {
            const auto records=ValidRecords(LoadGraph(*impl.Repository,impl.Document,plan),*impl.Registry,*impl.Geometry);
            const auto found=records.find(ports[i].OperationId);Require(found!=records.end()&&Text(found->second,"state")=="Current","TOOLPATH_INPUT_NOT_CURRENT");record=found->second;
        }
        auto output=FindOutput(ports[i].OperationId,ports[i].PortId,record);impl.Geometry->Validate(output.Value.Geometry);
        in.Inputs.push_back({{"input/"+std::to_string(i),ports[i],true,{}},output.PathId,std::move(output.Value),output.Fingerprint});
    }
    Require(in.Inputs.size()>=type->MinimumInputs&&in.Inputs.size()<=type->MaximumInputs,"TOOLPATH_INPUT_ARITY");
    if(type->NormalizeParameters)in.Parameters=type->NormalizeParameters(in.Inputs,in.Parameters);
    const auto contextSnapshot=FreezeOperationContext(type.get(),context);Require(contextSnapshot.Error.empty(),contextSnapshot.Error);in.RuntimeContext=contextSnapshot.Value;
    if(type->ValidateInput)type->ValidateInput(in);
    auto algorithmInput=in;
    if(!type->ReadChannels.contains("*"))for(auto& input:algorithmInput.Inputs)
        std::erase_if(input.Value.Channels,[&](const auto& item){return !type->ReadChannels.contains(item.first);});
    auto result=type->Evaluate(algorithmInput,*impl.Geometry);
    if(!type->DeclaredOutputRoles.empty()){std::set<std::string> roles;for(const auto& output:result.Outputs)roles.insert(output.RoleKey);Require(roles==type->DeclaredOutputRoles,"TOOLPATH_DECLARED_OUTPUTS_MISMATCH");}
    PropagateChannels(in,result,*type);ValidateCandidate(in,result,*impl.Geometry);if(type->ValidateResult)type->ValidateResult(in,result);return result;
}
std::shared_ptr<CToolpathGeneration> CEditableToolpathDocument::GenerateAsync(IntentId plan,std::size_t concurrency,const std::vector<IntentId>& forceOperations)
{
    auto& impl=*m_Impl;impl.Owner();auto graph=LoadGraph(*impl.Repository,impl.Document,plan);auto state=std::make_shared<SGenerationState>();state->Session=impl.Session;state->Document=impl.Document;state->Plan=plan;state->Ticket=++impl.NextTicket;
    std::map<IntentId,std::uint64_t> epochs;std::map<IntentId,std::shared_ptr<const SOperationType>> types;std::map<IntentId,SFrozenContext> contexts;
    for(auto id:graph.Order){epochs[id]=impl.Observer->Epochs[id];types[id]=impl.Registry->Find(Text(graph.Definitions.at(id),"typeId"));contexts[id]=FreezeOperationContext(types[id].get(),MapAt(graph.Definitions.at(id),"context"));}
    if(!concurrency)concurrency=std::max(1u,std::thread::hardware_concurrency());concurrency=std::min<std::size_t>(concurrency,64);
    const std::set<IntentId> force(forceOperations.begin(),forceOperations.end());for(auto id:force)Require(graph.Definitions.contains(id),"TOOLPATH_REGENERATE_SCOPE_INVALID");
    // The worker owns frozen values only. A packaged task avoids std::async's
    // blocking future destructor when the user closes the document or drops a
    // cancelled generation while an algorithm is still returning.
    std::packaged_task<std::vector<SPrepared>()> task([graph=std::move(graph),epochs=std::move(epochs),types=std::move(types),contexts=std::move(contexts),geometry=impl.Geometry,cancelled=state->Cancelled,concurrency,force]()mutable{return EvaluateGraph(std::move(graph),epochs,types,contexts,geometry,cancelled,concurrency,force);});
    state->Future=task.get_future();std::thread(std::move(task)).detach();
    impl.Generations.push_back(state);auto holder=std::make_shared<CToolpathGeneration::Impl>();holder->State=state;return std::shared_ptr<CToolpathGeneration>(new CToolpathGeneration(std::move(holder)));
}
bool CEditableToolpathDocument::Publish(const std::shared_ptr<CToolpathGeneration>& handle)
{
    auto& impl=*m_Impl;impl.Owner();Require(handle!=nullptr,"TOOLPATH_GENERATION_REQUIRED");auto& state=*handle->m_Impl->State;Require(state.Session==impl.Session&&state.Document==impl.Document,"TOOLPATH_GENERATION_SESSION_EXPIRED");
    if(state.Published)return true;if(!handle->IsReady())return false;
    if(impl.Repository->IsWriteInProgress())return false;
    if(!state.Prepared)state.Prepared=state.Future.get();
    if(state.Cancelled->load()){state.Published=true;state.Prepared.reset();return true;}
    const auto activePlan=Component<CEditablePlanComponent>(*impl.Repository,state.Plan);
    if(!activePlan||!impl.Repository->GetEntity(impl.Document))
    {state.Published=true;state.Prepared.reset();return true;}
    // Publication is synchronous on the repository owner. Keep one owned graph
    // for this call and validate only the inputs each prepared node actually
    // needs. Full values are never copied into a second validation graph.
    SGraph graph;std::map<IntentId,std::uint64_t> snapshotChanges;std::map<IntentId,std::string> definitionHashes;
    using SDependencyEvidence=std::tuple<SPortRef,bool,std::string,std::uint64_t>;
    struct SValidated final
    {
        std::shared_ptr<const SOperationType> Type;
        std::string ContextFingerprint;
        std::vector<SDependencyEvidence> Dependencies;
        std::uint64_t ResultRevision=0;
        bool Current=false;
    };
    struct SObservedContext final{std::shared_ptr<const SOperationType> Type;SFrozenContext Frozen;};
    struct SInputReadHash final{std::shared_ptr<const SOperationType> Type;std::uint64_t GraphRevision=0;std::string Fingerprint;};
    std::map<IntentId,SValidated> validated;std::map<IntentId,SObservedContext> frameContexts;
    std::map<IntentId,std::vector<SOperationInput>> bindings;
    std::map<IntentId,SInputReadHash> inputReadHashes;
    std::uint64_t graphRevision=0;
    const auto refresh=[&]()->bool
    {
        if(!impl.Repository->GetEntity(impl.Document)||!Component<CEditablePlanComponent>(*impl.Repository,state.Plan))return false;
        graph=LoadGraph(*impl.Repository,impl.Document,state.Plan);snapshotChanges.clear();definitionHashes.clear();validated.clear();frameContexts.clear();bindings.clear();inputReadHashes.clear();++graphRevision;
        snapshotChanges[impl.Document]=impl.Observer->Changes[impl.Document];snapshotChanges[state.Plan]=impl.Observer->Changes[state.Plan];
        for(const auto& [id,definition]:graph.Definitions){snapshotChanges[id]=impl.Observer->Changes[id];const auto result=ResultId(impl.Document,id);snapshotChanges[result]=impl.Observer->Changes[result];definitionHashes[id]=DefinitionHash(definition);}return true;
    };
    const auto changed=[&](IntentId ownResult=IntentId{})
    {for(const auto& [id,revision]:snapshotChanges)if(id!=ownResult&&impl.Observer->Changes[id]!=revision)return true;return false;};
    const auto context=[&](IntentId id,const std::shared_ptr<const SOperationType>& type)->const SFrozenContext&
    {
        if(const auto found=frameContexts.find(id);found!=frameContexts.end()&&found->second.Type==type)return found->second.Frozen;
        auto frozen=FreezeOperationContext(type.get(),MapAt(graph.Definitions.at(id),"context"));
        return frameContexts.insert_or_assign(id,SObservedContext{type,std::move(frozen)}).first->second.Frozen;
    };
    const auto inputsFor=[&](IntentId id)->const std::vector<SOperationInput>&
    {if(const auto found=bindings.find(id);found!=bindings.end())return found->second;return bindings.emplace(id,Inputs(graph.Definitions.at(id))).first->second;};
    const auto inputReadHash=[&](IntentId id,const std::shared_ptr<const SOperationType>& type)->const std::string&
    {
        // The hash is pure data work over this owned graph. Do not cache the
        // external context/current-input decision with it. Accepted writes and
        // refreshes advance GraphRevision even when output geometry is equal.
        if(const auto found=inputReadHashes.find(id);found!=inputReadHashes.end()&&found->second.Type==type&&found->second.GraphRevision==graphRevision)return found->second.Fingerprint;
        const auto inputs=FrozenInputs(graph.Definitions.at(id),graph.Records);
        return inputReadHashes.insert_or_assign(id,SInputReadHash{type,graphRevision,ReadHash(inputs,*type)}).first->second.Fingerprint;
    };
    std::function<bool(IntentId)> currentInput;
    currentInput=[&](IntentId id)->bool
    {
        const auto& definition=graph.Definitions.at(id);const auto& record=graph.Records.at(id);const auto type=impl.Registry->Find(Text(definition,"typeId"));
        // A blocked/missing stored result cannot become current merely because
        // an ancestor changed; only an accepted publication can replace it.
        if(!type||!IsActive(graph,id)||Text(record,"state")!="Current")return false;
        // A successful read proof covers the expensive immutable value checks,
        // not observation of the current descriptor, context or dependency
        // closure. Copy it before callbacks: a nested read may replace/evict
        // the owner's cache entry without changing the persisted graph.
        std::optional<SEditValidationMemo> readProof;
        if(const auto found=impl.ReadValidation.Entries.find(id);found!=impl.ReadValidation.Entries.end()&&found->second.Current)readProof=found->second;
        const auto proofObservationSerial=impl.Observer->Serial;
        try{ValidateType(definition,*type);}catch(const std::exception&){return false;}
        // Observe contexts and registry descriptors through the dependency
        // closure before consulting the memo, including after listener writes.
        const auto frozen=context(id,type);std::vector<SDependencyEvidence> dependencies;bool inputsCurrent=true;
        for(const auto& input:inputsFor(id))
        {const bool ready=currentInput(input.Source.OperationId);inputsCurrent&=ready;const auto& source=graph.Records.at(input.Source.OperationId);const auto* output=OutputData(source,input.Source.PortId);
         dependencies.emplace_back(input.Source,ready,output?Text(*output,"fingerprint"):std::string(),snapshotChanges.at(ResultId(impl.Document,input.Source.OperationId)));}
        const auto resultRevision=snapshotChanges.at(ResultId(impl.Document,id));
        if(impl.Registry->Find(Text(definition,"typeId"))!=type)return false;
        if(const auto found=validated.find(id);found!=validated.end()&&found->second.Type==type&&found->second.ResultRevision==resultRevision
            &&found->second.ContextFingerprint==frozen.Fingerprint&&found->second.Dependencies==dependencies)return found->second.Current;
        bool ready=false;
        try
        {
            Require(inputsCurrent,"TOOLPATH_INPUT_NOT_CURRENT");Require(frozen.Error.empty(),frozen.Error);Require(Text(record,"evaluationRevision")==type->EvaluationRevision,"TOOLPATH_RESULT_STALE_EVALUATOR");
            Require(Text(record,"definitionFingerprint")==definitionHashes.at(id),"TOOLPATH_RESULT_STALE_DEFINITION");Require(Text(record,"contextFingerprint")==frozen.Fingerprint,"TOOLPATH_RESULT_STALE_CONTEXT");
            Require(impl.Registry->Find(Text(definition,"typeId"))==type,"TOOLPATH_OPERATION_VERSION_UNSUPPORTED");
            bool readProofMatches=false;
            if(readProof&&readProof->Type==type&&readProof->DefinitionRevision==impl.Observer->Changes[id]
                &&readProof->RepositoryRevision==resultRevision&&readProof->RepositoryRevision==impl.Observer->Changes[ResultId(impl.Document,id)]
                &&readProof->DefinitionFingerprint==definitionHashes.at(id)&&impl.Observer->Serial==proofObservationSerial)
            {
                Array revisions;for(const auto& input:inputsFor(id))revisions.emplace_back(static_cast<unsigned long long>(impl.Observer->Changes[ResultId(impl.Document,input.Source.OperationId)]));
                // Keep this key identical to ValidRecords' read memo. Both
                // source-result versions and their complete input state are
                // required; an equal output fingerprint alone is insufficient.
                const auto key=Hash(Map{{"definition",definitionHashes.at(id)},{"resultRevision",static_cast<unsigned long long>(resultRevision)},
                    {"context",frozen.Fingerprint},{"inputResultRevisions",std::move(revisions)},{"inputs",InputStateHash(definition,graph.Records)}});
                readProofMatches=readProof->Key==key;
            }
            if(!readProofMatches)
            {
                Require(Text(record,"readFingerprint")==inputReadHash(id,type),"TOOLPATH_RESULT_STALE_INPUT");
                for(const auto& output:Outputs(id,record))impl.Geometry->Validate(output.Value.Geometry);
            }
            ready=true;
        }
        catch(const std::exception&){}
        validated[id]={type,frozen.Fingerprint,std::move(dependencies),resultRevision,ready};return ready;
    };
    const auto inputState=[&](const Map& definition)
    {
        std::map<IntentId,Map> states;
        for(const auto& input:Inputs(definition))
        {
            const auto id=input.Source.OperationId;const auto& record=graph.Records.at(id);Array outputs;
            if(record.contains("outputs"))for(const auto& item:std::get<Array>(At(record,"outputs").m_Value)){const auto& output=std::get<Map>(item.m_Value);outputs.emplace_back(Map{{"portId",At(output,"portId")},{"fingerprint",Text(output,"fingerprint")}});}
            states[id]={{"state",currentInput(id)?std::string("Current"):std::string("Blocked")},{"definitionFingerprint",Text(record,"definitionFingerprint")},{"readFingerprint",Text(record,"readFingerprint")},{"outputs",std::move(outputs)}};
        }
        return InputStateHash(definition,states);
    };
    if(!refresh()){state.Published=true;state.Prepared.reset();return true;}
    for(const auto& job:*state.Prepared)
    {
        if(changed()&&!refresh())break;frameContexts.clear();
        if(impl.PublishedTickets[job.Operation]>state.Ticket)continue;
        if(!impl.Repository->GetEntity(impl.Document)||!Component<CEditableOperationComponent>(*impl.Repository,job.Operation))continue;
        if(!graph.Definitions.contains(job.Operation)||!IsActive(graph,job.Operation))continue;
        const auto& current=graph.Definitions.at(job.Operation);if(definitionHashes.at(job.Operation)!=DefinitionHash(job.Definition)||impl.Observer->Epochs[job.Operation]!=job.Epoch||!Flag(current,"enabled",true))continue;
        if(impl.Registry->Find(Text(current,"typeId"))!=job.Type)continue;
        if(job.Type&&Text(job.Record,"evaluationRevision")!=job.Type->EvaluationRevision)continue;
        if(context(job.Operation,job.Type).Fingerprint!=Text(job.Record,"contextFingerprint"))continue;
        const bool unchangedRecord=graph.Records.at(job.Operation)==job.Record;
        if(Text(job.Record,"state")=="Current")
        {
            try
            {
                if(unchangedRecord)
                {
                    // Equality alone never certifies currentness. Re-observe
                    // this descriptor/context and its entire input closure;
                    // a matching successful read proof may then replace the
                    // duplicate parse/hash/geometry check for this exact record.
                    frameContexts.erase(job.Operation);
                    if(!currentInput(job.Operation))continue;
                }
                else
                {
                    bool ready=true;for(const auto& input:inputsFor(job.Operation))ready&=currentInput(input.Source.OperationId);
                    if(!ready||!job.Type||inputReadHash(job.Operation,job.Type)!=Text(job.Record,"readFingerprint"))continue;
                }
            }
            catch(const std::exception&){continue;}
        }
        if(Text(job.Record,"state")!="Current")
        {if(inputState(current)!=job.InputStateFingerprint)continue;}
        // Freeze callbacks and synchronous listeners may invalidate the owner
        // snapshot. A changed graph is refreshed rather than trusted as Current.
        if(changed()){if(!refresh())break;continue;}
        // A read-only callback can publish a newer ticket or replace a
        // descriptor without changing this graph's repository entities.
        if(impl.PublishedTickets[job.Operation]>state.Ticket||impl.Registry->Find(Text(current,"typeId"))!=job.Type)continue;
        if(unchangedRecord){impl.PublishedTickets[job.Operation]=state.Ticket;continue;}
        // Validate every identity before opening a transaction. A failed
        // publication keeps its frozen candidate so the caller can retry.
        std::vector<IntentId> outputIds;
        if(job.Record.contains("outputs"))for(const auto& item:std::get<Array>(At(job.Record,"outputs").m_Value))
        {const auto path=ID(At(std::get<Map>(item.m_Value),"pathId"));outputIds.push_back(path);if(impl.Repository->GetEntity(path))
         {const auto identity=Component<CEditableDerivedComponent>(*impl.Repository,path);Require(identity&&identity->GetOwner()==impl.Document,"TOOLPATH_PATH_ID_COLLISION");}}
        const auto initialLabels=AllocateInitialToolpathLabels(*impl.Repository,impl.Document,job.Operation,
            current.contains("outputLabelPolicy")?MapAt(current,"outputLabelPolicy"):Map{},outputIds);
        auto& tx=impl.Repository->BeginDerivedTransaction(impl.Document,"Publish editable toolpaths");
        bool committing=false;
        try
        {
            WriteDerived(tx,*impl.Repository,ResultId(impl.Document,job.Operation),impl.Document,job.Record);
            for(const auto& [id,data]:initialLabels)WriteDerived(tx,*impl.Repository,id,impl.Document,data);
            committing=true;Commit(*impl.Repository,tx);
        }
        catch(...){if(!committing)impl.Repository->CancelTransaction(tx);throw;}
        impl.PublishedTickets[job.Operation]=state.Ticket;
        const auto result=ResultId(impl.Document,job.Operation);
        if(changed(result)){if(!refresh())break;}
        else{graph.Records[job.Operation]=job.Record;snapshotChanges[result]=impl.Observer->Changes[result];validated.erase(job.Operation);++graphRevision;}
    }
    state.Published=true;state.Prepared.reset();
    return true;
}

IntentMap CEditableToolpathDocument::Execute(const IntentMap& request)
{
    return Execute(request,{});
}
IntentMap CEditableToolpathDocument::Execute(const IntentMap& request,const SEditableToolpathHostEdit& host)
{return ExecuteBatch({request},host).at(0);}
std::vector<IntentMap> CEditableToolpathDocument::ExecuteBatch(const std::vector<IntentMap>& requests,const SEditableToolpathHostEdit& host)
{
    auto& impl=*m_Impl;impl.Owner();Require(!impl.Editing,"TOOLPATH_REENTRANT_EDIT");Require(!requests.empty()&&requests.size()<=16384,"TOOLPATH_BATCH_SIZE_INVALID");
    ValidateHostSourceIdentities(requests,host);
    struct Busy final{bool& Value;explicit Busy(bool& value):Value(value){Value=true;}~Busy(){Value=false;}} busy(impl.Editing);
    SEditOverlay overlay;overlay.Repository=impl.Repository.get();overlay.Document=impl.Document;std::vector<Map> replies;
    const auto repositorySerial=impl.Observer->Serial;
    overlay.RepositoryRevision=[&](IntentId id){const auto found=impl.Observer->Changes.find(id);return found==impl.Observer->Changes.end()?std::uint64_t(0):found->second;};
    overlay.RequireUnchangedRepository=[&]{Require(impl.Observer->Serial==repositorySerial,"TOOLPATH_REPOSITORY_CHANGED_DURING_EDIT_VALIDATION");};
    {
        CEditOverlayScope scope(overlay);
        std::map<IntentId,Map> priorReplies;
        for(const auto& request:requests)
        {
            overlay.RequireUnchangedRepository();
            auto fingerprint=Hash(request);const auto command=ID(At(request,"commandId"));
            if(const auto identity=host.SourceIdentities.find(command);identity!=host.SourceIdentities.end())
                fingerprint=Hash(Map{{"request",request},{"hostSourceIdentity",Map{{"mode",static_cast<int>(identity->second.Mode)},{"pathEntityId",identity->second.PathEntityId},{"sourceOperationId",identity->second.SourceOperationId}}}});
            if(!host.RecordUndo)fingerprint=Hash(Map{{"requestFingerprint",fingerprint},{"recordUndo",false}});
            const auto receiptId=NamedID(Key(impl.Document)+"/receipt/"+Key(command));
            if(Text(request,"kind")=="ClonePlan")
            {
                const auto referencedId=[&](const std::string& field)
                {const auto value=At(request,field);if(!value.Is<Map>())return ID(value);const auto reference=value.To<Map>();const auto previous=priorReplies.find(ID(At(reference,"fromCommand")));Require(previous!=priorReplies.end(),"TOOLPATH_BATCH_REFERENCE_NOT_AVAILABLE");return ID(At(previous->second,Text(reference,"field","planId")));};
                Map saved;if(const auto found=overlay.Receipts.find(receiptId);found!=overlay.Receipts.end())saved=found->second;
                else if(const auto previous=Component<CEditableDerivedComponent>(*impl.Repository,receiptId))saved=previous->GetData();
                const auto input=saved.contains("cloneInput")?MapAt(saved,"cloneInput"):CapturePlanCloneInput(*impl.Repository,LoadGraph(*impl.Repository,impl.Document,referencedId("sourcePlanId")));
                const auto found=host.PlanClones.find(command);const SPlanCloneTransform transform=found==host.PlanClones.end()?SPlanCloneTransform{}:found->second;
                auto projection=PrepareToolpathPlanClone(input,command,referencedId("planId"),Text(request,"labelScope"),transform,*impl.Geometry);
                fingerprint=Hash(Map{{"requestFingerprint",fingerprint},{"cloneProjection",projection}});overlay.CloneInputs[command]=input;overlay.CloneProjections[command]=std::move(projection);
            }
            if(overlay.Receipts.contains(receiptId)||Component<CEditableDerivedComponent>(*impl.Repository,receiptId))
            {auto receipt=impl.StageCommand(request,fingerprint,host);priorReplies[command]=receipt;replies.push_back(std::move(receipt));continue;}
            const auto substitute=[&](auto&& self,const Variant& value)->Variant
            {
                if(value.Is<Array>()){Array array;for(const auto& item:value.To<Array>())array.push_back(self(self,item));return array;}
                if(!value.Is<Map>())return value;const auto map=value.To<Map>();
                if(map.contains("fromCommand")&&(map.size()==1||(map.size()==2&&map.contains("field"))))
                {
                    const auto previous=priorReplies.find(ID(At(map,"fromCommand")));Require(previous!=priorReplies.end(),"TOOLPATH_BATCH_REFERENCE_NOT_AVAILABLE");
                    const auto field=Text(map,"field",previous->second.contains("port")?"port":previous->second.contains("planId")?"planId":"operationId");
                    return At(previous->second,field);
                }
                Map rewritten;for(const auto& [key,item]:map)rewritten[key]=self(self,item);return rewritten;
            };
            auto resolved=substitute(substitute,request).To<Map>();
            auto expected=resolved.contains("expectedInputs")?MapAt(resolved,"expectedInputs"):Map{};
            auto expectedProducers=resolved.contains("expectedProducers")?MapAt(resolved,"expectedProducers"):Map{};
            const auto captureDeclared=[&](const Map& original,const Map& rewritten)->std::string
            {
                if(!original.contains("fromCommand"))return {};
                const auto port=PortFromData(rewritten);const auto producer=impl.Operation(port.OperationId);const auto record=Result(*impl.Repository,impl.Document,port.OperationId);
                const auto producerType=impl.Registry->Find(Text(producer,"typeId"));
                if(producerType&&Text(record,"state")=="Current"&&Text(record,"definitionFingerprint")==DefinitionHash(producer)
                    &&Text(record,"evaluationRevision")==producerType->EvaluationRevision)
                {const auto path=FindOutput(port.OperationId,port.PortId,record);expected[Key(port)]=path.Fingerprint;return {};}
                Require(DeclaresPort(producer,port),"TOOLPATH_BATCH_OUTPUT_NOT_DECLARED");return DefinitionHash(producer);
            };
            if(request.contains("inputs"))
            {const auto before=ArrayAt(request,"inputs");auto after=ArrayAt(resolved,"inputs");for(std::size_t i=0;i<before.size();++i)
             {auto fields=after[i].To<Map>();const auto pending=captureDeclared(MapAt(before[i].To<Map>(),"source"),MapAt(fields,"source"));if(!pending.empty()){fields["deferred"]=true;fields["producerFingerprint"]=pending;}after[i]=std::move(fields);}resolved["inputs"]=std::move(after);}
            if(request.contains("targets"))
            {const auto before=ArrayAt(request,"targets"),after=ArrayAt(resolved,"targets");for(std::size_t i=0;i<before.size();++i)
             {const auto pending=captureDeclared(before[i].To<Map>(),after[i].To<Map>());if(!pending.empty())
              {Require(Text(resolved,"kind")=="DeletePaths"||Text(resolved,"kind")=="SetParticipation"||Text(resolved,"kind")=="RestoreOutputs","TOOLPATH_TARGET_RESULT_NOT_READY");expectedProducers[Key(PortFromData(after[i].To<Map>()))]=pending;}}}
            if(!expected.empty())resolved["expectedInputs"]=std::move(expected);
            if(!expectedProducers.empty())resolved["expectedProducers"]=std::move(expectedProducers);
            if(request.contains("operationId")&&At(request,"operationId").Is<Map>()&&!resolved.contains("expectedOperationFingerprint"))resolved["expectedOperationFingerprint"]=DefinitionHash(impl.Operation(ID(At(resolved,"operationId"))));
            if(request.contains("planId")&&At(request,"planId").Is<Map>()&&Text(resolved,"kind")!="CreatePlan"&&!resolved.contains("expectedPlanFingerprint"))resolved["expectedPlanFingerprint"]=DefinitionHash(Definition<CEditablePlanComponent>(*impl.Repository,ID(At(resolved,"planId"))));
            auto receipt=impl.StageCommand(resolved,fingerprint,host);overlay.RequireUnchangedRepository();priorReplies[command]=receipt;replies.push_back(std::move(receipt));
        }
    }
    overlay.RequireUnchangedRepository();
    if(overlay.NewReceipts.empty())return replies;
    CommitToolpathHostEdit(*impl.Repository,impl.Document,host,overlay.NewReceipts,overlay.ReceiptIds,[&](ITransaction& tx)
    {
        for(const auto& [key,data]:overlay.Definitions)
        {const auto& [id,type]=key;if(impl.Repository->GetEntity(id))tx.ModifyComponent(id,type,{{"Definition",data}});else Create(tx,id,type,{{"Definition",data}});}
        for(const auto& [id,data]:overlay.Identities)WriteDerived(tx,*impl.Repository,id,impl.Document,data);
        for(const auto& [id,data]:overlay.SeedRecords)WriteDerived(tx,*impl.Repository,ResultId(impl.Document,id),impl.Document,data);
        for(const auto& [id,data]:overlay.Labels)
        {if(Component<CEditablePathLabelsComponent>(*impl.Repository,id))tx.ModifyComponent(id,CEditablePathLabelsComponent::S_ClassName,data);else tx.AttachComponent(id,CEditablePathLabelsComponent::S_ClassName,data);}
        for(const auto& [id,data]:overlay.Receipts)WriteDerived(tx,*impl.Repository,id,impl.Document,data);
    });
    return replies;
}
IntentMap CEditableToolpathDocument::Impl::StageCommand(const IntentMap& request,const std::string& requestHash,const SEditableToolpathHostEdit& host)
{
    auto& impl=*this;impl.Owner();Require(ActiveEditOverlay!=nullptr,"TOOLPATH_EDIT_OVERLAY_REQUIRED");auto doc=impl.DocumentDefinition();const auto command=ID(At(request,"commandId"));Require(!command.is_nil(),"TOOLPATH_COMMAND_ID_REQUIRED");
    const auto receiptId=NamedID(Key(impl.Document)+"/receipt/"+Key(command));
    if(const auto staged=ActiveEditOverlay->Receipts.find(receiptId);staged!=ActiveEditOverlay->Receipts.end())
    {Require(Text(staged->second,"requestFingerprint")==requestHash,"TOOLPATH_COMMAND_ID_REUSED");return MapAt(staged->second,"receipt");}
    if(const auto previous=Component<CEditableDerivedComponent>(*impl.Repository,receiptId))
    {const auto saved=previous->GetData();Require(Text(saved,"requestFingerprint")==requestHash,"TOOLPATH_COMMAND_ID_REUSED");return MapAt(saved,"receipt");}
    Require(!request.contains("requestEpoch")||ID(At(request,"requestEpoch"))==ID(At(doc,"requestEpoch")),"TOOLPATH_REQUEST_EPOCH_EXPIRED");
    if(!doc.contains("layers"))
    {doc["layers"]=InitialToolpathLayers(impl.Document);Map initial;for(const auto& source:ArrayAt(doc,"sources")){const auto definition=impl.Operation(ID(source));if(definition.contains("sourcePathId"))initial[Key(ID(At(definition,"sourcePathId")))]=ToolpathLayerDefaults(doc);}doc["initialPathLayers"]=std::move(initial);}
    if(Text(request,"kind")=="CreatePlan")
    {
        const auto id=ID(At(request,"planId"));Require(!id.is_nil()&&!impl.Repository->GetEntity(id)&&!HasStagedEntity(id),"TOOLPATH_PLAN_ID_COLLISION");
        auto plans=ArrayAt(doc,"plans");Require(!Contains(plans,id),"TOOLPATH_PLAN_ID_COLLISION");plans.emplace_back(id);doc["plans"]=plans;
        auto& overlay=*ActiveEditOverlay;overlay.Definitions[{impl.Document,CEditableDocumentComponent::S_ClassName}]=doc;
        overlay.Definitions[{id,CEditablePlanComponent::S_ClassName}]={{"documentId",impl.Document},{"name",Text(request,"name")},{"roots",Array{}},{"operations",Array{}},{"outputOperations",Array{}},{"exclusions",Map{}},{"groups",Map{}},{"executionConstraints",Map{}}};
        Map receipt{{"commandId",command},{"accepted",true},{"planId",id},{"taskState",std::string("Idle")},{"resultValidity",std::string("Current")}};
        overlay.Receipts[receiptId]={{"kind",std::string("commandReceipt")},{"requestFingerprint",requestHash},{"receipt",receipt}};overlay.NewReceipts.push_back(receipt);overlay.ReceiptIds.push_back(receiptId);return receipt;
    }
    const auto kindName=Text(request,"kind");
    bool identityOnly=kindName=="CreateLayer"||kindName=="UpdateLayer"||kindName=="DeleteLayer"||kindName=="SetDefaultLayers"
        ||((kindName=="AssignPathLayers"||kindName=="ResetPathLayerOverrides")&&request.contains("pathIds")),declaredInputs=false;
    if(kindName=="UpdateOperation"){
        const auto definition=impl.Operation(ID(At(request,"operationId")));const auto type=impl.Registry->Find(Text(definition,"typeId"));
        identityOnly=type&&!type->NormalizeParameters;
    }else if(kindName=="AddOperation"){
        const auto type=impl.Registry->Find(Text(request,"typeId"));const auto inputs=ArrayAt(request,"inputs");
        declaredInputs=type&&!type->NormalizeParameters&&!type->ValidateInput&&!inputs.empty()
            &&std::all_of(inputs.begin(),inputs.end(),[](const auto& item){return Flag(item.To<Map>(),"deferred");});
        identityOnly=declaredInputs;
    }
    const auto planId=ID(At(request,"planId"));auto graph=LoadGraph(*impl.Repository,impl.Document,planId,identityOnly);auto plan=graph.PlanDefinition;
    if(request.contains("expectedPlanFingerprint"))Require(Text(request,"expectedPlanFingerprint")==DefinitionHash(plan),"TOOLPATH_STALE_PLAN");
    auto kind=Text(request,"kind");Map reply{{"commandId",command},{"accepted",true},{"taskState",std::string("Queued")},{"resultValidity",std::string("Stale")}};
    std::map<IntentId,Map> writes;std::map<IntentId,Map> labels;std::map<IntentId,Map> identities;
    auto roots=ArrayAt(plan,"roots"),operations=ArrayAt(plan,"operations"),outputRoots=ArrayAt(plan,"outputOperations");auto exclusions=MapAt(plan,"exclusions");
    const auto expected=[&](IntentId operation,const Map& definition)
    {Require(request.contains("expectedOperationFingerprint"),"TOOLPATH_EXPECTED_OPERATION_REQUIRED");Require(Text(request,"expectedOperationFingerprint")==DefinitionHash(definition),"TOOLPATH_STALE_OPERATION");};
    const auto requireOperationUnlocked=[&](IntentId operation)
    {
        for(const auto& item:ArrayAt(doc,"plans"))
        {
            const auto candidate=ID(item)==planId?graph:LoadGraph(*impl.Repository,impl.Document,ID(item),identityOnly);
            if(!candidate.Definitions.contains(operation))continue;
            std::set<IntentId> affected{operation};bool changed=true;
            while(changed){changed=false;for(const auto& [id,definition]:candidate.Definitions)if(!affected.contains(id))for(const auto& input:Inputs(definition))
                if(affected.contains(input.Source.OperationId)){affected.insert(id);changed=true;break;}}
            for(auto id:affected)for(const auto& port:InspectionPorts(candidate,candidate.Records,id))
                Require(!PortGroupState(*impl.Repository,candidate,candidate.Records,port).second,"TOOLPATH_PATH_LOCKED");
        }
    };
    const auto requirePathUnlocked=[&](IntentId path)
    {
        Require(!Flag(PathLabels(*impl.Repository,path),"OwnLocked"),"TOOLPATH_PATH_LOCKED");
        for(const auto& item:ArrayAt(doc,"plans"))
        {const auto candidate=ID(item)==planId?graph:LoadGraph(*impl.Repository,impl.Document,ID(item));
         for(const auto& [id,definition]:candidate.Definitions)for(const auto& port:InspectionPorts(candidate,candidate.Records,id))
            if(InspectionPathId(candidate,candidate.Records,port)==path)Require(!PortGroupState(*impl.Repository,candidate,candidate.Records,port).second,"TOOLPATH_PATH_LOCKED");}
    };
    const auto selectedPorts=[&]()->std::vector<SPortRef>
    {
        std::vector<SPortRef> ports;for(const auto& item:ArrayAt(request,"targets"))ports.push_back(PortFromData(item.To<Map>()));Require(!ports.empty(),"TOOLPATH_TARGET_REQUIRED");
        const auto resolved=ResolveGraph(*impl.Repository,graph,*impl.Registry,*impl.Geometry,true);
        const auto fingerprints=request.contains("expectedInputs")?MapAt(request,"expectedInputs"):Map{};
        const auto producers=request.contains("expectedProducers")?MapAt(request,"expectedProducers"):Map{};
        std::set<SPortRef> unique;for(const auto& port:ports)
        {Require(unique.insert(port).second,"TOOLPATH_TARGET_DUPLICATE");bool found=false;for(const auto& path:resolved.Paths)if(path.Port==port)
         {Require(!path.Locked,"TOOLPATH_PATH_LOCKED");if(path.ResultState=="Pending")break;found=true;
          if(kind=="CopyPaths"||kind=="Detach")Require(path.ResultState=="Current"&&path.Participating,"TOOLPATH_COPY_REQUIRES_CURRENT_RESULT");
          Require(fingerprints.contains(Key(port))&&Text(fingerprints,Key(port))==path.Fingerprint,"TOOLPATH_STALE_INPUT");}
         // A declared pending output has no previous geometry to read. Check
         // its exact producer revision before the stale-result fallback.
         if(!found&&producers.contains(Key(port))&&(kind=="DeletePaths"||kind=="SetParticipation"||kind=="RestoreOutputs"))
         {const auto producer=graph.Definitions.find(port.OperationId);Require(producer!=graph.Definitions.end()&&IsActive(graph,port.OperationId)&&!graph.Replacements.contains(port)&&DeclaresPort(producer->second,port)
             &&Text(producers,Key(port))==DefinitionHash(producer->second),"TOOLPATH_PENDING_TARGET_DECLARATION_REQUIRED");Require(!PortGroupState(*impl.Repository,graph,graph.Records,port).second,"TOOLPATH_PATH_LOCKED");found=true;}
         if(!found&&Flag(request,"allowStaleTargets")&&kind!="CopyPaths"&&kind!="Detach")
         {Require(graph.Definitions.contains(port.OperationId)&&!graph.Replacements.contains(port),"TOOLPATH_TARGET_NOT_TERMINAL");Require(!PortGroupState(*impl.Repository,graph,graph.Records,port).second,"TOOLPATH_PATH_LOCKED");const auto previous=FindOutput(port.OperationId,port.PortId,graph.Records.at(port.OperationId));Require(fingerprints.contains(Key(port))&&Text(fingerprints,Key(port))==previous.Fingerprint,"TOOLPATH_STALE_INPUT");found=true;}
         Require(found,"TOOLPATH_TARGET_NOT_TERMINAL");}return ports;
    };
    const auto addSource=[&](const SToolpathValue& value,IntentId path,const std::string& name,bool shared,IntentId sourceOperation=IntentId{})->std::pair<IntentId,SPortRef>
    {
        impl.Geometry->Validate(value.Geometry);const auto operation=sourceOperation.is_nil()?iCAX::Data::GenerateNewUUID():sourceOperation;if(path.is_nil())path=iCAX::Data::GenerateNewUUID();
        Require(operation!=path&&path!=ResultId(impl.Document,operation)&&!impl.Repository->GetEntity(operation)&&!HasStagedEntity(operation),"TOOLPATH_SOURCE_OPERATION_ID_COLLISION");
        const auto type=impl.Registry->Find("icax.source");Require(type!=nullptr,"TOOLPATH_SOURCE_TYPE_UNSUPPORTED");
        Map definition{{"documentId",impl.Document},{"planId",shared?IntentId{}:planId},{"typeId",std::string("icax.source")},{"schemaVersion",type->SchemaVersion},
            {"algorithmVersion",type->AlgorithmVersion},{"inputs",Array{}},{"parameters",Map{{"value",ToData(value)}}},{"context",Map{}},{"enabled",true},{"sourcePathId",path},
            {"sourceBinding",Map{{"mode",std::string("EmbeddedSnapshot")}}}};
        definition["outputDeclarations"]=DeclareOutputs(operation,*type);
        writes[operation]=definition;graph.Definitions[operation]=definition;graph.Selected.insert(operation);
        if(!shared)operations.emplace_back(operation);else{auto sources=ArrayAt(doc,"sources");sources.emplace_back(operation);doc["sources"]=sources;}
        const SPortRef port{operation,NamedID(Key(operation)+"/port/main")};roots.emplace_back(ToData(port));
        if(!Component<CEditableDerivedComponent>(*impl.Repository,path)&&!ActiveEditOverlay->Identities.contains(path))
        {identities[path]={{"kind",std::string("pathIdentity")},{"originOperation",operation}};labels[path]={{"Name",name},{"UserNumber",std::string()}};}
        auto initial=doc.contains("initialPathLayers")?MapAt(doc,"initialPathLayers"):Map{};if(!initial.contains(Key(path)))initial[Key(path)]=ToolpathLayerDefaults(doc);doc["initialPathLayers"]=std::move(initial);
        return {operation,port};
    };
    std::vector<IntentId> layerTargets;std::map<IntentId,Map> otherLayerPlans;
    if(kind=="AssignPathLayers"||kind=="ResetPathLayerOverrides")
    {
        if(request.contains("pathIds"))
        {Require(request.contains("expectedPlanFingerprint"),"TOOLPATH_EXPECTED_PLAN_REQUIRED");std::set<IntentId> unique;
         for(const auto& item:ArrayAt(request,"pathIds")){const auto path=ID(item);Require(unique.insert(path).second,"TOOLPATH_TARGET_DUPLICATE");bool found=false;
          for(const auto& [operation,_]:graph.Definitions)for(const auto& port:InspectionPorts(graph,graph.Records,operation))if(InspectionPathId(graph,graph.Records,port)==path){Require(!PortGroupState(*impl.Repository,graph,graph.Records,port).second,"TOOLPATH_PATH_LOCKED");found=true;}
          Require(found,"TOOLPATH_PATH_OUTSIDE_PLAN");layerTargets.push_back(path);}}
        else for(const auto& port:selectedPorts())layerTargets.push_back(InspectionPathId(graph,graph.Records,port));
    }
    if(kind=="SetOperationLayers"){const auto operation=ID(At(request,"operationId"));Require(graph.Definitions.contains(operation),"TOOLPATH_LAYER_OPERATION_OUTSIDE_PLAN");requireOperationUnlocked(operation);}
    if(kind=="DeleteLayer")for(const auto& item:ArrayAt(doc,"plans"))if(ID(item)!=planId)otherLayerPlans[ID(item)]=Definition<CEditablePlanComponent>(*impl.Repository,ID(item));
    const bool layerEdit=EditToolpathLayers(request,doc,plan,layerTargets,otherLayerPlans);
    if(layerEdit)
    {for(const auto& [id,definition]:otherLayerPlans)ActiveEditOverlay->Definitions[{id,CEditablePlanComponent::S_ClassName}]=definition;reply["taskState"]=std::string("Idle");reply["resultValidity"]=std::string("Current");if(request.contains("layerId"))reply["layerId"]=At(request,"layerId");}
    else if(kind=="ClonePlan")
    {
        Require(ID(At(request,"sourcePlanId"))!=planId,"TOOLPATH_CLONE_SOURCE_EQUALS_DESTINATION");Require(roots.empty()&&operations.empty()&&outputRoots.empty()&&MapAt(plan,"groups").empty()&&exclusions.empty(),"TOOLPATH_CLONE_DESTINATION_NOT_EMPTY");
        const auto input=ActiveEditOverlay->CloneInputs.at(command);Require(Text(request,"expectedSourcePlanFingerprint")==Hash(input),"TOOLPATH_STALE_CLONE_SOURCE");
        const auto projection=ActiveEditOverlay->CloneProjections.at(command);auto clonedPlan=MapAt(projection,"plan");clonedPlan["name"]=Text(plan,"name");plan=std::move(clonedPlan);
        roots=ArrayAt(plan,"roots");operations=ArrayAt(plan,"operations");outputRoots=ArrayAt(plan,"outputOperations");exclusions=MapAt(plan,"exclusions");
        graph.Definitions.clear();graph.Records.clear();graph.Selected.clear();
        for(const auto& [key,item]:MapAt(projection,"definitions"))
        {
            const auto id=ID(Variant(key));Require(!impl.Repository->GetEntity(id)&&!HasStagedEntity(id),"TOOLPATH_CLONE_OPERATION_ID_COLLISION");const auto definition=item.To<Map>();
            const auto type=impl.Registry->Find(Text(definition,"typeId"));Require(type!=nullptr,"TOOLPATH_OPERATION_UNSUPPORTED");ValidateType(definition,*type);writes[id]=definition;graph.Definitions[id]=definition;graph.Records[id]=Map{};
            if(Text(definition,"typeId")=="icax.source"&&ID(At(definition,"planId")).is_nil()){auto sources=ArrayAt(doc,"sources");sources.emplace_back(id);doc["sources"]=std::move(sources);}
        }
        for(const auto& [key,item]:MapAt(projection,"identities")){const auto id=ID(Variant(key));Require(!impl.Repository->GetEntity(id)&&!HasStagedEntity(id),"TOOLPATH_CLONE_PATH_ID_COLLISION");identities[id]=item.To<Map>();}
        for(const auto& [key,item]:MapAt(projection,"labels"))labels[ID(Variant(key))]=item.To<Map>();
        if(projection.contains("initialPathLayers")){auto initial=MapAt(doc,"initialPathLayers");for(const auto& [key,value]:MapAt(projection,"initialPathLayers"))initial[key]=value;doc["initialPathLayers"]=std::move(initial);}
        for(const auto& [key,item]:MapAt(projection,"seeds")){const auto id=ID(Variant(key));ActiveEditOverlay->SeedRecords[id]=item.To<Map>();ActiveEditOverlay->Records[id]=item.To<Map>();graph.Records[id]=item.To<Map>();++ActiveEditOverlay->RecordRevisions[id];}
        for(const auto& item:roots)graph.Selected.insert(PortFromData(item.To<Map>()).OperationId);for(auto id:Ids(operations))graph.Selected.insert(id);for(auto id:Ids(outputRoots))graph.Selected.insert(id);
        reply["operationIds"]=At(projection,"operationIds");reply["pathIds"]=At(projection,"pathIds");reply["portRemap"]=At(projection,"portRemap");reply["planId"]=planId;
    }
    else if(kind=="CreateSource")
    {
        IntentId requestedPath,requestedOperation;
        if(const auto identity=host.SourceIdentities.find(command);identity!=host.SourceIdentities.end())requestedOperation=identity->second.SourceOperationId;
        if(const auto identity=host.SourceIdentities.find(command);identity!=host.SourceIdentities.end()&&identity->second.Mode!=ESourcePathIdentityMode::AllocateNew)
        {
            requestedPath=identity->second.PathEntityId;const auto exists=impl.Repository->GetEntity(requestedPath)!=nullptr;
            Require(!HasStagedEntity(requestedPath)&&!identities.contains(requestedPath),"TOOLPATH_PATH_ID_COLLISION");
            if(identity->second.Mode==ESourcePathIdentityMode::CreateWithId)Require(!exists,"TOOLPATH_PATH_ID_COLLISION");
            else Require(exists&&!Component<CEditableDerivedComponent>(*impl.Repository,requestedPath)&&!Component<CEditablePathLabelsComponent>(*impl.Repository,requestedPath),"TOOLPATH_PATH_ALREADY_OWNED_OR_MISSING");
            for(const auto& entity:impl.Repository->FilterEntities([](const auto&){return true;}))
                if(const auto source=Component<CEditableOperationComponent>(*impl.Repository,entity->GetID());source)
                {const auto data=source->GetDefinition();Require(!data.contains("sourcePathId")||ID(At(data,"sourcePathId"))!=requestedPath,"TOOLPATH_PATH_ALREADY_OWNED_OR_MISSING");}
        }
        const auto value=ToolpathFromData(MapAt(request,"value"));const auto [operation,port]=addSource(value,requestedPath,Text(request,"name"),true,requestedOperation);
        labels[ID(At(writes.at(operation),"sourcePathId"))]["UserNumber"]=Text(request,"userNumber");
        if(request.contains("labelScope")){const auto scope=Text(request,"labelScope");ValidateToolpathLabelScope(scope);identities.at(ID(At(writes.at(operation),"sourcePathId")))["labelScope"]=scope;}
        if(request.contains("sourceBinding")){auto binding=MapAt(request,"sourceBinding");const auto mode=Text(binding,"mode");Require(mode=="EmbeddedSnapshot"||mode=="LiveLinked","TOOLPATH_SOURCE_MODE_INVALID");writes[operation]["sourceBinding"]=binding;graph.Definitions[operation]=writes[operation];}
        reply["operationId"]=operation;reply["port"]=ToData(port);reply["pathId"]=At(writes.at(operation),"sourcePathId");
    }
    else if(kind=="SetPlanRoots")
    {
        Require(request.contains("expectedPlanFingerprint"),"TOOLPATH_EXPECTED_PLAN_REQUIRED");
        Array selected;std::set<SPortRef> unique;
        for(const auto& item:ArrayAt(request,"roots"))
        {
            const auto port=PortFromData(item.To<Map>());const auto source=impl.Operation(port.OperationId);
            Require(ID(At(source,"planId")).is_nil()&&Text(source,"typeId")=="icax.source"
                &&Contains(ArrayAt(doc,"sources"),port.OperationId)
                &&port.PortId==NamedID(Key(port.OperationId)+"/port/main"),"TOOLPATH_PLAN_ROOT_NOT_SHARED_SOURCE");
            Require(unique.insert(port).second,"TOOLPATH_PLAN_ROOT_DUPLICATE");selected.emplace_back(ToData(port));
            graph.Definitions[port.OperationId]=source;graph.Records[port.OperationId]=Result(*impl.Repository,impl.Document,port.OperationId);
        }
        for(const auto& item:roots)if(std::find(selected.begin(),selected.end(),item)==selected.end())requireOperationUnlocked(PortFromData(item.To<Map>()).OperationId);
        roots=std::move(selected);graph.Selected.clear();
        for(const auto& root:roots)graph.Selected.insert(PortFromData(root.To<Map>()).OperationId);
        for(auto id:Ids(operations))graph.Selected.insert(id);for(auto id:Ids(outputRoots))graph.Selected.insert(id);
    }
    else if(kind=="AddOperation")
    {
        const auto typeId=Text(request,"typeId");const auto type=impl.Registry->Find(typeId);Require(type!=nullptr,"TOOLPATH_OPERATION_UNSUPPORTED");
        const auto operation=request.contains("operationId")?ID(At(request,"operationId")):iCAX::Data::GenerateNewUUID();Require(!operation.is_nil()&&!impl.Repository->GetEntity(operation)&&!HasStagedEntity(operation),"TOOLPATH_OPERATION_ID_COLLISION");const auto parameters=MapAt(request,"parameters");ValidateParameterSchema(type->ParameterSchema,parameters);if(type->ValidateParameters)type->ValidateParameters(parameters);
        Map definition{{"documentId",impl.Document},{"planId",planId},{"typeId",typeId},{"schemaVersion",type->SchemaVersion},{"algorithmVersion",type->AlgorithmVersion},
            {"inputs",ArrayAt(request,"inputs")},{"parameters",parameters},{"context",request.contains("context")?MapAt(request,"context"):Map{}},{"enabled",true}};
        if(request.contains("outputLabelPolicy")){const auto policy=MapAt(request,"outputLabelPolicy");ValidateToolpathOutputLabelPolicy(policy);definition["outputLabelPolicy"]=policy;}
        if(!type->DeclaredOutputRoles.empty())definition["outputDeclarations"]=DeclareOutputs(operation,*type);
        if(!type->DeclaredOutputIdentityInputs.empty())definition["outputIdentityInputs"]=DeclareOutputIdentities(*type);
        IntentId insertionConsumer;std::string insertionSlot;Map insertionDefinition;
        if(request.contains("insertBefore"))
        {
            const auto insertion=MapAt(request,"insertBefore");insertionConsumer=ID(At(insertion,"operationId"));insertionSlot=Text(insertion,"inputSlot");
            Require(type->DeclaredOutputRoles==std::set<std::string>{"main"}&&Inputs(definition).size()==1&&Inputs(definition).front().Replace,"TOOLPATH_INSERT_REQUIRES_FIXED_SINGLE_MAIN");
            Require(graph.Definitions.contains(insertionConsumer)&&insertionConsumer!=operation,"TOOLPATH_INSERT_CONSUMER_OUTSIDE_PLAN");insertionDefinition=graph.Definitions.at(insertionConsumer);
            Require(ID(At(insertionDefinition,"planId"))==planId&&Text(insertionDefinition,"typeId")!="icax.source","TOOLPATH_INSERT_CONSUMER_INVALID");
            Require(Text(insertion,"expectedOperationFingerprint")==DefinitionHash(insertionDefinition),"TOOLPATH_STALE_OPERATION");requireOperationUnlocked(insertionConsumer);
            bool matched=false;for(const auto& input:Inputs(insertionDefinition))if(input.Slot==insertionSlot)
            {Require(input.Replace&&input.Source==Inputs(definition).front().Source,"TOOLPATH_INSERT_SOURCE_OR_SLOT_MISMATCH");matched=true;}
            Require(matched,"TOOLPATH_INSERT_INPUT_SLOT_MISSING");
        }
        ValidateType(definition,*type);const auto valid=declaredInputs?graph.Records:ValidRecords(graph,*impl.Registry,*impl.Geometry);std::vector<SFrozenPathInput> inputs;
        const auto fingerprints=request.contains("expectedInputs")?MapAt(request,"expectedInputs"):Map{};const auto bindings=Inputs(definition);const auto inputData=ArrayAt(definition,"inputs");
        bool replace=false,deferred=false;for(std::size_t index=0;index<bindings.size();++index)
        {
            const auto& binding=bindings[index];replace|=binding.Replace;
            if(binding.Replace){const auto replacement=graph.Replacements.find(binding.Source);Require((replacement==graph.Replacements.end()||replacement->second==insertionConsumer)&&!exclusions.contains(Key(binding.Source)),"TOOLPATH_REPLACEMENT_CONFLICT");Require(!PortGroupState(*impl.Repository,graph,valid,binding.Source).second,"TOOLPATH_PATH_LOCKED");}
            const auto record=valid.find(binding.Source.OperationId);
            if(!declaredInputs&&record!=valid.end()&&Text(record->second,"state")=="Current")
            {
                auto output=FindOutput(binding.Source.OperationId,binding.Source.PortId,record->second);
                Require(fingerprints.contains(Key(binding.Source))&&Text(fingerprints,Key(binding.Source))==output.Fingerprint,"TOOLPATH_STALE_INPUT");
                inputs.push_back({binding,output.PathId,std::move(output.Value),output.Fingerprint});
                for(const auto& range:binding.Ranges){(void)Locate(range.Start,inputs.back());(void)Locate(range.End,inputs.back());}
            }
            else
            {
                const auto fields=inputData[index].To<Map>();const auto producer=graph.Definitions.find(binding.Source.OperationId);
                Require(Flag(fields,"deferred")&&producer!=graph.Definitions.end()&&IsActive(graph,binding.Source.OperationId)&&DeclaresPort(producer->second,binding.Source)
                    &&Text(fields,"producerFingerprint")==DefinitionHash(producer->second),"TOOLPATH_PENDING_INPUT_DECLARATION_REQUIRED");
                Require(binding.Ranges.empty()&&!type->NormalizeParameters,"TOOLPATH_DEFERRED_INPUT_REQUIRES_RESOLVED_SELECTION");deferred=true;
            }
        }
        if(!deferred&&type->NormalizeParameters)definition["parameters"]=type->NormalizeParameters(inputs,parameters);
        if(!deferred&&type->ValidateInput)
        {const auto frozen=FreezeOperationContext(type.get(),MapAt(definition,"context"));Require(frozen.Error.empty(),frozen.Error);SOperationEvaluation validation{operation,inputs,MapAt(definition,"parameters"),MapAt(definition,"context"),{}};validation.RuntimeContext=frozen.Value;type->ValidateInput(validation);}
        writes[operation]=definition;graph.Definitions[operation]=definition;graph.Selected.insert(operation);operations.emplace_back(operation);
        if(!insertionConsumer.is_nil())
        {
            auto data=ArrayAt(insertionDefinition,"inputs");for(auto& item:data){auto input=item.To<Map>();if(Text(input,"slot")==insertionSlot)
                {input["source"]=ToData(SPortRef{operation,NamedID(Key(operation)+"/port/main")});input["deferred"]=true;input["producerFingerprint"]=DefinitionHash(definition);item=std::move(input);}}
            insertionDefinition["inputs"]=std::move(data);writes[insertionConsumer]=insertionDefinition;graph.Definitions[insertionConsumer]=insertionDefinition;reply["rewiredOperationId"]=insertionConsumer;
        }
        if(!replace&&Flag(request,"outputRoot"))outputRoots.emplace_back(operation);
        reply["operationId"]=operation;
        if(definition.contains("outputDeclarations"))
        {const auto ports=MapAt(definition,"outputDeclarations");reply["ports"]=ports;if(ports.size()==1)reply["port"]=ports.begin()->second;}
        reply["definitionFingerprint"]=DefinitionHash(definition);
    }
    else if(kind=="UpdateOperation"||kind=="UpdateSource"||kind=="Rebind"||kind=="RemoveOperation"||kind=="SetOperationEnabled"||kind=="DeleteSource")
    {
        const auto operation=ID(At(request,"operationId"));auto definition=impl.Operation(operation);expected(operation,definition);
        Require(ID(At(definition,"planId")).is_nil()||ID(At(definition,"planId"))==planId,"TOOLPATH_CROSS_PLAN_REFERENCE");requireOperationUnlocked(operation);
        if(kind=="UpdateSource")
        {
            Require(Text(definition,"typeId")=="icax.source","TOOLPATH_NOT_A_SOURCE");const auto value=ToolpathFromData(MapAt(request,"value"));impl.Geometry->Validate(value.Geometry);
            const auto previous=ToolpathFromData(MapAt(MapAt(definition,"parameters"),"value"));
            for(const auto& segment:value.Geometry.Segments)for(const auto& old:previous.Geometry.Segments)if(segment.Id==old.Id)
                Require(segment.Curve.index()==old.Curve.index()&&segment.Parameterization==old.Parameterization&&GeometryCurveFamily(segment)==GeometryCurveFamily(old),"TOOLPATH_CHANGED_PARAMETERIZATION_REQUIRES_NEW_SEGMENT_ID");
            definition["parameters"]=Map{{"value",ToData(value)}};
            if(request.contains("sourceBinding"))definition["sourceBinding"]=MapAt(request,"sourceBinding");
        }
        else if(kind=="UpdateOperation")
        {
            Require(Text(definition,"typeId")!="icax.source","TOOLPATH_SOURCE_EDIT_REQUIRES_UPDATE_SOURCE");
            if(request.contains("parameters"))definition["parameters"]=MapAt(request,"parameters");
            if(request.contains("context"))definition["context"]=MapAt(request,"context");
            if(request.contains("outputLabelPolicy")){const auto policy=MapAt(request,"outputLabelPolicy");ValidateToolpathOutputLabelPolicy(policy);definition["outputLabelPolicy"]=policy;}
            const auto type=impl.Registry->Find(Text(definition,"typeId"));Require(type!=nullptr,"TOOLPATH_OPERATION_UNSUPPORTED");ValidateType(definition,*type);
        }
        else if(kind=="Rebind")
        {
            Require(Text(definition,"typeId")!="icax.source","TOOLPATH_SOURCE_EDIT_REQUIRES_UPDATE_SOURCE");
            definition["inputs"]=ArrayAt(request,"inputs");if(request.contains("parameters"))definition["parameters"]=MapAt(request,"parameters");
            const auto type=impl.Registry->Find(Text(definition,"typeId"));Require(type!=nullptr,"TOOLPATH_OPERATION_UNSUPPORTED");ValidateType(definition,*type);
            const auto inputs=FrozenInputs(definition,ValidRecords(graph,*impl.Registry,*impl.Geometry));
            const auto fingerprints=request.contains("expectedInputs")?MapAt(request,"expectedInputs"):Map{};
            for(const auto& input:inputs)
            {
                Require(fingerprints.contains(Key(input.Binding.Source))&&Text(fingerprints,Key(input.Binding.Source))==input.Fingerprint,"TOOLPATH_STALE_INPUT");
                const auto replacement=graph.Replacements.find(input.Binding.Source);
                if(input.Binding.Replace)Require((replacement==graph.Replacements.end()||replacement->second==operation)&&!exclusions.contains(Key(input.Binding.Source)),"TOOLPATH_REPLACEMENT_CONFLICT");
                if(input.Binding.Replace)Require(!PortGroupState(*impl.Repository,graph,graph.Records,input.Binding.Source).second,"TOOLPATH_PATH_LOCKED");
                for(const auto& range:input.Binding.Ranges){(void)Locate(range.Start,input);(void)Locate(range.End,input);}
            }
            if(type->NormalizeParameters)definition["parameters"]=type->NormalizeParameters(inputs,MapAt(definition,"parameters"));
            if(type->ValidateInput)
            {const auto frozen=FreezeOperationContext(type.get(),MapAt(definition,"context"));Require(frozen.Error.empty(),frozen.Error);SOperationEvaluation validation{operation,inputs,MapAt(definition,"parameters"),MapAt(definition,"context"),{}};validation.RuntimeContext=frozen.Value;type->ValidateInput(validation);}
        }
        else
        {
            const auto enabled=kind=="RemoveOperation"||kind=="DeleteSource"?false:Flag(request,"enabled");
            if(kind=="DeleteSource")Require(Text(definition,"typeId")=="icax.source","TOOLPATH_NOT_A_SOURCE");
            bool hasDependents=false;
            for(const auto& entity:impl.Repository->FilterEntities([](const auto&){return true;}))
            {
                const auto component=std::dynamic_pointer_cast<CEditableOperationComponent>(entity->GetComponent(CEditableOperationComponent::S_ClassName));
                if(!component||entity->GetID()==operation)continue;const auto d=component->GetDefinition();
                if(ID(At(d,"documentId"))!=impl.Document||!Flag(d,"enabled",true))continue;
                for(const auto& input:Inputs(d))hasDependents|=input.Source.OperationId==operation;
            }
            for(const auto& [key,d]:ActiveEditOverlay->Definitions)
                if(key.second==CEditableOperationComponent::S_ClassName&&key.first!=operation&&ID(At(d,"documentId"))==impl.Document&&Flag(d,"enabled",true))
                    for(const auto& input:Inputs(d))hasDependents|=input.Source.OperationId==operation;
            if(!enabled&&hasDependents)Require(Text(request,"dependencyPolicy")=="KeepInvalid","TOOLPATH_DEPENDENT_POLICY_REQUIRED");
            definition["enabled"]=enabled;
            if(kind=="DeleteSource"&&!hasDependents)std::erase_if(roots,[&](const auto& root){return PortFromData(root.template To<Map>()).OperationId==operation;});
        }
        if(kind=="UpdateOperation"&&Flag(definition,"enabled",true))
        {const auto type=impl.Registry->Find(Text(definition,"typeId"));if(type&&type->NormalizeParameters)
         {const auto inputs=FrozenInputs(definition,ValidRecords(graph,*impl.Registry,*impl.Geometry));
          definition["parameters"]=type->NormalizeParameters(inputs,MapAt(definition,"parameters"));}}
        writes[operation]=definition;graph.Definitions[operation]=definition;reply["operationId"]=operation;
    }
    else if(kind=="DeletePaths"||kind=="SetParticipation")
    {
        const auto ports=selectedPorts();for(const auto& port:ports)
        {
            const auto enabled=kind=="SetParticipation"&&Flag(request,"enabled");
            if(enabled){Require(exclusions.contains(Key(port)),"TOOLPATH_EXCLUSION_MISSING");exclusions.erase(Key(port));}else exclusions[Key(port)]=Map{{"port",ToData(port)},{"reason",kind=="DeletePaths"?std::string("Deleted"):std::string("Disabled")},{"commandId",command}};
        }
    }
    else if(kind=="RestoreOutputs")
    {for(const auto& port:selectedPorts()){Require(exclusions.contains(Key(port)),"TOOLPATH_EXCLUSION_MISSING");exclusions.erase(Key(port));}}
    else if(kind=="SetPathLocked")
    {
        std::set<IntentId> unique;for(const auto& item:ArrayAt(request,"pathIds"))
        {
            const auto path=ID(item);Require(unique.insert(path).second,"TOOLPATH_TARGET_DUPLICATE");
            const auto identity=Component<CEditableDerivedComponent>(*impl.Repository,path);
            Require((identity&&identity->GetOwner()==impl.Document&&Text(identity->GetData(),"kind")=="pathIdentity")||ActiveEditOverlay->Identities.contains(path),"TOOLPATH_PATH_NOT_IN_DOCUMENT");
            auto current=PathLabels(*impl.Repository,path);current["OwnLocked"]=Flag(request,"locked");labels[path]=std::move(current);
        }
        Require(!unique.empty(),"TOOLPATH_TARGET_REQUIRED");reply["taskState"]=std::string("Idle");
    }
    else if(kind=="SetLabels")
    {
        const auto path=ID(At(request,"pathId"));const auto identity=Component<CEditableDerivedComponent>(*impl.Repository,path);Require((identity&&identity->GetOwner()==impl.Document)||ActiveEditOverlay->Identities.contains(path),"TOOLPATH_PATH_NOT_IN_DOCUMENT");
        requirePathUnlocked(path);auto currentLabels=PathLabels(*impl.Repository,path);
        if(const auto previous=Component<CEditablePathLabelsComponent>(*impl.Repository,path))
        {Require(request.contains("expectedLabelsVersion")&&UnsignedValue(At(request,"expectedLabelsVersion"))==previous->Version(),"TOOLPATH_STALE_LABELS");}
        currentLabels["Name"]=Text(request,"name",Text(currentLabels,"Name"));currentLabels["UserNumber"]=Text(request,"userNumber",Text(currentLabels,"UserNumber"));labels[path]=std::move(currentLabels);
        reply["taskState"]=std::string("Idle");reply["resultValidity"]=std::string("Current");
    }
    else if(kind=="SetExecutionConstraints")
    {
        auto constraints=MapAt(request,"constraints");std::optional<SEffectiveToolpaths> current;
        const auto capture=[&](const Variant& value)->Variant
        {if(value.Is<Map>()){const auto port=PortFromData(value.To<Map>());Require(graph.Definitions.contains(port.OperationId),"TOOLPATH_CONSTRAINT_SOURCE_OUTSIDE_PLAN");return ToData(port);}
         if(!current)current=ResolveGraph(*impl.Repository,graph,*impl.Registry,*impl.Geometry);
         const auto id=ID(value);for(const auto& path:current->Paths)if(path.PathId==id)return ToData(path.Port);throw std::invalid_argument("TOOLPATH_CONSTRAINT_PATH_NOT_EFFECTIVE");};
        if(constraints.contains("blocks")){Array blocks;for(const auto& item:ArrayAt(constraints,"blocks")){auto block=item.To<Map>();Array members;for(const auto& member:ArrayAt(block,"members"))members.push_back(capture(member));block["members"]=members;blocks.emplace_back(std::move(block));}constraints["blocks"]=blocks;}
        if(constraints.contains("precedence")){Array edges;for(const auto& item:ArrayAt(constraints,"precedence")){auto edge=item.To<Map>();edge["before"]=capture(At(edge,"before"));edge["after"]=capture(At(edge,"after"));edges.emplace_back(std::move(edge));}constraints["precedence"]=edges;}
        ValidateConstraints(constraints);plan["executionConstraints"]=constraints;
    }
    else if(kind=="Group"||kind=="Ungroup")
    {
        auto groups=MapAt(plan,"groups");const auto id=Text(request,"groupId");Require(!id.empty(),"TOOLPATH_GROUP_ID_REQUIRED");
        if(kind=="Ungroup")
        {Require(!GroupState(plan,id).second,"TOOLPATH_GROUP_LOCKED");const auto parent=groups.contains(id)?Text(groups.at(id).To<Map>(),"parentGroupId"):std::string();
         if(groups.contains(id))for(const auto& member:ArrayAt(groups.at(id).To<Map>(),"members"))requirePathUnlocked(ID(member));
         groups.erase(id);for(auto& [child,value]:groups){auto group=value.To<Map>();if(Text(group,"parentGroupId")==id){Require(!Flag(group,"locked"),"TOOLPATH_GROUP_LOCKED");group["parentGroupId"]=parent;value=group;}}}
        else
        {auto group=groups.contains(id)?groups.at(id).To<Map>():Map{{"members",Array{}},{"name",id},{"parentGroupId",std::string()}};
         const auto members=request.contains("members")?ArrayAt(request,"members"):ArrayAt(group,"members");std::set<IntentId> unique;for(const auto& member:members){const auto path=ID(member);const auto identity=Component<CEditableDerivedComponent>(*impl.Repository,path);Require(unique.insert(path).second&&((identity&&identity->GetOwner()==impl.Document)||ActiveEditOverlay->Identities.contains(path)),"TOOLPATH_GROUP_MEMBER_INVALID");}
         for(const auto& [key,value]:groups)if(key!=id)for(const auto& member:ArrayAt(value.To<Map>(),"members"))Require(!unique.contains(ID(member)),"TOOLPATH_GROUP_OWNERSHIP_CONFLICT");
         auto proposed=group;proposed["members"]=members;proposed["name"]=Text(request,"name",Text(group,"name"));proposed["parentGroupId"]=Text(request,"parentGroupId",Text(group,"parentGroupId"));proposed["enabled"]=Flag(request,"enabled",Flag(group,"enabled",true));proposed["locked"]=Flag(request,"locked",Flag(group,"locked"));
         Require(!GroupState(plan,Text(group,"parentGroupId")).second&&!GroupState(plan,Text(proposed,"parentGroupId")).second,"TOOLPATH_GROUP_LOCKED");
         auto before=group,after=proposed;before.erase("locked");after.erase("locked");if(Flag(group,"locked"))Require(before==after,"TOOLPATH_GROUP_LOCKED");
         if(ArrayAt(group,"members")!=members||Text(group,"parentGroupId")!=Text(proposed,"parentGroupId")||Flag(group,"enabled",true)!=Flag(proposed,"enabled",true))
         {for(const auto& member:ArrayAt(group,"members"))requirePathUnlocked(ID(member));for(const auto& member:members)requirePathUnlocked(ID(member));}
         groups[id]=std::move(proposed);}
        for(const auto& [groupId,value]:groups){std::set<std::string> visited;auto cursor=groupId;while(!cursor.empty()){Require(groups.contains(cursor)&&visited.insert(cursor).second,"TOOLPATH_GROUP_PARENT_MISSING_OR_CYCLE");cursor=Text(groups.at(cursor).To<Map>(),"parentGroupId");}}
        plan["groups"]=groups;
    }
    else if(kind=="CopyPaths"||kind=="Detach")
    {
        const auto ports=selectedPorts();Require(Text(request,"mode","Snapshot")=="Snapshot","TOOLPATH_LINKED_COPY_USE_REGISTERED_COPY_OPERATION");Array created;
        for(const auto& port:ports)
        {
            const auto previous=FindOutput(port.OperationId,port.PortId,graph.Records.at(port.OperationId));
            auto frozen=previous.Value;frozen.Geometry=Slice({{"snapshot",port,false,{}},previous.PathId,previous.Value,previous.Fingerprint},0,static_cast<double>(frozen.Geometry.Segments.size()),"snapshot");frozen.Geometry.Closed=previous.Value.Geometry.Closed;
            const auto [operation,source]=addSource(frozen,kind=="Detach"?previous.PathId:IntentId{},previous.Name,false);created.emplace_back(ToData(source));
            const auto assignment=ResolveToolpathLayer(doc,graph.PlanDefinition,graph.Definitions,graph.Records,port,[&](SPortRef address){return InspectionPathId(graph,graph.Records,address);});
            auto initial=MapAt(doc,"initialPathLayers");initial[Key(ID(At(writes.at(operation),"sourcePathId")))]=Map{{"visibleLayerId",At(assignment,"visibleLayerId")},{"processingLayerId",At(assignment,"processingLayerId")}};doc["initialPathLayers"]=std::move(initial);
            if(kind=="Detach")
            {
                requireOperationUnlocked(port.OperationId);
                exclusions[Key(port)]=Map{{"port",ToData(port)},{"reason",std::string("Detached")},{"commandId",command}};
                for(auto& [id,d]:graph.Definitions)if(id!=operation)
                {auto inputs=Inputs(d);bool changed=false;for(auto& input:inputs)if(input.Source==port){input.Source=source;changed=true;}if(changed){Array data;for(const auto& input:inputs)data.emplace_back(InputData(input));d["inputs"]=data;writes[id]=d;}}
                const auto rewriteConstraint=[&](auto&& self,const Variant& value)->Variant
                {if(value.Is<Array>()){Array result;for(const auto& item:value.To<Array>())result.push_back(self(self,item));return result;}
                 if(!value.Is<Map>())return value;auto result=value.To<Map>();if(result.contains("operationId")&&result.contains("portId")&&PortFromData(result)==port)return ToData(source);
                 for(auto& [key,item]:result)item=self(self,item);return result;};
                plan["executionConstraints"]=rewriteConstraint(rewriteConstraint,At(plan,"executionConstraints"));
            }
        }
        reply["ports"]=created;
    }
    else throw std::invalid_argument("TOOLPATH_COMMAND_UNSUPPORTED: "+kind);
    if(kind=="AddOperation"&&request.contains("outputLayers"))
    {Map policy{{"kind",std::string("SetOperationLayers")},{"operationId",At(reply,"operationId")},{"layers",At(request,"outputLayers")}};std::map<IntentId,Map> none;EditToolpathLayers(policy,doc,plan,{},none);}
    plan["roots"]=roots;plan["operations"]=operations;plan["outputOperations"]=outputRoots;plan["exclusions"]=exclusions;graph.PlanDefinition=plan;CheckGraph(graph);
    ActiveEditOverlay->Definitions[{impl.Document,CEditableDocumentComponent::S_ClassName}]=doc;
    if(writes.empty())
    {reply["taskState"]=std::string("Idle");
     // Ordering and layer edits do not evaluate geometry. Its validity belongs
     // to a separate query, not an eager resolve in the command receipt.
     if(kind=="SetExecutionConstraints"||layerEdit)reply.erase("resultValidity");
     else {const auto proposed=ResolveGraph(*impl.Repository,graph,*impl.Registry,*impl.Geometry);reply["resultValidity"]=proposed.Status==EOutputCompleteness::Complete?std::string("Current"):std::string("Stale");}}
    auto& overlay=*ActiveEditOverlay;
    for(const auto& [id,data]:writes)
    {
        overlay.Definitions[{id,CEditableOperationComponent::S_ClassName}]=data;
        ++overlay.DefinitionRevisions[id];
        if(Text(data,"typeId")=="icax.source")
        {
            // This is the exact embedded definition projected for preflight of
            // later commands in this batch, never a published computation.
            const auto sourceType=impl.Registry->Find("icax.source");Require(sourceType!=nullptr,"TOOLPATH_SOURCE_TYPE_UNSUPPORTED");
            SOperationEvaluation in{id,{},MapAt(data,"parameters"),MapAt(data,"context"),{}};
            SOperationCandidate candidate;candidate.Outputs.push_back({"main",{},{},ToolpathFromData(MapAt(in.Parameters,"value"))});
            overlay.Records[id]=PrepareRecord(data,Result(*impl.Repository,impl.Document,id),in,candidate,*sourceType);
            ++overlay.RecordRevisions[id];
        }
    }
    overlay.Definitions[{planId,CEditablePlanComponent::S_ClassName}]=plan;
    overlay.Definitions[{impl.Document,CEditableDocumentComponent::S_ClassName}]=doc;
    for(const auto& [id,data]:identities)overlay.Identities[id]=data;
    for(const auto& [id,data]:labels)overlay.Labels[id]=data;
    overlay.Receipts[receiptId]={{"kind",std::string("commandReceipt")},{"requestFingerprint",requestHash},{"receipt",reply}};
    if(kind=="ClonePlan")overlay.Receipts[receiptId]["cloneInput"]=overlay.CloneInputs.at(command);
    overlay.NewReceipts.push_back(reply);overlay.ReceiptIds.push_back(receiptId);
    return reply;
}

IntentMap CEditableToolpathDocument::ExportDocument()const
{
    auto& impl=*m_Impl;impl.Owner();Require(!impl.Repository->IsWriteInProgress(),"TOOLPATH_SAVE_DURING_TRANSACTION");Array entities;
    const auto all=impl.Repository->FilterEntities([](const auto&){return true;});
    for(const auto& entity:all)
    {
        bool owned=entity->GetID()==impl.Document;
        for(const auto& type:{std::string(CEditablePlanComponent::S_ClassName),std::string(CEditableOperationComponent::S_ClassName)})
            if(const auto c=entity->GetComponent(type))owned|=ID(At(c->GetProperty("Definition").To<Map>(),"documentId"))==impl.Document;
        if(const auto c=Component<CEditableDerivedComponent>(*impl.Repository,entity->GetID()))owned|=c->GetOwner()==impl.Document;
        if(!owned)continue;Array components;
        for(const auto& type:entity->GetComponentClasses())
        {if(type!=CEditableDocumentComponent::S_ClassName&&type!=CEditablePlanComponent::S_ClassName&&type!=CEditableOperationComponent::S_ClassName&&type!=CEditableDerivedComponent::S_ClassName&&type!=CEditablePathLabelsComponent::S_ClassName)continue;
         const auto c=entity->GetComponent(type);components.emplace_back(Map{{"class",type},{"properties",c->GetProperties()},{"enabled",c->IsEnable()}});}
        entities.emplace_back(Map{{"id",entity->GetID()},{"components",std::move(components)}});
    }
    return {{"schema",std::string("icax.editable.document/3")},{"documentId",impl.Document},{"entities",std::move(entities)}};
}
IntentId CEditableToolpathDocument::ImportDocument(std::shared_ptr<IRepository> repository,const IntentMap& saved,
    std::shared_ptr<const IToolpathGeometryServices> geometry)
{
    ValidateToolpathImport(saved,*(geometry?geometry:CreateToolpathGeometryServices()));
    Require(repository&&Text(saved,"schema")=="icax.editable.document/3","TOOLPATH_DOCUMENT_SCHEMA_UNSUPPORTED");const auto document=ID(At(saved,"documentId"));const auto entities=ArrayAt(saved,"entities");
    const std::set<std::string> allowed{CEditableDocumentComponent::S_ClassName,CEditablePlanComponent::S_ClassName,CEditableOperationComponent::S_ClassName,CEditablePathLabelsComponent::S_ClassName,CEditableDerivedComponent::S_ClassName};
    std::set<IntentId> ids;std::map<IntentId,Map> definitions,plans;
    for(const auto& item:entities)
    {
        const auto e=item.To<Map>();const auto id=ID(At(e,"id"));Require(!id.is_nil()&&ids.insert(id).second&&!repository->GetEntity(id),"TOOLPATH_IMPORT_ID_COLLISION");
        for(const auto& component:ArrayAt(e,"components"))
        {
            const auto c=component.To<Map>();const auto type=Text(c,"class");Require(allowed.contains(type),"TOOLPATH_IMPORT_COMPONENT_UNSUPPORTED");const auto properties=MapAt(c,"properties");
            if(type==CEditableDerivedComponent::S_ClassName)Require(ID(At(properties,"Owner"))==document,"TOOLPATH_IMPORT_OWNER_INVALID");
            if(type==CEditablePlanComponent::S_ClassName||type==CEditableOperationComponent::S_ClassName)
            {const auto d=MapAt(properties,"Definition");Require(ID(At(d,"documentId"))==document,"TOOLPATH_IMPORT_DOCUMENT_MISMATCH");if(type==CEditablePlanComponent::S_ClassName)plans[id]=d;else definitions[id]=d;}
            if(type==CEditableOperationComponent::S_ClassName&&Text(MapAt(properties,"Definition"),"typeId")=="icax.source")
                (void)ToolpathFromData(MapAt(MapAt(MapAt(properties,"Definition"),"parameters"),"value"));
        }
    }
    Require(ids.contains(document),"TOOLPATH_IMPORT_DOCUMENT_MISSING");
    for(const auto& [id,plan]:plans)
    {SGraph g;g.Document=document;g.Plan=id;g.PlanDefinition=plan;g.Definitions=definitions;for(auto op:Ids(ArrayAt(plan,"operations")))g.Selected.insert(op);for(const auto& root:ArrayAt(plan,"roots"))g.Selected.insert(PortFromData(root.To<Map>()).OperationId);for(auto op:Ids(ArrayAt(plan,"outputOperations")))g.Selected.insert(op);CheckGraph(g);ValidateConstraints(MapAt(plan,"executionConstraints"));}
    auto undo=repository->BeginUndoCommand("导入刀路文档");auto& tx=repository->BeginTransaction("Import editable toolpath document");
    bool committing=false;
    try
    {
        for(const auto& item:entities){const auto e=item.To<Map>();const auto id=ID(At(e,"id"));tx.CreateEntity(id);for(const auto& component:ArrayAt(e,"components")){const auto c=component.To<Map>();tx.AttachComponent(id,Text(c,"class"),MapAt(c,"properties"),Flag(c,"enabled",true));}}
        committing=true;Commit(*repository,tx);undo->End();
    }
    catch(...){if(!committing)repository->CancelTransaction(tx);undo->Cancel();throw;}
    return document;
}
}
