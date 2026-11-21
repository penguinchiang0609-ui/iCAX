Map RemoveWorkpiece(Scene& scene,const Map& request)
{
    CutFields(request,{"commandId","expected","setupId","placementId"});
    const auto runtime=Runtime(scene);
    const auto commandID=iCAX::Data::to_string(Identifier(request,"commandId"));
    const auto payload=Text("RemoveWorkpiece:")+iCAX::Data::VariantSerializer::Serialize(request);
    if(const auto prior=runtime->Receipts.find(commandID);prior!=runtime->Receipts.end()) {
        if(prior->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");
        return prior->second.second;
    }
    for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==commandID)throw std::invalid_argument("CAM_COMMAND_REUSED");
    CheckExpected(*runtime,request);CheckCapacity(*runtime);
    for(const auto& [_,task]:runtime->Tasks)if(task.Status=="Running"&&task.Kind!="RapidPreview")throw std::runtime_error("CAM_WORKPIECE_BUSY");
    auto& db=scene.Database();
    const auto placementID=Identifier(request,"placementId"),setupID=OptionalIdentifier(request,"setupId");
    const auto placement=Require<CFiveAxisPlacementComponent>(db,placementID);
    if(placement->GetSetupID()!=setupID)throw std::invalid_argument("CAM_SETUP_MISMATCH");
    const auto workpieceID=placement->GetWorkpieceID();
    const auto workpiece=Require<CFiveAxisWorkpieceComponent>(db,workpieceID);
    RequireWorkpieceUnlocked(scene,workpieceID);
    const auto jobID=db.GetMetaEntity()->GetID();
    const auto job=Require<CFiveAxisJobComponent>(db,jobID);
    struct Remaining {ID Placement,Setup;UInt Order;};
    std::vector<Remaining> remaining;
    std::vector<ID> cuts,fixtures,ownedPlacements;std::set<ID> ownedSetups;
    for(const auto& e:db.FilterEntities([](auto){return true;}))if(const auto p=Component<CFiveAxisPlacementComponent>(e);p&&p->GetWorkpieceID()==workpieceID){ownedPlacements.push_back(e->GetID());if(!p->GetSetupID().is_nil())ownedSetups.insert(p->GetSetupID());}
    for(const auto& entity:db.FilterEntities([](auto){return true;})){
        if(const auto p=Component<CFiveAxisPlacementComponent>(entity);p&&entity->GetID()!=placementID){
            if(p->GetWorkpieceID()==workpieceID)continue;
            remaining.push_back({entity->GetID(),p->GetSetupID(),Require<CFiveAxisWorkpieceComponent>(db,p->GetWorkpieceID())->GetListOrder()});
        }
        if(const auto f=Component<CFiveAxisFixtureComponent>(entity);f&&ownedSetups.contains(f->GetSetupID()))fixtures.push_back(entity->GetID());
        if(const auto u=Component<CFiveAxisFixtureUnitComponent>(entity);u&&ownedSetups.contains(u->GetSetupID()))fixtures.push_back(entity->GetID());
    }
    std::sort(remaining.begin(),remaining.end(),[](const auto& a,const auto& b){if(a.Order!=b.Order)return a.Order<b.Order;if(a.Setup.is_nil()!=b.Setup.is_nil())return !a.Setup.is_nil();return iCAX::Data::to_string(a.Placement)<iCAX::Data::to_string(b.Placement);});
    auto next=std::find_if(remaining.begin(),remaining.end(),[&](const auto& p){return p.Order>workpiece->GetListOrder();});
    if(!remaining.empty()){
        const auto order=next==remaining.end()?remaining.back().Order:next->Order;
        next=std::find_if(remaining.begin(),remaining.end(),[&](const auto& p){return p.Order==order;});
        const auto preferred=std::find_if(next,remaining.end(),[&](const auto& p){return p.Order==order&&p.Setup.is_nil()==setupID.is_nil();});
        if(preferred!=remaining.end())next=preferred;
    }
    const auto nextPlacement=remaining.empty()?ID():next->Placement;
    const auto nextSetup=remaining.empty()?ID():next->Setup;
    std::vector<Map> commands;std::set<ID> retiredPlans;
    for(const auto& owned:ownedSetups)
    {
        for(const auto& cut:IntentCuts(scene,owned))cuts.push_back(cut->GetID());retiredPlans.insert(IntentPlan(scene,owned));
        const auto retire=RetireIntentPlanCommands(scene,owned,Identifier(request,"commandId"));commands.insert(commands.end(),retire.begin(),retire.end());
    }
    CommitIntentHostCommands(scene,commands,[&](auto& tx,const auto&){
        for(const auto& id:fixtures)tx.DisposeEntity(id);
        for(const auto& id:ownedPlacements)tx.DisposeEntity(id);
        tx.DisposeEntity(workpieceID);for(const auto& id:ownedSetups)tx.DisposeEntity(id);
        // Retain the observable selection ID: removing its setup activates the
        // transactional default. Undo restores that same selected setup.
        tx.ModifyComponent(jobID,CFiveAxisJobComponent::S_ClassName,
            {{"DefaultSetupID",nextSetup},{"DefaultPlacementID",nextPlacement},{"InputRevision",Next(job->GetInputRevision())}});
    },"删除工件");
    for(const auto& plan:retiredPlans)if(const auto generation=runtime->IntentGenerations.find(plan);generation!=runtime->IntentGenerations.end())
    {generation->second->Cancel();runtime->IntentGenerations.erase(generation);}
    for(auto& [_,task]:runtime->Tasks)if(ownedSetups.contains(task.SetupID)&&task.Status=="Running")task.Cancelled->store(true);
    Map result{{"placementId",request.at("placementId")},{"workpieceId",iCAX::Data::to_string(workpieceID)},
        {"nextPlacementId",nextPlacement.is_nil()?Text():iCAX::Data::to_string(nextPlacement)},
        {"removedCuts",static_cast<UInt>(cuts.size())},{"removedFixtures",static_cast<UInt>(fixtures.size())}};
    runtime->Receipts.emplace(commandID,std::make_pair(payload,result));
    return result;
}

