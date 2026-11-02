#include "FiveAxisIntentBranchProcess.h"
#include "FiveAxisIntentPoseField.h"
#include "FiveAxisIntentProjection.h"
#include "../../cam/IntentToolpath/EditableToolpathProjection.h"
#include "../../cam/IntentToolpath/EditableToolpathPreview.h"
#include <algorithm>
#include <stdexcept>

namespace iCAX::FiveAxisCAM
{
namespace
{
using namespace iCAX::CAM::Intent;
using Map=IntentMap;using Array=IntentArray;using Variant=iCAX::Data::Variant;
void Check(bool condition,const char* error){if(!condition)throw std::invalid_argument(error);}
Map Field(const char* type,bool required=true){return {{"type",std::string(type)},{"required",required}};}
std::string Text(const Map& map,const char* key,const char* fallback="")
{const auto found=map.find(key);return found==map.end()?fallback:found->second.To<std::string>();}
bool Flag(const Map& map,const char* key){return map.contains(key)&&map.at(key).To<bool>();}
void AddExcluded(std::vector<SPathRange>& ranges,const std::vector<SPathRange>& added)
{for(const auto& range:added)if(std::none_of(ranges.begin(),ranges.end(),[&](const auto& previous){return ToData(previous)==ToData(range);}))ranges.push_back(range);}
}
void RegisterFiveAxisIntentBranchProcess(CToolpathOperationRegistry& registry,FiveAxisIntentContextResolver resolver)
{
    // Capture immutable registered algorithms; workers neither resolve a Scene
    // nor consult a mutable owner registry while rebuilding a local branch.
    auto processes=std::make_shared<CToolpathOperationRegistry>();
    for(const auto* name:{"fiveaxis.kerf","fiveaxis.trace","fiveaxis.leads","fiveaxis.correction","fiveaxis.holeDimensions"})
    {const auto type=registry.Find(name);Check(bool(type),"FIVEAXIS_INTENT_BRANCH_PROCESS_ALGORITHM_MISSING");processes->Register(*type);}
    SOperationType type;type.TypeId="fiveaxis.branch.process";type.EvaluationRevision="5";type.MinimumInputs=2;type.MaximumInputs=2;
    type.ReadChannels={"*"};type.WriteChannels={"*"};type.PropagateUnwrittenChannels=false;
    type.DeclaredOutputRoles={"main"};type.DeclaredOutputIdentityInputs={{"main",0}};
    type.ParameterSchema={{"fields",Map{{"kerf",Field("object")},{"gapLengthMm",Field("number")},{"overcutLengthMm",Field("number")},
        {"leadIn",Field("object")},{"leadOut",Field("object")},{"closed",Field("boolean")},{"start",Field("object",false)},{"startLocked",Field("boolean",false)},
        {"posePolicy",Field("object",false)},{"clearPoseControls",Field("boolean",false)}}}};
    type.FreezeContext=[resolver=std::move(resolver)](const Map& persisted)->std::shared_ptr<const IToolpathOperationContext>
    {const auto context=resolver(persisted);Check(bool(context),"FIVEAXIS_INTENT_CONTEXT_MISSING");return context;};
    type.ValidateInput=[](const SOperationEvaluation& input)
    {Check(input.Inputs.size()==2&&input.Inputs[0].Binding.Replace&&!input.Inputs[1].Binding.Replace,"FIVEAXIS_INTENT_BRANCH_INPUT_CONTRACT_INVALID");
     Check(input.Inputs[1].Value.Geometry.Meaning=="NominalBoundary","FIVEAXIS_INTENT_BRANCH_NOMINAL_INPUT_REQUIRED");};
    type.Evaluate=[processes](const SOperationEvaluation& input,const IToolpathGeometryServices& geometry)
    {
        const auto* context=dynamic_cast<const CFiveAxisIntentOperationContext*>(input.RuntimeContext.get());Check(context!=nullptr,"FIVEAXIS_INTENT_CONTEXT_MISSING");
        const auto& selected=input.Inputs.at(0);const auto& nominal=input.Inputs.at(1);const auto outputPort=DeclaredToolpathPort(input.OperationId,"main");
        for(std::size_t i=0;i<selected.Value.Geometry.Segments.size();++i)
            if(selected.Value.Geometry.Segments[i].ConstructionRole=="OffsetConnector")
                Check(i>0&&i+1<selected.Value.Geometry.Segments.size(),"FIVEAXIS_INTENT_BRANCH_NEEDS_REBIND_CONNECTOR_BOUNDARY");
        auto projected=ProjectToolpathSelection(selected,nominal,input.OperationId,{"LeadIn","LeadOut","Lead","OffsetConnector"},geometry);
        auto value=std::move(projected.Value);value.Geometry.Closed=Flag(input.Parameters,"closed");std::vector<SPathRange> excluded=std::move(projected.ExcludedRanges);
        const auto edits=selected.Value.Channels.contains("fiveaxis.geometryEdits")?selected.Value.Channels.at("fiveaxis.geometryEdits").To<Map>().at("entries").To<Array>():Array{};
        value.Channels.erase("fiveaxis.correction");value.Channels.erase("fiveaxis.geometryEdits");
        if(input.Parameters.contains("posePolicy"))
        {
            auto channel=value.Channels.contains("pose")?value.Channels.at("pose").To<Map>():Map{{"schema",std::string("fiveaxis.pose/1")},{"attachment",std::string("Path")}};
            channel["policy"]=input.Parameters.at("posePolicy");value.Channels["pose"]=channel;
            if(Flag(input.Parameters,"clearPoseControls")&&value.Channels.contains("poseControls"))
            {auto controls=value.Channels.at("poseControls").To<Map>();auto entries=controls.at("entries").To<Array>();
             std::erase_if(entries,[](const auto& item){const auto point=item.template To<Map>();return Text(point,"set","active")=="active";});controls["entries"]=std::move(entries);value.Channels["poseControls"]=std::move(controls);}
            value.Channels.erase("poseField");value.Channels["poseField"]=BuildFiveAxisIntentPoseField(*context,value,outputPort);
        }
        Map constraints;bool repeats=false;
        const auto run=[&](const std::string& name,Map parameters)
        {
            SFrozenPathInput current{{"input/0",outputPort,true,{}},selected.PathId,value,ToolpathFingerprint(value)};
            SOperationEvaluation evaluation{input.OperationId,{},std::move(parameters),input.Context,input.Cancelled};evaluation.RuntimeContext=input.RuntimeContext;evaluation.Inputs.push_back(current);
            auto candidate=EvaluateFrozenToolpathOperation(*processes,geometry,name,std::move(evaluation));
            Check(candidate.Outputs.size()==1&&candidate.Outputs.front().RoleKey=="main","FIVEAXIS_INTENT_BRANCH_RESULT_CONTRACT_INVALID");
            AddExcluded(excluded,ProjectToolpathRangesToInput(current,candidate.ExcludedRanges,selected));
            for(const auto& [key,item]:candidate.Constraints)constraints[key]=item;repeats|=candidate.AllowRepeatedCoverage;value=std::move(candidate.Outputs.front().Value);
        };
        run("fiveaxis.kerf",{{"policy",input.Parameters.at("kerf")}});
        Map trace{{"gapLengthMm",input.Parameters.at("gapLengthMm")},{"overcutLengthMm",input.Parameters.at("overcutLengthMm")}};
        if(input.Parameters.contains("start"))trace["start"]=input.Parameters.at("start");run("fiveaxis.trace",std::move(trace));
        run("fiveaxis.leads",{{"leadIn",input.Parameters.at("leadIn")},{"leadOut",input.Parameters.at("leadOut")}});
        SFrozenPathInput finalStage{{"input/0",outputPort,true,{}},selected.PathId,value,ToolpathFingerprint(value)};
        value=FinalizeProjectedToolpath(finalStage,input.OperationId,{input.OperationId},geometry);
        // A base-only field is an exact CAD expression. Re-anchor it to the
        // final stored domains after trace trimming; authored interval profiles
        // retain their original real input addresses and interpolation law.
        if(!value.Channels.contains("poseField")||!Flag(value.Channels.at("poseField").To<Map>(),"authored"))
        {value.Channels.erase("poseField");value.Channels["poseField"]=BuildFiveAxisIntentPoseField(*context,value,outputPort);}
        for(const auto& item:edits){const auto edit=item.To<Map>();run(Text(edit,"type"),edit.at("parameters").To<Map>());}
        SOperationCandidate result;result.Outputs.push_back({"main",{},std::size_t(0),std::move(value)});
        result.ExcludedRanges=std::move(excluded);result.Constraints=std::move(constraints);result.AllowRepeatedCoverage=repeats;return result;
    };
    registry.Register(std::move(type));
}
}
