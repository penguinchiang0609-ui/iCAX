namespace { Variant BrowserWireValue(const Variant&); }
namespace keypose_tests {
    void SelectHead(Scene& scene){
        const auto root=(std::filesystem::path(__FILE__).parent_path()/"../../../../../..").lexically_normal();
        std::ifstream file(root/"src/apps/five-axis-cam/product.manifest.json",std::ios::binary);
        const auto manifest=iCAX::TemplateRuntime::CStandardJsonCodec::Parse(Text((std::istreambuf_iterator<char>(file)),{})).To<Map>();Array sources;
        for(const auto& value:manifest.at("capabilities").To<Map>().at("cuttingHead").To<Map>().at("defaults").To<Array>()){auto source=value.To<Map>();source["sourcePath"]=(root/"src"/source.at("sourcePath").To<Text>()).lexically_normal().string();sources.emplace_back(source);}
        BindBuiltinHeadSources(scene,sources);SelectInstalledHead(scene,sources.front().To<Map>());
    }
}
TEST(FiveAxisFullHeadCollision, LimitsPrecedeGeometryAndPathKindsKeepIndependentReports){
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,true);PrepareOrderPart(scene);keypose_tests::SelectHead(scene);GenerateAxis(scene);
    const auto ids=OrderedIDs(Generated(scene));const double angle=122.*std::numbers::pi/180.;
    for(const auto& id:ids)Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,id,"Fixed",{std::sin(angle),0.,std::cos(angle)}));
    collision_repair_tests::GenerateRepairAxis(scene);const auto cad=Read(scene).at("workpieces");
    auto request=GenerateRequest(scene);request["pathKind"]=Text("Cutting");
    const auto checked=Wait(scene,Invoke(&scene,"CamSimulation","CheckCollision",request));ASSERT_EQ("Completed",checked.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(checked);
    const auto cuts=SimulationReport(scene);EXPECT_EQ("Cutting",cuts.at("checkedPathKind").To<Text>());
    EXPECT_EQ("0",cuts.at("distanceQueries").To<Text>());
    ASSERT_FALSE(cuts.at("keyposeChecks").To<Array>().empty());
    for(const auto& value:cuts.at("keyposeChecks").To<Array>()){const auto row=value.To<Map>();EXPECT_NE("Rapid",row.at("location").To<Map>().at("role").To<Text>());EXPECT_EQ("CAM_HEAD_LIMIT_NO_SOLUTION",row.at("reason").To<Text>());EXPECT_FALSE(row.contains("witness"));}
    for(const auto& value:cuts.at("interpolationChecks").To<Array>())EXPECT_EQ("Skipped",value.To<Map>().at("status").To<Text>());
    // A failed cutting check is not a prerequisite for ordinary rapid generation.
    auto generate=GenerateRequest(scene);generate["safe"]=false;generate["geometryRevision"]=Generated(scene).at("revision");
    const auto generated=Wait(scene,Invoke(&scene,"CamRapid","Generate",generate));ASSERT_EQ("Completed",generated.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(generated);
    request=GenerateRequest(scene);request["pathKind"]=Text("Cutting");ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamSimulation","CheckCollision",request)).at("status").To<Text>());
    const auto cutRows=SimulationReport(scene).at("keyposeChecks");const auto before=Generated(scene);
    request=GenerateRequest(scene);request["pathKind"]=Text("Rapid");ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamSimulation","CheckCollision",request)).at("status").To<Text>());
    const auto both=SimulationReport(scene);EXPECT_EQ("Rapid",both.at("checkedPathKind").To<Text>());Array retained;bool rapid=false;
    for(const auto& value:both.at("keyposeChecks").To<Array>()){const auto row=value.To<Map>();if(row.at("location").To<Map>().at("role").To<Text>()=="Rapid")rapid=true;else retained.push_back(value);}
    EXPECT_TRUE(rapid);EXPECT_EQ(cutRows,Variant(retained));EXPECT_EQ(before,Generated(scene));EXPECT_EQ(cad,Read(scene).at("workpieces"));
}

TEST(FiveAxisTaskProgressPDO, SelectedHeadKeyposeLocationsSurviveProgressSerialization){
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,true,true);
    PrepareOrderPart(scene);keypose_tests::SelectHead(scene);GenerateAxis(scene);
    const auto receipt=Invoke(&scene,"CamSimulation","CheckCollision",GenerateRequest(scene));
    ASSERT_TRUE(receipt.contains("progressPDO"));
    const auto terminal=WaitTerminalPDO(scene,receipt);ASSERT_EQ("Completed",terminal.at("status").To<Text>());
    const auto progress=terminal.at("details").To<Map>().at("collisionProgress").To<Map>();
    ASSERT_TRUE(progress.contains("location"));auto location=progress.at("location").To<Map>();
    if(location.contains("first"))location=location.at("first").To<Map>();
    ASSERT_TRUE(location.contains("anchor"));const auto port=location.at("anchor").To<Map>().at("port").To<Map>();
    EXPECT_TRUE(port.at("operationId").Is<Text>());EXPECT_TRUE(port.at("portId").Is<Text>());
    EXPECT_EQ("Completed",QueryTaskOnce(scene,receipt).at("status").To<Text>());
    EXPECT_FALSE(scene.PDOHub().HasSlot(PDOIDOf(receipt,"progressPDO")));
}
TEST(FiveAxisFullHeadCollision, LimitRepairDoesNotTryToSolveCollisionBeforeLimitStageCompletes){
    Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,true);PrepareOrderPart(scene);keypose_tests::SelectHead(scene);GenerateAxis(scene);
    const auto id=OrderedIDs(Generated(scene)).front();const auto points=Invoke(&scene,"CamCut","ListToolAxisPoints",{{"expected",Read(scene).at("expected")},{"intentId",id}}).at("points").To<Array>();
    const auto anchor=points.front().To<Map>().at("anchor").To<Map>();const double angle=122.*std::numbers::pi/180.;
    Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,Text("start"),anchor,{std::sin(angle),0.,std::cos(angle)}));
    File fixture(BRepPrimAPI_MakeBox(gp_Pnt(-20000,-20000,-20000),40000,40000,40000).Shape());auto import=GenerateRequest(scene);import["sourcePath"]=fixture.Utf8();ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamFixture","Import",import)).at("status").To<Text>());
    collision_repair_tests::GenerateRepairAxis(scene);auto check=GenerateRequest(scene);check["intentId"]=id;ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamSimulation","CheckCollision",check)).at("status").To<Text>());
    Map issue;for(const auto& value:SimulationReport(scene).at("keyposeChecks").To<Array>()){const auto row=value.To<Map>();if(row.at("reason").To<Text>()=="CAM_HEAD_LIMIT_NO_SOLUTION"){issue=row;break;}}
    ASSERT_FALSE(issue.empty());const auto history=scene.Database().GetUndoArray().size();const auto before=AxisPointRead(scene,id,anchor);
    auto request=GenerateRequest(scene);request["keyposeFrameIndex"]=issue.at("frameIndex");request["repairPhase"]=Text("Limits");request["maxDeviationDeg"]=15.;request["maxTranslationMm"]=0.;
    const auto fixed=collision_repair_tests::WaitRepair(scene,Invoke(&scene,"CamSimulation","AutoRepairCollision",request));ASSERT_EQ("Completed",fixed.at("status").To<Text>());const auto result=fixed.at("result").To<Map>();ASSERT_TRUE(result.at("changed").To<bool>())<<iCAX::Data::VariantSerializer::Serialize(result);
    EXPECT_EQ("Limits",result.at("repairPhase").To<Text>());ASSERT_FALSE(result.at("toleranceOrigins").To<Array>().empty());EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
    collision_repair_tests::GenerateRepairAxis(scene);check=GenerateRequest(scene);check["intentId"]=id;ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamSimulation","CheckCollision",check)).at("status").To<Text>());
    bool collision=false;for(const auto& value:SimulationReport(scene).at("keyposeChecks").To<Array>()){const auto row=value.To<Map>();EXPECT_NE("CAM_HEAD_LIMIT_NO_SOLUTION",row.at("reason").To<Text>());collision|=row.at("status").To<Text>()=="Collision";}EXPECT_TRUE(collision);
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before.at("direction"),AxisPointRead(scene,id,anchor).at("direction"));ASSERT_TRUE(scene.Database().Redo());
}
TEST(FiveAxisFullHeadCollision, YokeOnlyContactUsesSelectedAssemblyAndHonorsDisabledBoxesAndUndo){
    using namespace head_instance_tests;namespace R=iCAX::GeometryServices::RigidMotion;
    const auto project=iCAX::Data::GenerateNewUUID();Scene scene(project,project,true,true);PrepareOrderPart(scene);
    const auto root=(std::filesystem::path(__FILE__).parent_path()/"../../../../../..").lexically_normal();
    std::ifstream manifestFile(root/"src/apps/five-axis-cam/product.manifest.json",std::ios::binary);
    const auto manifest=iCAX::TemplateRuntime::CStandardJsonCodec::Parse(Text((std::istreambuf_iterator<char>(manifestFile)),{})).To<Map>();
    Array sources;for(const auto& v:manifest.at("capabilities").To<Map>().at("cuttingHead").To<Map>().at("defaults").To<Array>()){
        auto source=v.To<Map>();source["sourcePath"]=(root/"src"/source.at("sourcePath").To<Text>()).lexically_normal().string();sources.emplace_back(source);
    }
    BindBuiltinHeadSources(scene,sources);SelectInstalledHead(scene,sources.front().To<Map>());GenerateAxis(scene);
    ASSERT_EQ("9",Head(scene).at("collisionModel").To<Map>().at("boxCount").To<Text>());
    RunCollision(scene);const auto baseline=SimulationReport(scene);
    ASSERT_EQ("CompleteSelectedHeadAssembly",baseline.at("headCoverage").To<Map>().at("scope").To<Text>());
    ASSERT_EQ("9",baseline.at("headBoxCount").To<Text>());
    EXPECT_LE(std::stoull(baseline.at("headCoverage").To<Map>().at("inverseSolveCount").To<Text>()),std::stoull(baseline.at("headCoverage").To<Map>().at("keyposeCount").To<Text>()));
    EXPECT_LT(std::stoull(baseline.at("headCoverage").To<Map>().at("keyposeCount").To<Text>()),baseline.at("frames").To<Array>().size());
    EXPECT_EQ("KeyposeJointInterpolationWithOriginalTCP",baseline.at("headCoverage").To<Map>().at("motion").To<Text>());
    const auto rows=baseline.at("segments").To<Array>();ASSERT_FALSE(rows.empty());
    const auto row=rows.front().To<Map>();ASSERT_TRUE(row.contains("headPose"))<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(row);
    const auto frame=baseline.at("frames").To<Array>().front().To<Map>();const auto rawPosition=frame.at("position").To<Array>(),rawRotation=frame.at("rotation").To<Array>();
    R::Vector position{};R::Quaternion rotation{};for(int k=0;k<3;++k)position[k]=rawPosition[k].To<double>();for(int k=0;k<4;++k)rotation[k]=rawRotation[k].To<double>();
    const auto firstBox=row.at("headPose").To<Map>().at("boxes").To<Array>().front().To<Map>();R::Vector local{};for(int k=0;k<3;++k)local[k]=firstBox.at("center").To<Array>()[k].To<double>();
    const auto center=R::Add(position,R::Rotate(rotation,local));
    File fixture(BRepPrimAPI_MakeBox(gp_Pnt(center[0]-1,center[1]-1,center[2]-1),2,2,2).Shape());auto importRequest=GenerateRequest(scene);importRequest["sourcePath"]=fixture.Utf8();
    const auto imported=Wait(scene,Invoke(&scene,"CamFixture","Import",importRequest));ASSERT_EQ("Completed",imported.at("status").To<Text>());
    const auto fixtureID=imported.at("result").To<Map>().at("fixtureId");
    GenerateAxis(scene);RunCollision(scene);const auto full=SimulationReport(scene);Map hit;
    ASSERT_TRUE(full.contains("keyposeChecks"));ASSERT_TRUE(full.contains("interpolationChecks"));
    EXPECT_LT(full.at("interpolationChecks").To<Array>().size(),full.at("segments").To<Array>().size());
    bool skipped=false;for(const auto& value:full.at("interpolationChecks").To<Array>())skipped|=value.To<Map>().at("status").To<Text>()=="Skipped";EXPECT_TRUE(skipped);
    for(const auto& value:full.at("segments").To<Array>()){const auto s=value.To<Map>();if(s.at("status").To<Text>()=="Collision"&&s.at("obstacleId")==fixtureID){hit=s;break;}}
    ASSERT_FALSE(hit.empty())<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(full);
    const auto boxIndex=std::stoull(hit.at("witness").To<Map>().at("headBoxIndex").To<Text>());
    const auto boxIdentity=full.at("headCoverage").To<Map>().at("boxes").To<Array>().at(boxIndex).To<Map>();EXPECT_EQ("0",boxIdentity.at("partIndex").To<Text>());
    EXPECT_EQ(2u,hit.at("headPose").To<Map>().at("partTransforms").To<Array>().size());
    EXPECT_EQ(Instance(scene).at("id"),full.at("headCoverage").To<Map>().at("headInstanceId"));EXPECT_FALSE(full.at("headCoverage").To<Map>().at("machineInstanceIncluded").To<bool>());
    const auto directory=root/"output/acceptance/selected-collision-20261102/full-head";std::filesystem::create_directories(directory);
    std::ofstream(directory/"yoke-contact.json")<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(BrowserWireValue(Map{{"head",Head(scene)},{"report",full},{"hit",hit},{"fixtureId",fixtureID}}));
    SaveProductPlanFixture(scene,directory/"yoke-contact.i5cam");
    auto boxes=Head(scene).at("assembly").To<Map>().at("nodes").To<Array>().front().To<Map>().at("collisionBoxes").To<Array>();
    for(auto& value:boxes){auto b=value.To<Map>();b["enabled"]=false;value=b;}
    auto disable=Command(scene,"colliders");disable["instanceId"]=Instance(scene).at("id");disable["collisionOverrides"]=Array{Map{{"partIndex",0},{"boxes",boxes}}};
    const auto undo=scene.Database().GetUndoArray().size();Invoke(&scene,"CamHead","Instance",disable);EXPECT_EQ(undo+1,scene.Database().GetUndoArray().size());
    GenerateAxis(scene);RunCollision(scene);const auto terminalOnly=SimulationReport(scene);EXPECT_EQ("6",terminalOnly.at("headBoxCount").To<Text>());
    for(const auto& value:terminalOnly.at("segments").To<Array>()){const auto s=value.To<Map>();EXPECT_FALSE(s.at("status").To<Text>()=="Collision"&&s.at("obstacleId")==fixtureID);}
    EXPECT_EQ(undo+1,scene.Database().GetUndoArray().size());ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ("9",Head(scene).at("collisionModel").To<Map>().at("boxCount").To<Text>());
    ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ("6",Head(scene).at("collisionModel").To<Map>().at("boxCount").To<Text>());
    std::ofstream(directory/"native-acceptance.json")<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(BrowserWireValue(Map{{"selectedHead",Instance(scene).at("id")},{"fullHeadBoxes",9},{"terminalBoxes",6},{"yokeHitBox",boxIdentity},{"terminalOnlyReport",terminalOnly}}));
}

