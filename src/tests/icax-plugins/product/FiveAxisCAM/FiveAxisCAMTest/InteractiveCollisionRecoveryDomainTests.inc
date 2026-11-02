namespace interactive_collision_recovery_tests {
    Map FK(Scene& scene,ID machine,ID part,const Text& token,double q){return Invoke(&scene,"CamSimulation","MoveMachineIK",{{"expected",Read(scene).at("expected")},{"machineId",iCAX::Data::to_string(machine)},{"tipId",iCAX::Data::to_string(part)},{"editToken",token},{"motionMode",Text("FK")},{"jointPosition",q}});}
    void Clear(const Map& result){EXPECT_EQ("Ready",result.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(result);EXPECT_EQ("Clear",result.at("collisionStatus").To<Text>());}
}
TEST(FiveAxisInteractiveCollisionRecovery, CancelledAndDeadlineTargetsAreUncheckedAndSameGestureCanRecover)
{
    namespace T=interactive_collision_recovery_tests;Scene scene;PrepareProductPlanPaths(scene);const auto machine=CreateProductPlanMachine(scene);const auto token=PDOEditToken();const auto history=scene.Database().GetUndoArray().size();
    T::Clear(T::FK(scene,machine.Machine,machine.X,token,1));
    auto request=Map{{"expected",Read(scene).at("expected")},{"machineId",iCAX::Data::to_string(machine.Machine)},{"tipId",iCAX::Data::to_string(machine.X)},{"editToken",token},{"motionMode",Text("FK")},{"jointPosition",2.}};
    int checks=0;scene.StopProbe=[&]{return ++checks>=6;};const auto cancelled=Invoke(&scene,"CamSimulation","MoveMachineIK",request);scene.StopProbe={};
    ASSERT_EQ("Unchecked",cancelled.at("status").To<Text>());EXPECT_EQ("Unchecked",cancelled.at("collisionStatus").To<Text>());EXPECT_EQ("Cancelled",cancelled.at("collisionInterruption").To<Text>());EXPECT_DOUBLE_EQ(1,PDOJoint(scene,machine.X));
    const auto retry=T::FK(scene,machine.Machine,machine.X,token,2);T::Clear(retry);EXPECT_TRUE(retry.at("cacheReused").To<bool>());EXPECT_GT(std::stoull(retry.at("collisionCacheHits").To<Text>()),0u);
    request["expected"]=Read(scene).at("expected");request["jointPosition"]=3.;checks=0;scene.StopProbe=[&]{if(++checks==5)std::this_thread::sleep_for(std::chrono::milliseconds(270));return false;};
    const auto expired=Invoke(&scene,"CamSimulation","MoveMachineIK",request);scene.StopProbe={};ASSERT_EQ("Unchecked",expired.at("status").To<Text>());EXPECT_EQ("Unchecked",expired.at("collisionStatus").To<Text>());EXPECT_EQ("Deadline",expired.at("collisionInterruption").To<Text>());EXPECT_EQ(Text::npos,expired.at("reason").To<Text>().find("COLLISION_DISTANCE_CANCELLED"));EXPECT_DOUBLE_EQ(2,PDOJoint(scene,machine.X));
    T::Clear(T::FK(scene,machine.Machine,machine.X,token,3));PDOEnd(scene,token,0,true);EXPECT_DOUBLE_EQ(0,PDOJoint(scene,machine.X));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    const auto next=PDOEditToken();T::Clear(T::FK(scene,machine.Machine,machine.X,next,1));PDOEnd(scene,next,0,true);
}
TEST(FiveAxisInteractiveCollisionRecovery, DistanceMemoDoesNotReuseClearanceAfterRelevantJointMovesIntoRealMaterial)
{
    namespace T=interactive_collision_recovery_tests;Scene scene;PrepareProductPlanPaths(scene);const auto machine=CreateProductPlanMachine(scene);
    Invoke(&scene,"CamSimulation","MoveMachine",{{"expected",Read(scene).at("expected")},{"machineId",iCAX::Data::to_string(machine.Machine)},{"positions",Map{{iCAX::Data::to_string(machine.X),10.}}}});
    const auto token=PDOEditToken();T::Clear(T::FK(scene,machine.Machine,machine.Tool,token,31));
    const auto blocked=T::FK(scene,machine.Machine,machine.Tool,token,-10);ASSERT_EQ("Blocked",blocked.at("status").To<Text>())<<blocked.at("reason").To<Text>();EXPECT_EQ("Blocked",blocked.at("collisionStatus").To<Text>());EXPECT_TRUE(blocked.contains("collisionWitnessFraction"));EXPECT_DOUBLE_EQ(31,PDOJoint(scene,machine.Tool));
    T::Clear(T::FK(scene,machine.Machine,machine.Tool,token,32));PDOEnd(scene,token,0,true);EXPECT_DOUBLE_EQ(30,PDOJoint(scene,machine.Tool));
}
TEST(FiveAxisInteractiveCollisionRecovery, ActualTC800ProjectColdAndWarmCradleFKUsesCompletedPointProofs)
{
    namespace T=interactive_collision_recovery_tests;const auto root=std::filesystem::path("C:/icax-machine-kinematics/machine-pose-collision-20261024");const auto source=root/"user-pose-copy.i5cam";ASSERT_TRUE(std::filesystem::is_regular_file(source));
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});const auto prepared=file.PrepareOpen(source);const auto info=prepared.Result().Info;Scene scene(info.ProjectID,info.MainSceneID,false,true,true);file.Restore(prepared,scene.Database(),scene.Resources());InitializeScene(scene);
    ID machine;std::map<Text,ID> axes;Map rest;
    for(const auto& entity:scene.Database().FilterEntities([](auto){return true;})){
        if(entity->GetComponent("CMachineInstanceComponent"))machine=entity->GetID();
        if(const auto e=entity->GetComponent("CMachineElementComponent"))rest[iCAX::Data::to_string(entity->GetID())]=e->GetProperty("RestPose");
        if(const auto j=entity->GetComponent("CMachineJointComponent"))axes[j->GetProperty("JointName").To<Text>()]=entity->GetID();
    }
    ASSERT_FALSE(machine.is_nil());ASSERT_TRUE(axes.contains("A"));ASSERT_TRUE(axes.contains("C"));ASSERT_TRUE(axes.contains("Z"));ASSERT_FALSE(Read(scene).at("placements").To<Array>().empty());
    Array reports;
    for(const auto& word:{Text("C"),Text("A")}){
        const auto initial=PDOJoint(scene,axes.at(word));const auto token=PDOEditToken();Array records;std::vector<double> hot;
        for(int step=1;step<=20;++step){const auto report=T::FK(scene,machine,axes.at(word),token,initial+step*(word=="C"?.01:.005));T::Clear(report);ASSERT_EQ("Clear",report.at("collisionStatus").To<Text>());records.emplace_back(report);if(step>1)hot.push_back(report.at("collisionMilliseconds").To<double>());}
        const auto cold=records.front().To<Map>();std::sort(hot.begin(),hot.end());reports.emplace_back(Map{{"axis",word},{"coldMs",cold.at("collisionMilliseconds")},{"hotMedianMs",hot[hot.size()/2]},{"hotMaximumMs",hot.back()},{"records",records}});PDOEnd(scene,token,20,true);EXPECT_DOUBLE_EQ(initial,PDOJoint(scene,axes.at(word)));
    }
    // A new run has a cold proof cache: a real nozzle/table crossing must
    // still be rejected and must leave the authoritative joint unchanged.
    const auto z=PDOJoint(scene,axes.at("Z"));const auto token=PDOEditToken();const auto blocked=T::FK(scene,machine,axes.at("Z"),token,-500.);ASSERT_EQ("Blocked",blocked.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(blocked);EXPECT_EQ("Blocked",blocked.at("collisionStatus").To<Text>());EXPECT_DOUBLE_EQ(z,PDOJoint(scene,axes.at("Z")));reports.emplace_back(Map{{"coldCollision",blocked}});PDOEnd(scene,token,0,true);
    for(const auto& [id,pose]:rest)EXPECT_EQ(pose,scene.Database().GetEntity(ID::from_string(id).value())->GetComponent("CMachineElementComponent")->GetProperty("RestPose"));
    std::ofstream(root/"native-pose-performance.json")<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(Map{{"source",source.string()},{"results",reports},{"sourceModified",false}});
}
