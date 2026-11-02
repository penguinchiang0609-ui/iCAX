#include "pch.h"
#include "EditableToolpathPreview.h"
#include "EditableToolpathInternal.h"

namespace iCAX::CAM::Intent
{
SOperationCandidate EvaluateFrozenToolpathOperation(const CToolpathOperationRegistry& registry,
    const IToolpathGeometryServices& geometry,const std::string& typeId,SOperationEvaluation input)
{
    using namespace Editable;
    const auto type=registry.Find(typeId);
    Require(type!=nullptr,"TOOLPATH_OPERATION_UNSUPPORTED");
    Require(!input.OperationId.is_nil(),"TOOLPATH_PREVIEW_OPERATION_ID_REQUIRED");
    Require(input.Inputs.size()>=type->MinimumInputs&&input.Inputs.size()<=type->MaximumInputs,"TOOLPATH_INPUT_ARITY");
    Require(!type->FreezeContext||input.RuntimeContext!=nullptr,"TOOLPATH_PREVIEW_FROZEN_CONTEXT_REQUIRED");
    ValidateParameterSchema(type->ParameterSchema,input.Parameters);if(type->ValidateParameters)type->ValidateParameters(input.Parameters);
    for(const auto& path:input.Inputs)
    {
        geometry.Validate(path.Value.Geometry);
        Require(ToolpathFingerprint(path.Value)==path.Fingerprint,"TOOLPATH_PREVIEW_INPUT_FINGERPRINT_MISMATCH");
        for(const auto& range:path.Binding.Ranges){(void)Locate(range.Start,path);(void)Locate(range.End,path);}
    }
    if(!input.Cancelled)input.Cancelled=std::make_shared<std::atomic_bool>(false);
    Require(!input.Cancelled->load(),"TOOLPATH_CANCELLED");
    if(type->NormalizeParameters)input.Parameters=type->NormalizeParameters(input.Inputs,input.Parameters);
    if(type->ValidateInput)type->ValidateInput(input);
    SOperationCandidate candidate;
    if(type->ReadChannels.contains("*"))candidate=type->Evaluate(input,geometry);
    else
    {
        SOperationEvaluation algorithmInput{input.OperationId,{},input.Parameters,input.Context,input.Cancelled,input.RuntimeContext};
        algorithmInput.Inputs.reserve(input.Inputs.size());
        for(const auto& path:input.Inputs)
        {
            SFrozenPathInput filtered{path.Binding,path.PathId,{},path.Fingerprint};filtered.Value.Geometry=path.Value.Geometry;
            for(const auto& [name,value]:path.Value.Channels)if(type->ReadChannels.contains(name))filtered.Value.Channels.emplace(name,value);
            algorithmInput.Inputs.push_back(std::move(filtered));
        }
        candidate=type->Evaluate(algorithmInput,geometry);
    }
    if(!type->DeclaredOutputRoles.empty())
    {std::set<std::string> roles;for(const auto& output:candidate.Outputs)roles.insert(output.RoleKey);Require(roles==type->DeclaredOutputRoles,"TOOLPATH_DECLARED_OUTPUTS_MISMATCH");}
    PropagateChannels(input,candidate,*type);ValidateCandidate(input,candidate,geometry);
    if(type->ValidateResult)type->ValidateResult(input,candidate);return candidate;
}
std::map<SPortRef,SFrozenPathInput> EvaluateFrozenToolpathGraph(const CToolpathOperationRegistry& registry,
    const IToolpathGeometryServices& geometry,std::map<SPortRef,SFrozenPathInput> values,
    const std::vector<SFrozenToolpathPreviewNode>& nodes,const std::shared_ptr<std::atomic_bool>& cancelled)
{
    using namespace Editable;std::set<IntentId> visited;
    for(const auto& node:nodes)
    {
        auto input=node.Evaluation;Require(visited.insert(input.OperationId).second,"TOOLPATH_PREVIEW_OPERATION_DUPLICATE");
        if(cancelled)input.Cancelled=cancelled;
        for(auto& binding:input.Inputs)
        {
            const auto source=values.find(binding.Binding.Source);Require(source!=values.end(),"TOOLPATH_PREVIEW_DEPENDENCY_NOT_READY");
            binding.PathId=source->second.PathId;binding.Value=source->second.Value;binding.Fingerprint=source->second.Fingerprint;
        }
        const auto operation=input.OperationId;std::vector<IntentId> identities;identities.reserve(input.Inputs.size());
        for(const auto& binding:input.Inputs)identities.push_back(binding.PathId);
        auto candidate=EvaluateFrozenToolpathOperation(registry,geometry,node.TypeId,std::move(input));
        for(auto& output:candidate.Outputs)
        {
            const auto port=DeclaredToolpathPort(operation,output.RoleKey);Require(!values.contains(port),"TOOLPATH_PREVIEW_PORT_DUPLICATE");
            IntentId identity;if(output.InheritInput)identity=identities.at(*output.InheritInput);
            const auto fingerprint=ToolpathFingerprint(output.Value);
            values.emplace(port,SFrozenPathInput{{"preview",port,false,{}},identity,std::move(output.Value),fingerprint});
        }
    }
    return values;
}
}
