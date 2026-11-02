TEST(FiveAxisRapidParallel, BoundedWorkersActuallyOverlapAndJoinBeforeReturning)
{
    std::mutex mutex;std::condition_variable changed;unsigned entered=0;std::atomic_uint active=0;std::vector<unsigned> slots(12);
    const auto peak=iCAX::FiveAxisCAM::RapidParallel::Run(slots.size(),4,{},[&](std::size_t index,const auto&){
        ++active;
        if(index<4){std::unique_lock lock(mutex);++entered;changed.notify_all();if(!changed.wait_for(lock,std::chrono::seconds(2),[&]{return entered==4;}))throw std::runtime_error("Workers did not overlap");}
        slots[index]=static_cast<unsigned>(index+1);--active;
    });
    EXPECT_EQ(4u,peak);EXPECT_EQ(0u,active.load());for(unsigned i=0;i<slots.size();++i)EXPECT_EQ(i+1,slots[i]);
}
TEST(FiveAxisRapidParallel, WorkerFailureStopsAndJoinsEverySibling)
{
    std::atomic_uint active=0,finished=0;
    EXPECT_THROW(iCAX::FiveAxisCAM::RapidParallel::Run(50,4,{},[&](std::size_t index,const auto& stop){
        ++active;
        if(index==0){--active;throw std::runtime_error("Injected route failure");}
        while(!stop())std::this_thread::yield();++finished;--active;
    }),std::runtime_error);
    EXPECT_EQ(0u,active.load());EXPECT_LT(finished.load(),50u);
    std::atomic_bool cancelled=true;EXPECT_THROW(iCAX::FiveAxisCAM::RapidParallel::Run(5,3,[&]{return cancelled.load();},[](auto,const auto&){throw std::runtime_error("Cancelled work started");}),std::runtime_error);
}
TEST(FiveAxisRapidParallel, SafeBatchChecksUncheckedGeometryAndMatchesSerialReferenceExactly)
{
    Scene scene;PrepareOrderPart(scene);fast_rapid_tests::ExpectUncheckedGeometry(fast_rapid_tests::Batch(scene));
    const auto setupID=ID::from_string(Read(scene).at("activeSetupId").To<Text>()).value();
    const auto setup=std::dynamic_pointer_cast<CFiveAxisSetupComponent>(scene.Database().GetEntity(setupID)->GetComponent(CFiveAxisSetupComponent::S_ClassName));ASSERT_TRUE(setup);
    const auto unchecked=setup->GetGeneratedRapidPaths();
    auto request=RapidRequest(scene,false);request["safe"]=true;request["maxParallelTasks"]=Text("1");
    const auto serial=Wait(scene,Invoke(&scene,"CamRapid","Generate",request));ASSERT_EQ("Completed",serial.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(serial);
    const auto first=serial.at("result").To<Map>();EXPECT_EQ("CollisionChecked",first.at("generation").To<Text>());EXPECT_NE("0",first.at("calculatedCount").To<Text>());
    EXPECT_EQ(first.at("count"),first.at("clearCount"));
    ASSERT_TRUE(setup->SetProperty("GeneratedRapidPaths",unchecked));
    request=RapidRequest(scene,false);request["safe"]=true;request["maxParallelTasks"]=Text("4");
    const auto parallel=Wait(scene,Invoke(&scene,"CamRapid","Generate",request));ASSERT_EQ("Completed",parallel.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(parallel);
    const auto second=parallel.at("result").To<Map>();EXPECT_EQ(first.at("connections"),second.at("connections"));
    const auto performance=second.at("performance").To<Map>();EXPECT_GT(performance.at("peakParallelTasks").To<double>(),1);EXPECT_LE(performance.at("peakParallelTasks").To<double>(),4);
    request=RapidRequest(scene,false);request["safe"]=true;const auto reused=Wait(scene,Invoke(&scene,"CamRapid","Generate",request)).at("result").To<Map>();EXPECT_EQ("0",reused.at("calculatedCount").To<Text>());EXPECT_EQ(second.at("connections"),reused.at("connections"));
}
TEST(FiveAxisRapidParallel, SafeBatchRequiresDeclaredBoxesWhileFastBatchDoesNot)
{
    Scene scene;PrepareOrderPart(scene);File source(BRepPrimAPI_MakeBox(2.,2.,20.).Shape());
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamHead","Import",ImportTestHead(scene,source))).at("status").To<Text>());
    Invoke(&scene,"CamHead","Configure",CalibrateTestHead(scene));fast_rapid_tests::ExpectUncheckedGeometry(fast_rapid_tests::Batch(scene));
    auto request=RapidRequest(scene,false);request["safe"]=true;EXPECT_THROW(Invoke(&scene,"CamRapid","Generate",request),std::exception);
    fast_rapid_tests::ExpectUncheckedGeometry(fast_rapid_tests::Batch(scene));
}
TEST(FiveAxisRapidParallel, SafeBatchRetainsAnAuthoredBlockedManualPathWithoutCallingItSafe)
{
    Text stage="prepare";try{
    Scene scene;PrepareOrderPart(scene);File fixture(BRepPrimAPI_MakeBox(gp_Pnt(500,500,500),5.,5.,5.).Shape());
    stage="import fixture";auto imported=GenerateRequest(scene);imported["sourcePath"]=fixture.Utf8();ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamFixture","Import",imported)).at("status").To<Text>());
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>());
    stage="save manual source";auto edit=RapidRequest(scene);edit["manualPath"]=Map{{"points",Array{Variant(Array{502.,502.,502.})}},{"directions",Array{Variant(Array{0.,0.,1.})}}};Invoke(&scene,"CamRapid","EditPath",edit);
    stage="read manual source";const auto manual=fast_rapid_tests::Row(scene,edit.at("from"),edit.at("to")).at("manualPath");
    auto request=RapidRequest(scene,false);request["safe"]=true;
    stage="parallel safe generation";const auto checked=Wait(scene,Invoke(&scene,"CamRapid","Generate",request));ASSERT_EQ("Completed",checked.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(checked);
    stage="read failed source";const auto row=fast_rapid_tests::Row(scene,edit.at("from"),edit.at("to"));EXPECT_EQ(manual,row.at("manualPath"));EXPECT_EQ("Failed",row.at("geometryStatus").To<Text>());EXPECT_NE("Clear",row.at("validation").To<Text>());
    EXPECT_EQ("RAPID_MANUAL_PATH_BLOCKED",row.at("reason").To<Text>());EXPECT_NE(checked.at("result").To<Map>().at("count"),checked.at("result").To<Map>().at("clearCount"));
    }catch(const std::exception& error){FAIL()<<stage<<": "<<error.what();}
}
