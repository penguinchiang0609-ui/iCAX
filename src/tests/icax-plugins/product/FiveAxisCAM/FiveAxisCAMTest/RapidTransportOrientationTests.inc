TEST(FiveAxisRapidOrientation, EccentricDeclaredBoxUsesThePrecedingCutRollForPreviewAndFullCam)
{
    namespace G=iCAX::GeometryServices;namespace R=G::RigidMotion;
    Text stage="prepare source";try{
    Scene scene;PrepareOrderPart(scene);const auto ids=OrderedIDs(Generated(scene));ASSERT_GE(ids.size(),4u);
    Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Manual",ids));
    const auto setupID=ID::from_string(Read(scene).at("activeSetupId").To<Text>()).value();
    const auto setup=std::dynamic_pointer_cast<CFiveAxisSetupComponent>(scene.Database().GetEntity(setupID)->GetComponent(CFiveAxisSetupComponent::S_ClassName));ASSERT_TRUE(setup);
    const Map box{{"pose",Array{.5,0.,.005,0.,0.,0.}},{"geometry",Map{{"type",Text("box")},{"size",Array{.001,.001,.001}}}},{"enabled",true}};
    SetProjectTestHead(scene,setup,Map{{"kind",Text("Builtin")},{"name",Text("偏心碰撞盒")},{"calibrated",true},{"tcpLocalPosition",Array{0.,0.,0.}},
        {"beamLocalDirection",Array{0.,0.,-1.}},{"collisionSource",Text("MachineDefinition")},{"collisionBoxes",Array{box}}});
    // Keep this connection's Y/Z endpoint directions fixed throughout the test.
    // Only the preceding cut changes, from Z to X, to create transport holonomy.
    const auto fromID=ids[2],toID=ids[3];
    Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,fromID,"Fixed",Array{0.,1.,0.},"Workpiece"));
    Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,toID,"Fixed",Array{0.,0.,1.},"Workpiece"));GenerateAxis(scene);
    const auto selectedRequest=[&]{auto request=RapidRequest(scene);request["from"]=fromID;request["to"]=toID;return request;};
    const auto selectedPreview=[&]{return Wait(scene,Invoke(&scene,"CamRapid","Preview",selectedRequest()));};
    stage="preview original eccentric head";const auto original=selectedPreview();ASSERT_EQ("Completed",original.at("status").To<Text>());
    auto apply=GenerateRequest(scene);apply["taskId"]=original.at("taskId");Invoke(&scene,"CamRapid","Apply",apply);
    const auto beforeGeometry=Generated(scene),before=beforeGeometry.at("transitions").To<Array>().at(2).To<Map>();ASSERT_EQ("Generated",before.at("geometryStatus").To<Text>());

    // Four real constant-axis cuts form Z-X-Y-Z. This exercises the published
    // execution field without relying on local-control authoring semantics.
    stage="generate four-cut Z-X-Y-Z direction loop";Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,ids[1],"Fixed",Array{1.,0.,0.},"Workpiece"));
    GenerateAxis(scene);const auto generated=Generated(scene);const auto after=generated.at("transitions").To<Array>().at(2).To<Map>();
    for(std::size_t i=0;i<4;++i){const std::array<double,3> expected=i==1?std::array<double,3>{1,0,0}:i==2?std::array<double,3>{0,1,0}:std::array<double,3>{0,0,1};
        double error=0;std::size_t samples=0;for(const auto& value:AxisOperation(generated,ids[i]).at("parts").To<Array>())for(const auto& direction:value.To<Map>().at("directions").To<Array>()){
            const auto axis=AxisSampleVector(direction);for(int k=0;k<3;++k)error=std::max(error,std::abs(axis[k]-expected[k]));++samples;
        }ASSERT_GT(samples,0u);ASSERT_LT(error,1e-9)<<"Published fixed-axis cut "<<i<<" did not enter the generated execution field";
    }
    const auto from=AxisOperation(generated,fromID),to=AxisOperation(generated,toID);
    EXPECT_EQ(AxisOperation(beforeGeometry,fromID).at("setupEndDirection"),from.at("setupEndDirection"));EXPECT_EQ(AxisOperation(beforeGeometry,toID).at("setupStartDirection"),to.at("setupStartDirection"));
    EXPECT_NE(before.at("inputKey"),after.at("inputKey"));EXPECT_EQ("Stale",after.at("geometryStatus").To<Text>());
    stage="read full CAM endpoint pose";RunCollision(scene);const auto frames=SimulationReport(scene).at("frames").To<Array>();std::size_t endpoint=0;bool found=false;
    R::Quaternion closedLoopRotation{};bool loopClosed=false;
    for(std::size_t i=0;i<frames.size();++i){const auto frame=frames[i].To<Map>();if(frame.at("role").To<Text>()=="Rapid")continue;
        if(frame.at("intentId")==fromID){endpoint=i;found=true;}
        if(frame.at("intentId")==toID){const auto raw=frame.at("rotation").To<Array>();for(int k=0;k<4;++k)closedLoopRotation[k]=raw[k].To<double>();loopClosed=true;}
    }
    ASSERT_TRUE(loopClosed);const auto closedLoopX=R::Rotate(closedLoopRotation,{1,0,0});EXPECT_NEAR(0,closedLoopX[0],1e-8);EXPECT_NEAR(1,closedLoopX[1],1e-8);EXPECT_NEAR(0,closedLoopX[2],1e-8);
    ASSERT_TRUE(found);const auto frame=frames[endpoint].To<Map>();const auto raw=frame.at("rotation").To<Array>();R::Quaternion rotation{};for(int k=0;k<4;++k)rotation[k]=raw[k].To<double>();
    const auto position=AxisSampleVector(frame.at("position")),offset=R::Rotate(rotation,{500,0,5}),contact=R::Add(position,offset);
    const auto resetOffset=R::Rotate(R::Between({0,0,1},AxisSampleVector(from.at("setupEndDirection"))),{500,0,5});EXPECT_GT(R::Length(R::Sub(offset,resetOffset)),400.);
    stage="import fixture at transported box";File fixture(BRepPrimAPI_MakeBox(gp_Pnt(contact[0]-1,contact[1]-1,contact[2]-1),2.,2.,2.).Shape());
    auto imported=GenerateRequest(scene);imported["sourcePath"]=fixture.Utf8();ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamFixture","Import",imported)).at("status").To<Text>());
    GenerateAxis(scene);

    // The neutral planner alone has no preceding sequence. Its reset pose is
    // deliberately clear; the product must supply the actual transported roll.
    auto environment=std::make_shared<G::RapidCollisionGeometry>();environment->Boxes={G::CollisionBox{{500,0,5},{.5,.5,.5}}};
    G::TravelObstacle obstacle;obstacle.Id="transported-head-fixture";obstacle.Mesh=std::make_shared<G::CollisionMesh>(BoxCollisionMesh(contact,{1,1,1}));environment->Obstacles.push_back(obstacle);
    G::RapidPathPlanningQuery neutral;neutral.Collision=environment;neutral.Clearance=.3;neutral.Geometry.Kind=G::RapidPathKind::Retract;neutral.Geometry.FollowHeight=1;neutral.Geometry.LiftHeight=10;
    neutral.Geometry.Start={AxisSampleVector(from.at("setupEnd")),AxisSampleVector(from.at("setupEndDirection"))};
    neutral.Geometry.End={AxisSampleVector(to.at("setupStart")),AxisSampleVector(to.at("setupStartDirection"))};
    stage="compare neutral reset against product preview";const auto reset=scene.Services().Resolve<G::IRapidPathPlanningService>()->Plan({neutral}).front();ASSERT_TRUE(reset.Generated)<<reset.Reason;
    const auto preview=selectedPreview();ASSERT_EQ("Completed",preview.at("status").To<Text>());const auto connection=preview.at("result").To<Map>().at("connections").To<Map>().begin()->second.To<Map>();
    EXPECT_EQ("Failed",connection.at("result").To<Map>().at("geometryStatus").To<Text>());EXPECT_EQ("RAPID_ENDPOINT_BLOCKED",connection.at("result").To<Map>().at("reason").To<Text>());
    stage="check manual endpoint against transported box";auto check=selectedRequest();check["manualPath"]=Map{{"points",Array{}},{"directions",Array{}}};
    const auto checked=Wait(scene,Invoke(&scene,"CamRapid","CheckPath",check));ASSERT_EQ("Completed",checked.at("status").To<Text>());
    EXPECT_EQ("Failed",checked.at("result").To<Map>().at("connections").To<Map>().begin()->second.To<Map>().at("result").To<Map>().at("geometryStatus").To<Text>());
    stage="verify same full CAM endpoint is colliding";RunCollision(scene);bool samePoseCollision=false;
    for(const auto& value:SimulationReport(scene).at("keypoints").To<Array>()){const auto point=value.To<Map>();if(point.at("frameIndex").To<Text>()==std::to_string(endpoint))samePoseCollision|=point.at("status").To<Text>()=="Collision";}
    EXPECT_TRUE(samePoseCollision)<<"Full CAM and rapid preview must both reject the same transported endpoint box";
    stage="compare parallel safe batch against fresh transported preview";
    auto batchRequest=RapidRequest(scene,false);batchRequest["safe"]=true;batchRequest["maxParallelTasks"]=Text("4");
    const auto batch=Wait(scene,Invoke(&scene,"CamRapid","Generate",batchRequest));ASSERT_EQ("Completed",batch.at("status").To<Text>());
    const auto batchRecord=batch.at("result").To<Map>().at("connections").To<Map>().at(fromID.To<Text>()+"/"+toID.To<Text>()).To<Map>();
    const auto fresh=selectedPreview();ASSERT_EQ("Completed",fresh.at("status").To<Text>());
    const auto freshRecord=fresh.at("result").To<Map>().at("connections").To<Map>().begin()->second.To<Map>();
    EXPECT_EQ(batchRecord.at("inputKey"),freshRecord.at("inputKey"));
    EXPECT_EQ(batchRecord.at("result").To<Map>().at("validation"),freshRecord.at("result").To<Map>().at("validation"));
    EXPECT_EQ(batchRecord.at("result").To<Map>().at("reason"),freshRecord.at("result").To<Map>().at("reason"));
    }catch(const std::exception& error){FAIL()<<stage<<": "<<error.what();}
}
