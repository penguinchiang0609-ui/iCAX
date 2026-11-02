TEST(FiveAxisIntentWorkflow, ReadStateImmediatelyAfterPublicCreateShowsPendingDefinitionsWithoutGeometry)
{
    Scene scene;File file(RecognitionPart());const auto ids=Imported(scene,file);const auto extraction=ExtractRequest(scene,ids);
    const auto candidates=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));const auto create=CreateRequest(scene,extraction,candidates);
    const auto receipt=Invoke(&scene,"CamCut","Create",create);const auto created=receipt.at("createdIntentIds").To<Array>();ASSERT_FALSE(created.empty());
    // Deliberately bypass Read(scene), whose convenience loop pumps generation
    // until completion. The worker may finish, but no owner publication has run.
    const auto pending=iCAX::FiveAxisCAM::ReadState(scene);const auto rows=pending.at("intentToolpaths").To<Array>();ASSERT_EQ(created.size(),rows.size());
    for(const auto& item:rows)
    {
        const auto row=item.To<Map>();EXPECT_NE(created.end(),std::find(created.begin(),created.end(),row.at("id")));
        EXPECT_EQ("Pending",row.at("sourceState").To<Text>());EXPECT_EQ("PendingGeneration",row.at("processEligibility").To<Text>());
        EXPECT_TRUE(row.at("previewPoints").To<Array>().empty());EXPECT_TRUE(row.at("startPoint").To<Array>().empty());
        EXPECT_EQ(extraction.at("setupId"),row.at("setupId"));EXPECT_EQ(extraction.at("placementId"),row.at("placementId"));EXPECT_FALSE(row.at("sourceKey").To<Text>().empty());
    }
    EXPECT_FALSE(pending.at("intentGenerationTasks").To<Array>().empty());const auto current=Read(scene).at("intentToolpaths").To<Array>();ASSERT_EQ(created.size(),current.size());
    for(const auto& item:current){const auto row=item.To<Map>();EXPECT_EQ("Current",row.at("sourceState").To<Text>());EXPECT_FALSE(row.at("previewPoints").To<Array>().empty());}
}

TEST(FiveAxisIntentWorkflow, TreeCanDeletePendingRowsWithoutPublishingTheirGeometry)
{
    Scene scene;File file(RecognitionPart());const auto ids=Imported(scene,file),extraction=ExtractRequest(scene,ids);
    const auto candidates=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));const auto create=CreateRequest(scene,extraction,candidates);
    const auto receipt=Invoke(&scene,"CamCut","Create",create);const auto created=receipt.at("createdIntentIds").To<Array>();ASSERT_FALSE(created.empty());
    const auto pending=iCAX::FiveAxisCAM::ReadState(scene);for(const auto& row:pending.at("intentToolpaths").To<Array>())EXPECT_EQ(row.To<Map>().at("sourceState").To<Text>(),"Pending");
    Map request{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",pending.at("expected")},{"setupId",pending.at("activeSetupId")},{"action",Text("Delete")},{"intentIds",created},{"groupIds",Array{}}};
    const auto result=Invoke(&scene,"CamCut","ManageTree",request);EXPECT_TRUE(result.at("changed").To<bool>());EXPECT_EQ(result,Invoke(&scene,"CamCut","ManageTree",request));
    EXPECT_TRUE(iCAX::FiveAxisCAM::ReadState(scene).at("intentToolpaths").To<Array>().empty());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(created.size(),iCAX::FiveAxisCAM::ReadState(scene).at("intentToolpaths").To<Array>().size());ASSERT_TRUE(scene.Database().Redo());EXPECT_TRUE(Read(scene).at("intentToolpaths").To<Array>().empty());
}
