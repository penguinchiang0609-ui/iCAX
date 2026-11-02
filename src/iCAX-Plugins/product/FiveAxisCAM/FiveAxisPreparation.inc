// Scene-owned copies keep immutable resources, but receive independent entity
// identities and input state. Computed plans and measured registration never copy.
namespace {
    using PreparationIds=std::map<Text,ID>;
    Text CopyPreparationKey(const Text& key,const PreparationIds& ids){
        if(const auto found=ids.find(key);found!=ids.end())return iCAX::Data::to_string(found->second);
        const auto split=key.find('/');if(split!=Text::npos){const auto a=ids.find(key.substr(0,split)),b=ids.find(key.substr(split+1));if(a!=ids.end()&&b!=ids.end())return iCAX::Data::to_string(a->second)+"/"+iCAX::Data::to_string(b->second);}
        return key;
    }
    iCAX::Data::Variant CopyPreparationValue(const iCAX::Data::Variant& value,const PreparationIds& ids) {
        if(value.Is<ID>()){const auto id=value.To<ID>();const auto it=ids.find(iCAX::Data::to_string(id));return it==ids.end()?value:iCAX::Data::Variant(it->second);}
        if(value.Is<Text>()){const auto it=ids.find(value.To<Text>());return it==ids.end()?value:iCAX::Data::Variant(iCAX::Data::to_string(it->second));}
        if(value.Is<Array>()){Array out;for(const auto& item:value.To<Array>())out.push_back(CopyPreparationValue(item,ids));return out;}
        if(value.Is<Map>()){Map out;for(const auto& [key,item]:value.To<Map>())out.emplace(CopyPreparationKey(key,ids),CopyPreparationValue(item,ids));return out;}
        return value;
    }
    Map CopyPreparation(Scene& scene,const Map& request,const Text& kind) {
        CutFields(request,kind=="fixture"?std::initializer_list<const char*>{"commandId","expected","setupId","unitId"}:std::initializer_list<const char*>{"commandId","expected","placementId"});
        auto runtime=Runtime(scene);const auto command=String(request,"commandId"),payload="CopyPreparation:"+kind+iCAX::Data::VariantSerializer::Serialize(request);(void)Identifier(request,"commandId");
        if(const auto prior=runtime->Receipts.find(command);prior!=runtime->Receipts.end()){if(prior->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return prior->second.second;}
        CheckExpected(*runtime,request);CheckCapacity(*runtime);SetupIdle(scene);
        auto& db=scene.Database();const auto fixture=kind=="fixture",whole=kind=="workpiece";
        const auto sourceID=Identifier(request,fixture?"unitId":"placementId");
        const auto unit=fixture?Require<CFiveAxisFixtureUnitComponent>(db,sourceID):nullptr;
        const auto placement=fixture?nullptr:Require<CFiveAxisPlacementComponent>(db,sourceID);
        const auto setupID=fixture?unit->GetSetupID():placement->GetSetupID();
        if(fixture&&setupID!=Identifier(request,"setupId"))throw std::invalid_argument("CAM_SETUP_MISMATCH");
        if(!setupID.is_nil())RequireSetupWorkpieceEnabled(scene,setupID);
        else if(!whole)throw std::invalid_argument("CAM_SETUP_REQUIRED");
        else if(!Require<CFiveAxisWorkpieceComponent>(db,placement->GetWorkpieceID())->GetEnabled())throw std::invalid_argument("CAM_WORKPIECE_DISABLED");
        if(fixture)RequireSetupUnlocked(scene,setupID);
        const auto jobID=db.GetMetaEntity()->GetID();const auto job=Require<CFiveAxisJobComponent>(db,jobID);
        std::set<ID> owned,setupIDs;PreparationIds ids;iCAX::Data::uuid_name_generator cloneNames(Identifier(request,"commandId"));
        if(fixture){owned.insert(sourceID);}
        else{
            if(whole)owned.insert(placement->GetWorkpieceID());
            for(const auto& e:db.FilterEntities([](auto){return true;}))if(const auto p=Component<CFiveAxisPlacementComponent>(e);p&&(whole?p->GetWorkpieceID()==placement->GetWorkpieceID():e->GetID()==sourceID)){owned.insert(e->GetID());if(!p->GetSetupID().is_nil()){owned.insert(p->GetSetupID());setupIDs.insert(p->GetSetupID());}}
        }
        for(const auto& e:db.FilterEntities([](auto){return true;})){
            if(const auto f=Component<CFiveAxisFixtureComponent>(e);f&&(fixture?f->GetUnitID()==sourceID:setupIDs.contains(f->GetSetupID())))owned.insert(e->GetID());
            if(!fixture){
                if(const auto u=Component<CFiveAxisFixtureUnitComponent>(e);u&&setupIDs.contains(u->GetSetupID()))owned.insert(e->GetID());
            }
        }
        for(const auto& id:owned)ids.emplace(iCAX::Data::to_string(id),cloneNames("copy/entity/"+iCAX::Data::to_string(id)));
        std::vector<ID> orderedSetups(setupIDs.begin(),setupIDs.end());
        std::sort(orderedSetups.begin(),orderedSetups.end(),[&](const ID& a,const ID& b){return Require<CFiveAxisSetupComponent>(db,a)->GetSequence()<Require<CFiveAxisSetupComponent>(db,b)->GetSequence();});
        UInt listOrder=0;for(const auto& e:db.FilterEntities([](auto){return true;}))if(const auto w=Component<CFiveAxisWorkpieceComponent>(e))listOrder=std::max(listOrder,w->GetListOrder());
        auto sequence=whole?NextSetupSequence(scene):SetupInsertionSequence(scene,placement?placement->GetWorkpieceID():ID());
        std::map<ID,UInt> sequenceByID;for(const auto& id:orderedSetups)sequenceByID[id]=sequence++;
        const auto mapped=[&](const ID& id){const auto found=ids.find(iCAX::Data::to_string(id));return found==ids.end()?id:found->second;};
        const auto selected=mapped(sourceID),targetSetup=fixture?setupID:mapped(setupID);
        const Text undoName=whole?"复制工件":fixture?"复制夹具单元":"复制装夹姿态";
        std::map<ID,ID> targetPlans;std::vector<Map> commands;iCAX::CAM::Intent::SEditableToolpathHostEdit host;host.UndoName=undoName;
        if(!fixture)for(const auto& originalSetup:orderedSetups)
        {
            using namespace iCAX::CAM::Intent;
            const auto targetPlan=cloneNames("copy/plan/"+iCAX::Data::to_string(originalSetup));targetPlans[originalSetup]=targetPlan;
            const auto create=cloneNames("copy/create-plan/"+iCAX::Data::to_string(originalSetup)),clone=cloneNames("copy/clone-plan/"+iCAX::Data::to_string(originalSetup));
            const auto sourcePlan=IntentPlan(scene,originalSetup);const auto sourceDefinition=IntentDocument(scene).ReadPlan(sourcePlan);
            commands.push_back({{"kind",Text("CreatePlan")},{"commandId",create},{"planId",targetPlan},{"name",Require<CFiveAxisSetupComponent>(db,originalSetup)->GetName()+Text(" 副本")}});
            commands.push_back({{"kind",Text("ClonePlan")},{"commandId",clone},{"sourcePlanId",sourcePlan},{"planId",targetPlan},
                {"expectedSourcePlanFingerprint",sourceDefinition.at("graphFingerprint")},{"labelScope",iCAX::Data::to_string(mapped(originalSetup))}});
            auto& transform=host.PlanClones[clone];
            for(const auto& [oldId,newId]:ids)transform.ExternalIdentityRemap.emplace(ID::from_string(oldId).value(),newId);
            transform.TransformSource=[](const ID&,const ID&,const SToolpathValue& remapped){return SPlanCloneSourceTransform{remapped,true};};
        }
        const auto stage=[&](iCAX::Database::ITransaction& tx,const auto&){
            if(!fixture&&!whole)ShiftSetupSequences(scene,tx,SetupInsertionSequence(scene,placement->GetWorkpieceID()));
            for(const auto& id:owned){
                const auto entity=db.GetEntity(id);const auto target=mapped(id);tx.CreateEntity(target);
                for(const auto& type:entity->GetComponentClasses()){
                    auto properties=CopyPreparationValue(entity->GetComponent(type)->GetProperties(),ids).To<Map>();
                    if(type==CFiveAxisSetupComponent::S_ClassName){
                        properties["Sequence"]=sequenceByID.at(id);properties["Locked"]=false;properties["InputRevision"]=1ull;properties["PlanValidity"]=Text("Missing");properties["IntentPlanID"]=targetPlans.at(id);
                        for(const auto* key:{"RegistrationPlan","LastSuccessfulPlanRef","GeneratedToolpaths","GeneratedRapidPaths","SimulationCheckData","FixtureCheck","FixtureDesign"})properties[key]=Map();
                        if(!whole)properties["Name"]=properties.at("Name").To<Text>()+" 副本";
                    }else if(type==CFiveAxisWorkpieceComponent::S_ClassName){properties["Name"]=properties.at("Name").To<Text>()+" 副本";properties["ListOrder"]=Next(listOrder);properties["InputRevision"]=1ull;properties["Locked"]=false;}
                    else if(type==CFiveAxisPlacementComponent::S_ClassName){properties["RegistrationData"]=Map();properties["RegistrationMethod"]=Text();properties["ApplicationMode"]=Text("Pending");properties["InputRevision"]=1ull;}
                    else if(type==CFiveAxisFixtureUnitComponent::S_ClassName){auto design=properties.at("Design").To<Map>();design.erase("delivery");properties["Design"]=design;if(fixture){properties["Name"]=properties.at("Name").To<Text>()+" 副本";properties["Locked"]=false;}}
                    else if(type==CFiveAxisFixtureComponent::S_ClassName&&fixture)properties["Locked"]=false;
                    tx.AttachComponent(target,type,properties);
                }
            }
            if(fixture)Invalidate(tx,db,setupID);
            else tx.ModifyComponent(jobID,CFiveAxisJobComponent::S_ClassName,{{"DefaultSetupID",targetSetup},{"ActiveSetupID",targetSetup},{"DefaultPlacementID",selected},{"ActivePlacementID",selected},{"InputRevision",Next(job->GetInputRevision())}});
        };
        if(fixture||commands.empty())Transaction(db,undoName,[&](auto& tx){stage(tx,std::vector<Map>{});});
        else
        {
            host.Stage=stage;IntentDocument(scene).ExecuteBatch(commands,host);
            for(const auto& originalSetup:orderedSetups)QueueIntentGeneration(scene,mapped(originalSetup));
        }
        for(const auto& id:owned)if(const auto p=Component<CFiveAxisPlacementComponent>(db.GetEntity(mapped(id))))ProjectPlacement(scene,mapped(id),p->GetTransform());
        Map result=fixture?Map{{"unitId",iCAX::Data::to_string(selected)}}:Map{{"placementId",iCAX::Data::to_string(selected)},{"setupId",OptionalID(targetSetup)},{"workpieceId",iCAX::Data::to_string(mapped(placement->GetWorkpieceID()))}};
        runtime->Receipts.emplace(command,std::make_pair(payload,result));return result;
    }
}
Map CopyWorkpiece(Scene& scene,const Map& request){return CopyPreparation(scene,request,"workpiece");}
Map RenameWorkpiece(Scene& scene,const Map& request){
    CutFields(request,{"commandId","expected","workpieceId","name"});const auto runtime=Runtime(scene);
    const auto command=iCAX::Data::to_string(Identifier(request,"commandId")),payload="RenameWorkpiece:"+iCAX::Data::VariantSerializer::Serialize(request);
    if(const auto prior=runtime->Receipts.find(command);prior!=runtime->Receipts.end()){if(prior->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return prior->second.second;}
    for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==command)throw std::invalid_argument("CAM_COMMAND_REUSED");
    CheckExpected(*runtime,request);CheckCapacity(*runtime);SetupIdle(scene);
    const auto id=Identifier(request,"workpieceId");auto& db=scene.Database();const auto w=Require<CFiveAxisWorkpieceComponent>(db,id);
    const auto name=String(request,"name");
    if(name.empty()||name.size()>512||name.find_first_not_of(" \t\r\n")==Text::npos||std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32||c==127;}))throw std::invalid_argument("CAM_WORKPIECE_NAME_INVALID");
    const bool changed=name!=w->GetName();
    // A label is metadata: keep CAD, poses, machining inputs and qualification.
    if(changed)Transaction(db,"修改工件名称",[&](auto& tx){tx.ModifyComponent(id,CFiveAxisWorkpieceComponent::S_ClassName,{{"Name",name}});});
    Map result{{"workpieceId",request.at("workpieceId")},{"name",name},{"changed",changed}};runtime->Receipts.emplace(command,std::make_pair(payload,result));return result;
}
Map CopySetup(Scene& scene,const Map& request){return CopyPreparation(scene,request,"setup");}
Map CopyFixtureUnit(Scene& scene,const Map& request){return CopyPreparation(scene,request,"fixture");}
Map SetWorkpieceLocked(Scene& scene,const Map& request){
    CutFields(request,{"commandId","expected","placementId","locked"});const auto runtime=Runtime(scene);
    const auto command=iCAX::Data::to_string(Identifier(request,"commandId")),payload="WorkpieceLocked:"+iCAX::Data::VariantSerializer::Serialize(request);
    if(const auto prior=runtime->Receipts.find(command);prior!=runtime->Receipts.end()){if(prior->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return prior->second.second;}
    CheckExpected(*runtime,request);CheckCapacity(*runtime);SetupIdle(scene);auto& db=scene.Database();
    const auto p=Require<CFiveAxisPlacementComponent>(db,Identifier(request,"placementId"));const auto locked=request.at("locked").To<bool>();
    Transaction(db,locked?"锁定工件":"解锁工件",[&](auto& tx){tx.ModifyComponent(p->GetWorkpieceID(),CFiveAxisWorkpieceComponent::S_ClassName,{{"Locked",locked}});for(const auto& e:db.FilterEntities([](auto){return true;}))if(const auto other=Component<CFiveAxisPlacementComponent>(e);other&&other->GetWorkpieceID()==p->GetWorkpieceID()&&!other->GetSetupID().is_nil())tx.ModifyComponent(other->GetSetupID(),CFiveAxisSetupComponent::S_ClassName,{{"Locked",locked}});});
    Map result{{"locked",locked}};runtime->Receipts.emplace(command,std::make_pair(payload,result));return result;
}
