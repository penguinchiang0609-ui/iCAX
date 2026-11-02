namespace fast_rapid_tests {
    Map Batch(Scene& scene){
        const auto task=Wait(scene,Invoke(&scene,"CamRapid","Generate",RapidRequest(scene,false)));
        if(task.at("status").To<Text>()!="Completed")throw std::runtime_error(iCAX::Data::VariantSerializer::Serialize(task));
        return task.at("result").To<Map>();
    }
    void ExpectUncheckedGeometry(const Map& batch){
        const auto connections=batch.at("connections").To<Map>();ASSERT_FALSE(connections.empty());
        EXPECT_EQ("GeometryOnly",batch.at("generation").To<Text>());
        for(const auto& [_,value]:connections){const auto record=value.To<Map>(),result=record.at("result").To<Map>();
            EXPECT_EQ("Generated",result.at("geometryStatus").To<Text>());EXPECT_EQ("Unchecked",result.at("validation").To<Text>());
            EXPECT_EQ("GeometryOnly",result.at("generation").To<Text>());EXPECT_EQ("CAM_RAPID_COLLISION_CHECK_REQUIRED",result.at("reason").To<Text>());
            EXPECT_EQ("0",result.at("candidateCount").To<Text>());EXPECT_EQ("0",result.at("headBoxCount").To<Text>());ASSERT_GE(result.at("points").To<Array>().size(),2u);
        }
    }
    Map Row(Scene& scene,const Variant& from,const Variant& to){
        for(const auto& value:Generated(scene).at("transitions").To<Array>()){const auto row=value.To<Map>();if(row.at("from")==from&&row.at("to")==to)return row;}
        throw std::runtime_error("Rapid test row missing");
    }
    // The resource remains at the same frozen URL/version. Any accidental
    // collision-mesh preparation now fails, while rule geometry stays usable.
    struct EmptyGeometryGuard {
        std::shared_ptr<iCAX::GeometryData::BRepModel> Value;iCAX::GeometryData::BRepModel Original;
        EmptyGeometryGuard(Scene& scene,const Map& reference){
            Value=scene.Resources().Get<iCAX::GeometryData::BRepModel>(reference.at("url").To<Text>(),std::stoull(reference.at("version").To<Text>()));
            if(!Value)throw std::runtime_error("Native test geometry missing");Original=*Value;*Value=iCAX::GeometryData::BRepModel{};
        }
        ~EmptyGeometryGuard(){*Value=std::move(Original);}
    };
}
TEST(FiveAxisFastRapid, BatchDoesNotRequireImportedHeadMeshOrDeclaredCollisionBoxes)
{
    namespace F=fast_rapid_tests;Scene scene;PrepareOrderPart(scene);File source(BRepPrimAPI_MakeBox(2.,2.,20.).Shape());
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamHead","Import",ImportTestHead(scene,source))).at("status").To<Text>());
    Invoke(&scene,"CamHead","Configure",CalibrateTestHead(scene));const auto head=Head(scene);EXPECT_EQ("CollisionBoxesRequired",head.at("collisionModel").To<Map>().at("status").To<Text>());
    F::EmptyGeometryGuard unavailableHead(scene,head.at("geometryRef").To<Map>());
    F::ExpectUncheckedGeometry(F::Batch(scene));EXPECT_EQ("0",Generated(scene).at("rapidClearCount").To<Text>());
    const auto reject=[&](const Text& domain,const Text& action,const Map& request){Text reason;try{Invoke(&scene,domain,action,request);ADD_FAILURE()<<domain<<"."<<action<<" must require enabled declared head boxes";}catch(const std::exception& error){reason=error.what();}EXPECT_NE(Text::npos,reason.find("CAM_HEAD_COLLISION_BOXES_REQUIRED"))<<reason;};
    reject("CamSimulation","CheckCollision",GenerateRequest(scene));reject("CamRapid","Preview",RapidRequest(scene));
    F::ExpectUncheckedGeometry(F::Batch(scene));
}
TEST(FiveAxisFastRapid, BatchConstructsGeometryWhileObstacleMeshPreparationIsUnavailable)
{
    namespace F=fast_rapid_tests;Scene scene;PrepareOrderPart(scene);File fixture(BRepPrimAPI_MakeBox(gp_Pnt(200,200,200),5.,5.,5.).Shape());
    auto import=GenerateRequest(scene);import["sourcePath"]=fixture.Utf8();ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamFixture","Import",import)).at("status").To<Text>());
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>());
    const auto state=Read(scene);ASSERT_EQ(1u,state.at("fixtures").To<Array>().size());F::EmptyGeometryGuard unavailableObstacle(scene,state.at("fixtures").To<Array>().front().To<Map>().at("geometryRef").To<Map>());
    F::ExpectUncheckedGeometry(F::Batch(scene));const auto generated=Generated(scene);EXPECT_EQ("0",generated.at("rapidClearCount").To<Text>());
    // Prove that the obstacle trap is active on the actual checked workflow.
    EXPECT_THROW(Invoke(&scene,"CamRapid","Preview",RapidRequest(scene)),std::exception);EXPECT_EQ(generated,Generated(scene));
}
TEST(FiveAxisFastRapid, BatchPreservesAuthoredWaypointsAndReusesUnchangedUncheckedGeometry)
{
    namespace F=fast_rapid_tests;Scene scene;PrepareOrderPart(scene);auto request=RapidRequest(scene);request.erase("commandId");const auto source=Invoke(&scene,"CamRapid","ReadEditPath",request);
    auto points=source.at("points").To<Array>();const auto directions=source.at("directions").To<Array>();ASSERT_GE(points.size(),4u);auto waypoint=points[1].To<Array>();waypoint[1]=17.;waypoint[2]=23.;points[1]=waypoint;
    const Map manual{{"points",Array(points.begin()+1,points.end()-1)},{"directions",Array(directions.begin()+1,directions.end()-1)}};request["manualPath"]=manual;request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());Invoke(&scene,"CamRapid","EditPath",request);
    const auto state=Read(scene);const auto setup=std::dynamic_pointer_cast<CFiveAxisSetupComponent>(scene.Database().GetEntity(*ID::from_string(state.at("activeSetupId").To<Text>()))->GetComponent(CFiveAxisSetupComponent::S_ClassName));ASSERT_TRUE(setup);const auto authored=setup->GetRapidOverrides();
    const auto first=F::Batch(scene);F::ExpectUncheckedGeometry(first);const auto row=F::Row(scene,request.at("from"),request.at("to"));EXPECT_EQ(manual,row.at("manualPath").To<Map>());EXPECT_EQ(points,row.at("points").To<Array>());EXPECT_EQ(directions,row.at("directions").To<Array>());EXPECT_EQ(authored,setup->GetRapidOverrides());
    const auto repeated=F::Batch(scene);F::ExpectUncheckedGeometry(repeated);EXPECT_EQ("0",repeated.at("calculatedCount").To<Text>());EXPECT_EQ(first.at("connections"),repeated.at("connections"));EXPECT_EQ(row,F::Row(scene,request.at("from"),request.at("to")));EXPECT_EQ(authored,setup->GetRapidOverrides());
}
TEST(FiveAxisFastRapid, SelectedSafePreviewAppliesOnlyItsConnectionAndBatchRetainsItsCertificate)
{
    namespace F=fast_rapid_tests;Scene scene;PrepareOrderPart(scene);F::ExpectUncheckedGeometry(F::Batch(scene));const auto before=Generated(scene);const auto original=before.at("transitions").To<Array>();ASSERT_GT(original.size(),1u);
    const auto request=RapidRequest(scene);const auto preview=Wait(scene,Invoke(&scene,"CamRapid","Preview",request));ASSERT_EQ("Completed",preview.at("status").To<Text>());const auto records=preview.at("result").To<Map>().at("connections").To<Map>();ASSERT_EQ(1u,records.size());const auto certified=records.begin()->second.To<Map>().at("result").To<Map>();ASSERT_EQ("Clear",certified.at("validation").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(certified);EXPECT_EQ("CollisionChecked",certified.at("generation").To<Text>());EXPECT_EQ(before,Generated(scene));
    auto apply=GenerateRequest(scene);apply["taskId"]=preview.at("taskId");Invoke(&scene,"CamRapid","Apply",apply);const auto applied=Generated(scene),selected=F::Row(scene,request.at("from"),request.at("to"));EXPECT_EQ("Clear",selected.at("validation").To<Text>());EXPECT_EQ("1",applied.at("rapidClearCount").To<Text>());
    for(const auto& value:original){const auto row=value.To<Map>();if(row.at("from")==request.at("from")&&row.at("to")==request.at("to"))continue;EXPECT_EQ(row,F::Row(scene,row.at("from"),row.at("to")));}
    const auto repeated=F::Batch(scene);EXPECT_EQ("0",repeated.at("calculatedCount").To<Text>());const auto retained=F::Row(scene,request.at("from"),request.at("to"));EXPECT_EQ("Clear",retained.at("validation").To<Text>());EXPECT_EQ(selected.at("inputKey"),retained.at("inputKey"));EXPECT_EQ(selected.at("points"),retained.at("points"));EXPECT_EQ("1",Generated(scene).at("rapidClearCount").To<Text>());
}
