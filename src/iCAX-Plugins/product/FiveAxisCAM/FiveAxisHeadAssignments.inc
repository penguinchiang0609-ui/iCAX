// Assignment declarations are inputs; HeadInstanceID remains their resolved
// per-setup cache so all machining consumers share one effective snapshot.
namespace {
    Map HeadAssignmentValue(const Map& value,const ID& legacy=ID()){
        if(value.empty())return legacy.is_nil()?Map{{"mode",Text("Inherit")}}:Map{{"mode",Text("Instance")},{"instanceId",iCAX::Data::to_string(legacy)}};
        const auto mode=String(value,"mode");
        if(mode!="Inherit"&&mode!="None"&&mode!="Instance")throw std::invalid_argument("CAM_HEAD_ASSIGNMENT_INVALID");
        if(mode=="Instance")(void)Identifier(value,"instanceId");return value;
    }
    struct HeadScopeState {
        ID JobID;
        Map Instances,Project,Product;
        std::map<ID,Map> Workpieces,Setups;
        std::map<ID,ID> Owners;
        std::map<ID,Text> MachineTypes;
        std::map<Text,Array> CandidateMachineTypes;
    };
    HeadScopeState ReadHeadScopeState(Scene& scene,const ID& jobID){
        const auto job=Require<CFiveAxisJobComponent>(scene.Database(),jobID);HeadScopeState result;
        result.JobID=jobID;result.Instances=job->GetHeadInstances();result.Project=HeadAssignmentValue(job->GetHeadAssignment());result.Product=HeadAssignmentValue(job->GetProductHeadDefault());
        for(const auto& entity:scene.Database().FilterEntities([](auto){return true;})){
            if(const auto w=Component<CFiveAxisWorkpieceComponent>(entity))result.Workpieces[entity->GetID()]=HeadAssignmentValue(w->GetHeadAssignment());
            if(const auto s=Component<CFiveAxisSetupComponent>(entity);s&&s->GetJobID()==jobID){result.Setups[entity->GetID()]=HeadAssignmentValue(s->GetHeadAssignment(),s->GetHeadInstanceID());const auto type=s->GetMachineTypeAssignment();result.MachineTypes[entity->GetID()]=type.contains("machineType")?String(type,"machineType"):EffectiveMachineTypeValue(scene,entity->GetID());}
            if(const auto p=Component<CFiveAxisPlacementComponent>(entity);p&&!p->GetSetupID().is_nil()){
                if(const auto found=result.Owners.find(p->GetSetupID());found!=result.Owners.end()&&found->second!=p->GetWorkpieceID())throw std::invalid_argument("CAM_HEAD_WORKPIECE_CONTEXT");
                result.Owners[p->GetSetupID()]=p->GetWorkpieceID();
            }
        }return result;
    }
    struct HeadResolution {ID Instance;Text Source="None",Mode="Inherit";};
    HeadResolution ResolveHeadScope(const HeadScopeState& state,const Map& local,const ID& workpieceID,const Text& machineType={}){
        const auto work=state.Workpieces.contains(workpieceID)?state.Workpieces.at(workpieceID):Map{{"mode",Text("Inherit")}};
        for(const auto& level:std::array<std::pair<Text,Map>,4>{{{"Setup",local},{"Workpiece",work},{"Project",state.Project},{"Product",state.Product}}}){
            const auto mode=String(level.second,"mode");if(mode=="Inherit")continue;
            if(mode=="Instance"&&level.first!="Setup"&&!machineType.empty()){
                const auto key=String(level.second,"instanceId");const auto found=state.Instances.find(key);
                if(found!=state.Instances.end()&&!HeadFitsMachineType(found->second.To<Map>(),machineType))continue;
                const auto candidate=state.CandidateMachineTypes.find(key);
                if(found==state.Instances.end()&&candidate!=state.CandidateMachineTypes.end()&&std::none_of(candidate->second.begin(),candidate->second.end(),[&](const auto& value){return value.template To<Text>()==machineType;}))continue;
            }
            return {mode=="Instance"?Identifier(level.second,"instanceId"):ID(),level.first,mode};
        }return {};
    }
    HeadResolution ResolveHeadScope(const HeadScopeState& state,const ID& setupID){
        return ResolveHeadScope(state,state.Setups.at(setupID),state.Owners.contains(setupID)?state.Owners.at(setupID):ID(),state.MachineTypes.contains(setupID)?state.MachineTypes.at(setupID):Text());
    }
    ID InheritedHeadForWorkpiece(Scene& scene,const ID& workpieceID){
        const auto state=ReadHeadScopeState(scene,scene.Database().GetMetaEntity()->GetID());
        return ResolveHeadScope(state,Map{{"mode",Text("Inherit")}},workpieceID).Instance;
    }
    bool HeadInstanceHasScopeReference(Scene& scene,const ID& instanceID){
        const auto state=ReadHeadScopeState(scene,scene.Database().GetMetaEntity()->GetID());const auto text=iCAX::Data::to_string(instanceID);
        const auto uses=[&](const Map& assignment){return String(assignment,"mode")=="Instance"&&String(assignment,"instanceId")==text;};
        if(uses(state.Project)||uses(state.Product))return true;
        for(const auto& [_,assignment]:state.Workpieces)if(uses(assignment))return true;
        for(const auto& [_,assignment]:state.Setups)if(uses(assignment))return true;
        return false;
    }
    Map HeadAssignmentSummary(const HeadScopeState& state,const Map& assignment){
        Map result=assignment;result["instanceId"]=Text();result["name"]=Text();
        if(String(assignment,"mode")=="Instance"){
            const auto id=String(assignment,"instanceId");const auto found=state.Instances.find(id);
            if(found==state.Instances.end())throw std::invalid_argument("CAM_HEAD_INSTANCE_MISSING");
            result["instanceId"]=id;result["name"]=found->second.To<Map>().at("name");
        }return result;
    }
    std::optional<LibraryRecord> ProductHeadPreferences(Scene& scene){
        const auto store=Runtime(scene)->FixtureLibraryStore;
        return store?store->Get(HeadConfigurationFeature,"preferences","icax.five-axis-cam"):std::nullopt;
    }
    Map ProductHeadPreferenceSummary(const std::optional<LibraryRecord>& preferences){
        Map result{{"mode",Text("Inherit")},{"name",Text()},{"instanceId",Text()},{"revision",std::to_string(preferences?preferences->Revision:0)}};
        if(preferences){const auto value=preferences->Payload.To<Map>();result["mode"]=value.at("mode");if(value.contains("name"))result["name"]=value.at("name");}
        return result;
    }
    Map MakeScopedHeadInstance(const Map& model,const Map& parameters,const Map& profile,const Text& name,bool confirmed){
        const auto id=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
        return {{"id",id},{"name",HeadInstanceName(name.empty()?String(model,"name"):name)},{"model",model},{"parameters",NormalizeHeadParameters(model,parameters)},
            {"profile",profile},{"binding",Map{}},{"calibrationConfirmed",confirmed},{"revision",UInt(1)}};
    }
    Map StandaloneProductHead(Scene& scene,Map instance){
        if(instance.at("binding").To<Map>().empty())return instance;
        auto model=CalibratedHeadModel(instance);const auto users=HeadInstanceUsers(scene,Identifier(instance,"id"));
        if(!users.empty())model=EffectiveHeadDefinition(scene,*Require<CFiveAxisSetupComponent>(scene.Database(),users.front()));
        for(const auto* name:{"collisionMachineId","collisionToolId","machineHeadName"})model.erase(name);
        if(model.contains("machinePreviewRef")){model["previewRef"]=model.at("machinePreviewRef");model.erase("machinePreviewRef");}
        model["collisionSource"]=Text("HeadDescription");auto parameters=instance.at("parameters").To<Map>();parameters["axes"]=DefaultHeadParameters(model).at("axes");
        instance["model"]=model;instance["parameters"]=NormalizeHeadParameters(model,parameters);instance["binding"]=Map{};return instance;
    }
    Map PrepareScopedHead(Scene& scene,const HeadScopeState& state,const Map& request,bool product){
        const auto kind=String(request,"kind");
        if(kind=="Instance"){
            const auto id=iCAX::Data::to_string(Identifier(request,"instanceId"));const auto found=state.Instances.find(id);
            if(found==state.Instances.end())throw std::invalid_argument("CAM_HEAD_INSTANCE_MISSING");auto instance=found->second.To<Map>();
            if(product)return StandaloneProductHead(scene,std::move(instance));
            if(!instance.at("binding").To<Map>().empty())throw std::invalid_argument("CAM_HEAD_MACHINE_BOUND: 绑定机床的刀头由对应机床管理。");return instance;
        }
        const auto persistence=product?iCAX::Resource::EResourcePersistenceMode::RuntimeOnly:iCAX::Resource::EResourcePersistenceMode::Embedded;
        if(kind=="Builtin"){
            Array types;for(const auto& item:ListBuiltinHeads(scene,{}).at("items").To<Array>()){const auto candidate=item.To<Map>();if(String(candidate,"id")==String(request,"builtinDefinitionId"))types=candidate.at("machineTypes").To<Array>();}RequireHeadCatalogFitsOperation(scene,Identifier(request,"setupId"),types);
            const auto model=StandaloneBuiltinHeadDefinition(scene,String(request,"builtinDefinitionId"),persistence);
            return MakeScopedHeadInstance(model,DefaultHeadParameters(model),{},String(model,"name"),false);
        }
        if(kind=="Configuration"){
            const auto entry=FixtureStore(scene).Get(HeadConfigurationFeature,"configuration",String(request,"entryId"));if(!entry)throw std::invalid_argument("CAM_HEAD_CONFIGURATION_MISSING");
            RequireHeadCatalogFitsOperation(scene,Identifier(request,"setupId"),HeadConfigurationSummary(*entry).at("machineTypes").To<Array>());
            const auto data=entry->Payload.To<Map>();const auto model=UnpackHeadConfiguration(scene,data,persistence);
            return MakeScopedHeadInstance(model,data.at("parameters").To<Map>(),HeadConfigurationSummary(*entry),String(data,"name"),data.at("calibrationConfirmed").To<bool>());
        }throw std::invalid_argument("CAM_HEAD_ASSIGNMENT_KIND_REQUIRED");
    }
    void PutHeadDeclaration(HeadScopeState& state,const ID& setupID,const Text& scope,const Map& assignment){
        if(scope=="Setup")state.Setups[setupID]=assignment;
        else if(scope=="Project")state.Project=assignment;
        else if(scope=="Workpiece"){
            if(!state.Owners.contains(setupID)||!state.Workpieces.contains(state.Owners.at(setupID)))throw std::invalid_argument("CAM_HEAD_WORKPIECE_CONTEXT");
            state.Workpieces[state.Owners.at(setupID)]=assignment;
        }else throw std::invalid_argument("CAM_HEAD_ASSIGNMENT_SCOPE_REQUIRED");
    }
    std::vector<std::pair<ID,ID>> ChangedHeadCaches(Scene& scene,const HeadScopeState& state,bool validate){
        std::vector<std::pair<ID,ID>> result;
        for(const auto& [id,_]:state.Setups){const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),id);const auto next=ResolveHeadScope(state,id).Instance;
            if(next==setup->GetHeadInstanceID())continue;
            if(validate){RequireSetupUnlocked(scene,id);if(!setup->GetMachineSetup().empty())throw std::invalid_argument("CAM_HEAD_MACHINE_BOUND: 绑定机床的姿态不能继承独立刀头。");if(!next.is_nil()){const auto found=state.Instances.find(iCAX::Data::to_string(next));if(found!=state.Instances.end())RequireHeadFitsOperation(scene,id,found->second.To<Map>());}}
            result.emplace_back(id,next);
        }return result;
    }
    void WriteHeadCaches(Scene& scene,iCAX::Database::ITransaction& tx,const HeadScopeState& state,const std::vector<std::pair<ID,ID>>& changes){
        for(const auto& [id,instance]:changes){const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),id);
            // Persist the declaration beside its cache: an old nil binding is
            // inherited, even after its first non-nil effective head is stored.
            tx.ModifyComponent(id,CFiveAxisSetupComponent::S_ClassName,{{"HeadInstanceID",instance},{"HeadAssignment",state.Setups.at(id)},{"PlanValidity",Text(setup->GetLastSuccessfulPlanRef().empty()?"Missing":"Stale")}});
        }
    }
    void CaptureInitialProductHeadDefault(Scene& scene){
        const auto runtime=Runtime(scene);if(!runtime->CaptureInitialHeadDefault||!runtime->FixtureLibraryStore)return;
        const auto jobID=scene.Database().GetMetaEntity()->GetID();if(!Component<CFiveAxisJobComponent>(scene.Database().GetEntity(jobID)))return;
        const auto preferences=ProductHeadPreferences(scene);
        if(!preferences){runtime->CaptureInitialHeadDefault=false;return;}
        const auto payload=preferences->Payload.To<Map>();const auto mode=String(payload,"mode");
        auto state=ReadHeadScopeState(scene,jobID);Map snapshot{{"mode",mode},{"preferencesRevision",std::to_string(preferences->Revision)}};
        if(mode=="Instance"){
            const auto saved=payload.at("snapshot").To<Map>();const auto model=UnpackHeadConfiguration(scene,saved);
            const auto instance=MakeScopedHeadInstance(model,saved.at("parameters").To<Map>(),saved.contains("profile")?saved.at("profile").To<Map>():Map{},String(saved,"name"),saved.at("calibrationConfirmed").To<bool>());
            state.Instances[String(instance,"id")]=instance;snapshot["instanceId"]=instance.at("id");
        }else if(mode!="None"&&mode!="Inherit")throw std::invalid_argument("CAM_HEAD_ASSIGNMENT_INVALID");
        state.Product=snapshot;const auto changes=ChangedHeadCaches(scene,state,true);
        const auto job=Require<CFiveAxisJobComponent>(scene.Database(),jobID);
        Transaction(scene.Database(),"读取新工程切割头默认",[&](auto& tx){
            tx.ModifyComponent(jobID,CFiveAxisJobComponent::S_ClassName,{{"HeadInstances",state.Instances},{"ProductHeadDefault",snapshot},{"InputRevision",Next(job->GetInputRevision())}});WriteHeadCaches(scene,tx,state,changes);
        },false);runtime->CaptureInitialHeadDefault=false;
    }
}

