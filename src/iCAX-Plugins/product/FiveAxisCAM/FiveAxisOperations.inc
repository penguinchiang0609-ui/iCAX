// A machining operation reuses the persisted Setup aggregate: one reference
// to shared CAD, its own placement, fixtures, intent plan and machine structure.
namespace {
    constexpr const char* MachineTypeFeature="machine-type-configurations";
    Text MachineTypeName(const Text& type){
        if(type=="HeadHead")return "双摆头";
        if(type=="HeadTable")return "4+1 · 单摆头＋单转台";
        if(type=="TableTable")return "3+2 · 固定头＋双转台";
        if(type.empty())return {};
        throw std::invalid_argument("CAM_MACHINE_TYPE_INVALID: 请选择双摆头、4+1 或 3+2。");
    }
    Text RequiredOperationMachineType(const Map& request){
        if(!request.contains("machineType"))throw std::invalid_argument("CAM_MACHINE_TYPE_REQUIRED: 新建工序时必须选择机型。");
        const auto type=String(request,"machineType");if(type.empty())throw std::invalid_argument("CAM_MACHINE_TYPE_REQUIRED: 新建工序时必须选择机型。");
        (void)MachineTypeName(type);return type;
    }
    Map MachineTypeDeclaration(const Map& value){
        if(value.empty())return {{"mode",Text("Inherit")}};
        const auto mode=String(value,"mode");
        if(mode=="Inherit")return {{"mode",mode}};
        if(mode!="Type")throw std::invalid_argument("CAM_MACHINE_TYPE_ASSIGNMENT_INVALID");
        const auto type=String(value,"machineType");if(type.empty())throw std::invalid_argument("CAM_MACHINE_TYPE_REQUIRED");
        (void)MachineTypeName(type);return {{"mode",mode},{"machineType",type}};
    }
    Map MachineTypeSummary(const Map& declaration){
        auto result=MachineTypeDeclaration(declaration);const auto type=result.contains("machineType")?String(result,"machineType"):Text();
        result["machineType"]=type;result["name"]=MachineTypeName(type);return result;
    }
    struct MachineTypeScopeState {
        ID Job;
        Map Project,Product;
        std::map<ID,Map> Workpieces,Setups;
        std::map<ID,ID> Owners;
    };
    MachineTypeScopeState ReadMachineTypeScopeState(Scene& scene){
        MachineTypeScopeState result;result.Job=scene.Database().GetMetaEntity()->GetID();
        const auto job=Require<CFiveAxisJobComponent>(scene.Database(),result.Job);
        result.Project=MachineTypeDeclaration(job->GetMachineTypeAssignment());result.Product=MachineTypeDeclaration(job->GetProductMachineTypeDefault());
        for(const auto& entity:scene.Database().FilterEntities([](auto){return true;})){
            if(const auto workpiece=Component<CFiveAxisWorkpieceComponent>(entity))result.Workpieces[entity->GetID()]=MachineTypeDeclaration(workpiece->GetMachineTypeAssignment());
            if(const auto setup=Component<CFiveAxisSetupComponent>(entity);setup&&setup->GetJobID()==result.Job)result.Setups[entity->GetID()]=MachineTypeDeclaration(setup->GetMachineTypeAssignment());
            if(const auto placement=Component<CFiveAxisPlacementComponent>(entity);placement&&!placement->GetSetupID().is_nil()){
                if(result.Owners.contains(placement->GetSetupID()))throw std::invalid_argument("CAM_OPERATION_SINGLE_WORKPIECE_REQUIRED");
                result.Owners[placement->GetSetupID()]=placement->GetWorkpieceID();
            }
        }return result;
    }
    Map ResolveMachineType(const MachineTypeScopeState& state,const ID& setupID){
        const auto owner=state.Owners.contains(setupID)?state.Owners.at(setupID):ID();
        const std::array<std::pair<Text,Map>,4> levels{{
            {"Setup",state.Setups.at(setupID)},
            {"Workpiece",state.Workpieces.contains(owner)?state.Workpieces.at(owner):Map{}},
            {"Project",state.Project},{"Product",state.Product}}};
        for(const auto& [scope,value]:levels){const auto declaration=MachineTypeDeclaration(value);if(String(declaration,"mode")=="Inherit")continue;auto result=MachineTypeSummary(declaration);result["source"]=scope;return result;}
        auto result=MachineTypeSummary({});result["source"]=Text("None");return result;
    }
    Text EffectiveMachineTypeValue(Scene& scene,const ID& setupID){
        const auto state=ReadMachineTypeScopeState(scene);return String(ResolveMachineType(state,setupID),"machineType");
    }
    std::optional<LibraryRecord> ProductMachineTypePreferences(Scene& scene){
        const auto store=Runtime(scene)->FixtureLibraryStore;return store?store->Get(MachineTypeFeature,"preferences","icax.five-axis-cam"):std::nullopt;
    }
    void CaptureInitialProductMachineTypeDefault(Scene& scene){
        const auto runtime=Runtime(scene);if(!runtime->CaptureInitialMachineTypeDefault||!runtime->FixtureLibraryStore)return;
        const auto jobID=scene.Database().GetMetaEntity()->GetID();const auto job=Component<CFiveAxisJobComponent>(scene.Database().GetEntity(jobID));if(!job)return;
        const auto preferences=ProductMachineTypePreferences(scene);
        if(preferences){auto snapshot=MachineTypeDeclaration(preferences->Payload.To<Map>());snapshot["preferencesRevision"]=std::to_string(preferences->Revision);
            Transaction(scene.Database(),"读取新工程机型默认",[&](auto& tx){tx.ModifyComponent(jobID,CFiveAxisJobComponent::S_ClassName,{{"ProductMachineTypeDefault",snapshot},{"InputRevision",Next(job->GetInputRevision())}});},false);}
        runtime->CaptureInitialMachineTypeDefault=false;
    }
    void PutMachineTypeDeclaration(MachineTypeScopeState& state,const ID& setupID,const Text& scope,const Map& declaration){
        if(scope=="Setup")state.Setups[setupID]=declaration;
        else if(scope=="Project")state.Project=declaration;
        else if(scope=="Workpiece"){
            if(!state.Owners.contains(setupID)||!state.Workpieces.contains(state.Owners.at(setupID)))throw std::invalid_argument("CAM_OPERATION_WORKPIECE_REQUIRED");
            state.Workpieces[state.Owners.at(setupID)]=declaration;
        }else throw std::invalid_argument("CAM_MACHINE_TYPE_SCOPE_REQUIRED");
    }
    ID OperationPlacement(Scene& scene,const ID& setupID){
        ID result;for(const auto& entity:scene.Database().FilterEntities([](auto){return true;}))if(const auto p=Component<CFiveAxisPlacementComponent>(entity);p&&p->GetSetupID()==setupID){
            if(!result.is_nil())throw std::invalid_argument("CAM_OPERATION_SINGLE_WORKPIECE_REQUIRED");result=entity->GetID();
        }if(result.is_nil())throw std::invalid_argument("CAM_OPERATION_WORKPIECE_REQUIRED");return result;
    }
    void AttachOperationPlacement(iCAX::Database::ITransaction& tx,const ID& placementID,const ID& setupID,const ID& workpieceID,const CFiveAxisWorkpieceComponent& workpiece,const Array& transform){
        tx.CreateEntity(placementID);tx.AttachComponent(placementID,CFiveAxisPlacementComponent::S_ClassName,{{"SetupID",setupID},{"WorkpieceID",workpieceID},{"Transform",transform}});
        tx.AttachComponent(placementID,iCAX::Transform::CTransformComponent::S_ClassName,{});
        const auto preview=ReadWireRef(WireRef(workpiece.GetPreviewRef()));
        tx.AttachComponent(placementID,iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName,{{"GeometryResourceID",preview.URL},{"GeometryResourceVersion",static_cast<UInt>(preview.nVersion)}});
    }
    void EnsureIndependentWorkpiecePlacements(Scene& scene){
        std::vector<ID> workpieces;std::set<ID> sources;std::vector<std::pair<ID,ID>> missing;
        for(const auto& entity:scene.Database().FilterEntities([](auto){return true;})){
            if(Component<CFiveAxisWorkpieceComponent>(entity))workpieces.push_back(entity->GetID());
            if(const auto placement=Component<CFiveAxisPlacementComponent>(entity);placement&&placement->GetSetupID().is_nil())sources.insert(placement->GetWorkpieceID());
        }
        for(const auto& workpieceID:workpieces)if(!sources.contains(workpieceID))missing.emplace_back(workpieceID,iCAX::Data::GenerateNewUUID());
        if(missing.empty())return;
        Transaction(scene.Database(),"准备独立工件视图",[&](auto& tx){for(const auto& [workpieceID,placementID]:missing)AttachOperationPlacement(tx,placementID,ID(),workpieceID,*Require<CFiveAxisWorkpieceComponent>(scene.Database(),workpieceID),Identity());},false);
        for(const auto& [_,placementID]:missing)ProjectPlacement(scene,placementID,Identity());
    }
}

