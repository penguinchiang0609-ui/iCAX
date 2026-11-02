TEST(FiveAxisCollisionResume, LocalPoseEditPreservesOtherPathsAndRechecksOnlyChangedSweeps)
{
    Text stage="prepare";try{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);ZeroGenerationPolicies(scene,ids);GenerateAxis(scene);RunCollision(scene);
    const auto before=Read(scene),initial=SimulationReport(scene);const auto operations=Generated(scene).at("operations").To<Array>();
    ASSERT_EQ(2u,operations.size());const auto changed=operations.back().To<Map>().at("intentId"),unchanged=operations.front().To<Map>().at("intentId");
    const auto undo=scene.Database().GetUndoArray().size();
    stage="edit selected path";const auto point=Invoke(&scene,"CamCut","ListToolAxisPoints",{{"expected",Read(scene).at("expected")},{"intentId",changed}}).at("points").To<Array>().front().To<Map>();
    Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,changed,Text("start"),point.at("anchor").To<Map>(),{.02,0.,1.}));
    stage="read partial report";const auto partial=SimulationReport(scene);ASSERT_EQ("Current",partial.at("validity").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(partial);
    EXPECT_EQ("LocalEdit",partial.at("checkMode").To<Text>());EXPECT_GT(std::stoull(partial.at("reusedSegmentCount").To<Text>()),0u);
    const auto frames=partial.at("frames").To<Array>(),segments=partial.at("segments").To<Array>();
    for(std::size_t i=0;i<segments.size();++i)if(frames[i].To<Map>().at("intentId")==changed)
        EXPECT_EQ("Unchecked",segments[i].To<Map>().at("status").To<Text>());
    stage="prepare selected path";auto preparation=GenerateRequest(scene);preparation["intentId"]=changed;
    const auto generation=Wait(scene,Invoke(&scene,"CamPlan","Generate",preparation));
    ASSERT_EQ("Completed",generation.at("status").To<Text>());const auto result=generation.at("result").To<Map>();
    EXPECT_EQ("1",result.at("calculatedCount").To<Text>());EXPECT_EQ("1",result.at("reusedCount").To<Text>());
    EXPECT_EQ(AxisOperation(Generated(scene),unchanged).at("parts"),AxisOperation(before.at("setups").To<Array>().front().To<Map>().at("generatedGeometry").To<Map>(),unchanged).at("parts"));
    stage="check selected path";auto check=GenerateRequest(scene);check["intentId"]=changed;
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamSimulation","CheckCollision",check)).at("status").To<Text>());const auto checked=SimulationReport(scene);
    EXPECT_EQ("SelectedPath",checked.at("checkMode").To<Text>());EXPECT_GT(std::stoull(checked.at("reusedSegmentCount").To<Text>()),0u);
    EXPECT_LT(std::stoull(checked.at("checkedSegmentCount").To<Text>()),checked.at("segments").To<Array>().size());
    EXPECT_EQ(undo+1,scene.Database().GetUndoArray().size());EXPECT_EQ(before.at("workpieces"),Read(scene).at("workpieces"));
    stage="undo";ASSERT_TRUE(scene.Database().Undo());GenerateAxis(scene);EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
    stage="redo";ASSERT_TRUE(scene.Database().Redo());GenerateAxis(scene);EXPECT_EQ(undo+1,scene.Database().GetUndoArray().size());
    }catch(const std::exception& error){FAIL()<<stage<<": "<<error.what();}
}
TEST(FiveAxisCollisionResume, RechecksOnlyUncheckedAndPreservesVerifiedIndicesAfterEmbeddedReopen)
{
    Scene scene;PrepareOrderPart(scene);RunCollision(scene);const auto first=SimulationReport(scene);
    const auto initial=first.at("segments").To<Array>();const auto pending=std::stoull(first.at("uncheckedCount").To<Text>());
    ASSERT_GT(pending,0u);ASSERT_LT(pending,initial.size());EXPECT_EQ("Full",first.at("checkMode").To<Text>());
    // Missing rapid connectors are real Unchecked segments: more compute time
    // must not turn missing geometry into a positive clearance certificate.
    RunCollision(scene);const auto resumed=SimulationReport(scene);
    EXPECT_EQ("UncheckedOnly",resumed.at("checkMode").To<Text>());EXPECT_EQ("5000",resumed.at("initialSegmentTimeMs").To<Text>());
    EXPECT_EQ("20000",resumed.at("retrySegmentTimeMs").To<Text>());EXPECT_EQ("20000",resumed.at("maxSegmentTimeMs").To<Text>());
    EXPECT_EQ(std::to_string(pending),resumed.at("checkedSegmentCount").To<Text>());EXPECT_EQ(std::to_string(pending),resumed.at("retrySegmentCount").To<Text>());
    EXPECT_EQ(std::to_string(initial.size()-pending),resumed.at("reusedSegmentCount").To<Text>());EXPECT_EQ(first.at("frames"),resumed.at("frames"));
    EXPECT_EQ(first.at("segments"),resumed.at("segments"));EXPECT_EQ(first.at("uncheckedCount"),resumed.at("uncheckedCount"));
    // Disconnected intervals do not run distance queries; the explicit
    // keypoint pass still certifies the authored poses on every check.
    EXPECT_GT(std::stoull(resumed.at("keypointCheckCount").To<Text>()),0u);
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Resume current collision report";
    const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(resumed,SimulationReport(reopened));const auto source=resumed.at("sourceReportRevision").To<Map>();
    EXPECT_FALSE(source.at("revision").To<Text>().empty());EXPECT_TRUE(EngineeringStorageAudit(reopened).at("persistentCamSchemas").To<Array>().empty());
    RunCollision(reopened);const auto restored=SimulationReport(reopened);
    EXPECT_EQ("UncheckedOnly",restored.at("checkMode").To<Text>());EXPECT_EQ(resumed.at("segments"),restored.at("segments"));
    EXPECT_EQ(resumed.at("reusedSegmentCount"),restored.at("reusedSegmentCount"));std::filesystem::remove(path);
    RapidConfig(reopened,2,10);ASSERT_EQ("Stale",SimulationReport(reopened).at("validity").To<Text>());RunCollision(reopened);
    const auto changed=SimulationReport(reopened);EXPECT_EQ("Full",changed.at("checkMode").To<Text>());EXPECT_EQ("0",changed.at("reusedSegmentCount").To<Text>());
    EXPECT_EQ("5000",changed.at("maxSegmentTimeMs").To<Text>());
}
TEST(FiveAxisCollisionResume, ReusesCollisionWitnessesButNeverAcrossObstacleChanges)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);ZeroGenerationPolicies(scene,ids);
    File fixture(BRepPrimAPI_MakeBox(gp_Pnt(-100,-100,11),200,200,100).Shape());auto importRequest=GenerateRequest(scene);importRequest["sourcePath"]=fixture.Utf8();
    const auto imported=Wait(scene,Invoke(&scene,"CamFixture","Import",importRequest));ASSERT_EQ("Completed",imported.at("status").To<Text>());
    GenerateAxis(scene);RunCollision(scene);const auto initial=SimulationReport(scene);
    ASSERT_GT(std::stoull(initial.at("collisionCount").To<Text>()),0u);ASSERT_GT(std::stoull(initial.at("uncheckedCount").To<Text>()),0u);
    RunCollision(scene);const auto retried=SimulationReport(scene);EXPECT_EQ("UncheckedOnly",retried.at("checkMode").To<Text>());
    EXPECT_EQ(initial.at("segments"),retried.at("segments"));EXPECT_EQ(initial.at("collisionCount"),retried.at("collisionCount"));
    auto update=GenerateRequest(scene);update["fixtureId"]=imported.at("result").To<Map>().at("fixtureId");update["collisionEnabled"]=false;
    Invoke(&scene,"CamFixture","Update",update);ASSERT_EQ("Stale",SimulationReport(scene).at("validity").To<Text>());
    GenerateAxis(scene);RunCollision(scene);const auto changed=SimulationReport(scene);
    EXPECT_EQ("Full",changed.at("checkMode").To<Text>());EXPECT_EQ("0",changed.at("reusedSegmentCount").To<Text>());
    EXPECT_EQ("0",changed.at("collisionCount").To<Text>());
}