Map ReadHeadAssignments(Scene& scene,const Map& request){
    CutFields(request,{"setupId"});const auto setupID=Identifier(request,"setupId");const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);
    const auto state=ReadHeadScopeState(scene,setup->GetJobID());const auto owner=state.Owners.contains(setupID)?state.Owners.at(setupID):ID();
    const auto preferences=ProductHeadPreferences(scene);const auto product=ProductHeadPreferenceSummary(preferences);const auto resolution=ResolveHeadScope(state,setupID);
    const auto effective=HeadAssignmentSummary(state,resolution.Instance.is_nil()?Map{{"mode",Text("None")}}:Map{{"mode",Text("Instance")},{"instanceId",iCAX::Data::to_string(resolution.Instance)}});
    Map result{{"setupId",request.at("setupId")},{"workpieceId",OptionalID(owner)},
        {"scopes",Map{{"Setup",HeadAssignmentSummary(state,state.Setups.at(setupID))},{"Workpiece",HeadAssignmentSummary(state,state.Workpieces.contains(owner)?state.Workpieces.at(owner):Map{{"mode",Text("Inherit")}})},
            {"Project",HeadAssignmentSummary(state,state.Project)},{"Product",product}}},{"productSnapshot",HeadAssignmentSummary(state,state.Product)},{"effective",effective},{"productPreferencesRevision",product.at("revision")}};
    std::get<Map>(result.at("effective").m_Value)["source"]=resolution.Source;return result;
}

