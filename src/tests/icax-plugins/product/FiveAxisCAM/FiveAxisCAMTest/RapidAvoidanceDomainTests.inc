namespace rapid_avoidance_tests
{
    using namespace iCAX::GeometryServices;
    RapidPathPlanningQuery Query(Scene& scene){
        RapidPathPlanningQuery q;q.Geometry.Start={{-10,0,0},{0,0,1}};q.Geometry.End={{10,0,0},{0,0,1}};
        q.Geometry.Kind=RapidPathKind::Arch;q.Geometry.LiftHeight=2;q.Clearance=.1;
        auto env=std::make_shared<RapidCollisionGeometry>();env->Solids={{0,2,.2,.2}};
        env->Obstacles.push_back({"clamp",TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(-.2,-2,0),.4,4,30).Shape())});q.Collision=env;return q;
    }
    void ExpectOriginalEditingRoute(Scene& scene,const RapidPathPlanningQuery& query,const RapidPathPlanningResult& failed){
        const auto original=scene.Services().Resolve<IRapidPathGeometryService>()->Generate({query.Geometry},{}).front();ASSERT_TRUE(original.Generated)<<original.Reason;
        EXPECT_FALSE(failed.Generated);EXPECT_NE("Clear",failed.Status);EXPECT_EQ(0,failed.CertifiedClearance);ASSERT_EQ(original.Poses.size(),failed.Poses.size());
        for(size_t i=0;i<original.Poses.size();++i){EXPECT_EQ(original.Poses[i].Point,failed.Poses[i].Point);EXPECT_EQ(original.Poses[i].Direction,failed.Poses[i].Direction);}
        EXPECT_EQ(original.SurfacePoints,failed.SurfacePoints);EXPECT_EQ(original.Heights,failed.Heights);EXPECT_EQ(query.Geometry.Start.Point,failed.SourceStart.Point);EXPECT_EQ(query.Geometry.End.Point,failed.SourceEnd.Point);
        if(original.Construction){ASSERT_TRUE(failed.Construction);EXPECT_EQ(original.Construction->Kind,failed.Construction->Kind);EXPECT_EQ(original.Construction->LiftHeight,failed.Construction->LiftHeight);EXPECT_EQ(original.Construction->FollowHeight,failed.Construction->FollowHeight);EXPECT_EQ(original.Construction->LateralOffset,failed.Construction->LateralOffset);}
    }
    void IndependentCheck(Scene& scene,const RapidPathPlanningQuery& q,const RapidPathPlanningResult& route){
        ASSERT_FALSE(q.Collision->Solids.empty());CollisionQuery query;query.Obstacles=q.Collision->Obstacles;query.Solids=q.Collision->Solids;
        iCAX::GeometryServices::RigidMotion::Vector previous{0,0,1};iCAX::GeometryServices::RigidMotion::Quaternion rotation{0,0,0,1};
        std::vector<RigidMotionPose> poses;for(const auto& p:route.Poses){rotation=RigidMotion::Multiply(RigidMotion::Between(previous,p.Direction),rotation);previous=p.Direction;poses.push_back({p.Point,rotation});}
        for(std::size_t i=1;i<poses.size();++i)query.Segments.push_back({poses[i-1],poses[i],q.Clearance,true});
        // Independently certify each constituent solid; their union is the
        // full head. Smaller exact solves retain the kernel's time limit.
        for(const auto& solid:q.Collision->Solids){query.Solids={solid};
            const auto checked=CollisionService(scene)->Check(query,{});ASSERT_EQ(query.Segments.size(),checked.Segments.size());
            for(const auto& segment:checked.Segments)EXPECT_EQ("Clear",segment.Status)<<segment.Reason;
        }
    }
}
TEST(FiveAxisRapidAvoidance, FrogDetoursAtExactlyTheChosenHeightAndKeepsEndpoints)
{
    using namespace rapid_avoidance_tests;Scene scene;const auto q=Query(scene);const auto original=q.Geometry;
    const auto result=scene.Services().Resolve<IRapidPathPlanningService>()->Plan({q}).front();
    ASSERT_TRUE(result.Generated)<<result.Reason;EXPECT_EQ("Clear",result.Status);EXPECT_EQ("LateralDetour",result.Method);EXPECT_GT(result.Candidates,1u);
    EXPECT_NEAR(3,*std::max_element(result.Heights.begin(),result.Heights.end()),1e-9);
    EXPECT_EQ((TravelPoint{-10,0,1}),result.Poses.front().Point);EXPECT_EQ((TravelPoint{10,0,1}),result.Poses.back().Point);
    EXPECT_EQ(original.Kind,q.Geometry.Kind);EXPECT_EQ(original.LiftHeight,q.Geometry.LiftHeight);EXPECT_GE(result.CertifiedClearance,q.Clearance);
    IndependentCheck(scene,q,result);
}
TEST(FiveAxisRapidAvoidance, EnclosedStartHasNoRouteBelowChosenLiftAndNeverRaisesIt)
{
    using namespace rapid_avoidance_tests;Scene scene;auto q=Query(scene);auto env=std::make_shared<RapidCollisionGeometry>();env->Solids={{0,1,.2,.2}};
    for(const auto& shape:{BRepPrimAPI_MakeBox(gp_Pnt(-14,-4,-2),.5,8,8).Shape(),BRepPrimAPI_MakeBox(gp_Pnt(-6,-4,-2),.5,8,8).Shape(),BRepPrimAPI_MakeBox(gp_Pnt(-14,-4,-2),8,.5,8).Shape(),BRepPrimAPI_MakeBox(gp_Pnt(-14,3.5,-2),8,.5,8).Shape()})
        env->Obstacles.push_back({std::to_string(env->Obstacles.size()),TravelModel(scene,shape)});
    q.Collision=env;const auto service=scene.Services().Resolve<IRapidPathPlanningService>();const auto failed=service->Plan({q}).front();
    ExpectOriginalEditingRoute(scene,q,failed);EXPECT_EQ("RAPID_NO_FEASIBLE_ROUTE_WITHIN_PARAMETERS",failed.Reason);EXPECT_EQ(2,q.Geometry.LiftHeight);EXPECT_EQ(RapidPathKind::Arch,q.Geometry.Kind);
    q.Geometry.LiftHeight=20;const auto raised=service->Plan({q}).front();ASSERT_TRUE(raised.Generated)<<raised.Reason;EXPECT_EQ("Clear",raised.Status);IndependentCheck(scene,q,raised);
    EXPECT_THROW(service->Plan({q},[]{return true;}),std::exception);
}
TEST(FiveAxisRapidAvoidance, EndpointCollisionRetainsOriginalEditingGeometryAndRemainsFailed)
{
    using namespace rapid_avoidance_tests;Scene scene;auto q=Query(scene);q.Geometry.Start.Point={0,0,0};
    const auto result=scene.Services().Resolve<IRapidPathPlanningService>()->Plan({q}).front();
    ExpectOriginalEditingRoute(scene,q,result);EXPECT_EQ("RAPID_ENDPOINT_BLOCKED",result.Reason);EXPECT_EQ("clamp",result.Obstacle);
}
TEST(FiveAxisRapidAvoidance, BoxMeshFailuresRetainOriginalEditingGeometryWithoutPassingValidation)
{
    using namespace rapid_avoidance_tests;Scene scene;auto q=Query(scene);auto environment=std::make_shared<RapidCollisionGeometry>();environment->Boxes={CollisionBox{{0,0,0},{.1,.1,.1}}};
    for(const auto& [center,half]:std::vector<std::pair<TravelPoint,TravelPoint>>{{{-13.75,0,2},{.25,4,4}},{{-5.75,0,2},{.25,4,4}},{{-10,-3.75,2},{4,.25,4}},{{-10,3.75,2},{4,.25,4}}}){
        TravelObstacle obstacle;obstacle.Id=std::to_string(environment->Obstacles.size());obstacle.Mesh=std::make_shared<CollisionMesh>(BoxCollisionMesh(center,half));environment->Obstacles.push_back(std::move(obstacle));}
    q.Collision=environment;const auto service=scene.Services().Resolve<IRapidPathPlanningService>();const auto blocked=service->Plan({q}).front();
    EXPECT_EQ("RAPID_NO_FEASIBLE_ROUTE_WITHIN_PARAMETERS",blocked.Reason);ExpectOriginalEditingRoute(scene,q,blocked);
    q.Geometry.Start.Point={-13.75,0,0};const auto endpoint=service->Plan({q}).front();EXPECT_EQ("RAPID_ENDPOINT_BLOCKED",endpoint.Reason);ExpectOriginalEditingRoute(scene,q,endpoint);
}
TEST(FiveAxisRapidAvoidance, QuickAndFlyDetourWithoutSwitchingTheirConstructionOrHeight)
{
    using namespace rapid_avoidance_tests;Scene scene;auto q=Query(scene);
    q.Geometry.Surface=TravelModel(scene,BRepBuilderAPI_MakeFace(gp_Pln(gp::XOY()),-100,100,-100,100).Face());
    const auto service=scene.Services().Resolve<IRapidPathPlanningService>();
    for(const auto kind:{RapidPathKind::Retract,RapidPathKind::Follow}){
        q.Geometry.Kind=kind;const auto result=service->Plan({q}).front();ASSERT_TRUE(result.Generated)<<result.Reason;EXPECT_EQ("LateralDetour",result.Method);
        for(const auto& pose:result.Poses){EXPECT_LE(pose.Point[2],kind==RapidPathKind::Retract?3.+1e-9:1.+1e-9);EXPECT_EQ((TravelPoint{0,0,1}),pose.Direction);}
        EXPECT_EQ(kind,q.Geometry.Kind);EXPECT_EQ(2,q.Geometry.LiftHeight);IndependentCheck(scene,q,result);
    }
}
TEST(FiveAxisRapidAvoidance, DISABLED_Real206SeventhToEighthPassesIndependentContinuousCheck)
{
    using namespace rapid_avoidance_tests;using namespace ompl_evaluation;
    const auto path=Env(L"ICAX_RAPID_206_PROJECT");ASSERT_FALSE(path.empty());
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    const auto prepared=file.PrepareOpen(path);const auto info=prepared.Result().Info;Scene scene(info.ProjectID,info.MainSceneID,false);file.Restore(prepared,scene.Database(),scene.Resources());InitializeScene(scene);
    const auto before=Generated(scene);ASSERT_EQ(8u,before.at("operations").To<Array>().size());
    auto request=RapidRequest(scene);const auto connection=before.at("transitions").To<Array>()[6].To<Map>();request["from"]=connection.at("from");request["to"]=connection.at("to");
    const Map policy{{"strategy",Text("Frog")},{"marginMm",.2},{"liftHeightMm",10.}};request["policy"]=policy;
    if(connection.contains("manualPath"))request["manualPath"]=connection.at("manualPath");
    const auto started=Clock::now();const auto task=Wait(scene,Invoke(&scene,"CamRapid","Preview",request));ASSERT_EQ("Completed",task.at("status").To<Text>());
    const auto record=task.at("result").To<Map>().at("connections").To<Map>().begin()->second.To<Map>(),result=record.at("result").To<Map>();
    ASSERT_EQ("Generated",result.at("geometryStatus").To<Text>())<<result.at("reason").To<Text>();ASSERT_EQ("Clear",result.at("validation").To<Text>());EXPECT_EQ(policy,record.at("policy").To<Map>());
    RapidPathPlanningQuery check;auto environment=std::make_shared<RapidCollisionGeometry>();environment->Solids={{0,16,0,7},{16,16,7,11},{32,24,12,12}};
    const auto state=Read(scene);if(!state.at("fixtures").To<Array>().empty())EXPECT_EQ("LateralDetour",result.at("planningMethod").To<Text>());
    for(const auto& value:state.at("placements").To<Array>()){
        const auto placement=value.To<Map>();
        // This fixture contains exactly one placement and its original STEP.
        const auto reference=state.at("workpieces").To<Array>()[0].To<Map>().at("geometryRef").To<Map>();
        TravelObstacle obstacle;obstacle.Id=placement.at("id").To<Text>();obstacle.Geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(reference.at("url").To<Text>(),std::stoull(reference.at("version").To<Text>()));
        const auto transform=placement.at("transform").To<Array>();for(int i=0;i<16;++i)obstacle.Transform[i]=transform[i].To<double>();environment->Obstacles.push_back(std::move(obstacle));
    }
    for(const auto& value:state.at("fixtures").To<Array>()){
        const auto fixture=value.To<Map>();if(!fixture.at("collisionEnabled").To<bool>())continue;const auto reference=fixture.at("geometryRef").To<Map>();
        TravelObstacle obstacle;obstacle.Id=fixture.at("id").To<Text>();obstacle.Geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(reference.at("url").To<Text>(),std::stoull(reference.at("version").To<Text>()));
        const auto transform=fixture.at("transform").To<Array>();for(int i=0;i<16;++i)obstacle.Transform[i]=transform[i].To<double>();environment->Obstacles.push_back(std::move(obstacle));
    }
    check.Collision=environment;check.Clearance=.3;RapidPathPlanningResult route;
    const auto points=result.at("points").To<Array>(),directions=result.at("directions").To<Array>();for(std::size_t i=0;i<points.size();++i){TravelPose pose;for(int j=0;j<3;++j){pose.Point[j]=points[i].To<Array>()[j].To<double>();pose.Direction[j]=directions[i].To<Array>()[j].To<double>();}route.Poses.push_back(pose);}
    for(const auto& height:result.at("heightsMm").To<Array>())EXPECT_LE(height.To<double>(),11.+1e-9);
    IndependentCheck(scene,check,route);EXPECT_EQ(before,Generated(scene));
    std::clog<<"206 7->8 Frog 10 mm: "<<Ms(started)<<" ms including independent exact check; "<<route.Poses.size()<<" poses"<<std::endl;
}
TEST(FiveAxisRapid, FailedPreviewCannotBeAppliedOrPublishedAsGenerated)
{
    Scene scene;PrepareOrderPart(scene);File fixture(BRepPrimAPI_MakeBox(gp_Pnt(-100,-100,-100),200,200,200).Shape());
    auto import=GenerateRequest(scene);import["sourcePath"]=fixture.Utf8();ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamFixture","Import",import)).at("status").To<Text>());
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>());
    const auto before=Generated(scene);const auto preview=PreviewTravel(scene,"Frog"),result=preview.at("result").To<Map>().at("connections").To<Map>().begin()->second.To<Map>().at("result").To<Map>();
    EXPECT_EQ("Failed",result.at("geometryStatus").To<Text>());EXPECT_EQ("RAPID_ENDPOINT_BLOCKED",result.at("reason").To<Text>());EXPECT_TRUE(result.at("editingOnly").To<bool>());EXPECT_NE("Clear",result.at("validation").To<Text>());
    ASSERT_GE(result.at("points").To<Array>().size(),2u);EXPECT_EQ(result.at("points").To<Array>().size(),result.at("directions").To<Array>().size());EXPECT_EQ("Setup",result.at("frame").To<Text>());EXPECT_EQ("ToolTip",result.at("referencePoint").To<Text>());EXPECT_TRUE(result.contains("construction"));EXPECT_TRUE(result.contains("sourceStart"));EXPECT_TRUE(result.contains("sourceEnd"));
    auto apply=GenerateRequest(scene);apply["taskId"]=preview.at("taskId");EXPECT_THROW(Invoke(&scene,"CamRapid","Apply",apply),std::exception);EXPECT_EQ(before,Generated(scene));
    const auto batch=Wait(scene,Invoke(&scene,"CamRapid","Generate",RapidRequest(scene,false)));ASSERT_EQ("Completed",batch.at("status").To<Text>());EXPECT_GT(std::stoull(batch.at("result").To<Map>().at("generatedCount").To<Text>()),0u);EXPECT_EQ("0",Generated(scene).at("rapidClearCount").To<Text>());
    EXPECT_EQ(before.at("operations"),Generated(scene).at("operations"));
    const auto transition=Generated(scene).at("transitions").To<Array>().front().To<Map>();auto read=RapidRequest(scene);read.erase("commandId");const auto edit=Invoke(&scene,"CamRapid","ReadEditPath",read);
    EXPECT_EQ(transition.at("points"),edit.at("points"));EXPECT_EQ(transition.at("directions"),edit.at("directions"));EXPECT_EQ("Generated",edit.at("geometryStatus").To<Text>());EXPECT_EQ("Unchecked",edit.at("validation").To<Text>());EXPECT_FALSE(edit.at("endpointLocked").To<bool>());
}
