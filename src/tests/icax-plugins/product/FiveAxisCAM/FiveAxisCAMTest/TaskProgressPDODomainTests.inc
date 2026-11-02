namespace {
    Map ReadTaskPDO(Scene& scene,const Map& receipt){
        iCAX::PDO::CPDOReadLease lease(scene.PDOHub().GetSlot(PDOIDOf(receipt,"progressPDO")));
        flatbuffers::Verifier verifier(static_cast<const std::uint8_t*>(lease.Data()),lease.PayloadSize());
        if(!VerifyTaskProgressBuffer(verifier))throw std::runtime_error("Task progress FlatBuffer invalid");
        const auto* p=GetTaskProgress(lease.Data());
        return {{"taskId",p->task_id()->str()},{"epoch",p->epoch()->str()},{"sequence",std::to_string(p->sequence())},
            {"schemaVersion",p->schema_version()},{"status",p->status()->str()},{"kind",p->kind()->str()},
            {"progress",std::to_string(p->progress())},
            {"details",iCAX::TemplateRuntime::CStandardJsonCodec::Parse(p->details_json()->str())}};
    }
    Map TickTaskPDO(Scene& scene,const Map& receipt){PumpTasks(scene);PumpTaskProgressPDO(scene);scene.PDOHub().SwapOutSlot();return ReadTaskPDO(scene,receipt);}
    Map WaitTerminalPDO(Scene& scene,const Map& receipt){
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
        for(;;){const auto frame=TickTaskPDO(scene,receipt);if(frame.at("status").To<Text>()!="Running")return frame;
            if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Task PDO terminal timeout");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    }
    Map QueryTaskOnce(Scene& scene,const Map& receipt){return Invoke(&scene,"CamSimulation","QueryTask",{{"epoch",receipt.at("epoch")},{"taskId",receipt.at("taskId")}});}
}
TEST(FiveAxisTaskProgressPDO, RealMachinePlanPublishesChangedTelemetryAndOneFinalQueryRetiresSlot)
{
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,false,true);PrepareProductPlanPaths(scene);CreateProductPlanMachine(scene);
    const auto request=GenerateRequest(scene),receipt=Invoke(&scene,"CamSimulation","PlanMachine",request);ASSERT_EQ("Running",receipt.at("status").To<Text>());ASSERT_TRUE(receipt.contains("progressPDO"));
    EXPECT_EQ(receipt.at("progressPDO"),Invoke(&scene,"CamSimulation","PlanMachine",request).at("progressPDO"));
    const auto id=PDOIDOf(receipt,"progressPDO");ASSERT_TRUE(scene.PDOHub().HasSlot(id));
    const auto terminal=WaitTerminalPDO(scene,receipt);EXPECT_EQ("Completed",terminal.at("status").To<Text>());EXPECT_EQ(receipt.at("taskId"),terminal.at("taskId"));EXPECT_EQ(receipt.at("epoch"),terminal.at("epoch"));EXPECT_EQ(1u,terminal.at("schemaVersion").To<unsigned>());
    const auto details=terminal.at("details").To<Map>();EXPECT_TRUE(details.contains("machineProgress"));EXPECT_FALSE(details.contains("result"));EXPECT_FALSE(details.contains("frames"));
    EXPECT_EQ(terminal,TickTaskPDO(scene,receipt)); // Unchanged terminal telemetry does not increment sequence.
    const auto result=QueryTaskOnce(scene,receipt);ASSERT_EQ("Completed",result.at("status").To<Text>());EXPECT_EQ("Ready",result.at("result").To<Map>().at("status").To<Text>());EXPECT_FALSE(scene.PDOHub().HasSlot(id));
    EXPECT_EQ(result.at("result"),QueryTaskOnce(scene,receipt).at("result")); // The reliable task record survives slot retirement.
}
TEST(FiveAxisTaskProgressPDO, LiveIntentRefreshRenewsSequenceWithoutInventingProgressOrHistory)
{
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,false,true);File file(RecognitionPart());
    const auto ids=Imported(scene,file),extraction=ExtractRequest(scene,ids);const auto candidates=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));
    Invoke(&scene,"CamCut","Create",CreateRequest(scene,extraction,candidates));
    const auto tasks=iCAX::FiveAxisCAM::ReadState(scene).at("intentGenerationTasks").To<Array>();ASSERT_EQ(1u,tasks.size());const auto receipt=tasks.front().To<Map>();
    ASSERT_EQ("Running",receipt.at("status").To<Text>());ASSERT_EQ("IntentRefresh",receipt.at("kind").To<Text>());const auto history=scene.Database().GetUndoArray().size();
    // The real generation worker may finish, but its owner has not finalized it.
    // Pump only the production publisher so percentage/stage remain unchanged.
    const auto pulse=[&]{PumpTaskProgressPDO(scene);scene.PDOHub().SwapOutSlot();return ReadTaskPDO(scene,receipt);};
    const auto first=pulse();EXPECT_EQ(first,pulse());
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));const auto heartbeat=pulse();
    EXPECT_GT(std::stoull(heartbeat.at("sequence").To<Text>()),std::stoull(first.at("sequence").To<Text>()));
    auto original=first,current=heartbeat;original.erase("sequence");current.erase("sequence");EXPECT_EQ(original,current);EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    const auto terminal=WaitTerminalPDO(scene,receipt);EXPECT_EQ("Completed",terminal.at("status").To<Text>());std::this_thread::sleep_for(std::chrono::milliseconds(1100));EXPECT_EQ(terminal,pulse());
    const auto result=Invoke(&scene,"CamWorkpiece","QueryTask",{{"epoch",receipt.at("epoch")},{"taskId",receipt.at("taskId")}});EXPECT_EQ("Completed",result.at("status").To<Text>());EXPECT_FALSE(scene.PDOHub().HasSlot(PDOIDOf(receipt,"progressPDO")));
}
TEST(FiveAxisTaskProgressPDO, CancellationAndForeignEpochCannotConsumeTheCurrentSlotAndReadLeaseRetiresLater)
{
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,false,true);PrepareProductPlanPaths(scene);CreateProductPlanMachine(scene);const auto history=scene.Database().GetUndoArray().size();
    const auto receipt=Invoke(&scene,"CamSimulation","PlanMachine",GenerateRequest(scene));const auto id=PDOIDOf(receipt,"progressPDO");
    EXPECT_THROW(Invoke(&scene,"CamSimulation","QueryTask",{{"epoch",PDOEditToken()},{"taskId",receipt.at("taskId")}}),std::exception);EXPECT_TRUE(scene.PDOHub().HasSlot(id));
    Invoke(&scene,"CamSimulation","CancelTask",{{"epoch",receipt.at("epoch")},{"taskId",receipt.at("taskId")}});EXPECT_EQ("Cancelled",WaitTerminalPDO(scene,receipt).at("status").To<Text>());
    iCAX::PDO::CPDOReadLease held(scene.PDOHub().GetSlot(id));EXPECT_EQ("Cancelled",QueryTaskOnce(scene,receipt).at("status").To<Text>());EXPECT_TRUE(scene.PDOHub().HasSlot(id));held.Release();PumpTaskProgressPDO(scene);EXPECT_FALSE(scene.PDOHub().HasSlot(id));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
}
TEST(FiveAxisTaskProgressPDO, SceneReleaseCancelsWorkersAndRetiresAllTelemetry)
{
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,false,true);PrepareProductPlanPaths(scene);CreateProductPlanMachine(scene);
    const auto receipt=Invoke(&scene,"CamSimulation","PlanMachine",GenerateRequest(scene));const auto id=PDOIDOf(receipt,"progressPDO");ASSERT_TRUE(scene.PDOHub().HasSlot(id));
    ReleaseTaskProgressPDO(scene,JogRuntime(scene));EXPECT_FALSE(scene.PDOHub().HasSlot(id));
    scene.Closing=true;EXPECT_THROW(QueryTaskOnce(scene,receipt),std::exception);EXPECT_FALSE(scene.PDOHub().HasSlot(id));
}
TEST(FiveAxisTaskProgressPDO, ExplicitConsumerAbandonCancelsAndReleasesWithoutReallocatingAWindingDownTask)
{
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,false,true);PrepareProductPlanPaths(scene);CreateProductPlanMachine(scene);
    const auto request=GenerateRequest(scene),receipt=Invoke(&scene,"CamSimulation","PlanMachine",request);const auto id=PDOIDOf(receipt,"progressPDO");
    const auto abandoned=Invoke(&scene,"CamSimulation","CancelTask",{{"epoch",receipt.at("epoch")},{"taskId",receipt.at("taskId")},{"releaseProgress",true}});
    EXPECT_FALSE(abandoned.contains("progressPDO"));EXPECT_FALSE(scene.PDOHub().HasSlot(id));
    EXPECT_FALSE(Invoke(&scene,"CamSimulation","PlanMachine",request).contains("progressPDO"));
    const auto terminal=WaitMachinePlan(scene,receipt);EXPECT_EQ("Cancelled",terminal.at("status").To<Text>());EXPECT_FALSE(terminal.contains("progressPDO"));EXPECT_FALSE(scene.PDOHub().HasSlot(id));
}
