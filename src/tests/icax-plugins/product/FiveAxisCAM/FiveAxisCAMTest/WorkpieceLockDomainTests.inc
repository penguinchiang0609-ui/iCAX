TEST(FiveAxisWorkpieceLock, ProtectsAllGeometryMutationsWhileMachiningAndPersistenceRemainAvailable)
{
    Scene scene;File source(RecognitionPart());const auto part=Imported(scene,source);
    const auto setLock=[&](bool locked){auto r=ActivateRequest(scene,part);r["locked"]=locked;return Invoke(&scene,"CamSetup","SetLocked",r);};
    const auto unlocked=Read(scene);setLock(true);
    EXPECT_TRUE(Read(scene).at("workpieces").To<Array>().front().To<Map>().at("locked").To<bool>());
    EXPECT_EQ(unlocked.at("inputRevision"),Read(scene).at("inputRevision"));
    EXPECT_EQ(unlocked.at("setups").To<Array>().front().To<Map>().at("inputRevision"),Read(scene).at("setups").To<Array>().front().To<Map>().at("inputRevision"));
    const auto created=ConfirmWithPolicies(scene,.2,&part);ZeroGenerationPolicies(scene,created.at("createdIntentIds").To<Array>());
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>());
    const auto generated=Generated(scene);EXPECT_EQ("Current",generated.at("status").To<Text>());
    const auto rejectLocked=[&](const char* name,const Map& request){
        try{Invoke(&scene,name==Text("SetTransform")||name==Text("ApplyRegistration")?"CamPlacement":"CamWorkpiece",name,request);FAIL()<<name;}
        catch(const std::exception& error){EXPECT_NE(Text::npos,Text(error.what()).find("LOCKED"))<<error.what();}
    };
    rejectLocked("SetTransform",PlacementRequest(scene,part));
    auto remove=ActivateRequest(scene,part);remove["setupId"]=part.at("setupId");rejectLocked("Remove",remove);
    auto scale=remove;scale["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());rejectLocked("Scale",scale);
    auto alignment=remove;alignment["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());rejectLocked("ApplyRegistration",alignment);
    EXPECT_EQ(generated,Generated(scene));setLock(false);EXPECT_EQ(generated,Generated(scene));setLock(true);EXPECT_EQ(generated,Generated(scene));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_FALSE(Read(scene).at("workpieces").To<Array>().front().To<Map>().at("locked").To<bool>());
    ASSERT_TRUE(scene.Database().Redo());EXPECT_TRUE(Read(scene).at("workpieces").To<Array>().front().To<Map>().at("locked").To<bool>());
    const auto saved=Read(scene);
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="全局工件锁";
    auto path=source.Path;path.replace_extension(".i5cam");file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(saved.at("workpieces"),Read(reopened).at("workpieces"));EXPECT_EQ(generated,Generated(reopened));
    std::filesystem::remove(path);
}
