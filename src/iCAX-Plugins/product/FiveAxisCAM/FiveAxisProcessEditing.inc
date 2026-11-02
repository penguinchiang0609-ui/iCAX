namespace
{
    enum class ProcessEdit {Update,Assign,Pose,Clear,Restore};
    Map EditCutProcesses(Scene& scene,const Map& request,ProcessEdit edit)
    {
        Text phase="validate request";try{
        if(edit==ProcessEdit::Pose)return EditIntentPosePolicies(scene,request);
        if(edit==ProcessEdit::Assign)CutFields(request,{"commandId","expected","intentIds","libraryData","overrides"});
        else if(edit==ProcessEdit::Clear||edit==ProcessEdit::Restore)CutFields(request,{"commandId","expected","intentIds","categories"});
        else CutFields(request,{"commandId","expected","intentIds","patch"});
        const auto runtime=Runtime(scene);const auto commandID=iCAX::Data::to_string(Identifier(request,"commandId"));
        const auto payload=Text("EditCutProcesses:")+std::to_string(static_cast<int>(edit))+":"+iCAX::Data::VariantSerializer::Serialize(request);
        if(const auto prior=runtime->Receipts.find(commandID);prior!=runtime->Receipts.end())
        {if(prior->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return prior->second.second;}
        for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==commandID)throw std::invalid_argument("CAM_COMMAND_REUSED");
        CheckExpected(*runtime,request);CheckCapacity(*runtime);
        const auto ids=request.at("intentIds").To<Array>();if(ids.empty()||ids.size()>(edit==ProcessEdit::Pose&&request.at("patch").To<Map>().contains("reverseAxes")?16384u:256u))throw std::invalid_argument("CAM_INTENT_SELECTION_INVALID");
        Map patch;std::set<Text> categories;
        if(edit==ProcessEdit::Update||edit==ProcessEdit::Pose)
        {
            patch=request.at("patch").To<Map>();if(patch.empty())throw std::invalid_argument("CAM_EMPTY_PATCH");
            if(edit==ProcessEdit::Update)CutFields(patch,{"overrides","kerf","leadIn","leadOut","overcutLengthMm","gapLengthMm","layerKey","start"});
            else CutFields(patch,{"initialization","transitionMetric","leadAngleRad","sideAngleRad","leadDeviationLimitRad","sideDeviationLimitRad","angularToleranceRad","surfaceNormalPreferenceWeight","direction","reverseAxes"});
        }
        if(patch.contains("start")&&ids.size()!=1)throw std::invalid_argument("CAM_INTENT_SELECTION_INVALID");
        if(edit==ProcessEdit::Clear||edit==ProcessEdit::Restore)
        {
            for(const auto& c:request.at("categories").To<Array>())
            {
                const auto category=c.To<Text>();if(category!="Leads"&&category!="Kerf"&&category!="Overcut"&&category!="Gap")throw std::invalid_argument("CAM_PROCESS_CATEGORY_UNSUPPORTED");
                if(!categories.insert(category).second)throw std::invalid_argument("CAM_PROCESS_CATEGORY_DUPLICATE");
            }
            if(categories.empty())throw std::invalid_argument("CAM_PROCESS_CATEGORY_REQUIRED");
        }
        struct Change{ID Intent,Setup;Map Assignment;Map Library,ProcessRef,PoseRef;bool StartChanged=false,ProcessChanged=true,GeometryChanged=false;};
        std::vector<Change> changes;std::set<ID> unique,setups;
        // Complete validation precedes all publication and the single transaction.
        for(const auto& id:ids)
        {
            phase="read assignment";
            Change c;c.Intent=Identifier(Map{{"id",id}},"id");if(!unique.insert(c.Intent).second)throw std::invalid_argument("CAM_INTENT_DUPLICATE");
            const auto feature=ReadIntentCut(scene,c.Intent);
            if(String(feature->Projection.Metadata.Feature,"machiningKind")!="Cut")for(const auto* key:{"leadIn","leadOut"})
                if(patch.contains(key)&&String(patch.at(key).To<Map>(),"kind")!="None")throw std::invalid_argument("CAM_LEADS_REQUIRE_CUTTING_PATH");
            const auto cut=feature;
            RequireCutUnlocked(scene,c.Intent);c.Setup=feature->GetSetupID();
            c.ProcessChanged=edit!=ProcessEdit::Update||patch.size()!=1||!patch.contains("start");
            c.ProcessRef=cut->GetProcessData();c.PoseRef=feature->GetPoseData();
            ValidatePolicyClosure(scene,c.ProcessRef);
            c.Assignment=EngineeringBody(c.ProcessRef,"cam.process-assignment");
            if(feature->GetResultState()!="Current")c.Assignment=IntentAuthoredProcessForRecovery(scene,feature->Path,c.Assignment);
            const auto previousAssignment=c.Assignment;
            c.Library=c.Assignment.at("libraryData").To<Map>();
            if(edit==ProcessEdit::Assign)c.Library=request.at("libraryData").To<Map>();
            ValidatePolicyClosure(scene,c.Library);ExactPolicyDependencies(scene,c.Library,{});
            const auto library=EngineeringBody(c.Library,"cam.process-library");ValidateProcessLibrary(library);ValidateLibraryDefaults(library);
            if(edit==ProcessEdit::Assign)
            {
                if(!library.contains("defaults"))throw std::invalid_argument("CAM_PROCESS_INHERITANCE_UNAVAILABLE");
                c.Assignment=library.at("defaults").To<Map>();c.Assignment["schema"]=Text("cam.process-assignment");c.Assignment["schemaVersion"]=Text("1");
                c.Assignment["libraryData"]=WireRef(StoredRef(c.Library));c.Assignment["overrides"]=request.at("overrides");
            }
            if(edit==ProcessEdit::Update)for(const auto& [key,value]:patch)
            {
                if(key=="start")
                {
                    using namespace iCAX::CAM::Intent;
                    const auto anchor=AnchorFromData(value.To<Map>());const auto plan=IntentPlan(scene,c.Setup);
                    const auto observed=IntentDocument(scene).ReadPath(plan,anchor.Port);(void)IntentAnchor(observed,value.To<Map>());
                    const auto design=IntentDesignPath(scene,ReadIntentPath(scene,c.Intent));
                    (void)IntentDocument(scene).Preview("icax.edit.start",{{"anchor",ToData(anchor)}},{design.Port});c.StartChanged=true;
                }
                else if(key=="kerf")
                {auto kerf=c.Assignment.at(key).To<Map>();for(const auto& [k,v]:value.To<Map>())kerf[k]=v;c.Assignment[key]=kerf;}
                else c.Assignment[key]=value; // Lead policy is an entire tagged value, never a mix of old/new variants.
            }
            if(edit==ProcessEdit::Clear||edit==ProcessEdit::Restore)for(const auto& category:categories)
            {
                const std::vector<Text> keys=category=="Leads"?std::vector<Text>{"leadIn","leadOut"}:std::vector<Text>{category=="Kerf"?"kerf":category=="Gap"?"gapLengthMm":"overcutLengthMm"};
                for(const auto& key:keys)
                {
                    if(edit==ProcessEdit::Restore)
                    {
                        if(!library.contains("defaults")||!library.at("defaults").To<Map>().contains(key))throw std::invalid_argument("CAM_PROCESS_INHERITANCE_UNAVAILABLE");
                        c.Assignment[key]=library.at("defaults").To<Map>().at(key);
                    }
                    else if(key=="kerf"){auto kerf=c.Assignment.at(key).To<Map>();kerf["widthMm"]=0.;c.Assignment[key]=kerf;}
                    else if(key=="overcutLengthMm"||key=="gapLengthMm")c.Assignment[key]=0.;
                    else c.Assignment[key]=Map{{"kind",Text("None")},{"placement",Text("ScrapSide")},{"lengthMm",0.},{"angleRad",0.}};
                }
            }
            phase="validate assignment";ValidateAssignmentValues(c.Assignment,library);
            c.GeometryChanged=c.StartChanged;
            for(const auto* key:{"kerf","leadIn","leadOut","gapLengthMm","overcutLengthMm"})c.GeometryChanged|=c.Assignment.at(key)!=previousAssignment.at(key);
            // Clearing an already empty category must not expire a plan or add
            // a history entry. The receipt still covers every selected intent.
            if(edit==ProcessEdit::Clear)c.ProcessChanged=c.Assignment!=previousAssignment;
            if(c.ProcessChanged||c.StartChanged)setups.insert(c.Setup);
            changes.push_back(std::move(c));
        }
        phase="stage assignment";Array refs;
        for(auto& c:changes)
        {
            if(c.ProcessChanged)c.ProcessRef=EngineeringView(c.Assignment);
            refs.emplace_back(Map{{"intentId",iCAX::Data::to_string(c.Intent)},{"process",WireRef(StoredRef(c.ProcessRef))},{"pose",WireRef(StoredRef(c.PoseRef))}});
        }
        if(!setups.empty())
        {
            using namespace iCAX::CAM::Intent;std::vector<Map> commands;std::set<ID> selected;
            for(const auto& change:changes)selected.insert(change.Intent);
            iCAX::Data::uuid_name_generator names(Identifier(request,"commandId"));std::map<ID,std::pair<ID,Map>> updates;
            // Bind metadata edits to the currently observed results first. The
            // same atomic batch may then invalidate their upstream processes.
            for(const auto& change:changes)
            {
                if(!change.ProcessChanged&&!change.StartChanged)continue;
                phase="read edited path";const auto path=ReadIntentPath(scene,change.Intent);const auto plan=IntentPlan(scene,change.Setup);
                if(path.ResultState!="Current")
                {
                    const auto recovery=BuildIntentProcessRecoveryCommands(scene,path,change.Setup,change.Assignment,names("recover/"+iCAX::Data::to_string(change.Intent)),selected,
                        change.StartChanged?patch.at("start").To<Map>():Map{});
                    commands.insert(commands.end(),recovery.begin(),recovery.end());continue;
                }
                const auto feature=ReadFiveAxisIntentMetadata(path.Value).Feature;
                const auto hole=feature.contains("standardHole")?feature.at("standardHole").To<Map>():Map{};
                const bool manualHole=hole.contains("manual")&&hole.at("manual").To<bool>();
                if(change.GeometryChanged&&(manualHole||IntentExistingBranchProcess(scene,path.Port)||IntentDesignPath(scene,path).Port==path.Port))
                {
                    phase="rebuild local branch";
                    const auto branch=BuildIntentBranchProcessCommands(scene,path,change.Assignment,names("branch/"+iCAX::Data::to_string(change.Intent)),{},false,
                        change.StartChanged?patch.at("start").To<Map>():Map{});
                    commands.insert(commands.end(),branch.begin(),branch.end());continue;
                }
                if(change.ProcessChanged)
                {
                    phase="append assignment";
                    auto edit=AppendIntentPathOperation(scene,path,"fiveaxis.metadata",{{"process",change.Assignment}},names("assignment/"+iCAX::Data::to_string(change.Intent)));
                    commands.insert(commands.end(),edit.begin(),edit.end());
                }
                if(!change.GeometryChanged)continue;
                for(const auto& type:std::vector<Text>{"fiveaxis.kerf","fiveaxis.trace","fiveaxis.leads"})
                {
                    phase="locate "+type;
                    const auto operation=IntentProcessingOperation(scene,path.Port,type);const auto definition=IntentDocument(scene).ReadOperation(operation);
                    const auto previous=definition.at("parameters").To<Map>();auto parameters=previous;
                    if(type=="fiveaxis.kerf")parameters["policy"]=change.Assignment.at("kerf");
                    else if(type=="fiveaxis.trace")
                    {parameters["gapLengthMm"]=change.Assignment.at("gapLengthMm");parameters["overcutLengthMm"]=change.Assignment.at("overcutLengthMm");if(change.StartChanged){parameters["start"]=patch.at("start");parameters["startLocked"]=true;}}
                    else {parameters["leadIn"]=change.Assignment.at("leadIn");parameters["leadOut"]=change.Assignment.at("leadOut");}
                    if(parameters==previous)continue;RequireIntentOperationEditScope(scene,operation,selected);
                    if(const auto already=updates.find(operation);already!=updates.end()&&already->second.second!=parameters)throw std::invalid_argument("CAM_INTENT_SHARED_OPERATION_POLICY_CONFLICT");
                    updates[operation]={plan,std::move(parameters)};
                }
            }
            for(const auto& [operation,update]:updates)commands.push_back(IntentOperationUpdate(scene,operation,update.first,names("process/"+iCAX::Data::to_string(operation)),update.second));
            std::map<ID,Map> stagedUpdates;std::vector<Map> uniqueCommands;
            for(const auto& command:commands)
            {
                if(String(command,"kind")=="UpdateOperation")
                {
                    const auto operation=command.at("operationId").Is<ID>()?command.at("operationId").To<ID>():Identifier(command,"operationId");
                    const auto parameters=command.at("parameters").To<Map>();
                    if(const auto prior=stagedUpdates.find(operation);prior!=stagedUpdates.end())
                    {if(prior->second!=parameters)throw std::invalid_argument("CAM_INTENT_SHARED_OPERATION_POLICY_CONFLICT");continue;}
                    stagedUpdates.emplace(operation,parameters);
                }
                uniqueCommands.push_back(command);
            }
            phase="commit process graph";CommitIntentCommands(scene,uniqueCommands,setups,edit==ProcessEdit::Clear?"清除切割工艺":"修改切割工艺");
        }
        phase="project process result";Array items;for(const auto& change:changes)items.emplace_back(IntentPathView(scene,ReadIntentPath(scene,change.Intent)));
        Map result{{"assignments",refs},{"items",items}};runtime->Receipts.emplace(commandID,std::make_pair(payload,result));return result;
        }catch(const std::out_of_range&){throw std::invalid_argument("CAM_PROCESS_FIELD_MISSING: "+phase);}
    }
}
Map UpdateCutProcess(Scene& scene,const Map& request){return EditCutProcesses(scene,request,ProcessEdit::Update);}
Map AssignCutProcess(Scene& scene,const Map& request){return EditCutProcesses(scene,request,ProcessEdit::Assign);}
Map UpdateCutPose(Scene& scene,const Map& request){return EditCutProcesses(scene,request,ProcessEdit::Pose);}
Map ClearCutProcess(Scene& scene,const Map& request){return EditCutProcesses(scene,request,ProcessEdit::Clear);}
Map RestoreCutProcess(Scene& scene,const Map& request){return EditCutProcesses(scene,request,ProcessEdit::Restore);}
