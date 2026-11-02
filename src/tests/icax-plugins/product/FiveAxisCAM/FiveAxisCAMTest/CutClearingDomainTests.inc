TEST(FiveAxisCutClearing, SelectiveClearPreservesStartDirectionMaterialPoseAndLibrary)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);
    for(std::size_t i=0;i<ids.size();++i){
        auto start=DefinitionItem(scene,ids[i]).at("start").To<Map>();start["nativeParameter"]=1.2+i;
        const Map patch{{"start",start},{"leadIn",Lead("Line",.3)},{"leadOut",Lead("Arc",.2)},
            {"overcutLengthMm",i?2.:0.},{"gapLengthMm",i?0.:1.},{"overrides",Array{Map{{"parameterId",Text("feedMmPerSecond")},{"value",31.+i}}}}};
        Invoke(&scene,"CamProcess","Update",EditRequest(scene,{ids[i]},{{"patch",patch}}));
        Invoke(&scene,"CamProcess","UpdatePose",EditRequest(scene,{ids[i]},{{"patch",Map{{"leadAngleRad",.1}}}}));
    }
    Invoke(&scene,"CamCut","Reverse",EditRequest(scene,ids,{}));
    const auto before=Read(scene);std::vector<Map> original;
    for(const auto& id:ids)original.push_back(ProcessItem(scene,id));
    const auto count=scene.Database().GetUndoArray().size();const auto request=EditRequest(scene,ids,{{"categories",Array{Text("Leads")}}});
    const auto result=Invoke(&scene,"CamProcess","Clear",request);EXPECT_EQ(result,Invoke(&scene,"CamProcess","Clear",request));
    EXPECT_EQ(count+1,scene.Database().GetUndoArray().size());
    for(std::size_t i=0;i<ids.size();++i){
        auto expected=original[i].at("assignment").To<Map>();expected["leadIn"]=Lead("None",0);expected["leadOut"]=Lead("None",0);
        EXPECT_EQ(expected,ProcessItem(scene,ids[i]).at("assignment").To<Map>());
    }
    const auto leadsCleared=Read(scene);ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
    ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(leadsCleared.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
    Invoke(&scene,"CamProcess","Clear",EditRequest(scene,ids,{{"categories",Array{Text("Gap")}}}));
    EXPECT_DOUBLE_EQ(2.,ProcessItem(scene,ids[1]).at("assignment").To<Map>().at("overcutLengthMm").To<double>());
    Invoke(&scene,"CamProcess","Clear",EditRequest(scene,ids,{{"categories",Array{Text("Overcut"),Text("Kerf")}}}));
    const auto after=Read(scene);
    for(std::size_t i=0;i<ids.size();++i){
        const auto policy=ProcessItem(scene,ids[i]),assignment=policy.at("assignment").To<Map>();
        EXPECT_EQ(original[i].at("pose"),policy.at("pose"));EXPECT_EQ(original[i].at("library"),policy.at("library"));
        EXPECT_EQ(original[i].at("assignment").To<Map>().at("overrides"),assignment.at("overrides"));
        EXPECT_DOUBLE_EQ(0.,assignment.at("gapLengthMm").To<double>());EXPECT_DOUBLE_EQ(0.,assignment.at("overcutLengthMm").To<double>());
        EXPECT_DOUBLE_EQ(0.,assignment.at("kerf").To<Map>().at("widthMm").To<double>());
        const auto a=before.at("intentToolpaths").To<Array>()[i].To<Map>(),b=after.at("intentToolpaths").To<Array>()[i].To<Map>();
        for(const auto key:{"curveRef","featureRef","poseRef","sourceRecipeRef","sourceKey","start","startPoint","reversed","retainedSide","cutType"})EXPECT_EQ(a.at(key),b.at(key));
    }
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Cleared process";
    const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(after.at("intentToolpaths"),Read(reopened).at("intentToolpaths"));
    for(const auto& id:ids)EXPECT_EQ(ProcessItem(scene,id),ProcessItem(reopened,id));std::filesystem::remove(path);
}