Map ReadMachineTypeAssignments(Scene& scene,const Map& request){
    CutFields(request,{"setupId"});const auto setupID=Identifier(request,"setupId");Require<CFiveAxisSetupComponent>(scene.Database(),setupID);
    const auto state=ReadMachineTypeScopeState(scene);const auto owner=state.Owners.contains(setupID)?state.Owners.at(setupID):ID();const auto preferences=ProductMachineTypePreferences(scene);
    auto product=MachineTypeSummary(preferences?preferences->Payload.To<Map>():Map{});product["revision"]=std::to_string(preferences?preferences->Revision:0);
    return {{"setupId",request.at("setupId")},{"operationId",request.at("setupId")},{"workpieceId",OptionalID(owner)},
        {"scopes",Map{{"Setup",MachineTypeSummary(state.Setups.at(setupID))},{"Workpiece",MachineTypeSummary(state.Workpieces.contains(owner)?state.Workpieces.at(owner):Map{})},{"Project",MachineTypeSummary(state.Project)},{"Product",product}}},
        {"effective",ResolveMachineType(state,setupID)},{"productSnapshot",MachineTypeSummary(state.Product)},{"productPreferencesRevision",product.at("revision")}};
}

Map AssignMachineType(Scene& scene,const Map& request){
    CutFields(request,{"commandId","expected","setupId","scope","action","machineType","preferencesRevision"});
    const auto runtime=Runtime(scene);const auto command=iCAX::Data::to_string(Identifier(request,"commandId"));const auto fingerprint="MachineTypeAssignment:"+iCAX::Data::VariantSerializer::Serialize(request);
    if(const auto receipt=runtime->Receipts.find(command);receipt!=runtime->Receipts.end()){if(receipt->second.first!=fingerprint)throw std::invalid_argument("CAM_COMMAND_REUSED");return receipt->second.second;}
    CheckExpected(*runtime,request);CheckCapacity(*runtime);SetupIdle(scene);
    const auto setupID=Identifier(request,"setupId");Require<CFiveAxisSetupComponent>(scene.Database(),setupID);auto state=ReadMachineTypeScopeState(scene);
    const auto scope=String(request,"scope"),action=String(request,"action");
    if(scope!="Setup"&&scope!="Workpiece"&&scope!="Project"&&scope!="Product")throw std::invalid_argument("CAM_MACHINE_TYPE_SCOPE_REQUIRED");
    if(action!="set"&&action!="inherit")throw std::invalid_argument("CAM_MACHINE_TYPE_ACTION_REQUIRED");
    if(action=="inherit"&&request.contains("machineType"))throw std::invalid_argument("CAM_MACHINE_TYPE_SELECTOR_INVALID");
    const auto declaration=MachineTypeDeclaration(action=="inherit"?Map{}:Map{{"mode",Text("Type")},{"machineType",String(request,"machineType")}});
    if(scope=="Setup"&&String(state.Setups.at(setupID),"mode")=="Type"){
        if(action=="inherit"||state.Setups.at(setupID).at("machineType")!=declaration.at("machineType"))throw std::invalid_argument("CAM_OPERATION_MACHINE_TYPE_FIXED: 工序机型在创建后固定，请新建对应机型的工序。");
        auto result=ReadMachineTypeAssignments(scene,{{"setupId",request.at("setupId")}});result["scope"]=scope;result["changed"]=false;runtime->Receipts.emplace(command,std::make_pair(fingerprint,result));return result;
    }
    if(scope=="Product"){
        const auto revision=Integer(request,"preferencesRevision");const auto current=ProductMachineTypePreferences(scene);
        if(revision!=(current?current->Revision:0))throw std::invalid_argument("CAM_MACHINE_TYPE_DEFAULT_REVISION_CONFLICT: 产品默认已变化，请刷新后重试。");
        LibraryRecord record;record.FeatureID=MachineTypeFeature;record.RecordType="preferences";record.SubjectType="product";record.SubjectID="icax.five-axis-cam";record.RecordID="icax.five-axis-cam";record.Payload=declaration;FixtureStore(scene).Put(record,revision);
    }else{
        if(request.contains("preferencesRevision"))throw std::invalid_argument("CAM_MACHINE_TYPE_SELECTOR_INVALID");
        if(scope=="Setup")RequireSetupUnlocked(scene,setupID);
        if(scope=="Workpiece"){
            if(!state.Owners.contains(setupID))throw std::invalid_argument("CAM_OPERATION_WORKPIECE_REQUIRED");
            if(Require<CFiveAxisWorkpieceComponent>(scene.Database(),state.Owners.at(setupID))->GetLocked())throw std::invalid_argument("CAM_WORKPIECE_LOCKED");
        }
        const auto before=state;PutMachineTypeDeclaration(state,setupID,scope,declaration);std::vector<ID> changed;
        for(const auto& [id,_]:state.Setups)if(ResolveMachineType(before,id).at("machineType")!=ResolveMachineType(state,id).at("machineType")){RequireSetupUnlocked(scene,id);changed.push_back(id);}
        const auto job=Require<CFiveAxisJobComponent>(scene.Database(),state.Job);
        Transaction(scene.Database(),"设置工序机型",[&](auto& tx){
            iCAX::Data::PropertySet jobChanges{{"InputRevision",Next(job->GetInputRevision())}};
            if(scope=="Project")jobChanges["MachineTypeAssignment"]=declaration;
            if(scope=="Setup")tx.ModifyComponent(setupID,CFiveAxisSetupComponent::S_ClassName,{{"MachineTypeAssignment",declaration}});
            if(scope=="Workpiece")tx.ModifyComponent(state.Owners.at(setupID),CFiveAxisWorkpieceComponent::S_ClassName,{{"MachineTypeAssignment",declaration}});
            for(const auto& id:changed){const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),id);tx.ModifyComponent(id,CFiveAxisSetupComponent::S_ClassName,{{"PlanValidity",Text(setup->GetLastSuccessfulPlanRef().empty()?"Missing":"Stale")}});}
            tx.ModifyComponent(state.Job,CFiveAxisJobComponent::S_ClassName,jobChanges);
        });
    }
    auto result=ReadMachineTypeAssignments(scene,{{"setupId",request.at("setupId")}});result["scope"]=scope;result["changed"]=true;runtime->Receipts.emplace(command,std::make_pair(fingerprint,result));return result;
}

