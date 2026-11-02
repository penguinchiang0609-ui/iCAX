namespace
{
    std::optional<ID> IntentPrimaryDefinitionOfType(Scene& scene,iCAX::CAM::Intent::SPortRef port,const Text& requested)
    {
        using namespace iCAX::CAM::Intent;std::set<ID> visited;
        while(visited.insert(port.OperationId).second)
        {
            const auto& definition=IntentOperationDefinition(scene,port.OperationId);if(String(definition,"typeId")==requested)return port.OperationId;
            std::vector<SPortRef> replacing;for(const auto& item:definition.at("inputs").To<Array>())
            {const auto binding=item.To<Map>();if(binding.at("replace").To<bool>())replacing.push_back(PortFromData(binding.at("source").To<Map>()));}
            if(replacing.size()!=1)return {};port=replacing.front();
        }
        throw std::invalid_argument("CAM_INTENT_BRANCH_GRAPH_CYCLE");
    }
    Map IntentAuthoredProcessForRecovery(Scene& scene,const iCAX::CAM::Intent::SResolvedPath& path,Map fallback)
    {
        if(const auto metadata=IntentPrimaryDefinitionOfType(scene,path.Port,"fiveaxis.metadata"))
        {const auto parameters=IntentDocument(scene).ReadOperation(*metadata).at("parameters").To<Map>();if(parameters.contains("process"))return parameters.at("process").To<Map>();}
        return fallback;
    }
    std::vector<Map> BuildIntentProcessRecoveryCommands(Scene& scene,const iCAX::CAM::Intent::SResolvedPath& path,const ID& setup,
        const Map& assignment,const ID& command,const std::set<ID>& selected,const Map& start={})
    {
        const auto metadata=IntentPrimaryDefinitionOfType(scene,path.Port,"fiveaxis.metadata");
        const auto plan=IntentPlan(scene,setup);iCAX::Data::uuid_name_generator names(command);std::vector<Map> commands;
        if(metadata)
        {
            auto fields=IntentDocument(scene).ReadOperation(*metadata).at("parameters").To<Map>();fields["process"]=assignment;
            RequireIntentOperationEditScope(scene,*metadata,selected);commands.push_back(IntentOperationUpdate(scene,*metadata,plan,names("metadata"),fields));
        }
        else
        {
            using namespace iCAX::CAM::Intent;const auto source=IntentSourceOperation(scene,path.Port);RequireIntentOperationEditScope(scene,source,selected);
            const auto definition=IntentDocument(scene).ReadOperation(source);auto value=ToolpathFromData(definition.at("parameters").To<Map>().at("value").To<Map>());
            auto sourceMetadata=FiveAxisIntentMetadataFromChannel(value.Channels.at("fiveaxis").To<Map>());sourceMetadata.Process=assignment;++sourceMetadata.InputRevision;
            value.Channels["fiveaxis"]=ToFiveAxisIntentChannel(sourceMetadata);
            commands.push_back({{"kind",Text("UpdateSource")},{"commandId",names("source-metadata")},{"planId",plan},{"operationId",source},
                {"expectedOperationFingerprint",definition.at("definitionFingerprint")},{"value",ToData(value)}});
        }
        if(const auto branch=IntentPrimaryDefinitionOfType(scene,path.Port,"fiveaxis.branch.process"))
        {
            RequireIntentOperationEditScope(scene,*branch,selected);auto parameters=IntentDocument(scene).ReadOperation(*branch).at("parameters").To<Map>();
            parameters["kerf"]=assignment.at("kerf");for(const auto* key:{"gapLengthMm","overcutLengthMm","leadIn","leadOut"})parameters[key]=assignment.at(key);
            if(!start.empty()){parameters["start"]=start;parameters["startLocked"]=true;}commands.push_back(IntentOperationUpdate(scene,*branch,plan,names("branch"),parameters));return commands;
        }
        for(const auto& type:std::vector<Text>{"fiveaxis.kerf","fiveaxis.trace","fiveaxis.leads"})
        {
            const auto operation=IntentProcessingOperation(scene,path.Port,type);RequireIntentOperationEditScope(scene,operation,selected);
            auto parameters=IntentDocument(scene).ReadOperation(operation).at("parameters").To<Map>();
            if(type=="fiveaxis.kerf")parameters["policy"]=assignment.at("kerf");
            else if(type=="fiveaxis.trace"){parameters["gapLengthMm"]=assignment.at("gapLengthMm");parameters["overcutLengthMm"]=assignment.at("overcutLengthMm");if(!start.empty()){parameters["start"]=start;parameters["startLocked"]=true;}}
            else {parameters["leadIn"]=assignment.at("leadIn");parameters["leadOut"]=assignment.at("leadOut");}
            commands.push_back(IntentOperationUpdate(scene,operation,plan,names(type),parameters));
        }
        return commands;
    }
    std::optional<ID> IntentExistingBranchProcess(Scene& scene,iCAX::CAM::Intent::SPortRef port)
    {
        using namespace iCAX::CAM::Intent;std::set<ID> visited;
        while(visited.insert(port.OperationId).second)
        {
            const auto& definition=IntentOperationDefinition(scene,port.OperationId);
            if(String(definition,"typeId")=="fiveaxis.branch.process")return port.OperationId;
            const auto inputs=definition.at("inputs").To<Array>();std::vector<SPortRef> replacing;
            for(const auto& item:inputs){const auto binding=item.To<Map>();if(binding.at("replace").To<bool>())replacing.push_back(PortFromData(binding.at("source").To<Map>()));}
            if(replacing.size()!=1)return {};port=replacing.front();
        }
        throw std::invalid_argument("CAM_INTENT_BRANCH_GRAPH_CYCLE");
    }
    iCAX::CAM::Intent::SResolvedPath IntentBranchNominalInput(Scene& scene,const iCAX::CAM::Intent::SResolvedPath& path)
    {
        using namespace iCAX::CAM::Intent;const auto metadata=ReadFiveAxisIntentMetadata(path.Value);const auto plan=IntentPlan(scene,metadata.SetupID);
        if(const auto merge=IntentPrimaryDefinitionOfType(scene,path.Port,"fiveaxis.merge"))
        {
            auto nominal=ReadIntentPort(scene,plan,DeclaredToolpathPort(*merge,"main"));
            if(nominal.Value.Geometry.Meaning=="NominalBoundary")return nominal;
        }
        const auto branch=IntentExistingBranchProcess(scene,path.Port);
        const auto operation=branch?*branch:IntentProcessingOperation(scene,path.Port,"fiveaxis.kerf");
        const auto& definition=IntentOperationDefinition(scene,operation);const auto inputs=definition.at("inputs").To<Array>();
        const auto index=branch?1u:0u;if(inputs.size()<=index)throw std::invalid_argument("CAM_INTENT_BRANCH_NOMINAL_INPUT_MISSING");
        return ReadIntentPort(scene,plan,PortFromData(inputs[index].To<Map>().at("source").To<Map>()));
    }
    bool IntentBranchClosed(Scene& scene,const iCAX::CAM::Intent::SResolvedPath& path,const iCAX::CAM::Intent::SResolvedPath& nominal)
    {
        using namespace iCAX::CAM::Intent;auto port=path.Port;std::set<ID> visited;
        const std::set<Text> transparent{"fiveaxis.pose","fiveaxis.pose.policy","fiveaxis.pose.delta","fiveaxis.pose.remove","fiveaxis.pose.reverse",
            "fiveaxis.metadata","fiveaxis.reverse","icax.edit.reverse","icax.edit.start","icax.edit.channels","fiveaxis.trace","fiveaxis.leads"};
        while(visited.insert(port.OperationId).second)
        {
            const auto& definition=IntentOperationDefinition(scene,port.OperationId);const auto type=String(definition,"typeId");
            if(type=="fiveaxis.branch.process")return definition.at("parameters").To<Map>().at("closed").To<bool>();
            if(type=="fiveaxis.kerf")return nominal.Value.Geometry.Closed;
            if(!transparent.contains(type))return path.Value.Geometry.Closed;
            const auto inputs=definition.at("inputs").To<Array>();if(inputs.size()!=1)return path.Value.Geometry.Closed;
            port=PortFromData(inputs.front().To<Map>().at("source").To<Map>());
        }
        throw std::invalid_argument("CAM_INTENT_BRANCH_GRAPH_CYCLE");
    }
    std::vector<Map> BuildIntentBranchProcessCommands(Scene& scene,const iCAX::CAM::Intent::SResolvedPath& path,
        const Map& assignment,const ID& command,const Map& posePolicy={},bool clearPoseControls=false,const Map& start={})
    {
        using namespace iCAX::CAM::Intent;
        if(path.ResultState!="Current")throw std::invalid_argument("CAM_INTENT_PATH_NOT_CURRENT");
        const auto metadata=ReadFiveAxisIntentMetadata(path.Value);
        const auto hole=metadata.Feature.contains("standardHole")?metadata.Feature.at("standardHole").To<Map>():Map{};
        const bool manualHole=hole.contains("manual")&&hole.at("manual").To<bool>();
        if(!manualHole&&!IntentExistingBranchProcess(scene,path.Port)&&IntentDesignPath(scene,path).Port!=path.Port)
            throw std::invalid_argument("CAM_INTENT_BRANCH_REQUIRES_LOCAL_RETAINED_SELECTION");
        const auto plan=IntentPlan(scene,metadata.SetupID);iCAX::Data::uuid_name_generator names(command);
        const auto nominal=IntentBranchNominalInput(scene,path);
        Map parameters{{"kerf",assignment.at("kerf")},{"gapLengthMm",assignment.at("gapLengthMm")},{"overcutLengthMm",assignment.at("overcutLengthMm")},
            {"leadIn",assignment.at("leadIn")},{"leadOut",assignment.at("leadOut")},{"closed",IntentBranchClosed(scene,path,nominal)}};
        if(!posePolicy.empty()){parameters["posePolicy"]=posePolicy;parameters["clearPoseControls"]=clearPoseControls;}
        if(!start.empty()){parameters["start"]=start;parameters["startLocked"]=true;}
        auto existing=IntentExistingBranchProcess(scene,path.Port);
        if(existing)
        {
            try{RequireIntentOperationEditScope(scene,*existing,{path.PathId});}
            catch(const std::invalid_argument& error){if(Text(error.what())!="CAM_INTENT_SHARED_OPERATION_SELECT_ALL_RESULTS")throw;existing.reset();}
        }
        if(existing)
        {
            const auto previous=IntentDocument(scene).ReadOperation(*existing).at("parameters").To<Map>();
            if(posePolicy.empty())for(const auto* key:{"posePolicy","clearPoseControls"})if(previous.contains(key))parameters[key]=previous.at(key);
            if(start.empty()&&previous.contains("start")){parameters["start"]=previous.at("start");if(previous.contains("startLocked"))parameters["startLocked"]=previous.at("startLocked");}
            auto commands=AppendIntentPathOperation(scene,path,"fiveaxis.metadata",{{"process",assignment}},names("metadata"));
            commands.push_back(IntentOperationUpdate(scene,*existing,plan,names("update"),std::move(parameters)));return commands;
        }
        std::vector<Map> commands;
        if(!path.OwnParticipating)
        {
            if(path.ParticipationReason!="Disabled")throw std::invalid_argument("CAM_INTENT_DELETED_PATH_REQUIRES_RESTORE");
            commands.push_back({{"kind",Text("SetParticipation")},{"commandId",names("restore")},{"planId",plan},{"enabled",true},{"targets",Array{ToData(path.Port)}},
                {"expectedInputs",Map{{ToolpathPortKey(path.Port),path.Fingerprint}}}});
        }
        commands.push_back({{"kind",Text("AddOperation")},{"commandId",command},{"operationId",command},{"planId",plan},{"typeId",Text("fiveaxis.branch.process")},
            {"parameters",parameters},{"context",Map{{"sourceOperationId",IntentSourceOperation(scene,path.Port)}}},
            {"inputs",Array{ToData(SOperationInput{"selection",path.Port,true,{}}),ToData(SOperationInput{"nominal",nominal.Port,false,{}})}},
            {"expectedInputs",Map{{ToolpathPortKey(path.Port),path.Fingerprint},{ToolpathPortKey(nominal.Port),nominal.Fingerprint}}}});
        const auto metadataCommand=names("metadata");
        commands.push_back({{"kind",Text("AddOperation")},{"commandId",metadataCommand},{"operationId",metadataCommand},{"planId",plan},{"typeId",Text("fiveaxis.metadata")},
            {"parameters",Map{{"process",assignment}}},{"inputs",Array{Map{{"slot",Text("input/0")},{"source",Map{{"fromCommand",command},{"field",Text("port")}}},{"replace",true},{"ranges",Array{}}}}}});
        if(!path.OwnParticipating)commands.push_back({{"kind",Text("SetParticipation")},{"commandId",names("retain-disabled")},{"planId",plan},{"enabled",false},
            {"targets",Array{Map{{"fromCommand",metadataCommand},{"field",Text("port")}}}}});
        return commands;
    }
}
