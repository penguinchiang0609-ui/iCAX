namespace rapid_construction_tests
{
    using namespace iCAX::GeometryServices;
    RapidPathGeometryQuery Flat(){RapidPathGeometryQuery q;q.Start={{-20,0,0},{0,0,1}};q.End={{20,0,0},{0,0,1}};return q;}
    auto Service(Scene& scene){return scene.Services().Resolve<IRapidPathGeometryService>();}
}
TEST(FiveAxisRapidConstruction, RequiresNoCollisionServiceAndUsesExactlyTheSpecifiedHeights)
{
    using namespace rapid_construction_tests;
    Scene scene;iCAX::Services::CServiceProvider provider;
    iCAX::Services::CServiceRegistrationCatalog::ReplayByModulePaths(provider,{Module(L"OpenCascadeResourceImport.dll")});
    EXPECT_THROW(provider.Resolve<ICollisionDistanceService>(),std::exception);
    auto service=provider.Resolve<IRapidPathGeometryService>();auto q=Flat();q.LiftHeight=3;
    const auto quick=service->Generate({q},{}).front();ASSERT_TRUE(quick.Generated)<<quick.Reason;ASSERT_EQ(4u,quick.Poses.size());
    EXPECT_EQ((TravelPoint{-20,0,1}),quick.Poses[0].Point);EXPECT_EQ((TravelPoint{-20,0,4}),quick.Poses[1].Point);
    EXPECT_EQ((TravelPoint{20,0,4}),quick.Poses[2].Point);EXPECT_EQ((TravelPoint{20,0,1}),quick.Poses[3].Point);
    q.Kind=RapidPathKind::Arch;const auto frog=service->Generate({q},{}).front();ASSERT_TRUE(frog.Generated)<<frog.Reason;
    EXPECT_NEAR(4,*std::max_element(frog.Heights.begin(),frog.Heights.end()),1e-10);
    q.LiftHeight=7;const auto raised=service->Generate({q},{}).front();ASSERT_TRUE(raised.Generated);EXPECT_NEAR(8,*std::max_element(raised.Heights.begin(),raised.Heights.end()),1e-10);
}
TEST(FiveAxisRapidConstruction, FollowAcceptsAuthoredEndpointPositionsAndAxesWithoutSurfaceJumps)
{
    using namespace rapid_construction_tests;Scene scene;auto q=Flat();q.Kind=RapidPathKind::Follow;q.Manual=true;q.ConstructedManual=true;
    q.Surface=TravelModel(scene,BRepBuilderAPI_MakeFace(gp_Pln(gp::XOY()),-50,50,-50,50).Face());
    q.Start.Point[2]=3;q.End.Point[2]=5;q.Start.Direction={0.,.1,std::sqrt(.99)};q.End.Direction={.2,0.,std::sqrt(.96)};
    const auto result=Service(scene)->Generate({q},{}).front();ASSERT_TRUE(result.Generated)<<result.Reason;ASSERT_TRUE(result.Construction);EXPECT_EQ(RapidPathKind::Follow,result.Construction->Kind);
    for(int i=0;i<3;++i){EXPECT_NEAR(q.Start.Point[i]+q.Start.Direction[i]*q.FollowHeight,result.Poses.front().Point[i],1e-9);EXPECT_NEAR(q.End.Point[i]+q.End.Direction[i]*q.FollowHeight,result.Poses.back().Point[i],1e-9);}
    EXPECT_EQ(q.Start.Direction,result.Poses.front().Direction);EXPECT_EQ(q.End.Direction,result.Poses.back().Direction);ASSERT_GT(result.Poses.size(),2u);
    for(std::size_t i=1;i<result.Poses.size();++i)EXPECT_LT(iCAX::GeometryServices::RigidMotion::Length(iCAX::GeometryServices::RigidMotion::Sub(result.Poses[i].Point,result.Poses[i-1].Point)),2.1);
}
TEST(FiveAxisRapidConstruction, FollowUsesThePlacedSurfaceWithoutCollisionPreparation)
{
    using namespace rapid_construction_tests;
    Scene scene;auto q=Flat();const auto z=std::sqrt(500.);q.Kind=RapidPathKind::Follow;
    q.Surface=TravelModel(scene,BRepPrimAPI_MakeSphere(30).Shape());q.SurfaceTransform[3]=70;
    q.Start={{50,0,z},{-20/30.,0,z/30.}};q.End={{90,0,z},{20/30.,0,z/30.}};
    const auto r=Service(scene)->Generate({q},{}).front();ASSERT_TRUE(r.Generated)<<r.Reason;ASSERT_GT(r.Poses.size(),5u);
    for(const auto& p:r.Poses)EXPECT_NEAR(31,std::hypot(p.Point[0]-70,p.Point[1],p.Point[2]),1e-6);
}
TEST(FiveAxisRapidConstruction, AuthoredConstructionRetainsExactFormulaHandlesAndLockedEndpoints){
    using namespace rapid_construction_tests;Scene scene;const auto service=Service(scene);auto query=Flat();query.LiftHeight=7;query.LateralOffset={3,4,0};
    for(const auto kind:{RapidPathKind::Retract,RapidPathKind::Arch}){query.Kind=kind;query.Manual=false;query.ConstructedManual=false;const auto automatic=service->Generate({query},{}).front();ASSERT_TRUE(automatic.Generated);
        query.Manual=true;query.ConstructedManual=true;const auto edited=service->Generate({query},{}).front();ASSERT_TRUE(edited.Generated)<<edited.Reason;ASSERT_TRUE(edited.Construction);ASSERT_TRUE(edited.ShapeHandle);ASSERT_EQ(automatic.Poses.size(),edited.Poses.size());
        for(size_t i=0;i<automatic.Poses.size();++i){EXPECT_EQ(automatic.Poses[i].Point,edited.Poses[i].Point);EXPECT_EQ(automatic.Poses[i].Direction,edited.Poses[i].Direction);}
        EXPECT_EQ((TravelPoint{-20,0,1}),edited.Poses.front().Point);EXPECT_EQ((TravelPoint{20,0,1}),edited.Poses.back().Point);EXPECT_EQ((TravelPoint{3,4,8}),edited.ShapeHandle->Point);
        EXPECT_EQ(query.Start.Point,edited.SourceStart.Point);EXPECT_EQ(query.End.Point,edited.SourceEnd.Point);EXPECT_EQ(query.LateralOffset,edited.Construction->LateralOffset);
    }
    query=Flat();query.Kind=RapidPathKind::Follow;query.Surface=TravelModel(scene,BRepPrimAPI_MakeSphere(30).Shape());query.SurfaceTransform[3]=70;const auto z=std::sqrt(500.);
    query.Start={{50,0,z},{-20/30.,0,z/30.}};query.End={{90,0,z},{20/30.,0,z/30.}};query.Manual=true;query.ConstructedManual=true;
    const auto follow=service->Generate({query},{}).front();ASSERT_TRUE(follow.Generated)<<follow.Reason;ASSERT_TRUE(follow.ShapeHandle);ASSERT_TRUE(follow.Construction);EXPECT_NEAR(70,follow.ShapeHandle->Point[0],1e-8);EXPECT_NEAR(31,follow.ShapeHandle->Point[2],1e-8);
    query=Flat();query.Manual=true;query.Waypoints={{{-7,5,8},{0,0,1}},{{6,5,7},{0,0,1}}};const auto polyline=service->Generate({query},{}).front();ASSERT_TRUE(polyline.Generated);EXPECT_FALSE(polyline.Construction);EXPECT_FALSE(polyline.ShapeHandle);ASSERT_EQ(4u,polyline.Poses.size());EXPECT_EQ(query.Waypoints[0].Point,polyline.Poses[1].Point);EXPECT_EQ(query.Waypoints[1].Point,polyline.Poses[2].Point);
    query.ConstructedManual=true;EXPECT_THROW(service->Generate({query},{}),std::invalid_argument);
}
TEST(FiveAxisRapidConstruction, RetractTurnKeepsLiftedAnchorsAndUsesTheExactEndAxisAtItsHandle){
    using namespace rapid_construction_tests;Scene scene;auto query=Flat();query.LiftHeight=7;query.End.Direction={0,-1,0};const auto service=Service(scene);const auto original=service->Generate({query},{}).front();ASSERT_TRUE(original.Generated);ASSERT_EQ(4u,original.Poses.size());
    query.TurnPoint=TravelPoint{original.Poses[2].Point[0],original.Poses[1].Point[1],original.Poses[1].Point[2]};const auto turned=service->Generate({query},{}).front();ASSERT_TRUE(turned.Generated)<<turned.Reason;ASSERT_EQ(5u,turned.Poses.size());
    for(const auto [before,after]:std::array<std::array<size_t,2>,4>{{{0,0},{1,1},{2,3},{3,4}}}){EXPECT_EQ(original.Poses[before].Point,turned.Poses[after].Point);EXPECT_EQ(original.Poses[before].Direction,turned.Poses[after].Direction);}
    EXPECT_EQ(*query.TurnPoint,turned.Poses[2].Point);EXPECT_EQ(query.End.Direction,turned.Poses[2].Direction);ASSERT_TRUE(turned.ShapeHandle);EXPECT_EQ(*query.TurnPoint,turned.ShapeHandle->Point);EXPECT_EQ(query.End.Direction,turned.ShapeHandle->Direction);ASSERT_TRUE(turned.Construction);EXPECT_EQ(query.TurnPoint,turned.Construction->TurnPoint);
    for(int axis=0;axis<3;++axis){EXPECT_GE((*query.TurnPoint)[axis],std::min(original.Poses[1].Point[axis],original.Poses[2].Point[axis]));EXPECT_LE((*query.TurnPoint)[axis],std::max(original.Poses[1].Point[axis],original.Poses[2].Point[axis]));}
    // A thin obstacle blocks the original diagonal A->B at its midpoint. The
    // exact AABB corner construction must still pass independent whole-motion
    // CCD with the real +Z -> -Y orientation change on A->turn.
    namespace G=iCAX::GeometryServices;namespace M=G::RigidMotion;G::RigidMotionCollisionQuery collision;collision.Boxes={G::CollisionBox{{0,0,0},{.1,.1,.1}}};G::TravelObstacle obstacle;obstacle.Id="diagonal-only-blocker";obstacle.Mesh=std::make_shared<G::CollisionMesh>(BoxCollisionMesh({0,-4,4},{.2,.2,.2}));collision.Obstacles={obstacle};
    const auto certify=[&](const G::RapidPathGeometryResult& geometry){auto q=collision;std::vector<G::RigidMotionPose> poses;M::Vector previous{0,0,1};M::Quaternion rotation{0,0,0,1};for(const auto& pose:geometry.Poses){rotation=M::Multiply(M::Between(previous,pose.Direction),rotation);previous=pose.Direction;poses.push_back({pose.Point,rotation});}for(size_t i=1;i<poses.size();++i)q.Segments.push_back({poses[i-1],poses[i],.01,true});return scene.Services().Resolve<G::IRigidMotionCollisionService>()->Check(q,{});};
    const auto blocked=certify(original);EXPECT_TRUE(std::any_of(blocked.Segments.begin(),blocked.Segments.end(),[](const auto& segment){return segment.Status=="Collision";}));const auto clear=certify(turned);ASSERT_EQ(4u,clear.Segments.size());for(const auto& segment:clear.Segments)EXPECT_EQ("Clear",segment.Status)<<segment.Reason;
    query.Manual=true;query.ConstructedManual=true;const auto manual=service->Generate({query},{}).front();ASSERT_TRUE(manual.Generated);ASSERT_EQ(turned.Poses.size(),manual.Poses.size());for(size_t i=0;i<manual.Poses.size();++i){EXPECT_EQ(turned.Poses[i].Point,manual.Poses[i].Point);EXPECT_EQ(turned.Poses[i].Direction,manual.Poses[i].Direction);}
    query.LateralOffset={0,1,0};EXPECT_THROW(service->Generate({query},{}),std::invalid_argument);
    query.LateralOffset={};query.Kind=RapidPathKind::Arch;EXPECT_THROW(service->Generate({query},{}),std::invalid_argument);
    query=Flat();query.End.Direction={0,0,-1};const auto opposed=service->Generate({query},{}).front();ASSERT_TRUE(opposed.Generated)<<opposed.Reason;ASSERT_EQ(4u,opposed.Poses.size());
}
TEST(FiveAxisRapidConstruction, CollisionDetectionReportsTheExistingRouteAndDoesNotRaiseOrReplaceIt)
{
    using namespace rapid_construction_tests;
    Scene scene;auto q=Flat();q.LiftHeight=3;auto service=Service(scene);
    const auto route=service->Generate({q},{}).front();ASSERT_TRUE(route.Generated);ASSERT_EQ(4u,route.Poses.size());
    CollisionQuery check;check.Solids={{0,1,.1,.1}};
    check.Obstacles.push_back({"blocking-clamp",TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(-.1,-1,3.5),.2,2,2).Shape())});
    for(std::size_t i=1;i<route.Poses.size();++i)check.Segments.push_back({{route.Poses[i-1].Point},{route.Poses[i].Point},.01});
    const auto before=check.Segments;const auto report=CollisionService(scene)->Check(check,{});ASSERT_EQ(3u,report.Segments.size());
    EXPECT_EQ("Collision",report.Segments[1].Status);EXPECT_EQ("blocking-clamp",report.Segments[1].Obstacle);
    for(std::size_t i=0;i<before.size();++i){EXPECT_EQ(before[i].Start.Position,check.Segments[i].Start.Position);EXPECT_EQ(before[i].End.Position,check.Segments[i].End.Position);}
    const auto repeated=service->Generate({q},{}).front();ASSERT_EQ(route.Poses.size(),repeated.Poses.size());
    for(std::size_t i=0;i<route.Poses.size();++i)EXPECT_EQ(route.Poses[i].Point,repeated.Poses[i].Point);
}
TEST(FiveAxisRapidConstruction, Supports479ConnectionsWithoutPreparingAnyBRep)
{
    using namespace rapid_construction_tests;
    Scene scene;std::vector<RapidPathGeometryQuery> queries(479,Flat());
    for(std::size_t i=0;i<queries.size();++i)queries[i].Start.Point[1]=queries[i].End.Point[1]=static_cast<double>(i);
    const auto results=Service(scene)->Generate(queries,{});ASSERT_EQ(479u,results.size());
    for(std::size_t i=0;i<results.size();++i){ASSERT_TRUE(results[i].Generated)<<i;ASSERT_EQ(4u,results[i].Poses.size());EXPECT_EQ(static_cast<double>(i),results[i].Poses.front().Point[1]);}
    EXPECT_THROW(Service(scene)->Generate(queries,[]{return true;}),std::exception);
    queries.back().LiftHeight=-1;EXPECT_THROW(Service(scene)->Generate(queries,{}),std::exception);
    auto follow=Flat();follow.Kind=RapidPathKind::Follow;const auto failed=Service(scene)->Generate({follow},{}).front();EXPECT_FALSE(failed.Generated);EXPECT_TRUE(failed.Poses.empty());
}
TEST(FiveAxisRapidConstruction, SceneGenerationLeavesCollisionUncheckedAndExplicitDetectionDoesNotChangePaths)
{
    Scene scene;PrepareOrderPart(scene);RapidConfig(scene);
    const auto generated=Wait(scene,Invoke(&scene,"CamRapid","Generate",RapidRequest(scene,false)));ASSERT_EQ("Completed",generated.at("status").To<Text>());
    const auto before=Generated(scene);EXPECT_EQ("0",before.at("rapidClearCount").To<Text>());
    EXPECT_EQ(std::to_string(before.at("transitions").To<Array>().size()),before.at("rapidGeneratedCount").To<Text>());
    for(const auto& v:before.at("transitions").To<Array>()){const auto t=v.To<Map>();EXPECT_EQ("Generated",t.at("geometryStatus").To<Text>());EXPECT_EQ("Unchecked",t.at("validation").To<Text>());EXPECT_EQ("RuleBasedGeometry",t.at("scope").To<Text>());}
    const auto history=scene.Database().GetUndoArray().size();RunCollision(scene);
    EXPECT_EQ(before,Generated(scene));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    bool rapid=false;for(const auto& f:SimulationReport(scene).at("frames").To<Array>())rapid|=f.To<Map>().at("role").To<Text>()=="Rapid";EXPECT_TRUE(rapid);
}
TEST(FiveAxisRapidConstruction, DISABLED_RealSavedModel)
{
    using namespace ompl_evaluation;
    const auto path=Env(L"ICAX_RAPID_GEOMETRY_PROJECT"),reportName=Env(L"ICAX_RAPID_GEOMETRY_REPORT");ASSERT_FALSE(path.empty());ASSERT_FALSE(reportName.empty());
    const auto started=Clock::now();iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    const auto prepared=file.PrepareOpen(path);const auto info=prepared.Result().Info;Scene scene(info.ProjectID,info.MainSceneID,false);file.Restore(prepared,scene.Database(),scene.Resources());InitializeScene(scene);
    const auto loadMs=Ms(started);const auto readStart=Clock::now();const auto before=Generated(scene);const auto readMs=Ms(readStart);const auto operations=before.at("operations");const auto history=scene.Database().GetUndoArray().size();
    const auto request=RapidRequest(scene,false);const auto generateStart=Clock::now();const auto task=Wait(scene,Invoke(&scene,"CamRapid","Generate",request));const auto generationMs=Ms(generateStart);
    ASSERT_EQ("Completed",task.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);const auto result=task.at("result").To<Map>();
    EXPECT_EQ(result.at("generatedCount"),result.at("clearCount"));
    for(const auto& [_,v]:result.at("connections").To<Map>()){const auto r=v.To<Map>().at("result").To<Map>();
        if(r.at("geometryStatus").To<Text>()=="Generated"){EXPECT_EQ("Clear",r.at("validation").To<Text>());EXPECT_GE(r.at("points").To<Array>().size(),2u);}
        else{EXPECT_FALSE(r.at("reason").To<Text>().empty());EXPECT_NE("Clear",r.at("validation").To<Text>());if(!r.at("points").To<Array>().empty())EXPECT_TRUE(r.at("editingOnly").To<bool>());}}
    const auto after=Generated(scene);EXPECT_EQ(operations,after.at("operations"));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    Map report{{"passed",!HasFailure()},{"project",std::filesystem::path(path).string()},{"operations",std::to_string(operations.To<Array>().size())},{"connections",result.at("count")},{"generated",result.at("generatedCount")},
        {"loadMs",loadMs},{"readGeneratedMs",readMs},{"generationCallAndPublicationMs",generationMs},{"collisionChecksDuringGeneration",true},{"collisionStatus",Text("ConstrainedHeadEnvelope")},{"sourceOperationsUnchanged",operations==after.at("operations")}};
    std::ofstream output(std::filesystem::path(reportName),std::ios::binary);output<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(report);
    std::clog<<"Constrained rapid generation: "<<result.at("count").To<Text>()<<" connections, "<<generationMs<<" ms (includes scene capture/publication); read="<<readMs<<" ms"<<std::endl;
}
