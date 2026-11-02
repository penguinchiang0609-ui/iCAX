namespace
{
    Map BeginPathEdit(Scene& scene,const Map& request,const Text& verb,const std::vector<Map>& commands,const std::set<ID>& setups,const std::vector<ID>& operations)
    {
        auto runtime=Runtime(scene);CommitIntentCommands(scene,commands,setups,verb);ImportTask task;task.Kind="IntentEdit";task.CommandID=iCAX::Data::to_string(Identifier(request,"commandId"));task.Payload=verb+":"+iCAX::Data::VariantSerializer::Serialize(request);task.IntentEditOperations=operations;task.IntentEditSetups.assign(setups.begin(),setups.end());task.Sequence=runtime->Sequence;const auto id=task.TaskID;runtime->Tasks.emplace(id,std::move(task));return TaskState(scene,*runtime,runtime->Tasks.at(id));
    }
    std::optional<Map> PriorPathEdit(Scene& scene,const Map& request,const Text& verb)
    {
        const auto id=iCAX::Data::to_string(Identifier(request,"commandId"));const auto payload=verb+":"+iCAX::Data::VariantSerializer::Serialize(request);for(const auto& [_,task]:Runtime(scene)->Tasks)if(task.CommandID==id){if(task.Payload!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return TaskState(scene,*Runtime(scene),task);}return {};
    }
}
Map TransformCuts(Scene& scene,const Map& request)
{
    using namespace iCAX::CAM::Intent;CutFields(request,{"commandId","expected","intentIds","matrix"});if(auto previous=PriorPathEdit(scene,request,"修正刀路"))return *previous;CheckExpected(*Runtime(scene),request);CheckCapacity(*Runtime(scene));
    try{(void)RigidMatrix(request.at("matrix").To<Array>());}catch(const std::exception&){throw std::invalid_argument("CAM_CUT_RIGID_TRANSFORM_REQUIRED: 刀路仅支持平移和旋转；标准孔尺寸请在属性面板中修改。");}
    const auto ids=request.at("intentIds").To<Array>();if(ids.empty()||ids.size()>256)throw std::invalid_argument("CAM_INTENT_SELECTION_INVALID");iCAX::Data::uuid_name_generator names(Identifier(request,"commandId"));std::set<ID> selected,setups;std::vector<Map> commands;std::vector<ID> operations;
    for(const auto& value:ids){const auto id=CutID(value);if(!selected.insert(id).second)throw std::invalid_argument("CAM_INTENT_DUPLICATE");RequireCutUnlocked(scene,id);const auto path=ReadIntentPath(scene,id);if(!path.Participating||path.ResultState!="Current")throw std::invalid_argument("CAM_CUT_SOURCE_NOT_CURRENT");const auto metadata=ReadFiveAxisIntentMetadata(path.Value);const auto operation=names(iCAX::Data::to_string(id));const Map context{{"sourceOperationId",IntentSourceOperation(scene,path.Port)}};const Map parameters{{"matrix",request.at("matrix")}};(void)IntentDocument(scene).Preview("fiveaxis.correction",parameters,{path.Port},context);auto edits=AppendIntentPathOperation(scene,path,"fiveaxis.correction",parameters,operation,context);for(auto& edit:edits)if(String(edit,"kind")=="AddOperation")edit["operationId"]=operation;commands.insert(commands.end(),edits.begin(),edits.end());setups.insert(metadata.SetupID);operations.push_back(operation);}
    return BeginPathEdit(scene,request,"修正刀路",commands,setups,operations);
}
Map MergeCuts(Scene& scene,const Map& request)
{
    using namespace iCAX::CAM::Intent;CutFields(request,{"commandId","expected","intentIds","toleranceMm","visibleLayerId"});if(auto previous=PriorPathEdit(scene,request,"合并刀路"))return *previous;CheckExpected(*Runtime(scene),request);CheckCapacity(*Runtime(scene));
    const auto ids=request.at("intentIds").To<Array>();if(ids.size()<2||ids.size()>256)throw std::invalid_argument("CAM_MERGE_REQUIRES_TWO_PATHS");const auto operation=Identifier(request,"commandId");std::set<ID> selected,setups;Array inputs,sources;Map expected,assignment,pose;std::vector<SPortRef> ports;std::vector<SResolvedPath> nominal;ID setup,placement;Text name;
    for(const auto& value:ids){const auto id=CutID(value);if(!selected.insert(id).second)throw std::invalid_argument("CAM_INTENT_DUPLICATE");RequireCutUnlocked(scene,id);const auto path=ReadIntentPath(scene,id);if(!path.Participating||path.ResultState!="Current")throw std::invalid_argument("CAM_CUT_SOURCE_NOT_CURRENT");const auto metadata=ReadFiveAxisIntentMetadata(path.Value);if(setup.is_nil()){setup=metadata.SetupID;placement=metadata.PlacementID;name=path.Name;assignment=metadata.Process;pose=metadata.Pose;}else if(setup!=metadata.SetupID||placement!=metadata.PlacementID)throw std::invalid_argument("CAM_MERGE_SAME_SETUP_REQUIRED");inputs.emplace_back(ToData(SOperationInput{"input/"+std::to_string(ports.size()),path.Port,true,{}}));ports.push_back(path.Port);sources.emplace_back(IntentSourceOperation(scene,path.Port));expected[ToolpathPortKey(path.Port)]=path.Fingerprint;auto design=IntentDesignPath(scene,path);nominal.push_back(design.Port==path.Port?IntentBranchNominalInput(scene,path):design);}
    Array nominalIndices;std::map<SPortRef,std::size_t> slots;for(std::size_t i=0;i<nominal.size();++i){const auto& path=nominal[i];auto found=slots.find(path.Port);if(found==slots.end()){const auto index=ports.size();inputs.emplace_back(ToData(SOperationInput{"nominal/"+std::to_string(i),path.Port,false,{}}));ports.push_back(path.Port);expected[ToolpathPortKey(path.Port)]=path.Fingerprint;found=slots.emplace(path.Port,index).first;}nominalIndices.emplace_back(static_cast<unsigned int>(found->second));}
    const Map context{{"sourceOperationIds",sources}},parameters{{"toleranceMm",request.at("toleranceMm")},{"pathCount",static_cast<unsigned int>(ids.size())},{"nominalIndices",nominalIndices}};(void)IntentDocument(scene).Preview("fiveaxis.merge",parameters,ports,context);const auto plan=IntentPlan(scene,setup);
    std::vector<Map> commands{Map{{"kind",Text("AddOperation")},{"commandId",operation},{"operationId",operation},{"planId",plan},{"typeId",Text("fiveaxis.merge")},{"parameters",parameters},{"context",context},{"inputs",inputs},{"expectedInputs",expected},{"outputLabelPolicy",Map{{"numberScope",iCAX::Data::to_string(setup)},{"firstNumber",1u},{"name",name}}}}};
    {std::set<ID> visible;for(const auto& id:ids){const auto path=ReadIntentPath(scene,CutID(id));visible.insert(path.Layers.at("visibleLayerId").To<ID>());}
     if(visible.size()>1&&!request.contains("visibleLayerId"))throw std::invalid_argument("CAM_MERGE_LAYER_REQUIRED");
     if(request.contains("visibleLayerId"))commands.front()["outputLayers"]=Map{{"visibleLayerId",Identifier(request,"visibleLayerId")}};}
    const Map none{{"kind",Text("None")},{"placement",Text("ScrapSide")},{"lengthMm",0.},{"angleRad",0.}};assignment["leadIn"]=none;assignment["leadOut"]=none;assignment["gapLengthMm"]=0.;assignment["overcutLengthMm"]=0.;iCAX::Data::uuid_name_generator names(operation);auto parent=operation;
    const auto append=[&](const Text& type,Map values){const auto next=names(type);commands.push_back({{"kind",Text("AddOperation")},{"commandId",next},{"operationId",next},{"planId",plan},{"typeId",type},{"parameters",std::move(values)},{"context",context},{"inputs",Array{Map{{"slot",Text("input/0")},{"source",Map{{"fromCommand",parent},{"field",Text("port")}}},{"replace",true},{"ranges",Array{}}}}}});parent=next;};
    append("fiveaxis.pose",{{"policy",pose}});append("fiveaxis.kerf",{{"policy",assignment.at("kerf")}});append("fiveaxis.trace",{{"gapLengthMm",0.},{"overcutLengthMm",0.},{"allowTrimmedStart",true}});append("fiveaxis.leads",{{"leadIn",none},{"leadOut",none}});return BeginPathEdit(scene,request,"合并刀路",commands,{setup},{parent});
}
Map SetStandardHoleDimensions(Scene& scene,const Map& request)
{
    using namespace iCAX::CAM::Intent;
    CutFields(request,{"commandId","expected","intentId","dimensions"});
    if(auto previous=PriorPathEdit(scene,request,"修改标准孔尺寸"))return *previous;
    CheckExpected(*Runtime(scene),request);CheckCapacity(*Runtime(scene));
    const auto id=Identifier(request,"intentId");RequireCutUnlocked(scene,id);const auto path=ReadIntentPath(scene,id);
    if(!path.Participating||path.ResultState!="Current")throw std::invalid_argument("CAM_CUT_SOURCE_NOT_CURRENT");
    const auto operation=Identifier(request,"commandId");const auto metadata=ReadFiveAxisIntentMetadata(path.Value);
    const auto hole=IntentStandardHoleView(path);const auto kind=String(hole,"kind");
    if(kind!="Circle"&&kind!="Ellipse"&&kind!="Rectangle"&&kind!="Slot")throw std::invalid_argument("CAM_CUT_NOT_RECOGNIZED_STANDARD_HOLE");
    const Map context{{"sourceOperationId",IntentSourceOperation(scene,path.Port)}};
    const Map parameters{{"dimensions",request.at("dimensions")},{"descriptor",hole}};
    if(kind!="Rectangle"&&kind!="Slot")
    {
        (void)IntentDocument(scene).Preview("fiveaxis.holeDimensions",parameters,{path.Port},context);
        auto commands=AppendIntentPathOperation(scene,path,"fiveaxis.holeDimensions",parameters,operation,context);
        for(auto& edit:commands)if(String(edit,"kind")=="AddOperation")edit["operationId"]=operation;
        return BeginPathEdit(scene,request,"修改标准孔尺寸",commands,{metadata.SetupID},{operation});
    }
    const auto design=IntentDesignPath(scene,path);const auto nominal=design.Port==path.Port?IntentBranchNominalInput(scene,path):design;
    const auto plan=IntentPlan(scene,metadata.SetupID);const auto runtime=Runtime(scene);const auto type=runtime->IntentRegistry->Find("fiveaxis.holeDimensions");
    SOperationEvaluation preview;preview.OperationId=operation;preview.Parameters=parameters;preview.Context=context;preview.RuntimeContext=type->FreezeContext(context);
    preview.Inputs={{{"selection",path.Port,true,{}},path.PathId,path.Value,path.Fingerprint},{{"nominal",nominal.Port,false,{}},nominal.PathId,nominal.Value,nominal.Fingerprint}};
    try{(void)EvaluateFrozenToolpathOperation(*runtime->IntentRegistry,*runtime->IntentGeometry,"fiveaxis.holeDimensions",std::move(preview));}
    catch(const std::exception& error){throw std::invalid_argument(Text("CAM_HOLE_PREVIEW: ")+error.what());}
    std::vector<Map> commands{Map{{"kind",Text("AddOperation")},{"commandId",operation},{"operationId",operation},{"planId",plan},{"typeId",Text("fiveaxis.holeDimensions")},
        {"parameters",parameters},{"context",context},{"inputs",Array{ToData(SOperationInput{"selection",path.Port,true,{}}),ToData(SOperationInput{"nominal",nominal.Port,false,{}})}},
        {"expectedInputs",Map{{ToolpathPortKey(path.Port),path.Fingerprint},{ToolpathPortKey(nominal.Port),nominal.Fingerprint}}}}};
    iCAX::Data::uuid_name_generator names(operation);auto parent=operation;
    const auto append=[&](const Text& name,Map values){const auto next=names(name);commands.push_back({{"kind",Text("AddOperation")},{"commandId",next},{"operationId",next},{"planId",plan},{"typeId",name},
        {"parameters",std::move(values)},{"context",context},{"inputs",Array{Map{{"slot",Text("input/0")},{"source",Map{{"fromCommand",parent},{"field",Text("port")}}},{"replace",true},{"ranges",Array{}}}}}});parent=next;};
    append("fiveaxis.kerf",{{"policy",metadata.Process.at("kerf")}});
    Map trace{{"gapLengthMm",metadata.Process.at("gapLengthMm")},{"overcutLengthMm",metadata.Process.at("overcutLengthMm")}};
    const auto oldTrace=IntentDocument(scene).ReadOperation(IntentProcessingOperation(scene,path.Port,"fiveaxis.trace")).at("parameters").To<Map>();
    if(oldTrace.contains("start"))trace["start"]=oldTrace.at("start");
    append("fiveaxis.trace",std::move(trace));append("fiveaxis.leads",{{"leadIn",metadata.Process.at("leadIn")},{"leadOut",metadata.Process.at("leadOut")}});
    return BeginPathEdit(scene,request,"修改标准孔尺寸",commands,{metadata.SetupID},{parent});
}
