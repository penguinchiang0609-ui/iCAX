namespace {
    Map ActivateRequest(Scene& scene,const Map& part) {
        return {{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")},{"placementId",part.at("placementId")}};
    }
    Map SetupState(Scene& scene,const Variant& setupID) {
        for(const auto& s:Read(scene).at("setups").To<Array>())if(s.To<Map>().at("id")==setupID)return s.To<Map>();
        throw std::runtime_error("Missing setup");
    }
}
TEST(FiveAxisMultiWorkpiece, IndependentMachiningSelectionAndEmbeddedProjectPersistence)
{
    Scene scene;File source(RecognitionPart());Array cuts;std::vector<Map> parts,results;
    for(int i=0;i<3;++i){
        parts.push_back(Imported(scene,source));
        EXPECT_EQ(parts.back().at("setupId"),Read(scene).at("activeSetupId"));
        const auto created=ConfirmWithPolicies(scene,.2,&parts.back());
        cuts.push_back(created.at("createdIntentIds").To<Array>().front());
        for(const auto& c:Read(scene).at("intentToolpaths").To<Array>())if(c.To<Map>().at("id")==cuts.back())EXPECT_EQ("1",c.To<Map>().at("number").To<Text>());
        if(i)Invoke(&scene,"CamPlacement","SetTransform",PlacementRequest(scene,parts.back()));
        ZeroGenerationPolicies(scene,{cuts.back()});
        const auto done=Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene)));
        ASSERT_EQ("Completed",done.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(done);
        const auto generated=Generated(scene);const auto operations=generated.at("operations").To<Array>();
        ASSERT_EQ(1u,operations.size());EXPECT_EQ(parts.back().at("placementId"),operations.front().To<Map>().at("placementId"));
        EXPECT_TRUE(generated.at("transitions").To<Array>().empty());
        results.push_back(generated);
        for(int prior=0;prior<i;++prior)EXPECT_EQ(results[prior],SetupState(scene,parts[prior].at("setupId")).at("generatedGeometry").To<Map>());
    }
    const auto before=Read(scene);ASSERT_EQ(3u,before.at("setups").To<Array>().size());
    for(int i=0;i<3;++i){EXPECT_EQ(parts[i].at("workpieceId"),before.at("workpieces").To<Array>()[i].To<Map>().at("id"));
        EXPECT_EQ(parts[i].at("placementId"),before.at("placements").To<Array>()[i].To<Map>().at("id"));}
    const auto request=ActivateRequest(scene,parts[0]);const auto undo=scene.Database().GetUndoArray().size();
    const auto activated=Invoke(&scene,"CamWorkpiece","Activate",request);EXPECT_TRUE(activated.at("changed").To<bool>());
    EXPECT_EQ(activated,Invoke(&scene,"CamWorkpiece","Activate",request));EXPECT_EQ(undo,scene.Database().GetUndoArray().size());
    EXPECT_EQ(parts[0].at("setupId"),Read(scene).at("activeSetupId"));EXPECT_EQ(before.at("inputRevision"),Read(scene).at("inputRevision"));
    EXPECT_EQ(results[0],Generated(scene));
    auto reused=request;reused["placementId"]=parts[1].at("placementId");EXPECT_THROW(Invoke(&scene,"CamWorkpiece","Activate",reused),std::exception);
    auto stale=request;stale["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());EXPECT_THROW(Invoke(&scene,"CamWorkpiece","Activate",stale),std::exception);
    // Changing one part only invalidates its own generated geometry.
    auto move=PlacementRequest(scene,parts[1]);auto matrix=move.at("transform").To<Array>();matrix[3]=250.;move["transform"]=matrix;
    Invoke(&scene,"CamPlacement","SetTransform",move);
    EXPECT_EQ("Stale",SetupState(scene,parts[1].at("setupId")).at("generatedGeometry").To<Map>().at("status").To<Text>());
    EXPECT_EQ(results[0],Generated(scene));EXPECT_EQ(results[2],SetupState(scene,parts[2].at("setupId")).at("generatedGeometry").To<Map>());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(results[1],SetupState(scene,parts[1].at("setupId")).at("generatedGeometry").To<Map>());
    // Switching must neither add an undo command nor destroy the redo edit.
    Invoke(&scene,"CamWorkpiece","Activate",ActivateRequest(scene,parts[1]));ASSERT_TRUE(scene.Database().Redo());
    EXPECT_EQ("Stale",Generated(scene).at("status").To<Text>());ASSERT_TRUE(scene.Database().Undo());
    Invoke(&scene,"CamWorkpiece","Activate",ActivateRequest(scene,parts[0]));
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="多个独立加工工件";
    auto path=source.Path;path.replace_extension(".i5cam");file.Save(path,info,scene.Database(),scene.Resources());
    ASSERT_TRUE(std::filesystem::remove(source.Path));Scene reopened(scene.Project,scene.GetSceneID(),false);
    file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    for(const auto* key:{"workpieces","placements","intentToolpaths","activeSetupId"})EXPECT_EQ(Read(scene).at(key),Read(reopened).at(key));
    for(int i=0;i<3;++i)EXPECT_EQ(results[i],SetupState(reopened,parts[i].at("setupId")).at("generatedGeometry").To<Map>());
    std::filesystem::remove(path);
}
TEST(FiveAxisMultiWorkpiece, FixturesAndCollisionChecksBelongToOneWorkpiece)
{
    Scene scene;File source(BRepPrimAPI_MakeBox(40,40,10).Shape()),fixture(BRepPrimAPI_MakeBox(5,5,5).Shape());
    const auto first=Imported(scene,source);const auto fixtureID=ImportedFixture(scene,fixture);
    const auto checked=CheckedFixtures(scene,0.);EXPECT_FALSE(checked.empty());
    const auto firstSetup=SetupState(scene,first.at("setupId"));const auto second=Imported(scene,source);
    EXPECT_EQ(firstSetup,SetupState(scene,first.at("setupId")));
    EXPECT_EQ(first.at("setupId"),Read(scene).at("fixtures").To<Array>().front().To<Map>().at("setupId"));
    EXPECT_NE(first.at("setupId"),second.at("setupId"));
    auto invalid=FixtureRequest(scene,fixtureID);invalid["visible"]=false;
    EXPECT_THROW(Invoke(&scene,"CamFixture","Update",invalid),std::exception);
    EXPECT_EQ(firstSetup,SetupState(scene,first.at("setupId")));
}
