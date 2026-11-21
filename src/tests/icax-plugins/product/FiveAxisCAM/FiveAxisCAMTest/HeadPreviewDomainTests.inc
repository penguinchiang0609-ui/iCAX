namespace head_preview_tests {
    Map Request(Scene& scene,const Text& kind){return {{"setupId",Read(scene).at("activeSetupId")},{"kind",kind}};}
    Map Preview(Scene& scene,const Map& request){return Invoke(&scene,"CamHead","Preview",request).at("head").To<Map>();}
    Map PersistentResources(Scene& scene){
        Map result;for(const auto& info:scene.Resources().GetManifest()){
            Array dependencies;for(const auto& ref:info.Dependencies)dependencies.emplace_back(Map{{"url",ref.URL},{"version",std::to_string(ref.nVersion)}});
            result[info.Key.Source+"@"+std::to_string(info.nVersion)]=Map{{"type",info.ResourceTypeID},{"hash",info.ContentHash},{"size",std::to_string(info.nSize)},{"persistence",static_cast<int>(info.Persistence)},{"dependencies",dependencies}};
        }return result;
    }
    void RuntimeVisuals(Scene& scene,const Map& head){
        for(const auto* name:{"geometryRef","previewRef"})if(head.contains(name)){
            const auto ref=head.at(name).To<Map>();const auto info=scene.Resources().GetInfo(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
            ASSERT_TRUE(info);EXPECT_EQ(iCAX::Resource::EResourcePersistenceMode::RuntimeOnly,info->Persistence);
        }
        if(head.contains("assembly"))for(const auto& value:head.at("assembly").To<Map>().at("nodes").To<Array>()){
            const auto node=value.To<Map>();const auto ref=node.at("previewRef").To<Map>();
            const auto info=scene.Resources().GetInfo(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
            ASSERT_TRUE(info);EXPECT_EQ(iCAX::Resource::EResourcePersistenceMode::RuntimeOnly,info->Persistence);
        }
    }
}
TEST(FiveAxisHeadPreview, BuiltinQueryKeepsLockedPoseBindingsHistoryAndSavedResourcesUnchanged){
    using namespace head_preview_tests;
    const auto project=iCAX::Data::GenerateNewUUID();Scene scene(project,project,true,true);PrepareOrderPart(scene);head_instance_tests::SelectAB(scene);
    const auto state=Read(scene);const auto setupID=*ID::from_string(state.at("activeSetupId").To<Text>());
    const auto setup=std::dynamic_pointer_cast<CFiveAxisSetupComponent>(scene.Database().GetEntity(setupID)->GetComponent(CFiveAxisSetupComponent::S_ClassName));
    ASSERT_TRUE(setup->SetProperty("Locked",true));
    const auto before=Read(scene);const auto undo=scene.Database().GetUndoArray(),redo=scene.Database().GetRedoArray();const auto manifest=PersistentResources(scene);
    auto request=Request(scene,"Builtin");request["builtinDefinitionId"]=Head(scene).at("builtinDefinitionId");
    const auto preview=Preview(scene,request);ASSERT_TRUE(preview.at("previewOnly").To<bool>());ASSERT_TRUE(preview.contains("assembly"));ASSERT_TRUE(preview.contains("motion"));
    EXPECT_EQ(Head(scene).at("tcpLocalPosition"),preview.at("tcpLocalPosition"));EXPECT_EQ(Head(scene).at("beamLocalDirection"),preview.at("beamLocalDirection"));
    EXPECT_EQ(head_instance_tests::Instance(scene).at("parameters"),preview.at("instance").To<Map>().at("parameters"));
    EXPECT_EQ(0u,preview.at("instance").To<Map>().at("revision").To<UInt>());RuntimeVisuals(scene,preview);
    EXPECT_EQ(preview,Preview(scene,request));EXPECT_EQ(before,Read(scene));EXPECT_EQ(undo,scene.Database().GetUndoArray());EXPECT_EQ(redo,scene.Database().GetRedoArray());
    EXPECT_EQ(manifest,PersistentResources(scene));
    auto assign=GenerateRequest(scene);assign["kind"]=Text("Builtin");assign["builtinDefinitionId"]=request.at("builtinDefinitionId");
    EXPECT_THROW(Invoke(&scene,"CamHead","Configure",assign),std::exception);EXPECT_EQ(before,Read(scene));
}
TEST(FiveAxisHeadPreview, InstanceAndPersonalConfigurationPreviewPreserveTheCurrentAssignedCalibration){
    using namespace head_preview_tests;using namespace head_instance_tests;
    auto store=TestFixtureLibraryStore();Scene scene;scene.FixtureLibraryStore=store;PrepareOrderPart(scene,true,"TableTable");
    File body(BRepPrimAPI_MakeBox(10,20,30).Shape());ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamHead","Import",ImportTestHead(scene,body))).at("status").To<Text>());
    auto parameters=Instance(scene).at("parameters").To<Map>();parameters["tcp"]=Array{1.,2.,3.};Apply(scene,parameters);const auto original=Head(scene);
    auto save=Command(scene,"save");save["name"]=Text("预览配置");const auto saved=Invoke(&scene,"CamHead","Configurations",save);
    InstanceAction(scene,"clone");parameters=Instance(scene).at("parameters").To<Map>();parameters["tcp"]=Array{4.,5.,6.};Apply(scene,parameters);
    const auto before=Read(scene);const auto undo=scene.Database().GetUndoArray(),redo=scene.Database().GetRedoArray();const auto manifest=PersistentResources(scene);
    auto request=Request(scene,"Instance");request["instanceId"]=original.at("instance").To<Map>().at("id");EXPECT_EQ(original,Preview(scene,request));
    request=Request(scene,"Configuration");request["entryId"]=saved.at("id");const auto preview=Preview(scene,request);
    EXPECT_TRUE(preview.at("previewOnly").To<bool>());EXPECT_EQ(original.at("instance").To<Map>().at("parameters"),preview.at("instance").To<Map>().at("parameters"));
    EXPECT_EQ(saved.at("name"),preview.at("instance").To<Map>().at("name"));EXPECT_EQ(original.at("tcpLocalPosition"),preview.at("tcpLocalPosition"));
    RuntimeVisuals(scene,preview);EXPECT_EQ(preview,Preview(scene,request));EXPECT_EQ(before,Read(scene));
    EXPECT_EQ(undo,scene.Database().GetUndoArray());EXPECT_EQ(redo,scene.Database().GetRedoArray());EXPECT_EQ(manifest,PersistentResources(scene));
    EXPECT_EQ((Array{4.,5.,6.}),Head(scene).at("tcpLocalPosition").To<Array>());
}
TEST(FiveAxisHeadPreview, QueriesRejectForeignInstancesMissingProfilesAndMixedSelectorsWithoutChangingThePose){
    using namespace head_preview_tests;using namespace head_instance_tests;
    auto store=TestFixtureLibraryStore();Scene scene;scene.FixtureLibraryStore=store;PrepareOrderPart(scene,true,"TableTable");File body(BRepPrimAPI_MakeBox(10,20,30).Shape());
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamHead","Import",ImportTestHead(scene,body))).at("status").To<Text>());
    const auto local=Instance(scene).at("id");Scene other;other.FixtureLibraryStore=store;PrepareOrderPart(other,true,"TableTable");const auto before=Read(other);
    auto request=Request(other,"Instance");request["instanceId"]=local;EXPECT_THROW(Preview(other,request),std::exception);
    request=Request(other,"Configuration");request["entryId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());EXPECT_THROW(Preview(other,request),std::exception);
    request=Request(other,"Instance");request["instanceId"]=local;request["builtinDefinitionId"]=Text("unrelated");EXPECT_THROW(Preview(other,request),std::exception);
    request=Request(other,"Invalid");EXPECT_THROW(Preview(other,request),std::exception);
    request=Request(other,"Instance");request["instanceId"]=local;request["setupId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());EXPECT_THROW(Preview(other,request),std::exception);
    EXPECT_EQ(before,Read(other));
}
