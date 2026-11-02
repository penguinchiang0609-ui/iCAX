TEST(FiveAxisEngineeringData, BusinessValuesPersistWithoutResourcesAndDefinitionContentEditsInvalidateWithTheSameContext)
{
    Scene scene;PrepareOrderPart(scene);const auto before=Generated(scene);
    const auto inventory=EngineeringStorageAudit(scene);
    EXPECT_EQ(inventory.at("cutCount"),inventory.at("completeCutDataCount"));
    EXPECT_EQ("0",inventory.at("legacyOwnerCount").To<Text>());
    EXPECT_TRUE(inventory.at("persistentCamSchemas").To<Array>().empty());
    const auto id=ID::from_string(OrderedIDs(before).front().To<Text>()).value();
    using iCAX::CAM::Intent::Editable::CEditableOperationComponent;
    std::shared_ptr<CEditableOperationComponent> component;ID operation;
    for(const auto& entity:scene.Database().FilterEntities([](auto){return true;}))
        if(const auto candidate=std::dynamic_pointer_cast<CEditableOperationComponent>(entity->GetComponent(CEditableOperationComponent::S_ClassName));candidate&&candidate->GetDefinition().at("typeId").To<Text>()=="fiveaxis.pose"){
            const auto input=candidate->GetDefinition().at("inputs").To<Array>().front().To<Map>().at("source").To<Map>();
            const auto sourceID=iCAX::CAM::Intent::PortFromData(input).OperationId;
            const auto source=std::dynamic_pointer_cast<CEditableOperationComponent>(scene.Database().GetEntity(sourceID)->GetComponent(CEditableOperationComponent::S_ClassName));
            if(source&&source->GetDefinition().at("sourcePathId")==Variant(id)){component=candidate;operation=entity->GetID();break;}
        }
    ASSERT_TRUE(component);auto definition=component->GetDefinition();const auto context=definition.at("context");
    auto parameters=definition.at("parameters").To<Map>(),policy=parameters.at("policy").To<Map>();policy["leadAngleRad"]=.02;parameters["policy"]=policy;definition["parameters"]=parameters;
    const auto undo=scene.Database().BeginUndoCommand("Edit current operation business value");
    auto& tx=scene.Database().BeginTransaction("Edit pose content without changing context");
    tx.ModifyComponent(operation,CEditableOperationComponent::S_ClassName,{{"Definition",definition}});
    ASSERT_TRUE(scene.Database().CommitTransaction(tx));undo->End();
    EXPECT_EQ(context,component->GetDefinition().at("context"));
    EXPECT_EQ("Stale",AxisOperation(Generated(scene),iCAX::Data::to_string(id)).at("status").To<Text>());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before,Generated(scene));
    ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ("Stale",Generated(scene).at("status").To<Text>());
    ASSERT_TRUE(scene.Database().Undo());
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Engineering data persistence";
    const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");
    file.Save(path,info,scene.Database(),scene.Resources());Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(before,Generated(reopened));EXPECT_EQ(inventory,EngineeringStorageAudit(reopened));std::filesystem::remove(path);
}
