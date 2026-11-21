TEST(FiveAxisWorkpieceRemoval, RemovesOwnedMachiningAtomicallyWithoutChangingOtherWorkpieces)
{
    Scene scene;File source(RecognitionPart()),fixture(BRepPrimAPI_MakeBox(5,5,5).Shape());
    const auto first=Imported(scene,source);
    const auto created=ConfirmWithPolicies(scene,.2,&first);ZeroGenerationPolicies(scene,created.at("createdIntentIds").To<Array>());
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>());
    ImportedFixture(scene,fixture);
    const auto firstSetup=SetupState(scene,first.at("setupId"));
    const auto second=Imported(scene,source);ConfirmWithPolicies(scene,.2,&second);ImportedFixture(scene,fixture);
    const auto before=Read(scene);const auto history=scene.Database().GetUndoArray().size();
    auto request=ActivateRequest(scene,second);request["setupId"]=second.at("setupId");
    const auto removed=Invoke(&scene,"CamWorkpiece","Remove",request);
    const auto after=Read(scene);
    EXPECT_EQ(1u,after.at("workpieces").To<Array>().size());EXPECT_EQ(2u,after.at("placements").To<Array>().size());
    EXPECT_EQ(1u,after.at("setups").To<Array>().size());EXPECT_EQ(1u,after.at("fixtures").To<Array>().size());
    EXPECT_EQ(first.at("setupId"),after.at("activeSetupId"));EXPECT_EQ(firstSetup,SetupState(scene,first.at("setupId")));
    for(const auto& c:after.at("intentToolpaths").To<Array>())EXPECT_EQ(first.at("placementId"),c.To<Map>().at("placementId"));
    EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
    EXPECT_EQ(removed,Invoke(&scene,"CamWorkpiece","Remove",request));EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
    auto reused=request;reused["placementId"]=first.at("placementId");EXPECT_THROW(Invoke(&scene,"CamWorkpiece","Remove",reused),std::exception);
    ASSERT_TRUE(scene.Database().Undo());
    for(const auto* key:{"workpieces","placements","setups","intentToolpaths","fixtures","activeSetupId"})EXPECT_EQ(before.at(key),Read(scene).at(key))<<key;
    ASSERT_TRUE(scene.Database().Redo());
    for(const auto* key:{"workpieces","placements","setups","intentToolpaths","fixtures","activeSetupId"})EXPECT_EQ(after.at(key),Read(scene).at(key))<<key;
}

TEST(FiveAxisWorkpieceRemoval, LastDeletionSupportsEmptyProjectPersistenceAndAnotherImport)
{
    Scene scene;File source(BRepPrimAPI_MakeBox(40,60,20).Shape());const auto part=Imported(scene,source);
    auto request=ActivateRequest(scene,part);request["setupId"]=part.at("setupId");
    auto mismatch=request;mismatch["setupId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());EXPECT_THROW(Invoke(&scene,"CamWorkpiece","Remove",mismatch),std::exception);
    const auto original=Read(scene);Invoke(&scene,"CamWorkpiece","Remove",request);const auto empty=Read(scene);
    for(const auto* key:{"workpieces","placements","intentToolpaths","fixtures"})EXPECT_TRUE(empty.at(key).To<Array>().empty());
    EXPECT_TRUE(empty.at("setups").To<Array>().empty());EXPECT_TRUE(empty.at("activeSetupId").To<Text>().empty());
    auto stale=ActivateRequest(scene,part);stale["setupId"]=part.at("setupId");stale["expected"]=original.at("expected");EXPECT_THROW(Invoke(&scene,"CamWorkpiece","Remove",stale),std::exception);
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="删除最后工件";
    auto path=source.Path;path.replace_extension(".i5cam");file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    for(const auto* key:{"workpieces","placements","setups","activeSetupId"})EXPECT_EQ(empty.at(key),Read(reopened).at(key));
    Imported(reopened,source);EXPECT_EQ(1u,Read(reopened).at("workpieces").To<Array>().size());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(original.at("placements"),Read(scene).at("placements"));
    std::filesystem::remove(path);
}

TEST(FiveAxisWorkpieceRemoval, RejectedDeletionRollsBackAndCanRetryWithTheSameCommand)
{
    class RejectRemoval final:public iCAX::Database::IChecker {
    public:
        bool Reject=true;
        bool AllowAttachByName(const iCAX::Database::IEntity&,const Text&,Text&)override{return true;}
        bool AllowRemoveByName(const iCAX::Database::IEntity&,const Text&,Text& error)override{if(Reject){Reject=false;error="Injected removal rejection";return false;}return true;}
        bool AllowModify(const iCAX::Database::CComponentBase&,const iCAX::Data::PropertySet&,Text&)override{return true;}
    };
    Scene scene;File source(RecognitionPart());const auto part=Imported(scene,source);ConfirmWithPolicies(scene,.2,&part);
    const auto before=Read(scene);auto request=ActivateRequest(scene,part);request["setupId"]=part.at("setupId");
    scene.Database().GetMetaRegistry()->RegistChecker<CFiveAxisPlacementComponent>(std::make_shared<RejectRemoval>());
    EXPECT_THROW(Invoke(&scene,"CamWorkpiece","Remove",request),std::exception);
    for(const auto* key:{"workpieces","placements","setups","intentToolpaths","activeSetupId"})EXPECT_EQ(before.at(key),Read(scene).at(key));
    request["expected"]=Read(scene).at("expected");Invoke(&scene,"CamWorkpiece","Remove",request);EXPECT_TRUE(Read(scene).at("workpieces").To<Array>().empty());
}
