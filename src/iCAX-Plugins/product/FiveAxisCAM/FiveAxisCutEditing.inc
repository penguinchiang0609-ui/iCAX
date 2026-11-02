namespace
{
    enum class CutEdit {Reverse,RetainedSide};
    Map EditCutDefinitions(Scene& scene,const Map& request,CutEdit edit)
    {
        using namespace iCAX::CAM::Intent;
        if(edit==CutEdit::RetainedSide)CutFields(request,{"commandId","expected","intentIds","retainedSide"});else CutFields(request,{"commandId","expected","intentIds"});
        const auto runtime=Runtime(scene);const auto command=Identifier(request,"commandId");const auto commandText=iCAX::Data::to_string(command);
        const auto payload=Text("EditCutDefinitions:")+std::to_string(static_cast<int>(edit))+":"+iCAX::Data::VariantSerializer::Serialize(request);
        if(const auto prior=runtime->Receipts.find(commandText);prior!=runtime->Receipts.end()){if(prior->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return prior->second.second;}
        CheckExpected(*runtime,request);CheckCapacity(*runtime);const auto ids=request.at("intentIds").To<Array>();if(ids.empty()||ids.size()>256)throw std::invalid_argument("CAM_INTENT_SELECTION_INVALID");
        std::vector<Map> commands;std::set<ID> unique,setups,selected,updatedSources,updatedConsumers;iCAX::Data::uuid_name_generator names(command);
        for(const auto& value:ids)selected.insert(CutID(value));
        for(const auto& value:ids)
        {
            const auto id=CutID(value);if(!unique.insert(id).second)throw std::invalid_argument("CAM_INTENT_DUPLICATE");RequireCutUnlocked(scene,id);
            const auto path=ReadIntentPath(scene,id);const auto metadata=ReadFiveAxisIntentMetadata(path.Value);const auto plan=IntentPlan(scene,metadata.SetupID);setups.insert(metadata.SetupID);
            Map parameters;Text type="fiveaxis.reverse";
            if(edit==CutEdit::RetainedSide)
            {
                const auto side=String(request,"retainedSide");if(side!="Interior"&&side!="Exterior")throw std::invalid_argument("CAM_CUT_RETAINED_SIDE_INVALID");
                // Changing retained material must invalidate/recompute the
                // compensation node, rather than relabeling an existing offset.
                const auto sourceId=IntentSourceOperation(scene,path.Port);RequireIntentOperationEditScope(scene,sourceId,selected);
                if(!updatedSources.insert(sourceId).second)continue;
                auto source=IntentDocument(scene).ReadOperation(sourceId);auto sourceValue=ToolpathFromData(source.at("parameters").To<Map>().at("value").To<Map>());
                auto sourceMetadata=ReadFiveAxisIntentMetadata(sourceValue);sourceMetadata.Feature["retainedSide"]=side;sourceValue.Channels["fiveaxis"]=ToFiveAxisIntentChannel(sourceMetadata);
                commands.push_back({{"kind",Text("UpdateSource")},{"commandId",names("retained/"+iCAX::Data::to_string(id))},{"planId",plan},
                    {"operationId",IntentSourceOperation(scene,path.Port)},{"expectedOperationFingerprint",source.at("definitionFingerprint")},{"value",ToData(sourceValue)}});continue;
            }
            const auto design=IntentDesignPath(scene,path);
            if(design.Port!=path.Port)
            {
                const auto consumer=IntentProcessingOperation(scene,path.Port,"fiveaxis.kerf");RequireIntentOperationEditScope(scene,consumer,selected);
                if(!updatedConsumers.insert(consumer).second)continue;
                const auto definition=IntentDocument(scene).ReadOperation(consumer);const auto input=definition.at("inputs").To<Array>().at(0).To<Map>();
                commands.push_back({{"kind",Text("AddOperation")},{"commandId",names("reverse-before-process/"+iCAX::Data::to_string(consumer))},{"planId",plan},{"typeId",type},{"parameters",parameters},
                    {"insertBefore",Map{{"operationId",consumer},{"inputSlot",input.at("slot")},{"expectedOperationFingerprint",definition.at("definitionFingerprint")}}},
                    {"inputs",Array{ToData(SOperationInput{"input/0",design.Port,true,{}})}},{"expectedInputs",Map{{ToolpathPortKey(design.Port),design.Fingerprint}}}});
            }
            else
            {
                auto edits=AppendIntentPathOperation(scene,path,type,parameters,names("reverse/"+iCAX::Data::to_string(id)));
                commands.insert(commands.end(),edits.begin(),edits.end());
            }
        }
        CommitIntentCommands(scene,commands,setups,"修改切割定义");Array items;for(const auto& value:ids)items.emplace_back(IntentPathView(scene,ReadIntentPath(scene,CutID(value))));
        Map result{{"items",items}};runtime->Receipts.emplace(commandText,std::make_pair(payload,result));return result;
    }

}
Map ReadCutDefinitions(Scene& scene,const Map& request)
{
    CutFields(request,{"expected","intentIds","projection"});CheckExpected(*Runtime(scene),request);
    const auto ids=request.at("intentIds").To<Array>();if(ids.empty()||ids.size()>256)throw std::invalid_argument("CAM_INTENT_SELECTION_INVALID");
    if(request.contains("projection")&&request.at("projection").To<bool>())return CutDefinitionsProjection(scene,ids);
    Array items;std::set<ID> unique;
    for(const auto& value:ids) {
        const auto id=Identifier(Map{{"id",value}},"id");if(!unique.insert(id).second)throw std::invalid_argument("CAM_INTENT_DUPLICATE");
        items.emplace_back(IntentPathView(scene,ReadIntentPath(scene,id)));
    }
    return {{"items",items}};
}
Map ResolveCutStart(Scene& scene,const Map& request)
{
    CutFields(request,{"expected","intentId","nearPoint","maximumDistanceMm","mode","frame"});CheckExpected(*Runtime(scene),request);
    if(String(request,"frame")!="Workpiece")throw std::invalid_argument("CAM_REFERENCE_FRAME_MISMATCH");
    const auto path=IntentDesignPath(scene,ReadIntentPath(scene,Identifier(request,"intentId")));if(path.ResultState!="Current")throw std::invalid_argument("CAM_INTENT_PATH_NOT_CURRENT");if(!path.Value.Geometry.Closed)throw std::invalid_argument("CAM_CUT_CLOSED_CURVE_REQUIRED");
    auto result=ResolveIntentPoint(scene,path,request,false);result["start"]=result.at("anchor");result.erase("anchor");return result;
}
Map SetCutStart(Scene& scene,const Map& request)
{
    CutFields(request,{"commandId","expected","intentIds","start"});
    auto edit=request;edit.erase("start");edit["patch"]=Map{{"start",request.at("start")}};
    return UpdateCutProcess(scene,edit);
}
Map ReverseCuts(Scene& scene,const Map& request){return EditCutDefinitions(scene,request,CutEdit::Reverse);}
Map SetCutRetainedSide(Scene& scene,const Map& request){return EditCutDefinitions(scene,request,CutEdit::RetainedSide);}

Map SetCutCommands(Scene& scene,const Map& request)
{
    CutFields(request,{"commandId","expected","intentId","beforeCommands","afterCommands"});
    const auto runtime=Runtime(scene);const auto command=iCAX::Data::to_string(Identifier(request,"commandId"));
    const auto payload=Text("SetCutCommands:")+iCAX::Data::VariantSerializer::Serialize(request);
    if(const auto prior=runtime->Receipts.find(command);prior!=runtime->Receipts.end()){
        if(prior->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return prior->second.second;}
    for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==command)throw std::invalid_argument("CAM_COMMAND_REUSED");
    CheckExpected(*runtime,request);CheckCapacity(*runtime);
    const auto id=Identifier(request,"intentId");RequireCutUnlocked(scene,id);
    const auto path=ReadIntentPath(scene,id);auto metadata=ReadFiveAxisIntentMetadata(path.Value);
    const auto before=String(request,"beforeCommands"),after=String(request,"afterCommands");if(before.size()>1048576||after.size()>1048576)throw std::invalid_argument("CAM_COMMAND_TEXT_TOO_LONG");
    const bool changed=before!=metadata.BeforeCommands||after!=metadata.AfterCommands;
    if(changed)
    {
        using namespace iCAX::CAM::Intent;metadata.BeforeCommands=before;metadata.AfterCommands=after;++metadata.InputRevision;
        CommitIntentCommands(scene,AppendIntentPathOperation(scene,path,"fiveaxis.metadata",{{"beforeCommands",before},{"afterCommands",after}},Identifier(request,"commandId")),{metadata.SetupID},"编辑刀路指令");
    }
    Map result{{"changed",changed},{"intentId",request.at("intentId")}};runtime->Receipts.emplace(command,std::make_pair(payload,result));return result;
}
Map RenameCut(Scene& scene,const Map& request)
{
    CutFields(request,{"commandId","expected","intentId","name"});
    const auto runtime=Runtime(scene);const auto command=iCAX::Data::to_string(Identifier(request,"commandId"));
    const auto payload=Text("RenameCut:")+iCAX::Data::VariantSerializer::Serialize(request);
    if(const auto prior=runtime->Receipts.find(command);prior!=runtime->Receipts.end()){
        if(prior->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return prior->second.second;}
    for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==command)throw std::invalid_argument("CAM_COMMAND_REUSED");
    CheckExpected(*runtime,request);CheckCapacity(*runtime);
    const auto id=Identifier(request,"intentId");RequireCutUnlocked(scene,id);
    const auto intent=ReadIntentCut(scene,id);
    const auto name=String(request,"name");
    if(name.empty()||name.size()>512||name.find_first_not_of(" \t\r\n")==Text::npos||std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32||c==127;}))throw std::invalid_argument("CAM_CUT_NAME_INVALID");
    const bool changed=name!=intent->GetName();
    if(changed){using namespace iCAX::CAM::Intent;const auto& path=intent->Path;IntentDocument(scene).Execute({{"kind",Text("SetLabels")},{"commandId",Identifier(request,"commandId")},{"planId",IntentPlan(scene,intent->GetSetupID())},{"pathId",id},{"name",name},{"expectedLabelsVersion",static_cast<unsigned long long>(path.LabelsVersion)}});}
    const auto feature=ReadIntentCut(scene,id);
    Map result{{"changed",changed},{"intentId",request.at("intentId")},{"name",name},{"statePatch",CutMetadataPatch(scene,feature->GetSetupID(),false)}};runtime->Receipts.emplace(command,std::make_pair(payload,result));return result;
}
Map ManageCutLayers(Scene& scene,const Map& request)
{
    CutFields(request,{"commandId","expected","action","setupId","intentIds","layerId","targetLayerId","properties","expectedLayersFingerprint"});
    const auto runtime=Runtime(scene);const auto command=Identifier(request,"commandId");const auto key=iCAX::Data::to_string(command);
    const auto payload=Text("ManageCutLayers:")+iCAX::Data::VariantSerializer::Serialize(request);
    if(const auto prior=runtime->Receipts.find(key);prior!=runtime->Receipts.end()){if(prior->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return prior->second.second;}
    CheckExpected(*runtime,request);CheckCapacity(*runtime);const auto setup=Identifier(request,"setupId");const auto plan=IntentPlan(scene,setup);auto& document=IntentDocument(scene);
    const auto action=String(request,"action");Map edit{{"commandId",command},{"planId",plan}};
    if(request.contains("expectedLayersFingerprint"))edit["expectedLayersFingerprint"]=request.at("expectedLayersFingerprint");
    if(action=="Assign"||action=="Reset")
    {
        const auto assignments=document.QueryLayerAssignments(plan);Array ids;std::set<ID> unique;
        for(const auto& value:request.at("intentIds").To<Array>())
        {const auto id=CutID(value);if(!unique.insert(id).second)throw std::invalid_argument("CAM_CUT_DUPLICATE");
         const auto found=assignments.find(iCAX::Data::to_string(id));if(found==assignments.end())throw std::invalid_argument("CAM_LAYER_SELECTION_SETUP_MISMATCH");
         if(found->second.To<Map>().at("locked").To<bool>())throw std::invalid_argument("CAM_CUT_LOCKED");ids.emplace_back(id);}
        if(ids.empty())throw std::invalid_argument("CAM_LAYER_SELECTION_REQUIRED");edit["kind"]=Text(action=="Assign"?"AssignPathLayers":"ResetPathLayerOverrides");edit["pathIds"]=std::move(ids);edit["expectedPlanFingerprint"]=document.ReadPlanDefinition(plan).at("definitionFingerprint");
        edit["layers"]=Map{{"visibleLayerId",action=="Assign"?Variant(Identifier(request,"layerId")):Variant(true)}};
    }
    else if(action=="Default"){edit["kind"]=Text("SetDefaultLayers");edit["layers"]=Map{{"visibleLayerId",Identifier(request,"layerId")}};}
    else if(action=="Create"||action=="Update"||action=="Delete")
    {
        const auto id=Identifier(request,"layerId");edit["layerId"]=id;edit["kind"]=Text(action=="Create"?"CreateLayer":action=="Update"?"UpdateLayer":"DeleteLayer");
        if(action!="Create"){const auto catalog=document.QueryLayers().at("catalog").To<Map>();const auto found=catalog.find(iCAX::Data::to_string(id));if(found==catalog.end()||String(found->second.To<Map>(),"kind")!="Visible")throw std::invalid_argument("CAM_VISIBLE_LAYER_REQUIRED");}
        if(action!="Delete")
        {const auto properties=request.at("properties").To<Map>();CutFields(properties,action=="Create"?std::initializer_list<const char*>{"name","color"}:std::initializer_list<const char*>{"name","color","visible","locked","order"});edit["properties"]=properties;}
        if(request.contains("targetLayerId"))edit["targetLayerId"]=Identifier(request,"targetLayerId");
    }
    else throw std::invalid_argument("CAM_LAYER_ACTION_INVALID");
    document.Execute(edit);Map result{{"changed",true},{"setupId",request.at("setupId")},{"expected",Map{{"epoch",runtime->Epoch},{"mutationSequence",std::to_string(runtime->Sequence)}}}};
    runtime->Receipts.emplace(key,std::make_pair(payload,result));return result;
}
Map QueryCutLayers(Scene& scene,const Map& request)
{
    CutFields(request,{"expected","setupId"});const auto runtime=Runtime(scene);CheckExpected(*runtime,request);
    const auto setup=Identifier(request,"setupId");auto& document=IntentDocument(scene);Array cuts;
    for(const auto& [id,value]:document.QueryLayerAssignments(IntentPlan(scene,setup))){const auto item=value.To<Map>();cuts.emplace_back(Map{{"id",id},{"layers",item.at("layers")},{"locked",item.at("locked")}});}
    return {{"layers",document.QueryLayers()},{"intentToolpaths",std::move(cuts)},{"expected",Map{{"epoch",runtime->Epoch},{"mutationSequence",std::to_string(runtime->Sequence)}}}};
}