TEST(FiveAxisFullHeadCollision, LimitIssueRepairsOnlySelectedKeypointAndRetainsHistory){
    using namespace head_instance_tests;Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,true);PrepareOrderPart(scene);keypose_tests::SelectHead(scene);
    // Keep real declared joint limits, while separating collision geometry from
    // the workpiece so this acceptance isolates a joint-limit failure.
    Array overrides;const auto nodes=Head(scene).at("assembly").To<Map>().at("nodes").To<Array>();
    for(std::size_t i=0;i<nodes.size();++i){auto boxes=nodes[i].To<Map>().at("collisionBoxes").To<Array>();for(auto& value:boxes){auto box=value.To<Map>();box["pose"]=Array{0.,0.,10.,0.,0.,0.};box["geometry"]=Map{{"type",Text("box")},{"size",Array{.002,.002,.002}}};value=box;}overrides.emplace_back(Map{{"partIndex",static_cast<unsigned>(i)},{"boxes",boxes}});}
    auto collider=Command(scene,"colliders");collider["instanceId"]=Instance(scene).at("id");collider["collisionOverrides"]=overrides;Invoke(&scene,"CamHead","Instance",collider);GenerateAxis(scene);
    const auto ids=OrderedIDs(Generated(scene));const auto id=ids.front();const auto points=Invoke(&scene,"CamCut","ListToolAxisPoints",{{"expected",Read(scene).at("expected")},{"intentId",id}}).at("points").To<Array>();
    const auto anchor=points.front().To<Map>().at("anchor").To<Map>();const double angle=122.*3.14159265358979323846/180.;
    Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,Text("start"),anchor,{std::sin(angle),0.,std::cos(angle)}));collision_repair_tests::GenerateRepairAxis(scene);collision_repair_tests::CheckRepairCollision(scene);
    const auto report=SimulationReport(scene);Map issue;
    for(const auto& value:report.at("keyposeChecks").To<Array>()){const auto row=value.To<Map>();if(row.at("location").To<Map>().at("intentId")==id&&row.contains("reason")&&row.at("reason").To<Text>()=="CAM_HEAD_LIMIT_NO_SOLUTION"){issue=row;break;}}
    ASSERT_FALSE(issue.empty())<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(BrowserWireValue(report));
    const auto before=AxisPointRead(scene,id,anchor),other=AxisOperation(Generated(scene),ids.back());const auto cad=Read(scene).at("workpieces");const auto history=scene.Database().GetUndoArray().size();
    auto request=GenerateRequest(scene);request["keyposeFrameIndex"]=issue.at("frameIndex");request["maxDeviationDeg"]=15.;request["maxTranslationMm"]=0.;
    const auto fixed=collision_repair_tests::WaitRepair(scene,Invoke(&scene,"CamSimulation","AutoRepairCollision",request));
    ASSERT_EQ("Completed",fixed.at("status").To<Text>())<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(BrowserWireValue(fixed));
    ASSERT_TRUE(fixed.at("result").To<Map>().at("changed").To<bool>())<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(BrowserWireValue(fixed));
    EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());const auto after=AxisPointRead(scene,id,anchor);
    EXPECT_EQ(before.at("point"),after.at("point"));const auto a=before.at("direction").To<Array>(),b=after.at("direction").To<Array>();double dot=0;for(int k=0;k<3;++k)dot+=a[k].To<double>()*b[k].To<double>();EXPECT_LE(std::acos(std::clamp(dot,-1.,1.)),15.*3.14159265358979323846/180.+1e-8);
    EXPECT_EQ(cad,Read(scene).at("workpieces"));EXPECT_EQ(other,AxisOperation(Generated(scene),ids.back()));ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before.at("direction"),AxisPointRead(scene,id,anchor).at("direction"));ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(after.at("direction"),AxisPointRead(scene,id,anchor).at("direction"));
}

