TEST(FiveAxisFixtureDraft, GenerationAndCancellationLeaveEveryCommittedFactAndHistoryUnchanged)
{
    Scene scene;File source(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),-60,60,-60,60).Face());Imported(scene,source);
    const auto before=Read(scene);const auto history=scene.Database().GetUndoArray().size(),resources=scene.Resources().GetManifest(false).size();
    auto request=FixtureRequest(scene);request["recipe"]=PlateRecipe(scene);request["newUnit"]=true;
    const auto task=Invoke(&scene,"CamFixture","Generate",request),done=Wait(scene,task);
    ASSERT_EQ("Completed",done.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(done);
    const auto candidate=FixtureQueryDesign(scene,Map{{"taskId",task.at("taskId")}}).at("candidate").To<Map>();
    EXPECT_FALSE(candidate.at("parts").To<Array>().empty());EXPECT_EQ(before,Read(scene));EXPECT_EQ(history,scene.Database().GetUndoArray().size());EXPECT_EQ(resources,scene.Resources().GetManifest(false).size());
    const Map cancel{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}};Invoke(&scene,"CamFixture","CancelTask",cancel);
    auto confirm=FixtureRequest(scene);confirm["taskId"]=task.at("taskId");confirm["candidateId"]=candidate.at("id");confirm["supportReviewed"]=true;
    EXPECT_THROW(Invoke(&scene,"CamFixture","Confirm",confirm),std::exception);EXPECT_EQ(before,Read(scene));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
}
TEST(FiveAxisFixtureDraft, ConfirmationIsOneUndoableCommitAndAddingAnotherUnitPreservesTheFirst)
{
    Scene scene;File source(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),-60,60,-60,60).Face());Imported(scene,source);
    const auto before=Read(scene);const auto history=scene.Database().GetUndoArray().size();
    const auto generate=[&](){auto request=FixtureRequest(scene);request["recipe"]=PlateRecipe(scene);request["newUnit"]=true;const auto task=Invoke(&scene,"CamFixture","Generate",request);const auto done=Wait(scene,task);
        if(done.at("status").To<Text>()!="Completed")throw std::runtime_error(iCAX::Data::VariantSerializer::Serialize(done));
        auto confirm=FixtureRequest(scene);confirm["taskId"]=task.at("taskId");confirm["candidateId"]=done.at("result").To<Map>().at("candidateId");confirm["supportReviewed"]=true;return confirm;};
    const auto confirm=generate();const auto first=Invoke(&scene,"CamFixture","Confirm",confirm),adopted=Read(scene);
    EXPECT_EQ(first,Invoke(&scene,"CamFixture","Confirm",confirm));EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
    EXPECT_EQ(1u,adopted.at("fixtureUnits").To<Array>().size());EXPECT_EQ(before.at("workpieces").To<Array>().front().To<Map>().at("geometryRef"),adopted.at("workpieces").To<Array>().front().To<Map>().at("geometryRef"));
    ASSERT_TRUE(scene.Database().Undo());const auto undone=Read(scene);for(const auto* key:{"workpieces","placements","setups","fixtures","fixtureUnits"})EXPECT_EQ(before.at(key),undone.at(key));ASSERT_TRUE(scene.Database().Redo());
    const auto secondRequest=generate();Invoke(&scene,"CamFixture","Confirm",secondRequest);const auto second=Read(scene);
    EXPECT_EQ(2u,second.at("fixtureUnits").To<Array>().size());EXPECT_EQ(adopted.at("fixtures").To<Array>().size()*2,second.at("fixtures").To<Array>().size());
    const auto secondFixtures=second.at("fixtures").To<Array>();
    for(const auto& part:adopted.at("fixtures").To<Array>())EXPECT_NE(secondFixtures.end(),std::find(secondFixtures.begin(),secondFixtures.end(),part));
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="确定夹具后保存";auto path=source.Path;path.replace_extension(".i5cam");
    file.Save(path,info,scene.Database(),scene.Resources());Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(second.at("fixtures"),Read(reopened).at("fixtures"));EXPECT_EQ(second.at("fixtureUnits"),Read(reopened).at("fixtureUnits"));std::filesystem::remove(path);
}
