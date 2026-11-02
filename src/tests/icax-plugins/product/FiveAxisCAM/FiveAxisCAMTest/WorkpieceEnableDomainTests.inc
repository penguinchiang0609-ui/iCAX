TEST(FiveAxisWorkpieceEnable, ExcludesMachiningAndRestoresCachedPathsWithoutChangingCutSwitches)
{
    Scene scene;File source(RecognitionPart());const auto part=Imported(scene,source);
    const auto created=ConfirmWithPolicies(scene,.2,&part);const auto ids=created.at("createdIntentIds").To<Array>();ZeroGenerationPolicies(scene,ids);
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>());
    const auto generated=Generated(scene),before=Read(scene);ASSERT_EQ("Current",generated.at("status").To<Text>());
    auto request=ActivateRequest(scene,part);request["enabled"]=false;
    const auto history=scene.Database().GetUndoArray().size();const auto receipt=Invoke(&scene,"CamWorkpiece","SetEnabled",request);
    const auto disabled=Read(scene);EXPECT_FALSE(disabled.at("workpieces").To<Array>().front().To<Map>().at("enabled").To<bool>());
    EXPECT_EQ("Disabled",Generated(scene).at("status").To<Text>());EXPECT_TRUE(Generated(scene).at("operations").To<Array>().empty());
    EXPECT_EQ(before.at("placements"),disabled.at("placements"));EXPECT_EQ(before.at("inputRevision"),disabled.at("inputRevision"));
    for(const auto& c:disabled.at("intentToolpaths").To<Array>()){EXPECT_FALSE(c.To<Map>().at("enabled").To<bool>());EXPECT_TRUE(c.To<Map>().at("ownEnabled").To<bool>());}
    EXPECT_EQ(receipt,Invoke(&scene,"CamWorkpiece","SetEnabled",request));EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
    auto reused=request;reused["enabled"]=true;EXPECT_THROW(Invoke(&scene,"CamWorkpiece","SetEnabled",reused),std::exception);
    for(const auto* scope:{"CamPlan","CamRapid","CamSimulation"}){
        if(Text(scope)=="CamPlan"){
            const auto terminal=Wait(scene,Invoke(&scene,scope,"Generate",GenerateRequest(scene)));
            EXPECT_EQ("Failed",terminal.at("status").To<Text>());EXPECT_NE(Text::npos,terminal.at("error").To<Text>().find("CAM_WORKPIECE_DISABLED"));continue;
        }
        try{Invoke(&scene,scope,Text(scope)=="CamSimulation"?"CheckCollision":"Generate",GenerateRequest(scene));FAIL()<<scope;}
        catch(const std::exception& error){EXPECT_NE(Text::npos,Text(error.what()).find("CAM_WORKPIECE_DISABLED"))<<scope<<": "<<error.what();}
    }
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before.at("workpieces"),Read(scene).at("workpieces"));EXPECT_EQ(generated,Generated(scene));
    ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(disabled.at("workpieces"),Read(scene).at("workpieces"));
    request=ActivateRequest(scene,part);request["enabled"]=true;Invoke(&scene,"CamWorkpiece","SetEnabled",request);EXPECT_EQ(generated,Generated(scene));
    auto cutRequest=GenerateRequest(scene);cutRequest["action"]=Text("Disable");cutRequest["intentIds"]=Array{ids.front()};
    Invoke(&scene,"CamCut","ManageTree",cutRequest);const auto ownDisabled=Read(scene).at("intentToolpaths");
    for(const auto enabled:{false,true}){request=ActivateRequest(scene,part);request["enabled"]=enabled;Invoke(&scene,"CamWorkpiece","SetEnabled",request);}
    EXPECT_EQ(ownDisabled,Read(scene).at("intentToolpaths"));
}

TEST(FiveAxisWorkpieceEnable, PreservesIndependentWorkpiecesLocksAndSavedDisabledState)
{
    Scene scene;File source(RecognitionPart());const auto first=Imported(scene,source),second=Imported(scene,source);
    auto lock=ActivateRequest(scene,first);lock["locked"]=true;Invoke(&scene,"CamSetup","SetLocked",lock);
    const auto before=Read(scene),secondSetup=SetupState(scene,second.at("setupId"));auto request=ActivateRequest(scene,first);request["enabled"]=false;Invoke(&scene,"CamWorkpiece","SetEnabled",request);
    const auto disabled=Read(scene);const auto workpieces=disabled.at("workpieces").To<Array>();
    EXPECT_FALSE(workpieces[0].To<Map>().at("enabled").To<bool>());EXPECT_TRUE(workpieces[0].To<Map>().at("locked").To<bool>());
    EXPECT_TRUE(workpieces[1].To<Map>().at("enabled").To<bool>());EXPECT_EQ(before.at("placements"),disabled.at("placements"));
    EXPECT_EQ(SetupState(scene,second.at("setupId")),secondSetup);
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="工件启用状态";
    auto path=source.Path;path.replace_extension(".i5cam");file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(disabled.at("workpieces"),Read(reopened).at("workpieces"));EXPECT_EQ(disabled.at("placements"),Read(reopened).at("placements"));
    std::filesystem::remove(path);
}
