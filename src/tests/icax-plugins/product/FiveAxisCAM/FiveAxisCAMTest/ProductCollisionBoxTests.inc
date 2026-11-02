TEST(FiveAxisProductCollisionBoxes, BuiltinReportsBoxMeshAndActualSegmentProgress)
{
    Scene scene;PrepareOrderPart(scene);const auto head=Head(scene).at("collisionModel").To<Map>();
    EXPECT_EQ("BoxCompoundMesh",head.at("representation").To<Text>());EXPECT_EQ("25",head.at("boxCount").To<Text>());EXPECT_DOUBLE_EQ(.01,head.at("meshDeflectionMm").To<double>());
    const auto task=RunCollision(scene),report=SimulationReport(scene),progress=task.at("collisionProgress").To<Map>();
    EXPECT_EQ("BoxCompoundMesh",report.at("representation").To<Text>());EXPECT_EQ("25",report.at("headBoxCount").To<Text>());EXPECT_DOUBLE_EQ(.01,report.at("meshDeflectionMm").To<double>());
    const auto segments=report.at("segments").To<Array>();ASSERT_FALSE(segments.empty());
    EXPECT_EQ(std::to_string(segments.size()),progress.at("totalSegmentCount").To<Text>());EXPECT_EQ(progress.at("totalSegmentCount"),progress.at("completedSegmentCount"));EXPECT_EQ(progress.at("fullSegmentCount"),progress.at("totalSegmentCount"));
    std::set<Text> segmentIndices;
    for(std::size_t i=0;i<segments.size();++i){const auto index=segments[i].To<Map>().at("index").To<Text>();EXPECT_EQ(std::to_string(i),index);EXPECT_TRUE(segmentIndices.insert(index).second);}
    EXPECT_EQ(segments.size(),segmentIndices.size());
    // Workers finish independently: the last completed item need not have the
    // greatest source index. It must belong to the actual checked set.
    EXPECT_FALSE(progress.contains("currentSegmentIndex"));EXPECT_TRUE(segmentIndices.contains(progress.at("lastCompletedSegmentIndex").To<Text>()));
    const auto repeated=RunCollision(scene),retry=SimulationReport(scene),retried=repeated.at("collisionProgress").To<Map>();
    EXPECT_EQ(retry.at("checkedSegmentCount"),retried.at("totalSegmentCount"));EXPECT_EQ(retried.at("totalSegmentCount"),retried.at("completedSegmentCount"));EXPECT_EQ(progress.at("fullSegmentCount"),retried.at("fullSegmentCount"));
    auto checkedIndices=segmentIndices;
    if(retry.at("checkMode").To<Text>()=="UncheckedOnly"){
        checkedIndices.clear();for(const auto& item:segments){const auto row=item.To<Map>();if(row.at("status").To<Text>()=="Unchecked")checkedIndices.insert(row.at("index").To<Text>());}
    }
    EXPECT_EQ(std::to_string(checkedIndices.size()),retried.at("totalSegmentCount").To<Text>());
    if(!checkedIndices.empty())EXPECT_TRUE(checkedIndices.contains(retried.at("lastCompletedSegmentIndex").To<Text>()));
    std::set<Text> retryIndices;const auto retrySegments=retry.at("segments").To<Array>();
    for(const auto& item:retrySegments)EXPECT_TRUE(retryIndices.insert(item.To<Map>().at("index").To<Text>()).second);
    EXPECT_EQ(segmentIndices,retryIndices);EXPECT_EQ(segments.size(),retrySegments.size());
}

TEST(FiveAxisProductCollisionBoxes, ImportedDisplayModelRequiresDeclaredBoxesInsteadOfMeshDerivedSlabs)
{
    Scene scene;PrepareOrderPart(scene);File source(BRepPrimAPI_MakeBox(gp_Pnt(-1,-2,5),2.,4.,64.).Shape());
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamHead","Import",ImportTestHead(scene,source))).at("status").To<Text>());
    EXPECT_TRUE(Head(scene).contains("collisionModel"));
    auto calibration=CalibrateTestHead(scene);calibration["tcpLocalPosition"]=Array{0.,0.,0.};Invoke(&scene,"CamHead","Configure",calibration);
    const auto original=Head(scene),model=original.at("collisionModel").To<Map>();
    EXPECT_EQ("0",model.at("boxCount").To<Text>());EXPECT_EQ("CollisionBoxesRequired",model.at("status").To<Text>());EXPECT_EQ("CalibratedTCP",model.at("frame").To<Text>());
    EXPECT_TRUE(original.at("calibrated").To<bool>());EXPECT_TRUE(original.contains("previewRef"));
    Text failure;try{Invoke(&scene,"CamSimulation","CheckCollision",GenerateRequest(scene));ADD_FAILURE()<<"A display mesh without declared collision boxes must not start collision planning";}catch(const std::exception& error){failure=error.what();}
    EXPECT_NE(Text::npos,failure.find("CAM_HEAD_COLLISION_BOXES_REQUIRED"));EXPECT_EQ("Missing",SimulationReport(scene).at("validity").To<Text>());
    calibration=CalibrateTestHead(scene);calibration["tcpLocalPosition"]=Array{0.,0.,0.};calibration["beamLocalDirection"]=Array{1.,0.,0.};Invoke(&scene,"CamHead","Configure",calibration);
    EXPECT_EQ("CollisionBoxesRequired",Head(scene).at("collisionModel").To<Map>().at("status").To<Text>());ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(original,Head(scene));
}