Map AssignHead(Scene& scene,const Map& request){
    CutFields(request,{"commandId","expected","setupId","scope","action","kind","builtinDefinitionId","instanceId","entryId","preferencesRevision"});
    const auto runtime=Runtime(scene);const auto command=iCAX::Data::to_string(Identifier(request,"commandId"));const auto fingerprint="HeadAssignment:"+iCAX::Data::VariantSerializer::Serialize(request);
    if(const auto prior=runtime->Receipts.find(command);prior!=runtime->Receipts.end()){if(prior->second.first!=fingerprint)throw std::invalid_argument("CAM_COMMAND_REUSED");return prior->second.second;}
    CheckExpected(*runtime,request);CheckCapacity(*runtime);
    const auto setupID=Identifier(request,"setupId");const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);auto state=ReadHeadScopeState(scene,setup->GetJobID());
    const auto scope=String(request,"scope"),action=String(request,"action");if(action!="set"&&action!="inherit")throw std::invalid_argument("CAM_HEAD_ASSIGNMENT_ACTION_REQUIRED");
    if(scope!="Setup"&&scope!="Workpiece"&&scope!="Project"&&scope!="Product")throw std::invalid_argument("CAM_HEAD_ASSIGNMENT_SCOPE_REQUIRED");
    if(action=="inherit")for(const auto* key:{"kind","builtinDefinitionId","instanceId","entryId"})if(request.contains(key))throw std::invalid_argument("CAM_HEAD_ASSIGNMENT_SELECTOR_INVALID");
    const auto kind=action=="set"?String(request,"kind"):Text();
    if(action=="set"){
        if(kind!="None"&&kind!="Builtin"&&kind!="Instance"&&kind!="Configuration")throw std::invalid_argument("CAM_HEAD_ASSIGNMENT_KIND_REQUIRED");
        for(const auto* key:{"builtinDefinitionId","instanceId","entryId"})if(request.contains(key)&&!((kind=="Builtin"&&Text(key)=="builtinDefinitionId")||(kind=="Instance"&&Text(key)=="instanceId")||(kind=="Configuration"&&Text(key)=="entryId")))throw std::invalid_argument("CAM_HEAD_ASSIGNMENT_SELECTOR_INVALID");
    }
    Map declaration{{"mode",Text(action=="inherit"?"Inherit":kind=="None"?"None":"Instance")}};
    if(scope=="Product"){
        const auto revision=Integer(request,"preferencesRevision");Map payload=declaration;
        const auto current=ProductHeadPreferences(scene);
        if(revision!=(current?current->Revision:0))throw std::invalid_argument("CAM_HEAD_DEFAULT_REVISION_CONFLICT: 产品默认已变化，请刷新后重试。");
        if(String(declaration,"mode")=="Instance"){
            const auto instance=PrepareScopedHead(scene,state,request,true);RequireHeadFitsOperation(scene,setupID,instance);auto packed=PackHeadConfiguration(scene,instance.at("model").To<Map>(),instance.at("parameters").To<Map>());
            packed["name"]=instance.at("name");packed["profile"]=instance.at("profile");packed["calibrationConfirmed"]=instance.at("calibrationConfirmed");
            payload["snapshot"]=std::move(packed);payload["name"]=instance.at("name");
        }
        LibraryRecord record;record.FeatureID=HeadConfigurationFeature;record.RecordType="preferences";record.SubjectType="product";record.SubjectID="icax.five-axis-cam";record.RecordID="icax.five-axis-cam";record.Payload=payload;
        FixtureStore(scene).Put(record,revision);
    }else{
        if(request.contains("preferencesRevision"))throw std::invalid_argument("CAM_HEAD_ASSIGNMENT_SELECTOR_INVALID");
        if(scope=="Setup"){RequireSetupUnlocked(scene,setupID);if(!setup->GetMachineSetup().empty())throw std::invalid_argument("CAM_HEAD_MACHINE_BOUND");}
        if(scope=="Workpiece"){
            if(!state.Owners.contains(setupID))throw std::invalid_argument("CAM_HEAD_WORKPIECE_CONTEXT");
            if(Require<CFiveAxisWorkpieceComponent>(scene.Database(),state.Owners.at(setupID))->GetLocked())throw std::invalid_argument("CAM_WORKPIECE_LOCKED");
        }
        // Validate every affected pose before importing or publishing resources.
        auto probe=state;
        if(String(declaration,"mode")=="Instance")declaration["instanceId"]=kind=="Instance"?request.at("instanceId"):Variant(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()));
        if(String(declaration,"mode")=="Instance"&&kind!="Instance"){
            Array types;
            if(kind=="Builtin")for(const auto& value:ListBuiltinHeads(scene,{}).at("items").To<Array>()){const auto item=value.To<Map>();if(String(item,"id")==String(request,"builtinDefinitionId"))types=item.at("machineTypes").To<Array>();}
            else if(kind=="Configuration"){const auto entry=FixtureStore(scene).Get(HeadConfigurationFeature,"configuration",String(request,"entryId"));if(!entry)throw std::invalid_argument("CAM_HEAD_CONFIGURATION_MISSING");types=HeadConfigurationSummary(*entry).at("machineTypes").To<Array>();}
            RequireHeadCatalogFitsOperation(scene,setupID,types);probe.CandidateMachineTypes[String(declaration,"instanceId")]=types;
        }
        PutHeadDeclaration(probe,setupID,scope,declaration);(void)ChangedHeadCaches(scene,probe,true);
        if(String(declaration,"mode")=="Instance"){
            const auto instance=PrepareScopedHead(scene,state,request,false);RequireHeadFitsOperation(scene,setupID,instance);state.Instances[String(instance,"id")]=instance;declaration["instanceId"]=instance.at("id");
        }
        PutHeadDeclaration(state,setupID,scope,declaration);const auto changes=ChangedHeadCaches(scene,state,true);const auto job=Require<CFiveAxisJobComponent>(scene.Database(),state.JobID);
        Transaction(scene.Database(),"指定切割头范围",[&](auto& tx){
            iCAX::Data::PropertySet jobChanges{{"HeadInstances",state.Instances},{"InputRevision",Next(job->GetInputRevision())}};
            if(scope=="Project")jobChanges["HeadAssignment"]=declaration;
            if(scope=="Setup")tx.ModifyComponent(setupID,CFiveAxisSetupComponent::S_ClassName,{{"HeadAssignment",declaration}});
            if(scope=="Workpiece")tx.ModifyComponent(state.Owners.at(setupID),CFiveAxisWorkpieceComponent::S_ClassName,{{"HeadAssignment",declaration}});
            tx.ModifyComponent(state.JobID,CFiveAxisJobComponent::S_ClassName,jobChanges);WriteHeadCaches(scene,tx,state,changes);
        });
    }
    auto result=ReadHeadAssignments(scene,{{"setupId",request.at("setupId")}});result["scope"]=scope;result["changed"]=true;
    runtime->Receipts.emplace(command,std::make_pair(fingerprint,result));return result;
}

namespace {Map PrepareOperationHead(Scene& scene,const ID& workpieceID,const Text& machineType){
    const auto state=ReadHeadScopeState(scene,scene.Database().GetMetaEntity()->GetID());
    const auto assignment=Map{{"mode",Text("Inherit")}};const auto resolved=ResolveHeadScope(state,assignment,workpieceID,machineType);
    if(!resolved.Instance.is_nil())RequireHeadFitsMachineType(state.Instances.at(iCAX::Data::to_string(resolved.Instance)).To<Map>(),machineType);
    return {{"instanceId",OptionalID(resolved.Instance)},{"assignment",assignment},{"instances",state.Instances}};
}}
