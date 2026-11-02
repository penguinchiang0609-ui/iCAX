TEST(FiveAxisManualSetups, ImportHasNoSetupOrPlanAndLastRemovalKeepsOnlyWorkpiece)
{
    Scene scene;File source(BRepPrimAPI_MakeBox(40,60,20).Shape());
    EXPECT_TRUE(Read(scene).at("setups").To<Array>().empty());
    const auto ids=ImportedWorkpiece(scene,source),initial=Read(scene);
    EXPECT_TRUE(initial.at("setups").To<Array>().empty());EXPECT_TRUE(initial.at("fixtures").To<Array>().empty());
    EXPECT_TRUE(initial.at("activeSetupId").To<Text>().empty());
    EXPECT_TRUE(scene.Database().FilterEntities([](auto e){return e->HasComponent("CEditablePlanComponent");}).empty());
    Map request{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",initial.at("expected")},
        {"placementId",ids.at("placementId")},{"action",Text("create")},{"name",Text("装夹 1")}};
    const auto added=Invoke(&scene,"CamSetup","Manage",request),withSetup=Read(scene);
    EXPECT_EQ(1u,withSetup.at("setups").To<Array>().size());EXPECT_EQ(ids.at("placementId"),added.at("placementId"));
    EXPECT_EQ(initial.at("workpieces"),withSetup.at("workpieces"));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_TRUE(Read(scene).at("setups").To<Array>().empty());
    ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(withSetup.at("placements"),Read(scene).at("placements"));
    request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());request["expected"]=Read(scene).at("expected");request["action"]=Text("remove");request.erase("name");
    const auto removed=Invoke(&scene,"CamSetup","Manage",request),after=Read(scene);
    EXPECT_TRUE(removed.at("setupId").To<Text>().empty());EXPECT_TRUE(after.at("setups").To<Array>().empty());
    EXPECT_EQ(initial.at("workpieces"),after.at("workpieces"));EXPECT_EQ(1u,after.at("placements").To<Array>().size());
    EXPECT_EQ(ids.at("placementId"),after.at("activePlacementId"));EXPECT_TRUE(after.at("intentGenerationTasks").To<Array>().empty());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(withSetup.at("setups"),Read(scene).at("setups"));
    ASSERT_TRUE(scene.Database().Redo());EXPECT_TRUE(Read(scene).at("setups").To<Array>().empty());
}
TEST(FiveAxisManualSetups, StandaloneSelectionCopyLockEnableDeleteAndReopen)
{
    Scene scene;File source;const auto first=ImportedWorkpiece(scene,source),second=ImportedWorkpiece(scene,source);
    const auto command=[&](const Map& ids){return Map{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")},{"placementId",ids.at("placementId")}};};
    Invoke(&scene,"CamWorkpiece","Activate",command(first));EXPECT_EQ(first.at("placementId"),Read(scene).at("activePlacementId"));
    auto lock=command(first);lock["locked"]=true;Invoke(&scene,"CamWorkpiece","SetLocked",lock);
    auto remove=command(first);remove["setupId"]=Text();EXPECT_THROW(Invoke(&scene,"CamWorkpiece","Remove",remove),std::exception);
    lock=command(first);lock["locked"]=false;Invoke(&scene,"CamWorkpiece","SetLocked",lock);
    auto enable=command(first);enable["enabled"]=false;Invoke(&scene,"CamWorkpiece","SetEnabled",enable);
    enable=command(first);enable["enabled"]=true;Invoke(&scene,"CamWorkpiece","SetEnabled",enable);
    const auto copy=Invoke(&scene,"CamWorkpiece","Copy",command(first));EXPECT_TRUE(copy.at("setupId").To<Text>().empty());
    EXPECT_EQ(3u,Read(scene).at("workpieces").To<Array>().size());EXPECT_TRUE(Read(scene).at("setups").To<Array>().empty());
    remove=command(copy);remove["setupId"]=Text();Invoke(&scene,"CamWorkpiece","Remove",remove);
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(3u,Read(scene).at("workpieces").To<Array>().size());ASSERT_TRUE(scene.Database().Redo());
    const auto before=Read(scene);auto output=source.Path;output.replace_extension(".i5cam");
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="手动新增装夹";
    file.Save(output,info,scene.Database(),scene.Resources());Scene reopened(scene.Project,scene.GetSceneID(),false);
    file.Open(output,reopened.Database(),reopened.Resources());InitializeScene(reopened);const auto after=Read(reopened);
    EXPECT_EQ(before.at("workpieces"),after.at("workpieces"));EXPECT_EQ(before.at("placements"),after.at("placements"));
    EXPECT_EQ(before.at("activePlacementId"),after.at("activePlacementId"));EXPECT_TRUE(after.at("setups").To<Array>().empty());
    std::filesystem::remove(output);
    for(const auto& ids:{first,second}){remove=command(ids);remove["setupId"]=Text();Invoke(&scene,"CamWorkpiece","Remove",remove);}
    EXPECT_TRUE(Read(scene).at("workpieces").To<Array>().empty());EXPECT_TRUE(Read(scene).at("setups").To<Array>().empty());
}
TEST(FiveAxisManualSetups, StandaloneScaleAndObjectFrameResetNeedNoSetup)
{
    Scene scene;File source(BRepPrimAPI_MakeBox(40,60,20).Shape());const auto ids=ImportedWorkpiece(scene,source);auto state=Read(scene);
    Map request{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",state.at("expected")},{"placementId",ids.at("placementId")},{"setupId",Text()},
        {"sourceGeometry",state.at("workpieces").To<Array>().front().To<Map>().at("geometryRef")},{"factors",Array{2.,2.,2.}},{"center",Array{20.,30.,10.}}};
    const auto result=Wait(scene,Invoke(&scene,"CamWorkpiece","Scale",request));ASSERT_EQ("Completed",result.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(result);
    state=Read(scene);EXPECT_TRUE(state.at("setups").To<Array>().empty());
    const auto features=Invoke(&scene,"CamScene","ReadSnapFeatures",{{"expected",state.at("expected")},{"setupId",Text()},{"objectId",ids.at("placementId")}});
    Map chosen;for(const auto& v:features.at("features").To<Array>()){const auto f=v.To<Map>();if(f.at("mode").To<Text>()=="plane"){chosen={{"featureId",f.at("featureId")},{"mode",f.at("mode")},{"frame",Text("Workpiece")}};break;}}
    ASSERT_FALSE(chosen.empty());
    request={{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",state.at("expected")},{"placementId",ids.at("placementId")},{"setupId",Text()},{"resetWorkpieceFrame",true},
        {"registration",Map{{"method",Text("Alignment")},{"modelFrame",Text("Workpiece")},{"targetFrame",Text("Setup")},{"unit",Text("mm")},
            {"steps",Array{Map{{"kind",Text("plane")},{"source",chosen},{"target",Map{{"axis",Text("Y")}}},{"contact",true}}}}}}};
    EXPECT_NO_THROW(Invoke(&scene,"CamPlacement","ApplyRegistration",request));EXPECT_TRUE(Read(scene).at("setups").To<Array>().empty());
}
TEST(FiveAxisManualSetups, PlacementReceiptIsImmediateAndDoesNotGenerateOrChangeCad)
{
    Scene scene;File source;const auto ids=Imported(scene,source),before=Read(scene);auto request=PlacementRequest(scene,ids);
    const auto result=Invoke(&scene,"CamPlacement","SetTransform",request),patch=result.at("statePatch").To<Map>();
    EXPECT_EQ(result,Invoke(&scene,"CamPlacement","SetTransform",request));
    const auto after=Read(scene);EXPECT_EQ(after.at("expected"),patch.at("expected"));
    EXPECT_EQ(before.at("workpieces"),after.at("workpieces"));EXPECT_EQ(request.at("transform"),patch.at("placement").To<Map>().at("transform"));
    EXPECT_TRUE(after.at("intentGenerationTasks").To<Array>().empty());EXPECT_TRUE(after.at("intentGenerationErrors").To<Array>().empty());
}