TEST(FiveAxisCutClearing, ProtectedCategoriesInvalidSelectionsAndFailedBatchDoNotPartiallyClear)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);const auto before=Read(scene);const auto undo=scene.Database().GetUndoArray().size();
    for(const Text category:{"Start","Reverse","RetainedSide","Pose"})
        EXPECT_THROW(Invoke(&scene,"CamProcess","Clear",EditRequest(scene,ids,{{"categories",Array{category}}})),std::exception);
    EXPECT_THROW(Invoke(&scene,"CamProcess","Clear",EditRequest(scene,ids,{{"categories",Array{}}})),std::exception);
    EXPECT_THROW(Invoke(&scene,"CamProcess","Clear",EditRequest(scene,ids,{{"categories",Array{Text("Kerf"),Text("Kerf")}}})),std::exception);
    EXPECT_THROW(Invoke(&scene,"CamProcess","Clear",EditRequest(scene,{ids[0],ids[0]},{{"categories",Array{Text("Kerf")}}})),std::exception);
    EXPECT_THROW(Invoke(&scene,"CamProcess","Clear",EditRequest(scene,{ids[0],Text(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()))},{{"categories",Array{Text("Kerf")}}})),std::exception);
    class RejectSecond final:public iCAX::Database::IChecker {
    public:int Remaining=2;
        bool AllowAttachByName(const iCAX::Database::IEntity&,const Text&,Text&)override{return true;}
        bool AllowRemoveByName(const iCAX::Database::IEntity&,const Text&,Text&)override{return true;}
        bool AllowModify(const iCAX::Database::CComponentBase&,const iCAX::Data::PropertySet&,Text& error)override{
            if(--Remaining==0){error="Injected second clear failure";return false;}return true;
        }
    };
    scene.Database().GetMetaRegistry()->RegistChecker<iCAX::CAM::Intent::CCutIntentComponent>(std::make_shared<RejectSecond>());
    const auto request=EditRequest(scene,ids,{{"categories",Array{Text("Kerf")}}});
    EXPECT_THROW(Invoke(&scene,"CamProcess","Clear",request),std::exception);EXPECT_EQ(before,Read(scene));EXPECT_EQ(undo,scene.Database().GetUndoArray().size());
    EXPECT_NO_THROW(Invoke(&scene,"CamProcess","Clear",request));
    auto conflict=request;conflict["categories"]=Array{Text("Leads")};EXPECT_THROW(Invoke(&scene,"CamProcess","Clear",conflict),std::exception);
    auto stale=EditRequest(scene,ids,{{"categories",Array{Text("Leads")}}});stale["expected"]=before.at("expected");EXPECT_THROW(Invoke(&scene,"CamProcess","Clear",stale),std::exception);
}

TEST(FiveAxisCutClearing, EmptyCategoriesDoNotChangeRevisionReferencesOrUndoHistory)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);
    const auto all=Array{Text("Leads"),Text("Kerf"),Text("Overcut"),Text("Gap")};
    Invoke(&scene,"CamProcess","Clear",EditRequest(scene,ids,{{"categories",all}}));
    const auto before=Read(scene);const auto undo=scene.Database().GetUndoArray().size();
    const auto request=EditRequest(scene,ids,{{"categories",all}}),result=Invoke(&scene,"CamProcess","Clear",request);
    EXPECT_EQ(result,Invoke(&scene,"CamProcess","Clear",request));EXPECT_EQ(before,Read(scene));EXPECT_EQ(undo,scene.Database().GetUndoArray().size());
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,{ids[0]},{{"patch",Map{{"leadIn",Lead("Line",.3)}}}}));
    const auto second=ProcessItem(scene,ids[1]);const auto modified=Read(scene);const auto history=scene.Database().GetUndoArray().size();
    Invoke(&scene,"CamProcess","Clear",EditRequest(scene,ids,{{"categories",all}}));
    EXPECT_EQ(second,ProcessItem(scene,ids[1]));EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
    EXPECT_EQ(before.at("intentToolpaths").To<Array>()[1],Read(scene).at("intentToolpaths").To<Array>()[1]);
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(modified.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
}