Map ManageOperations(Scene& scene,const Map& request){
    CutFields(request,{"commandId","expected","action","workpieceId","setupId","name","machineType"});
    const auto runtime=Runtime(scene);const auto commandID=iCAX::Data::to_string(Identifier(request,"commandId"));const auto fingerprint="ManageOperations:"+iCAX::Data::VariantSerializer::Serialize(request);
    if(const auto receipt=runtime->Receipts.find(commandID);receipt!=runtime->Receipts.end()){if(receipt->second.first!=fingerprint)throw std::invalid_argument("CAM_COMMAND_REUSED");return receipt->second.second;}
    CheckExpected(*runtime,request);CheckCapacity(*runtime);SetupIdle(scene);
    auto& db=scene.Database();const auto workpieceID=Identifier(request,"workpieceId");const auto workpiece=Require<CFiveAxisWorkpieceComponent>(db,workpieceID);
    if(!workpiece->GetEnabled())throw std::invalid_argument("CAM_WORKPIECE_DISABLED");
    const auto action=String(request,"action");const auto jobID=db.GetMetaEntity()->GetID();ID setupID,placementID;
    if(action=="create"){
        if(request.contains("setupId"))throw std::invalid_argument("CAM_OPERATION_CREATE_SELECTOR_INVALID");
        const auto machineType=RequiredOperationMachineType(request);
        auto name=String(request,"name");if(name.empty()||name.size()>512||name.find_first_not_of(" \t\r\n")==Text::npos||std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32||c==127;}))throw std::invalid_argument("CAM_OPERATION_NAME_REQUIRED");
        setupID=iCAX::Data::GenerateNewUUID();placementID=iCAX::Data::GenerateNewUUID();const auto nextPlan=iCAX::Data::GenerateNewUUID();const auto sequence=NextSetupSequence(scene);
        const auto profile=StoredRef(DefaultPlanningProfile(scene));const auto head=PrepareOperationHead(scene,workpieceID,machineType);const auto headID=String(head,"instanceId").empty()?ID():Identifier(head,"instanceId");const auto defaultFixtures=PrepareDefaultFixtures(scene);
        Array transform=Identity();for(const auto& entity:db.FilterEntities([](auto){return true;}))if(const auto source=Component<CFiveAxisPlacementComponent>(entity);source&&source->GetWorkpieceID()==workpieceID&&source->GetSetupID().is_nil()){transform=source->GetTransform();break;}
        const std::vector<Map> commands{{{"kind",Text("CreatePlan")},{"commandId",Identifier(request,"commandId")},{"planId",nextPlan},{"name",name}}};
        CommitIntentHostCommands(scene,commands,[&](auto& tx,const auto&){
            tx.CreateEntity(setupID);tx.AttachComponent(setupID,CFiveAxisSetupComponent::S_ClassName,{{"JobID",jobID},{"Name",name},{"Sequence",sequence},{"IntentPlanID",nextPlan},{"PlanningProfileData",profile},{"HeadInstanceID",headID},{"HeadAssignment",head.at("assignment")},{"MachineTypeAssignment",Map{{"mode",Text("Type")},{"machineType",machineType}}}});
            AttachOperationPlacement(tx,placementID,setupID,workpieceID,*workpiece,transform);
            AttachLibraryFixtures(tx,setupID,defaultFixtures);
            tx.ModifyComponent(jobID,CFiveAxisJobComponent::S_ClassName,{{"HeadInstances",head.at("instances")},{"DefaultSetupID",setupID},{"ActiveSetupID",setupID},{"DefaultPlacementID",placementID},{"ActivePlacementID",placementID}});
        },"新增工序",{setupID});ProjectPlacement(scene,placementID,transform);
    }else if(action=="reference"){
        if(request.contains("machineType"))throw std::invalid_argument("CAM_OPERATION_MACHINE_TYPE_FIXED: 更换引用工件不能改变工序机型。");
        setupID=Identifier(request,"setupId");RequireSetupUnlocked(scene,setupID);placementID=OperationPlacement(scene,setupID);
        const auto placement=Require<CFiveAxisPlacementComponent>(db,placementID);const auto setup=Require<CFiveAxisSetupComponent>(db,setupID);
        if(placement->GetWorkpieceID()!=workpieceID){
            if(!setup->GetMachineSetup().empty()||!setup->GetFixtureDesign().empty()||!setup->GetRegistrationPlan().empty()||!setup->GetGeneratedToolpaths().empty()||!setup->GetGeneratedRapidPaths().empty())throw std::invalid_argument("CAM_OPERATION_REFERENCE_IN_USE: 已设置定位、工装或刀路的工序不能直接更换工件，请新建工序。");
            for(const auto& cut:IntentCuts(scene))if(cut->GetSetupID()==setupID)throw std::invalid_argument("CAM_OPERATION_REFERENCE_IN_USE: 已有刀路的工序不能直接更换工件，请新建工序。");
            for(const auto& entity:db.FilterEntities([](auto){return true;})){
                const auto fixture=Component<CFiveAxisFixtureComponent>(entity);const auto unit=Component<CFiveAxisFixtureUnitComponent>(entity);
                if((fixture&&fixture->GetSetupID()==setupID)||(unit&&unit->GetSetupID()==setupID))throw std::invalid_argument("CAM_OPERATION_REFERENCE_IN_USE: 已有工装的工序不能直接更换工件，请新建工序。");
            }
            auto headState=ReadHeadScopeState(scene,jobID);headState.Owners[setupID]=workpieceID;const auto head=ResolveHeadScope(headState,setupID).Instance;const auto preview=ReadWireRef(WireRef(workpiece->GetPreviewRef()));
            if(!head.is_nil())RequireHeadFitsOperation(scene,setupID,headState.Instances.at(iCAX::Data::to_string(head)).To<Map>());
            Transaction(db,"选择工序工件",[&](auto& tx){
                tx.ModifyComponent(placementID,CFiveAxisPlacementComponent::S_ClassName,{{"WorkpieceID",workpieceID},{"RegistrationData",Map()},{"RegistrationMethod",Text()},{"ApplicationMode",Text("Pending")},{"InputRevision",Next(placement->GetInputRevision())}});
                tx.ModifyComponent(placementID,iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName,{{"GeometryResourceID",preview.URL},{"GeometryResourceVersion",static_cast<UInt>(preview.nVersion)}});
                tx.ModifyComponent(setupID,CFiveAxisSetupComponent::S_ClassName,{{"HeadInstanceID",head},{"HeadAssignment",headState.Setups.at(setupID)},{"InputRevision",Next(setup->GetInputRevision())},{"PlanValidity",Text("Missing")}});
                const auto job=Require<CFiveAxisJobComponent>(db,jobID);tx.ModifyComponent(jobID,CFiveAxisJobComponent::S_ClassName,{{"InputRevision",Next(job->GetInputRevision())}});
            });ProjectPlacement(scene,placementID,placement->GetTransform());
        }
    }else throw std::invalid_argument("CAM_OPERATION_ACTION_INVALID");
    Map result{{"operationId",iCAX::Data::to_string(setupID)},{"setupId",iCAX::Data::to_string(setupID)},{"placementId",iCAX::Data::to_string(placementID)},{"workpieceId",iCAX::Data::to_string(workpieceID)}};
    runtime->Receipts.emplace(commandID,std::make_pair(fingerprint,result));return result;
}
