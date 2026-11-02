// Splitting edits retained exact results and their common point/interval fields.
// It never bakes an authored tool-axis field into replacement samples.
namespace
{
    void RequireCuttingSplitAnchor(const iCAX::CAM::Intent::SResolvedPath& path,const iCAX::CAM::Intent::SPathAnchor& anchor)
    {
        const auto found=std::find_if(path.Value.Geometry.Segments.begin(),path.Value.Geometry.Segments.end(),[&](const auto& segment){return segment.Id==anchor.SegmentId;});
        if(found==path.Value.Geometry.Segments.end())throw std::invalid_argument("CAM_INTENT_STALE_PICK");
        if(found->ConstructionRole=="LeadIn"||found->ConstructionRole=="LeadOut")throw std::invalid_argument("CAM_SPLIT_REQUIRES_CUTTING_BOUNDARY");
    }
    void PumpIntentEdit(Scene& scene,SceneRuntime& runtime,ImportTask& task)
    {
        using namespace iCAX::CAM::Intent;
        try
        {
            for(const auto& setup:task.IntentEditSetups)if(runtime.IntentGenerations.contains(IntentPlan(scene,setup)))return;
            Array ids;std::set<ID> operations(task.IntentEditOperations.begin(),task.IntentEditOperations.end());
            for(const auto& operation:operations)
            {
                const auto definition=IntentDocument(scene).ReadOperation(operation);
                if(!definition.at("enabled").To<bool>()){task.Status="Superseded";return;}
                const auto result=definition.at("result").To<Map>();
                if(!result.contains("state")||String(result,"state")!="Current")
                    throw std::invalid_argument("CAM_INTENT_GENERATION_FAILED: "+(result.contains("diagnostic")?String(result,"diagnostic"):Text("result missing")));
            }
            for(const auto& setup:task.IntentEditSetups)for(const auto& path:IntentPaths(scene,setup))
                if(operations.contains(path.Port.OperationId)&&path.ResultState=="Current")ids.emplace_back(iCAX::Data::to_string(path.PathId));
            task.Result={{"intentIds",ids}};task.Status="Completed";
        }
        catch(const std::exception& error){task.Status="Failed";task.Error=error.what();}
    }

}
Map ResolveCutSplitPoint(Scene& scene,const Map& request)
{
    CutFields(request,{"expected","intentId","nearPoint","maximumDistanceMm","mode","frame"});CheckExpected(*Runtime(scene),request);
    if(String(request,"frame")!="Workpiece")throw std::invalid_argument("CAM_REFERENCE_FRAME_MISMATCH");
    const auto path=ReadIntentPath(scene,Identifier(request,"intentId"));RequireCutUnlocked(scene,path.PathId);
    if(!path.Participating||path.ResultState!="Current")throw std::invalid_argument("CAM_CUT_SOURCE_NOT_CURRENT");
    auto result=ResolveIntentPoint(scene,path,request,true);
    RequireCuttingSplitAnchor(path,iCAX::CAM::Intent::AnchorFromData(result.at("anchor").To<Map>()));return result;
}
Map SplitCuts(Scene& scene,const Map& request)
{
    using namespace iCAX::CAM::Intent;
    CutFields(request,{"commandId","expected","cuts"});const auto runtime=Runtime(scene);
    const auto command=Identifier(request,"commandId");const auto commandText=iCAX::Data::to_string(command);
    const auto payload=Text("SplitCuts:")+iCAX::Data::VariantSerializer::Serialize(request);
    for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==commandText){if(task.Payload!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return TaskState(scene,*runtime,task);}
    if(runtime->Receipts.contains(commandText))throw std::invalid_argument("CAM_COMMAND_REUSED");
    CheckExpected(*runtime,request);CheckCapacity(*runtime);const auto selections=request.at("cuts").To<Array>();
    if(selections.empty()||selections.size()>256)throw std::invalid_argument("CAM_INTENT_SELECTION_INVALID");
    std::vector<Map> commands;std::set<ID> unique,setups;std::vector<ID> operations;iCAX::Data::uuid_name_generator names(command);std::size_t count=0;
    for(const auto& item:selections)
    {
        const auto selection=item.To<Map>();CutFields(selection,{"intentId","points"});const auto id=Identifier(selection,"intentId");
        if(!unique.insert(id).second)throw std::invalid_argument("CAM_INTENT_DUPLICATE");RequireCutUnlocked(scene,id);
        const auto path=ReadIntentPath(scene,id);const auto metadata=ReadFiveAxisIntentMetadata(path.Value);const auto plan=IntentPlan(scene,metadata.SetupID);
        if(!path.Participating||path.ResultState!="Current")throw std::invalid_argument("CAM_CUT_SOURCE_NOT_CURRENT");
        Array cuts;for(const auto& point:selection.at("points").To<Array>())
        {const auto anchor=IntentAnchor(path,point.To<Map>());RequireCuttingSplitAnchor(path,anchor);cuts.emplace_back(Map{{"id",iCAX::Data::to_string(names("point/"+iCAX::Data::to_string(id)+"/"+std::to_string(cuts.size())))},{"anchor",ToData(anchor)}});}
        count+=cuts.size();if(cuts.empty()||count>256)throw std::invalid_argument("CURVE_SPLIT_SELECTION_INVALID");
        const auto operation=names("split/"+iCAX::Data::to_string(id));operations.push_back(operation);setups.insert(metadata.SetupID);
        commands.push_back({{"kind",Text("AddOperation")},{"commandId",operation},{"operationId",operation},{"planId",plan},{"typeId",Text("fiveaxis.split")},
            {"parameters",Map{{"cuts",cuts},{"retainFirstIdentity",true}}},{"context",Map{{"sourceOperationId",IntentSourceOperation(scene,path.Port)}}},
            {"inputs",Array{ToData(SOperationInput{"input/0",path.Port,true,{}})}},{"expectedInputs",Map{{ToolpathPortKey(path.Port),path.Fingerprint}}},
            {"outputLabelPolicy",Map{{"numberScope",iCAX::Data::to_string(metadata.SetupID)},{"firstNumber",1u},{"name",path.Name}}}});
    }
    CommitIntentCommands(scene,commands,setups,"打断刀路");ImportTask task;task.Kind="IntentEdit";task.CommandID=commandText;task.Payload=payload;
    task.IntentEditOperations=std::move(operations);task.IntentEditSetups.assign(setups.begin(),setups.end());task.Sequence=runtime->Sequence;
    const auto taskId=task.TaskID;runtime->Tasks.emplace(taskId,std::move(task));return TaskState(scene,*runtime,runtime->Tasks.at(taskId));
}