Map SetSetupLocked(Scene& scene,const Map& request)
{
    CutFields(request,{"commandId","expected","placementId","locked"});
    const auto runtime=Runtime(scene);
    const auto commandID=iCAX::Data::to_string(Identifier(request,"commandId"));
    const auto payload=Text("SetSetupLocked:")+iCAX::Data::VariantSerializer::Serialize(request);
    if(const auto prior=runtime->Receipts.find(commandID);prior!=runtime->Receipts.end()){
        if(prior->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return prior->second.second;
    }
    for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==commandID)throw std::invalid_argument("CAM_COMMAND_REUSED");
    CheckExpected(*runtime,request);CheckCapacity(*runtime);
    for(const auto& [_,task]:runtime->Tasks)if(task.Status=="Running"&&task.Kind!="RapidPreview")throw std::runtime_error("CAM_WORKPIECE_BUSY");
    auto& db=scene.Database();const auto placement=Require<CFiveAxisPlacementComponent>(db,Identifier(request,"placementId"));
    const auto setup=Require<CFiveAxisSetupComponent>(db,placement->GetSetupID());
    const bool locked=request.at("locked").To<bool>();
    if(locked!=setup->GetLocked())Transaction(db,locked?"锁定装夹姿态":"解锁装夹姿态",[&](auto& tx){
        // Lock metadata does not change machining inputs or invalidate output.
        tx.ModifyComponent(placement->GetSetupID(),CFiveAxisSetupComponent::S_ClassName,{{"Locked",locked}});
    });
    Map result{{"placementId",request.at("placementId")},{"locked",locked}};
    runtime->Receipts.emplace(commandID,std::make_pair(payload,result));return result;
}

Map SetWorkpieceEnabled(Scene& scene,const Map& request)
{
    CutFields(request,{"commandId","expected","placementId","enabled"});
    const auto runtime=Runtime(scene);
    const auto commandID=iCAX::Data::to_string(Identifier(request,"commandId"));
    const auto payload=Text("SetWorkpieceEnabled:")+iCAX::Data::VariantSerializer::Serialize(request);
    if(const auto prior=runtime->Receipts.find(commandID);prior!=runtime->Receipts.end()){
        if(prior->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return prior->second.second;
    }
    for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==commandID)throw std::invalid_argument("CAM_COMMAND_REUSED");
    CheckExpected(*runtime,request);CheckCapacity(*runtime);
    for(const auto& [_,task]:runtime->Tasks)if(task.Status=="Running"&&task.Kind!="RapidPreview")throw std::runtime_error("CAM_WORKPIECE_BUSY");
    const auto placement=Require<CFiveAxisPlacementComponent>(scene.Database(),Identifier(request,"placementId"));
    const auto workpiece=Require<CFiveAxisWorkpieceComponent>(scene.Database(),placement->GetWorkpieceID());
    const bool enabled=request.at("enabled").To<bool>();
    if(enabled!=workpiece->GetEnabled())Transaction(scene.Database(),enabled?"启用工件":"禁用工件",[&](auto& tx){
        // Preserve each cut's own switches and cached geometry. Effective enable
        // and generated-state projection exclude the disabled workpiece.
        tx.ModifyComponent(placement->GetWorkpieceID(),CFiveAxisWorkpieceComponent::S_ClassName,{{"Enabled",enabled}});
    });
    Map result{{"placementId",request.at("placementId")},{"enabled",enabled}};
    runtime->Receipts.emplace(commandID,std::make_pair(payload,result));return result;
}
