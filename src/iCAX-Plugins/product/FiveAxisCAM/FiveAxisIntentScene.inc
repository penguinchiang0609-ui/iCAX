std::unique_ptr<iCAX::CAM::Intent::CEditableToolpathDocument> PrepareFiveAxisIntentScene(Scene& scene,
    std::shared_ptr<iCAX::CAM::Intent::CToolpathOperationRegistry> registry,
    std::shared_ptr<const iCAX::CAM::Intent::IToolpathGeometryServices> geometry)
{
    using namespace iCAX::CAM::Intent;
    if(scene.IsStopRequested())throw std::runtime_error("CAM_SCENE_CLOSING");
    auto& db=scene.Database();if(db.IsWriteInProgress())throw std::invalid_argument("CAM_INTENT_INITIALIZATION_WRITE_IN_PROGRESS");
    const auto root=db.GetMetaEntity();if(!root)throw std::invalid_argument("CAM_SCENE_META_MISSING");
    const auto job=Require<CFiveAxisJobComponent>(db,root->GetID());
    std::map<ID,std::shared_ptr<CFiveAxisSetupComponent>> setups;
    for(const auto& entity:db.FilterEntities([](auto){return true;}))
        if(const auto setup=Component<CFiveAxisSetupComponent>(entity);setup&&setup->GetJobID()==root->GetID())setups[entity->GetID()]=setup;
    if(!job->GetIntentDocumentID().is_nil())
    {
        auto document=std::make_unique<CEditableToolpathDocument>(root->GetRepository(),job->GetIntentDocumentID(),registry,geometry);
        for(const auto& [id,setup]:setups)
        {
            if(setup->GetIntentPlanID().is_nil())throw std::invalid_argument("CAM_INTENT_SETUP_PLAN_MISSING");
            (void)document->ReadPlan(setup->GetIntentPlanID());
        }
        return document;
    }
    const auto documentId=iCAX::Data::GenerateNewUUID();auto document=CEditableToolpathDocument::PrepareNewDocument(root->GetRepository(),documentId,registry,geometry);
    std::map<ID,ID> plans;std::vector<Map> commands;
    for(const auto& [setupId,setup]:setups)
    {
        const auto plan=iCAX::Data::GenerateNewUUID();plans[setupId]=plan;
        commands.push_back({{"kind",Text("CreatePlan")},{"commandId",iCAX::Data::GenerateNewUUID()},{"planId",plan},{"name",setup->GetName()}});
    }
    // An independent workpiece needs no persisted machining document/plan.
    // The prepared empty document is attached with the first explicit setup.
    if(commands.empty())return document;
    SEditableToolpathHostEdit host;host.UndoName="初始化统一刀路";host.RecordUndo=false;
    host.Stage=[root,documentId,plans](auto& transaction,const auto&)
    {
        transaction.ModifyComponent(root->GetID(),CFiveAxisJobComponent::S_ClassName,{{"IntentDocumentID",documentId}});
        for(const auto& [setup,plan]:plans)transaction.ModifyComponent(setup,CFiveAxisSetupComponent::S_ClassName,{{"IntentPlanID",plan}});
    };
    document->ExecuteBatch(commands,host);return document;
}