TEST(FiveAxisProductCollisionBoxes, MachineAuthoredHeadBoxCanTraverseARealTargetHoleButStopsAtItsMaterial)
{
    Scene scene;const auto plate=BRepPrimAPI_MakeBox(gp_Pnt(-10,-10,0),20.,20.,1.).Shape();
    const auto hole=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,0,-1),gp_Dir(0,0,1)),5.,3.).Shape();File source(BRepAlgoAPI_Cut(plate,hole).Shape());const auto imported=Imported(scene,source);
    const Array identity{1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.};auto placement=PlacementRequest(scene,imported);placement["transform"]=identity;Invoke(&scene,"CamPlacement","SetTransform",placement);
    const auto machine=iCAX::Data::GenerateNewUUID(),tool=iCAX::Data::GenerateNewUUID(),support=iCAX::Data::GenerateNewUUID();
    const auto attach=[](const auto& entity,const Text& name,const Map& fields){const auto c=entity->AddComponent(name);for(const auto& [key,value]:fields){Text error;if(!c->SetProperty(key,value,error))throw std::runtime_error(error);}return c;};
    const auto root=scene.Database().CreateEntity(machine);attach(root,"CMachineInstanceComponent",{{"Name",Text("Own mesh hole test")}});attach(root,"CTransformComponent",{});
    const auto part=[&](ID id,Text name,Array rest,Array colliders){const auto entity=scene.Database().CreateEntity(id);attach(entity,"CTransformComponent",{{"ParentEntityID",machine}});
        attach(entity,"CMachineElementComponent",{{"MachineID",machine},{"Name",name},{"ElementKind",Text("part")},{"RestPose",rest}});attach(entity,"CMachineCollisionComponent",{{"Collisions",colliders}});return entity;};
    const auto collider=[](double size,double z){const Map geometry{{"type",Text("box")},{"size",Array{size,size,size}}};return Variant(Map{{"enabled",true},{"pose",Array{0.,0.,z,0.,0.,0.}},{"geometry",geometry}});};
    const auto moving=part(tool,"head",Array{0.,0.,.5,0.,0.,0.},Array{collider(.001,0.)});
    const auto joint=attach(moving,"CMachineJointComponent",{{"JointType",Text("prismatic")},{"Axis",Array{1.,0.,0.}},{"LowerLimit",-100.},{"UpperLimit",100.},{"VelocityLimit",100.},{"Position",0.}});
    ASSERT_TRUE(joint->SetProperty("Position",30.));
    const auto table=part(support,"support",Array{0.,0.,0.,0.,0.,0.},Array{collider(.05,-.1)});
    attach(table,"CMachineJointComponent",{{"JointType",Text("revolute")},{"Axis",Array{0.,0.,1.}},{"LowerLimit",-1.},{"UpperLimit",1.},{"VelocityLimit",1.},{"Position",0.}});
    auto configure=GenerateRequest(scene);configure["configuration"]=Map{{"machineId",iCAX::Data::to_string(machine)},{"toolId",iCAX::Data::to_string(tool)},{"supportId",iCAX::Data::to_string(support)},{"placement",identity},{"tcp",Array{0.,0.,0.}},{"beam",Array{0.,0.,-1.}},{"toolCollisionRepresentation",Text("Boxes")}};ConfigureMachineSimulation(scene,configure);
    // Mount validation deliberately uses assembly bounds. Set up the actual
    // hole test afterwards, with the one-mm tool initially inside the void.
    ASSERT_TRUE(joint->SetProperty("Position",0.));
    const auto move=[&](double x){const auto state=Read(scene);return CheckMachineMove(scene,{{"expected",state.at("expected")},{"setupId",state.at("activeSetupId")},{"positions",Map{{iCAX::Data::to_string(tool),x}}}});};
    const auto clear=move(.1);EXPECT_EQ("Clear",clear.at("collisionStatus").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(clear);EXPECT_EQ("MachineDeclaredBoxesAndWorkpieceFixtureMesh",clear.at("collisionScope").To<Text>());EXPECT_EQ("MachineDeclaredBoxesV5",clear.at("collisionAlgorithm").To<Text>());EXPECT_EQ("MachineBoxes; WorkpieceFixtureOwnMesh",clear.at("collisionRepresentation").To<Text>());
    const auto blocked=move(6.);EXPECT_EQ("Blocked",blocked.at("collisionStatus").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(blocked);EXPECT_TRUE(blocked.contains("collisionWitnessFraction"));
    if(blocked.contains("collisionWitnessFraction")){EXPECT_GE(blocked.at("collisionWitnessFraction").To<double>(),0.);EXPECT_LE(blocked.at("collisionWitnessFraction").To<double>(),1.);}
    ASSERT_TRUE(moving->GetComponent<iCAX::Transform::CTransformComponent>()->SetParentEntityID(support));
    auto rootBox=collider(.001,.0005).To<Map>();rootBox["pose"]=Array{.006,0.,.0005,0.,0.,0.};const auto rootCollision=attach(root,"CMachineCollisionComponent",{{"Collisions",Array{rootBox}}});
    const auto rootBlocked=move(.1);ASSERT_EQ("Blocked",rootBlocked.at("collisionStatus").To<Text>());
    const auto bodies=rootBlocked.at("collisionBodies").To<Array>();EXPECT_NE(bodies.end(),std::find(bodies.begin(),bodies.end(),Variant(iCAX::Data::to_string(machine))));
    auto disabled=rootBox;disabled["enabled"]=false;
    ASSERT_TRUE(rootCollision->SetProperty("Collisions",Array{disabled}));EXPECT_EQ("Clear",move(.1).at("collisionStatus").To<Text>());
}
