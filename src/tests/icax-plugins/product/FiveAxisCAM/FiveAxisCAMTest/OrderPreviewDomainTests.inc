TEST(FiveAxisOrderPreview, PreviewIsReadOnlyAndApplyIsFencedAtomicAndIdempotent)
{
    Scene scene;PrepareOrderPart(scene);const auto before=Read(scene),generated=Generated(scene);const auto history=scene.Database().GetUndoArray().size();
    auto request=OrderRequest(scene,"Automatic");request["preview"]=true;request["options"]=Map{{"pattern",Text("Snake")},{"objective",Text("Distance")}};
    const auto task=Wait(scene,Invoke(&scene,"CamPlan","BeginOrder",request));ASSERT_EQ("Completed",task.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);
    const auto candidate=task.at("result").To<Map>();EXPECT_EQ(history,scene.Database().GetUndoArray().size());EXPECT_EQ(before.at("expected"),Read(scene).at("expected"));EXPECT_EQ(generated,Generated(scene));
    auto apply=OrderRequest(scene,"Automatic");apply["options"]=request.at("options");apply["previewTaskId"]=task.at("taskId");
    const auto result=Invoke(&scene,"CamPlan","SetOrder",apply);EXPECT_EQ(candidate.at("definitionOrder"),result.at("definitionOrder"));EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
    EXPECT_EQ(result,Invoke(&scene,"CamPlan","SetOrder",apply));EXPECT_EQ(before.at("workpieces"),Read(scene).at("workpieces"));EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(generated,Generated(scene));ASSERT_TRUE(scene.Database().Redo());
    auto stale=OrderRequest(scene,"Automatic");stale["options"]=request.at("options");stale["previewTaskId"]=task.at("taskId");const auto after=Read(scene);EXPECT_THROW(Invoke(&scene,"CamPlan","SetOrder",stale),std::exception);EXPECT_EQ(after,Read(scene));
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Spatial ordering preview";
    const auto saved=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(saved,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(saved,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(Generated(scene),Generated(reopened));std::filesystem::remove(saved);
}
TEST(FiveAxisOrderPreview, RoughEstimatePreservesStartsAndDoesNotGenerateMachiningPaths)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);ZeroGenerationPolicies(scene,ids);
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,{ids[0]},{{"patch",Map{{"leadIn",Lead("Line",.2)},{"leadOut",Lead("Line",.2)},{"overcutLengthMm",.1}}}}));
    const auto before=Read(scene);const auto history=scene.Database().GetUndoArray().size();
    auto request=OrderRequest(scene,"Automatic");request["preview"]=true;request["options"]=Map{{"optimizeStarts",true},{"objective",Text("Distance")}};
    const auto task=Wait(scene,Invoke(&scene,"CamPlan","BeginOrder",request));ASSERT_EQ("Completed",task.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);
    const auto candidate=task.at("result").To<Map>(),report=candidate.at("optimization").To<Map>();
    EXPECT_FALSE(candidate.contains("startEdits"));EXPECT_FALSE(candidate.contains("operations"));EXPECT_FALSE(candidate.at("options").To<Map>().at("optimizeStarts").To<bool>());
    EXPECT_EQ("ExistingContourEndpoints",report.at("endpointSource").To<Text>());EXPECT_EQ("HeightPenalizedStraightLineTravelEstimate",report.at("costScope").To<Text>());
    EXPECT_DOUBLE_EQ(3.,candidate.at("options").To<Map>().at("heightPenaltyWeight").To<double>());
    EXPECT_EQ(ids.size(),candidate.at("endpoints").To<Array>().size());EXPECT_EQ("Missing",Generated(scene).at("status").To<Text>());
    EXPECT_LE(report.at("afterCost").To<double>(),report.at("beforeCost").To<double>());EXPECT_EQ(history,scene.Database().GetUndoArray().size());EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
    auto apply=OrderRequest(scene,"Automatic");apply["options"]=request.at("options");apply["previewTaskId"]=task.at("taskId");Invoke(&scene,"CamPlan","SetOrder",apply);
    EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());const auto after=Read(scene);
    for(const auto* key:{"workpieces","placements","fixtures","intentToolpaths"})EXPECT_EQ(before.at(key),after.at(key));
    EXPECT_EQ("Missing",Generated(scene).at("status").To<Text>());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));ASSERT_TRUE(scene.Database().Redo());
    EXPECT_EQ(after.at("intentToolpaths"),Read(scene).at("intentToolpaths"));EXPECT_EQ("Missing",Generated(scene).at("status").To<Text>());
}
TEST(FiveAxisOrderPreview, InvalidOptionsAndChangedSelectionCannotApplyAnotherPreview)
{
    Scene scene;PrepareOrderPart(scene);const auto before=Read(scene);auto request=OrderRequest(scene,"Automatic");request["preview"]=true;request["options"]=Map{{"scope",Text("Selected")}};
    EXPECT_THROW(Invoke(&scene,"CamPlan","BeginOrder",request),std::exception);
    request["options"]=Map{{"plane",Text("Custom")},{"normal",Array{0.,0.,0.}}};
    EXPECT_THROW(Invoke(&scene,"CamPlan","BeginOrder",request),std::exception);EXPECT_EQ(before,Read(scene));
    for(const auto& weight:{iCAX::Data::Variant(Text("invalid")),iCAX::Data::Variant(true)}){request["options"]=Map{{"heightPenaltyWeight",weight}};EXPECT_THROW(Invoke(&scene,"CamPlan","BeginOrder",request),std::exception);}
    EXPECT_EQ(before,Read(scene));
}
TEST(FiveAxisOrderPreview, ManualStartAndAllOtherStartsSurviveTimeBasedSorting)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);ZeroGenerationPolicies(scene,ids);
    const auto definition=Invoke(&scene,"CamCut","Read",{{"expected",Read(scene).at("expected")},{"intentIds",Array{ids[0]}}}).at("items").To<Array>().front().To<Map>();
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,{ids[0]},{{"patch",Map{{"start",definition.at("start")}}}}));const auto before=Read(scene);
    auto request=OrderRequest(scene,"Automatic");request["preview"]=true;request["options"]=Map{{"optimizeStarts",true},{"objective",Text("Time")},{"heightPenaltyWeight",1.5}};
    const auto task=Wait(scene,Invoke(&scene,"CamPlan","BeginOrder",request));ASSERT_EQ("Completed",task.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);
    const auto candidate=task.at("result").To<Map>();EXPECT_EQ("s",candidate.at("optimization").To<Map>().at("costUnit").To<Text>());
    EXPECT_FALSE(candidate.contains("startEdits"));EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
    auto apply=OrderRequest(scene,"Automatic");apply["options"]=request.at("options");apply["previewTaskId"]=task.at("taskId");Invoke(&scene,"CamPlan","SetOrder",apply);
    EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
    const auto actual=Invoke(&scene,"CamCut","Read",{{"expected",Read(scene).at("expected")},{"intentIds",Array{ids[0]}}}).at("items").To<Array>().front().To<Map>();EXPECT_EQ(definition.at("start"),actual.at("start"));
}
TEST(FiveAxisOrderPreview, SelectedAndFromScopesPreserveUnselectedPositions)
{
    Scene scene;PrepareOrderPart(scene);const auto before=Read(scene);const auto current=before.at("setups").To<Array>().front().To<Map>().at("definitionOrder").To<Array>();ASSERT_GE(current.size(),4u);
    auto request=OrderRequest(scene,"Automatic");request["preview"]=true;request["intentIds"]=Array{current[0],current[1]};request["options"]=Map{{"scope",Text("Selected")},{"pattern",Text("Z")},{"corner",Text("BottomRight")}};
    auto task=Wait(scene,Invoke(&scene,"CamPlan","BeginOrder",request));ASSERT_EQ("Completed",task.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);auto order=task.at("result").To<Map>().at("definitionOrder").To<Array>();for(std::size_t i=2;i<current.size();++i)EXPECT_EQ(current[i],order[i]);
    request=OrderRequest(scene,"Automatic");request["preview"]=true;request["fromIntentId"]=current[2];request["options"]=Map{{"scope",Text("From")}};
    task=Wait(scene,Invoke(&scene,"CamPlan","BeginOrder",request));ASSERT_EQ("Completed",task.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);order=task.at("result").To<Map>().at("definitionOrder").To<Array>();EXPECT_EQ(current[0],order[0]);EXPECT_EQ(current[1],order[1]);EXPECT_EQ(before.at("expected"),Read(scene).at("expected"));
}

TEST(FiveAxisOrderPreview, DirectAutomaticOrderAcceptsFiniteHeightWeightsWithoutRangeLimits)
{
    Scene scene;PrepareOrderPart(scene);const auto before=Read(scene);
    for(const double weight:{-2.5,0.,12.5}){
        auto request=OrderRequest(scene,"Automatic");request["options"]=Map{{"heightPenaltyWeight",weight}};
        const auto task=Wait(scene,Invoke(&scene,"CamPlan","BeginOrder",request));
        ASSERT_EQ("Completed",task.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);
        const auto result=task.at("result").To<Map>();
        EXPECT_DOUBLE_EQ(weight,result.at("ordering").To<Map>().at("options").To<Map>().at("heightPenaltyWeight").To<double>());
        const auto after=Read(scene);for(const auto* key:{"workpieces","placements","fixtures","intentToolpaths"})EXPECT_EQ(before.at(key),after.at(key));
        EXPECT_EQ(result,Wait(scene,Invoke(&scene,"CamPlan","BeginOrder",request)).at("result").To<Map>());
    }
}