TEST(FiveAxisFullHeadCollision, RapidLimitRepairChangesOnlySelectedVertexAndUsesOneUndoStep){
    using namespace head_instance_tests;Scene scene(iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID(),true,true);PrepareOrderPart(scene);keypose_tests::SelectHead(scene);
    Array overrides;const auto nodes=Head(scene).at("assembly").To<Map>().at("nodes").To<Array>();
    for(std::size_t i=0;i<nodes.size();++i){auto boxes=nodes[i].To<Map>().at("collisionBoxes").To<Array>();for(auto& value:boxes){auto box=value.To<Map>();box["pose"]=Array{0.,0.,10.,0.,0.,0.};box["geometry"]=Map{{"type",Text("box")},{"size",Array{.002,.002,.002}}};value=box;}overrides.emplace_back(Map{{"partIndex",static_cast<unsigned>(i)},{"boxes",boxes}});}
    auto collider=Command(scene,"colliders");collider["instanceId"]=Instance(scene).at("id");collider["collisionOverrides"]=overrides;Invoke(&scene,"CamHead","Instance",collider);GenerateAxis(scene);
    auto read=RapidRequest(scene);read.erase("commandId");const auto source=Invoke(&scene,"CamRapid","ReadEditPath",read);const auto points=source.at("points").To<Array>();auto axes=source.at("directions").To<Array>();ASSERT_GT(points.size(),2u);
    const double radians=122.*std::numbers::pi/180.;axes[1]=Array{std::sin(radians),0.,std::cos(radians)};
    auto edit=RapidRequest(scene);edit["manualPath"]=Map{{"points",Array(points.begin()+1,points.end()-1)},{"directions",Array(axes.begin()+1,axes.end()-1)}};Invoke(&scene,"CamRapid","EditPath",edit);
    collision_repair_tests::CheckRepairCollision(scene);const auto report=SimulationReport(scene);Map issue;std::size_t keyCount=0;
    for(const auto& value:report.at("keyposeChecks").To<Array>()){const auto row=value.To<Map>(),location=row.at("location").To<Map>();if(location.at("role").To<Text>()!="Rapid"||location.at("from")!=edit.at("from")||location.at("to")!=edit.at("to"))continue;++keyCount;if(location.at("rapidPointIndex").To<Text>()=="1")issue=row;}
    EXPECT_EQ(points.size(),keyCount);ASSERT_FALSE(issue.empty());ASSERT_EQ("CAM_HEAD_LIMIT_NO_SOLUTION",issue.at("reason").To<Text>());
    const auto before=Generated(scene);const auto cad=Read(scene).at("workpieces");const auto history=scene.Database().GetUndoArray().size();
    auto request=GenerateRequest(scene);request["keyposeFrameIndex"]=issue.at("frameIndex");request["maxDeviationDeg"]=0.;request["maxTranslationMm"]=0.;
    const auto unresolved=collision_repair_tests::WaitRepair(scene,Invoke(&scene,"CamSimulation","AutoRepairCollision",request));ASSERT_EQ("Completed",unresolved.at("status").To<Text>());EXPECT_FALSE(unresolved.at("result").To<Map>().at("changed").To<bool>());EXPECT_EQ(before,Generated(scene));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    request=GenerateRequest(scene);request["keyposeFrameIndex"]=issue.at("frameIndex");request["maxDeviationDeg"]=15.;request["maxTranslationMm"]=0.;
    const auto repaired=collision_repair_tests::WaitRepair(scene,Invoke(&scene,"CamSimulation","AutoRepairCollision",request));ASSERT_EQ("Completed",repaired.at("status").To<Text>())<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(BrowserWireValue(repaired));
    ASSERT_TRUE(repaired.at("result").To<Map>().at("changed").To<bool>())<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(BrowserWireValue(repaired));
    const auto after=Generated(scene),old=before.at("transitions").To<Array>().front().To<Map>(),now=after.at("transitions").To<Array>().front().To<Map>();
    EXPECT_EQ(before.at("operations"),after.at("operations"));EXPECT_EQ(cad,Read(scene).at("workpieces"));EXPECT_EQ(old.at("points"),now.at("points"));
    const auto changedAxes=now.at("directions").To<Array>();for(std::size_t i=0;i<axes.size();++i)if(i!=1)EXPECT_EQ(axes[i],changedAxes[i]);
    double dot=0;for(int k=0;k<3;++k)dot+=axes[1].To<Array>()[k].To<double>()*changedAxes[1].To<Array>()[k].To<double>();EXPECT_LE(std::acos(std::clamp(dot,-1.,1.)),15.*std::numbers::pi/180.+1e-8);
    EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before,Generated(scene));ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(after,Generated(scene));
    auto check=GenerateRequest(scene);check["from"]=edit.at("from");check["to"]=edit.at("to");collision_repair_tests::WaitRepairPreparation(scene,Invoke(&scene,"CamSimulation","CheckCollision",check));
    for(const auto& value:SimulationReport(scene).at("keyposeChecks").To<Array>()){const auto row=value.To<Map>(),location=row.at("location").To<Map>();if(location.at("role").To<Text>()=="Rapid"&&location.at("from")==edit.at("from")&&location.at("to")==edit.at("to"))EXPECT_EQ("Clear",row.at("status").To<Text>());}
}
TEST(FiveAxisFullHeadCollision, PositionAndDirectionEditIsDeferredUndoableAndLocal){
    Scene scene;PrepareOrderPart(scene);const auto ids=OrderedIDs(Generated(scene));ASSERT_GT(ids.size(),1u);const auto id=ids.front();
    const auto cad=Read(scene).at("workpieces");const auto other=AxisOperation(Generated(scene),ids.back());
    const auto points=Invoke(&scene,"CamCut","ListToolAxisPoints",{{"expected",Read(scene).at("expected")},{"intentId",id}}).at("points").To<Array>();
    const auto start=points.front().To<Map>(),anchor=start.at("anchor").To<Map>();auto position=start.at("point").To<Array>();position[2]=position[2].To<double>()+3.;
    auto request=AxisPointRequest(scene,id,Text("start"),anchor,{0.,.1,1.});request["position"]=position;
    const auto undo=scene.Database().GetUndoArray().size();const auto result=Invoke(&scene,"CamCut","EditToolAxisPoint",request);
    EXPECT_TRUE(result.at("deferred").To<bool>());EXPECT_EQ(position,result.at("selectedAxis").To<Map>().at("point").To<Array>());
    EXPECT_EQ(undo+1,scene.Database().GetUndoArray().size());
    const auto afterEdit=AxisOperation(Generated(scene),ids.back());for(const auto& [key,value]:other)EXPECT_EQ(value,afterEdit.at(key))<<key;
    EXPECT_EQ(cad,Read(scene).at("workpieces"));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(start.at("point"),AxisPointRead(scene,id,anchor).at("point"));ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(Variant(position),AxisPointRead(scene,id,anchor).at("point"));
    auto generation=GenerateRequest(scene);generation["intentId"]=id;const auto generated=Wait(scene,Invoke(&scene,"CamPlan","Generate",generation));ASSERT_EQ("Completed",generated.at("status").To<Text>())<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(BrowserWireValue(Map{{"task",generated},{"errors",Read(scene).at("intentGenerationErrors")}}));
    EXPECT_EQ("1",generated.at("result").To<Map>().at("calculatedCount").To<Text>());
    const auto afterGeneration=AxisOperation(Generated(scene),ids.back());for(const auto& [key,value]:other)EXPECT_EQ(value,afterGeneration.at(key))<<key;
    EXPECT_EQ(other.size()+(other.contains("simulationStartFrame")?0:1),afterGeneration.size());EXPECT_EQ(cad,Read(scene).at("workpieces"));
    bool matched=false;for(const auto& value:AxisOperation(Generated(scene),id).at("parts").To<Array>()){const auto part=value.To<Map>();const auto samples=part.at("sourceSamples").To<Array>();
        for(std::size_t i=0;i<samples.size();++i){const auto current=samples[i].To<Map>().at("anchor").To<Map>();if(current.at("segmentId")==anchor.at("segmentId")&&current.at("parameter")==anchor.at("parameter")){EXPECT_EQ(Variant(position),part.at("points").To<Array>().at(i));matched=true;}}}
    EXPECT_TRUE(matched);
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Keypoint XYZ and IJK";
    const auto saved=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(saved,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(saved,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(Variant(position),AxisPointRead(reopened,id,anchor).at("point"));EXPECT_EQ(cad,Read(reopened).at("workpieces"));std::filesystem::remove(saved);
}
