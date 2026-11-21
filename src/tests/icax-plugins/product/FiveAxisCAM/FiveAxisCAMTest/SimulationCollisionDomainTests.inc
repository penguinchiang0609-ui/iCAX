namespace {
    using CollisionQuery=iCAX::GeometryServices::RigidMotionCollisionQuery;
    using CollisionSegment=iCAX::GeometryServices::RigidMotionSegment;
    auto CollisionService(Scene& scene){return scene.Services().Resolve<iCAX::GeometryServices::IRigidMotionCollisionService>();}
    CollisionQuery SmallBody(Scene& scene){CollisionQuery q;q.Body=TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(-.01,-.01,0),.02,.02,.1).Shape());return q;}
    Map SimulationReport(Scene& scene){return Read(scene).at("setups").To<Array>()[0].To<Map>().at("simulationCheck").To<Map>();}
    Map RunCollision(Scene& scene){const auto task=Wait(scene,Invoke(&scene,"CamSimulation","CheckCollision",GenerateRequest(scene)));EXPECT_EQ("Completed",task.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);return task;}
}
namespace collision_parallel_tests {
    class ControlledService final:public iCAX::GeometryServices::IRigidMotionCollisionService {
    public:
        mutable std::atomic_size_t Active=0,Peak=0;bool Fail=false,FailBeforeNext=false;
        void OnLoad()override{}void OnUnload()override{}
        std::unique_ptr<iCAX::GeometryServices::IRigidMotionCollisionSession> CreatePrimitiveMeshSession(const CollisionQuery&,const std::function<bool()>&)const override{throw std::logic_error("Controlled parallel fixture does not use primitive-mesh sessions");}
        iCAX::GeometryServices::RigidMotionCollisionResult Check(const CollisionQuery&,const std::function<bool()>&,const std::function<void(std::size_t,std::size_t)>&)const override{throw std::logic_error("Unexpected serial call");}
        iCAX::GeometryServices::RigidMotionCollisionResult CheckIncremental(const CollisionQuery& q,const std::function<std::optional<std::size_t>()>& next,
            const std::function<void(std::size_t,const iCAX::GeometryServices::RigidMotionCollisionSegment&)>& receive,const std::function<bool()>& stop)const override{
            struct Guard{std::atomic_size_t& Active;~Guard(){--Active;}} guard{Active};const auto count=++Active;auto peak=Peak.load();while(peak<count&&!Peak.compare_exchange_weak(peak,count)){}
            if(FailBeforeNext)throw std::runtime_error("Injected preparation failure");
            iCAX::GeometryServices::RigidMotionCollisionResult out;
            while(const auto index=next()){
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);while(Peak.load()<2){if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Workers did not overlap");std::this_thread::yield();}
                if(Fail&&*index==1)throw std::runtime_error("Injected parallel failure");
                for(unsigned ms=0;ms<(*index==0?30u:1u);++ms){if(stop())throw std::runtime_error("MOTION_CANCELLED");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
                iCAX::GeometryServices::RigidMotionCollisionSegment value;value.Status="Clear";value.CertifiedClearance=q.Segments.at(*index).Clearance;value.Fraction=double(*index);out.Segments.push_back(value);++out.DistanceQueries;receive(*index,value);
            }return out;
        }
    };
}
TEST(FiveAxisCollisionParallel, OverlappingWorkersRestoreOriginalOrderAndJoinBeforeTheNextPhase)
{
    collision_parallel_tests::ControlledService service;CollisionQuery q;q.Segments.resize(12);std::vector<std::size_t> finished;
    const auto first=service.CheckParallel(q,2,{}, {},[&](auto i,const auto&){finished.push_back(i);});
    EXPECT_GE(service.Peak.load(),2u);EXPECT_EQ(0u,service.Active.load());ASSERT_EQ(12u,first.Segments.size());EXPECT_EQ(12u,first.DistanceQueries);ASSERT_EQ(12u,finished.size());EXPECT_NE(0u,finished.front());
    for(std::size_t i=0;i<first.Segments.size();++i)EXPECT_EQ(double(i),first.Segments[i].Fraction);
    const auto second=service.CheckParallel(q,2,{},[&](auto){EXPECT_EQ(12u,finished.size());});EXPECT_EQ(12u,second.Segments.size());EXPECT_EQ(0u,service.Active.load());
}
TEST(FiveAxisCollisionParallel, CancellationAndWorkerFailureJoinAllWorkersWithoutReturningPartialSuccess)
{
    CollisionQuery q;q.Segments.resize(20);collision_parallel_tests::ControlledService service;std::atomic_bool cancelled=false;std::size_t completed=0;
    EXPECT_THROW(service.CheckParallel(q,2,[&]{return cancelled.load();},{},[&](auto,const auto&){++completed;cancelled.store(true);}),std::exception);
    EXPECT_EQ(0u,service.Active.load());EXPECT_EQ(1u,completed);
    collision_parallel_tests::ControlledService failing;failing.Fail=true;EXPECT_THROW(failing.CheckParallel(q,2,{}),std::exception);EXPECT_EQ(0u,failing.Active.load());
    collision_parallel_tests::ControlledService preparation;preparation.FailBeforeNext=true;EXPECT_THROW(preparation.CheckParallel(q,2,{}),std::exception);EXPECT_EQ(0u,preparation.Active.load());
    EXPECT_THROW(failing.CheckParallel(q,0,{}),std::invalid_argument);
}
TEST(FiveAxisCollisionParallel, RealMeshContinuousCollisionClearanceAndDisconnectedResultsMatchSerial)
{
    Scene scene;CollisionQuery q;q.Boxes={CollisionBox{{0,0,.05},{.01,.01,.05}}};TravelObstacle obstacle;obstacle.Id="thin-interior";obstacle.Mesh=std::make_shared<CollisionMesh>(BoxCollisionMesh({.37,0,.05},{.001,.005,.005}));q.Obstacles={obstacle};
    for(unsigned i=0;i<96;++i){const double y=i%3==1?2.:0.;q.Segments.push_back({{{-1,y,0}},{{1,y,0}},.001,i%3!=2});}
    const auto service=CollisionService(scene);const auto serial=service->Check(q,{});const auto parallel=service->CheckParallel(q,8,{});ASSERT_EQ(serial.Segments.size(),parallel.Segments.size());
    for(std::size_t i=0;i<serial.Segments.size();++i){const auto& a=serial.Segments[i];const auto& b=parallel.Segments[i];EXPECT_EQ(a.Status,b.Status);EXPECT_EQ(a.Reason,b.Reason);EXPECT_EQ(a.Obstacle,b.Obstacle);
        if(b.Status=="Clear")EXPECT_GE(b.CertifiedClearance,q.Segments[i].Clearance);if(b.Status=="Collision"){EXPECT_GT(b.Fraction,0);EXPECT_LT(b.Fraction,1);ASSERT_TRUE(b.Witness);EXPECT_LE(b.Witness->Separation,q.Segments[i].Clearance+1e-7);}}
}
TEST(FiveAxisCollisionSession, RepeatedCandidatesMatchColdChecksAndFailureDoesNotPoisonLaterClearance){
    namespace G=iCAX::GeometryServices;Scene scene;const auto service=CollisionService(scene);CollisionQuery query;query.Boxes={G::CollisionBox{{0,0,0},{.1,.1,.1}}};
    G::TravelObstacle obstacle;obstacle.Id="session-target";obstacle.Mesh=std::make_shared<G::CollisionMesh>(BoxCollisionMesh({0,0,0},{1,1,1}));query.Obstacles={obstacle};
    const CollisionSegment blocked{{{-3,0,0}},{{3,0,0}},.01,true},clear{{{-3,3,0}},{{3,3,0}},.01,true};
    auto session=service->CreatePrimitiveMeshSession(query,{});ASSERT_TRUE(session);const auto prefix=session->Check({blocked,clear},{},true);ASSERT_EQ(1u,prefix.Segments.size());EXPECT_EQ("Collision",prefix.Segments.front().Status);
    for(const auto& candidate:std::vector<std::vector<CollisionSegment>>{{clear},{blocked},{clear}}){query.Segments=candidate;const auto cold=service->Check(query,{}),warm=session->Check(candidate,{});ASSERT_EQ(cold.Segments.size(),warm.Segments.size());for(size_t i=0;i<cold.Segments.size();++i){EXPECT_EQ(cold.Segments[i].Status,warm.Segments[i].Status);EXPECT_EQ(cold.Segments[i].Obstacle,warm.Segments[i].Obstacle);if(warm.Segments[i].Status=="Clear")EXPECT_GE(warm.Segments[i].CertifiedClearance,candidate[i].Clearance);}}
    // Reusing the same pose distance must not reuse its earlier threshold.
    const CollisionSegment low{{{1.3,0,0}},{{1.3,0,0}},.05,true},high{{{1.3,0,0}},{{1.3,0,0}},.5,true};
    const auto thresholds=session->Check({low,high,low},{});ASSERT_EQ(3u,thresholds.Segments.size());EXPECT_EQ("Clear",thresholds.Segments[0].Status);EXPECT_EQ("Collision",thresholds.Segments[1].Status);EXPECT_EQ("Clear",thresholds.Segments[2].Status);
    auto moved=query;moved.Obstacles[0].Transform[3]=10;auto movedSession=service->CreatePrimitiveMeshSession(moved,{});EXPECT_EQ("Clear",movedSession->Check({blocked},{}).Segments.front().Status);EXPECT_EQ("Collision",session->Check({blocked},{}).Segments.front().Status);
    auto enlarged=query;enlarged.Boxes.front().HalfExtents={2,2,2};auto enlargedSession=service->CreatePrimitiveMeshSession(enlarged,{});EXPECT_EQ("Collision",enlargedSession->Check({low},{}).Segments.front().Status);EXPECT_EQ("Clear",session->Check({low},{}).Segments.front().Status);
    EXPECT_THROW(session->Check({clear},[]{return true;}),std::exception);
    EXPECT_THROW(service->CreatePrimitiveMeshSession(query,[]{return true;}),std::exception);
    EXPECT_EQ("Clear",session->Check({clear},{}).Segments.front().Status);
}
TEST(FiveAxisCollisionSession, ConservativeBoundsKeepRotatingThinObstaclesAndContainmentVisible){
    namespace G=iCAX::GeometryServices;Scene scene;const auto service=CollisionService(scene);CollisionQuery query;query.Boxes={G::CollisionBox{{8,0,.5},{.1,.1,.5}}};G::TravelObstacle obstacle;obstacle.Id="thin-rotation";obstacle.Mesh=std::make_shared<G::CollisionMesh>(BoxCollisionMesh({0,8,.5},{.1,.1,.2}));query.Obstacles={obstacle};
    const CollisionSegment rotation{{{0,0,0},{0,0,0,1}},{{0,0,0},{0,0,1,0}},0,true};auto session=service->CreatePrimitiveMeshSession(query,{});const auto rotated=session->Check({rotation},{});ASSERT_EQ(1u,rotated.Segments.size());EXPECT_EQ("Collision",rotated.Segments[0].Status);EXPECT_GT(rotated.Segments[0].Fraction,0);EXPECT_LT(rotated.Segments[0].Fraction,1);
    query.Boxes={G::CollisionBox{{0,0,0},{.1,.1,.1}}};query.Obstacles[0].Id="enclosing-solid";query.Obstacles[0].Mesh=std::make_shared<G::CollisionMesh>(BoxCollisionMesh({0,0,0},{1,1,1}));session=service->CreatePrimitiveMeshSession(query,{});const CollisionSegment contained{{}, {},0,true};const auto inside=session->Check({contained},{});ASSERT_EQ(1u,inside.Segments.size());EXPECT_EQ("Collision",inside.Segments[0].Status);ASSERT_TRUE(inside.Segments[0].Witness);EXPECT_TRUE(inside.Segments[0].Witness->Containment);
    query.Boxes.clear();query.Solids={{0,.2,.1,.1}};session=service->CreatePrimitiveMeshSession(query,{});EXPECT_EQ("Collision",session->Check({contained},{}).Segments.front().Status);const CollisionSegment away{{{0,3,0}},{{0,3,0}},.01,true};EXPECT_EQ("Clear",session->Check({away},{}).Segments.front().Status);
}
TEST(FiveAxisCollisionGeometry, ImportedBodyDetectsThinObstacleBetweenClearEndpoints)
{
    Scene scene;auto q=SmallBody(scene);q.Obstacles.push_back({"thin",TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(.369,-.005,.02),.002,.01,.01).Shape())});
    q.Segments.push_back({{{-1,0,0}},{{1,0,0}},.001});const auto r=CollisionService(scene)->Check(q,{});
    ASSERT_EQ(1u,r.Segments.size());EXPECT_EQ("Collision",r.Segments[0].Status);EXPECT_EQ("thin",r.Segments[0].Obstacle);EXPECT_GT(r.Segments[0].Fraction,0);EXPECT_LT(r.Segments[0].Fraction,1);
    q.Segments[0].Start.Position[1]=q.Segments[0].End.Position[1]=2;const auto clear=CollisionService(scene)->Check(q,{});EXPECT_EQ("Clear",clear.Segments[0].Status);EXPECT_GE(clear.Segments[0].CertifiedClearance,.001);
    q.Body.reset();q.Solids={{0,.1,.01,.01}};q.Segments[0].Start.Position[1]=q.Segments[0].End.Position[1]=0;
    EXPECT_EQ("Collision",CollisionService(scene)->Check(q,{}).Segments[0].Status);
}
TEST(FiveAxisCollisionGeometry,CompoundBoxesUseOnlyTargetMeshAndDetectContinuousTranslationAndRotation)
{
    Scene scene;CollisionQuery q;q.Boxes={CollisionBox{{0,0,.05},{.01,.01,.05}}};TravelObstacle obstacle;obstacle.Id="own-mesh";
    // A deliberately distant CAD model cannot replace the supplied mesh.
    obstacle.Geometry=TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(100,100,100),1,1,1).Shape());
    obstacle.Mesh=std::make_shared<CollisionMesh>(BoxCollisionMesh({.37,0,.05},{.001,.005,.005}));q.Obstacles.push_back(obstacle);
    q.Segments={{{{-1,0,0}},{{1,0,0}},.001}};const auto service=CollisionService(scene);const auto translated=service->Check(q,{});
    ASSERT_EQ(1u,translated.Segments.size());EXPECT_EQ("Collision",translated.Segments[0].Status);EXPECT_EQ("own-mesh",translated.Segments[0].Obstacle);
    EXPECT_GT(translated.Segments[0].Fraction,0);EXPECT_LT(translated.Segments[0].Fraction,1);
    q.Segments[0].Start.Position[1]=q.Segments[0].End.Position[1]=2;EXPECT_EQ("Clear",service->Check(q,{}).Segments[0].Status);
    q.Boxes={CollisionBox{{8,0,.5},{.1,.1,.5}}};q.Obstacles[0].Mesh=std::make_shared<CollisionMesh>(BoxCollisionMesh({0,8,.5},{.1,.1,.2}));
    q.Segments={{{{0,0,0},{0,0,0,1}},{{0,0,0},{0,0,1,0}},0}};const auto rotated=service->Check(q,{});
    EXPECT_EQ("Collision",rotated.Segments[0].Status);EXPECT_NEAR(.5,rotated.Segments[0].Fraction,1e-10);
    q.Segments[0].End=q.Segments[0].Start;EXPECT_EQ("Clear",service->Check(q,{}).Segments[0].Status);
    q.Body=obstacle.Geometry;EXPECT_THROW(service->Check(q,{}),std::invalid_argument);
    q.Body.reset();q.Obstacles[0].Mesh.reset();EXPECT_THROW(service->Check(q,{}),std::invalid_argument);
}
TEST(FiveAxisCollisionGeometry,CompoundBoxMeshContainmentCalibrationThresholdReuseAndIncrementalOwnership)
{
    Scene scene;CollisionQuery q;q.Boxes={CollisionBox{{0,0,0},{.1,.1,.1}}};TravelObstacle obstacle;obstacle.Id="closed-target";
    obstacle.Mesh=std::make_shared<CollisionMesh>(BoxCollisionMesh({0,0,0},{1,1,1}));obstacle.Transform[3]=10;q.Obstacles={obstacle};q.BodyCalibration.Position={10,0,0};q.Segments={{ {},{},0 }};
    const auto service=CollisionService(scene);EXPECT_EQ("Collision",service->Check(q,{}).Segments[0].Status);
    q.BodyCalibration.Position={11.3,0,0};q.Segments={{ {},{},.05 },{ {},{},.5 },{ {},{},.05 }};
    const auto checked=service->Check(q,{});ASSERT_EQ(3u,checked.Segments.size());EXPECT_EQ("Clear",checked.Segments[0].Status);EXPECT_EQ("Collision",checked.Segments[1].Status);EXPECT_EQ("Clear",checked.Segments[2].Status);
    std::vector<std::size_t> order{2,1};std::size_t at=0;std::map<std::size_t,std::string> received;
    const auto delivered=service->CheckIncremental(q,[&]()->std::optional<std::size_t>{return at<order.size()?std::optional(order[at++]):std::nullopt;},[&](auto index,const auto& value){received[index]=value.Status;},{});
    EXPECT_EQ(2u,delivered.Segments.size());EXPECT_EQ("Clear",received[2]);EXPECT_EQ("Collision",received[1]);EXPECT_FALSE(received.contains(0));
    bool cancel=false;at=0;EXPECT_THROW(service->CheckIncremental(q,[&]()->std::optional<std::size_t>{return order[at++];},[&](auto,const auto&){cancel=true;},[&]{return cancel;}),std::exception);
    q.Segments[0].Connected=false;EXPECT_EQ("Unchecked",service->Check(q,{}).Segments[0].Status);
}
TEST(FiveAxisCollisionGeometry,RapidCompoundBoxesPreserveOwnMeshOpeningsAndRejectBlockedManualRoutes)
{
    Scene scene;auto environment=std::make_shared<RapidCollisionGeometry>();environment->Boxes={CollisionBox{{0,0,0},{.1,.1,.1}}};TravelObstacle obstacle;obstacle.Id="perforated-target";
    obstacle.Mesh=std::make_shared<CollisionMesh>(CollisionMesh{{{-10,-10,0},{10,-10,0},{10,10,0},{-10,10,0},{-2,-2,0},{2,-2,0},{2,2,0},{-2,2,0}},
        {{0,1,5},{0,5,4},{1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7}},0});environment->Obstacles={obstacle};
    RapidPathPlanningQuery query;query.Collision=environment;query.Clearance=.05;query.Geometry.Manual=true;query.Geometry.Start={{0,0,-2},{0,0,1}};query.Geometry.End={{0,0,0},{0,0,1}};
    // Anchors are beam targets; the default FollowHeight=1 places actual tool
    // tip endpoints at z=-1 and z=+1, both clear of the perforated plane.
    const auto built=scene.Services().Resolve<IRapidPathGeometryService>()->Generate({query.Geometry},{}).front();ASSERT_TRUE(built.Generated);
    EXPECT_DOUBLE_EQ(-1,built.Poses.front().Point[2]);EXPECT_DOUBLE_EQ(1,built.Poses.back().Point[2]);
    const auto service=scene.Services().Resolve<IRapidPathPlanningService>();const auto clear=service->Plan({query}).front();EXPECT_TRUE(clear.Generated);EXPECT_EQ("Clear",clear.Status);EXPECT_EQ("Manual",clear.Method);
    environment->Boxes.push_back(CollisionBox{{4,0,0},{.1,.1,.1}});const auto shoulder=service->Plan({query}).front();EXPECT_NE("Clear",shoulder.Status);EXPECT_EQ("RAPID_MANUAL_PATH_BLOCKED",shoulder.Reason);
    environment->Boxes.resize(1);environment->Obstacles[0].Mesh=std::make_shared<CollisionMesh>(BoxCollisionMesh({0,0,0},{10,10,.01}));
    EXPECT_EQ("RAPID_MANUAL_PATH_BLOCKED",service->Plan({query}).front().Reason);EXPECT_THROW(service->Plan({query},[]{return true;}),std::exception);
}
TEST(FiveAxisCollisionGeometry,BoxMeshWitnessUsesWorldCalibrationActualBoxAndCurrentClearance)
{
    Scene scene;CollisionQuery q;q.Boxes={CollisionBox{{50,0,0},{.1,.1,.1}},CollisionBox{{2,0,0},{.1,.1,.1}}};const double s=std::sqrt(.5);q.BodyCalibration={{1,0,.7},{0,0,s,s}};
    TravelObstacle obstacle;obstacle.Id="calibrated-target";obstacle.Mesh=std::make_shared<CollisionMesh>(BoxCollisionMesh({0,0,0},{1,1,1}));obstacle.Transform={0,-1,0,6.7,1,0,0,21,0,0,1,30.7,0,0,0,1};q.Obstacles={obstacle};
    const iCAX::GeometryServices::RigidMotionPose pose{{10,20,30},{0,0,s,s}};q.Segments={{pose,pose,.3},{pose,pose,.5},{pose,pose,.1},{pose,pose,.3},{pose,pose,.3,false}};
    const auto result=CollisionService(scene)->Check(q,{});ASSERT_EQ(5u,result.Segments.size());
    for(const auto i:{0u,1u,3u}){const auto& segment=result.Segments[i];EXPECT_EQ("Collision",segment.Status);ASSERT_TRUE(segment.Witness);const auto& w=*segment.Witness;
        EXPECT_EQ(1u,w.BoxIndex);EXPECT_EQ("calibrated-target",segment.Obstacle);EXPECT_FALSE(w.Containment);EXPECT_NEAR(.2,w.Separation,1e-6);EXPECT_NEAR(.2,w.SurfaceDistance,1e-6);EXPECT_EQ(q.Segments[i].Clearance,w.RequiredClearance);
        EXPECT_NEAR(7.9,w.BodyPoint[0],1e-6);EXPECT_NEAR(7.7,w.ObstaclePoint[0],1e-6);double squared=0;for(int axis=0;axis<3;++axis)squared+=std::pow(w.BodyPoint[axis]-w.ObstaclePoint[axis],2);EXPECT_NEAR(std::sqrt(squared),w.SurfaceDistance,1e-8);
    }
    EXPECT_EQ("Clear",result.Segments[2].Status);EXPECT_FALSE(result.Segments[2].Witness);EXPECT_EQ("Unchecked",result.Segments[4].Status);EXPECT_FALSE(result.Segments[4].Witness);
    q.Obstacles[0].Transform[3]=8;q.Segments.resize(1);const auto contained=CollisionService(scene)->Check(q,{});ASSERT_TRUE(contained.Segments[0].Witness);EXPECT_TRUE(contained.Segments[0].Witness->Containment);EXPECT_EQ(1u,contained.Segments[0].Witness->BoxIndex);EXPECT_EQ(0,contained.Segments[0].Witness->Separation);
}
TEST(FiveAxisCollisionGeometry, IncrementalRequestsKeepTheirIndicesAndCannotReuseClearanceAcrossAnObstacle)
{
    Scene scene;auto q=SmallBody(scene);q.Obstacles.push_back({"thin",TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(.369,-.005,.02),.002,.01,.01).Shape())});
    q.Segments={{{{-1,2,0}},{{1,2,0}},.001},{{{-1,0,0}},{{1,0,0}},.001},{{{-1,3,0}},{{1,3,0}},.001}};
    std::vector<std::size_t> requested{2,1};std::size_t at=0;std::map<std::size_t,Text> delivered;
    const auto service=CollisionService(scene);const auto result=service->CheckIncremental(q,[&]()->std::optional<std::size_t>{return at<requested.size()?std::optional(requested[at++]):std::nullopt;},
        [&](auto index,const auto& r){delivered[index]=r.Status;},{});
    ASSERT_EQ(2u,result.Segments.size());EXPECT_EQ("Clear",delivered[2]);EXPECT_EQ("Collision",delivered[1]);EXPECT_FALSE(delivered.contains(0));
    bool cancelled=false;at=0;
    EXPECT_THROW(service->CheckIncremental(q,[&]()->std::optional<std::size_t>{return requested[at++];},[&](auto,const auto&){cancelled=true;},[&]{return cancelled;}),std::exception);
}
TEST(FiveAxisPlayback, IncrementalContactDoesNotRunTheWholePathOrPersistAFullReport)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);ZeroGenerationPolicies(scene,ids);
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>());
    const auto before=Read(scene);const auto undo=scene.Database().GetUndoArray().size();auto request=GenerateRequest(scene);request["firstSegment"]=Text("0");
    const auto task=Invoke(&scene,"CamSimulation","StartPlayback",request);EXPECT_EQ("Running",task.at("status").To<Text>());
    EXPECT_EQ(task.at("taskId"),Invoke(&scene,"CamSimulation","StartPlayback",request).at("taskId"));
    Map query{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")},{"throughSegment",Text("0")}};Map reply;
    for(int i=0;i<1000;++i){reply=Invoke(&scene,"CamSimulation","QueryPlayback",query);if(!reply.at("segments").To<Array>().empty())break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    EXPECT_EQ("5000",reply.at("maxSegmentTimeMs").To<Text>());
    ASSERT_EQ(1u,reply.at("segments").To<Array>().size());EXPECT_EQ("0",reply.at("segments").To<Array>()[0].To<Map>().at("index").To<Text>());
    EXPECT_EQ("Running",reply.at("status").To<Text>());std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_EQ(1u,Invoke(&scene,"CamSimulation","QueryPlayback",query).at("segments").To<Array>().size());
    query["throughSegment"]=Text("2");query["afterSegment"]=Text("0");
    for(int i=0;i<1000;++i){reply=Invoke(&scene,"CamSimulation","QueryPlayback",query);if(reply.at("segments").To<Array>().size()==2)break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    ASSERT_EQ(2u,reply.at("segments").To<Array>().size());EXPECT_EQ(undo,scene.Database().GetUndoArray().size());EXPECT_EQ(before,Read(scene));
    Invoke(&scene,"CamSimulation","CancelTask",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});
    EXPECT_EQ("Cancelled",Wait(scene,task).at("status").To<Text>());EXPECT_EQ("Missing",SimulationReport(scene).at("validity").To<Text>());
    request=GenerateRequest(scene);request["firstSegment"]=Text("1");const auto stale=Invoke(&scene,"CamSimulation","StartPlayback",request);
    Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{ids[0]},{}));
    const auto rejected=Invoke(&scene,"CamSimulation","QueryPlayback",{{"epoch",stale.at("epoch")},{"taskId",stale.at("taskId")},{"throughSegment",Text("1")}});
    EXPECT_EQ("Superseded",rejected.at("status").To<Text>());EXPECT_TRUE(rejected.at("segments").To<Array>().empty());
    EXPECT_THROW(Invoke(nullptr,"CamSimulation","StartPlayback",request),std::exception);
}
TEST(FiveAxisCollisionGeometry, AsymmetricModelRotationIsCheckedBetweenPoses)
{
    Scene scene;CollisionQuery q;q.Body=TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(7.9,-.1,0),.2,.2,1).Shape());
    q.Obstacles.push_back({"side-clamp",TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(-.1,7.9,.3),.2,.2,.4).Shape())});
    q.Segments.push_back({{{0,0,0},{0,0,0,1}},{{0,0,0},{0,0,1,0}},0});const auto r=CollisionService(scene)->Check(q,{});
    EXPECT_EQ("Collision",r.Segments[0].Status);EXPECT_NEAR(.5,r.Segments[0].Fraction,1e-10);
    q.Segments[0].End=q.Segments[0].Start;EXPECT_EQ("Clear",CollisionService(scene)->Check(q,{}).Segments[0].Status);
}
TEST(FiveAxisCollisionGeometry, FullSolidContainmentInBothDirectionsAndCavity)
{
    Scene scene;auto q=SmallBody(scene);q.Obstacles.push_back({"enclosure",TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(-1,-1,-1),2,2,2).Shape())});q.Segments.push_back({{}, {},0});
    // This verifies containment topology with the product's interval budget;
    // scheduling under parallel test load must not become a 500 ms timeout test.
    q.MaxSegmentTimeMs=5000;
    EXPECT_EQ("Collision",CollisionService(scene)->Check(q,{}).Segments[0].Status);
    std::swap(q.Body,q.Obstacles[0].Geometry);EXPECT_EQ("Collision",CollisionService(scene)->Check(q,{}).Segments[0].Status);
    q.Body=TravelModel(scene,BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(gp_Pnt(-2,-2,-2),4,4,4).Shape(),BRepPrimAPI_MakeBox(gp_Pnt(-1,-1,-1),2,2,2).Shape()).Shape());
    EXPECT_EQ("Clear",CollisionService(scene)->Check(q,{}).Segments[0].Status);
}
TEST(FiveAxisCollisionGeometry, CalibrationObstacleTransformThresholdAndUnconnectedMotion)
{
    Scene scene;auto q=SmallBody(scene);q.BodyCalibration.Position={10,0,0};auto obstacle=TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(-.1,-.1,0),.2,.2,1).Shape());
    q.Obstacles.push_back({"fixture",obstacle});q.Obstacles[0].Transform[3]=10;q.Segments.push_back({{}, {},.1});
    EXPECT_EQ("Collision",CollisionService(scene)->Check(q,{}).Segments[0].Status);
    q.BodyCalibration.Position={10.2,0,0};EXPECT_EQ("Collision",CollisionService(scene)->Check(q,{}).Segments[0].Status);
    q.Segments[0].Clearance=.05;EXPECT_EQ("Clear",CollisionService(scene)->Check(q,{}).Segments[0].Status);
    q.Segments[0].Connected=false;EXPECT_EQ("Unchecked",CollisionService(scene)->Check(q,{}).Segments[0].Status);
    EXPECT_THROW(CollisionService(scene)->Check(q,[]{return true;}),std::exception);
    q.Segments[0].End.Rotation={0,0,0,0};EXPECT_THROW(CollisionService(scene)->Check(q,{}),std::exception);
}
TEST(FiveAxisCollisionGeometry, RationalSplineBoundsNeverCertifyAnIntersectingHead)
{
    Scene scene;NCollection_Array2<gp_Pnt> poles(1,3,1,3);NCollection_Array2<double> weights(1,3,1,3);
    for(int u=1;u<=3;++u)for(int v=1;v<=3;++v){poles.SetValue(u,v,gp_Pnt((u-2)*10.,(v-2)*10.,u==2&&v==2?4.:0.));weights.SetValue(u,v,u==2&&v==2?2.:1.);}
    NCollection_Array1<double> knots(1,2);knots.SetValue(1,0);knots.SetValue(2,1);NCollection_Array1<int> multiplicities(1,2);multiplicities.Init(3);
    occ::handle<Geom_BSplineSurface> surface=new Geom_BSplineSurface(poles,weights,knots,knots,multiplicities,multiplicities,2,2);
    auto q=SmallBody(scene);q.Obstacles.push_back({"rational-sheet",TravelModel(scene,BRepBuilderAPI_MakeFace(surface,1e-7).Face())});
    q.Obstacles[0].Transform[3]=7;q.Obstacles[0].Transform[7]=-4;q.Obstacles[0].Transform[11]=12;
    const auto point=surface->Value(.37,.53);iCAX::GeometryServices::RigidMotionPose pose{{point.X()+7,point.Y()-4,point.Z()+12-.05}};
    q.Segments.push_back({pose,pose,.001});const auto collision=CollisionService(scene)->Check(q,{});EXPECT_EQ("Collision",collision.Segments[0].Status);EXPECT_EQ("rational-sheet",collision.Segments[0].Obstacle);
    q.Body.reset();q.Solids={{0,.1,.01,.01}};EXPECT_EQ("Collision",CollisionService(scene)->Check(q,{}).Segments[0].Status);
    q.Segments[0].Start.Position[2]+=20;q.Segments[0].End=q.Segments[0].Start;
    EXPECT_EQ("Clear",CollisionService(scene)->Check(q,{}).Segments[0].Status);
}
TEST(FiveAxisCollisionGeometry, TrimmedHoleDoesNotBecomeUntrimmedPlaneCollision)
{
    Scene scene;auto q=SmallBody(scene);
    const auto plane=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,.05),gp_Dir(0,0,1)),-3.,3.,-3.,3.).Face();
    const auto hole=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,0,-1),gp_Dir(0,0,1)),1.,2.).Shape();
    q.Obstacles.push_back({"trimmed-sheet",TravelModel(scene,BRepAlgoAPI_Cut(plane,hole).Shape())});q.Segments.push_back({{}, {},.1});
    EXPECT_EQ("Clear",CollisionService(scene)->Check(q,{}).Segments[0].Status);
    q.Segments[0].Start.Position[0]=q.Segments[0].End.Position[0]=2;
    EXPECT_EQ("Collision",CollisionService(scene)->Check(q,{}).Segments[0].Status);
}
TEST(FiveAxisCollisionGeometry, DifferentClearancesAndMidCheckCancellation)
{
    Scene scene;auto q=SmallBody(scene);q.Obstacles.push_back({"offset",TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(.2,-1,-1),1,2,2).Shape())});
    q.Segments={{ {},{},.05 },{ {},{},.5 },{ {},{},.05 }};
    const auto result=CollisionService(scene)->Check(q,{});
    ASSERT_EQ(3u,result.Segments.size());EXPECT_EQ("Clear",result.Segments[0].Status);EXPECT_EQ("Collision",result.Segments[1].Status);EXPECT_EQ("Clear",result.Segments[2].Status);
    bool cancel=false;std::size_t completed=0;
    EXPECT_THROW(CollisionService(scene)->Check(q,[&]{return cancel;},[&](auto done,auto){completed=done;cancel=true;}),std::exception);
    EXPECT_EQ(1u,completed);
}
TEST(FiveAxisCollision, CoversCutAndRapidAndPersistsWithoutAddingUndoEntries)
{
    Scene scene;PrepareOrderPart(scene);RapidConfig(scene);ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamRapid","Generate",RapidRequest(scene,false))).at("status").To<Text>());
    const auto history=scene.Database().GetUndoArray().size();const auto paths=Generated(scene),request=GenerateRequest(scene);
    const auto task=Wait(scene,Invoke(&scene,"CamSimulation","CheckCollision",request));ASSERT_EQ("Completed",task.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);
    EXPECT_EQ(task,Invoke(&scene,"CamSimulation","CheckCollision",request));const auto report=SimulationReport(scene);ASSERT_EQ("Current",report.at("validity").To<Text>());
    EXPECT_EQ("5000",report.at("maxSegmentTimeMs").To<Text>());
    EXPECT_EQ("LimitsThenCollision",report.at("checkOrder").To<Text>());EXPECT_EQ("KeypointsThenIntervals",report.at("collisionOrder").To<Text>());EXPECT_GT(std::stoull(report.at("keypointCheckCount").To<Text>()),0u);
    EXPECT_TRUE(report.at("keypointResultsCurrent").To<bool>());EXPECT_GE(std::stoul(report.at("parallelWorkers").To<Text>()),1u);
    const auto keypoints=report.at("keypoints").To<Array>();ASSERT_EQ(std::stoull(report.at("keypointCheckCount").To<Text>()),keypoints.size());
    std::size_t staticCollisions=0,staticUnchecked=0;
    for(std::size_t i=0;i<keypoints.size();++i){const auto point=keypoints[i].To<Map>();EXPECT_EQ(std::to_string(i),point.at("index").To<Text>());EXPECT_LT(std::stoull(point.at("frameIndex").To<Text>()),report.at("frames").To<Array>().size());staticCollisions+=point.at("status").To<Text>()=="Collision";staticUnchecked+=point.at("status").To<Text>()=="Unchecked";}
    EXPECT_EQ(std::to_string(staticCollisions),report.at("keypointCollisionCount").To<Text>());EXPECT_EQ(std::to_string(staticUnchecked),report.at("keypointUncheckedCount").To<Text>());
    bool cut=false,rapid=false;for(const auto& f:report.at("frames").To<Array>()){cut|=f.To<Map>().at("role").To<Text>()=="Cut";rapid|=f.To<Map>().at("role").To<Text>()=="Rapid";}EXPECT_TRUE(cut);EXPECT_TRUE(rapid);
    EXPECT_EQ(report.at("frames").To<Array>().size()-1,report.at("segments").To<Array>().size());EXPECT_EQ(history,scene.Database().GetUndoArray().size());EXPECT_EQ(paths,Generated(scene));
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Collision";
    const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(report,SimulationReport(reopened));std::filesystem::remove(path);
    RapidConfig(scene,2,10);EXPECT_EQ("Stale",SimulationReport(scene).at("validity").To<Text>());ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(report,SimulationReport(scene));
    auto ids=OrderedIDs(paths);std::reverse(ids.begin(),ids.end());Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Manual",ids));EXPECT_EQ("Stale",SimulationReport(scene).at("validity").To<Text>());
}
TEST(FiveAxisCollision, ImportedDisplayCalibrationRemainsReadableButRequiresDeclaredBoxesForCollision)
{
    Scene scene;PrepareOrderPart(scene);File source(BRepPrimAPI_MakeBox(gp_Pnt(-1,-1,0),2,2,20).Shape());
    const auto originalHead=Head(scene);RunCollision(scene);const auto originalReport=SimulationReport(scene);
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamHead","Import",ImportTestHead(scene,source))).at("status").To<Text>());
    EXPECT_EQ("Stale",SimulationReport(scene).at("validity").To<Text>());EXPECT_FALSE(Head(scene).at("calibrated").To<bool>());
    EXPECT_THROW(Invoke(&scene,"CamSimulation","CheckCollision",GenerateRequest(scene)),std::exception);
    auto calibration=CalibrateTestHead(scene);calibration["tcpLocalPosition"]=Array{0.,0.,0.};Invoke(&scene,"CamHead","Configure",calibration);
    const auto calibrated=Head(scene);EXPECT_TRUE(calibrated.at("calibrated").To<bool>());EXPECT_TRUE(calibrated.contains("previewRef"));EXPECT_EQ("CollisionBoxesRequired",calibrated.at("collisionModel").To<Map>().at("status").To<Text>());EXPECT_EQ("0",calibrated.at("collisionModel").To<Map>().at("boxCount").To<Text>());
    Text failure;try{Invoke(&scene,"CamSimulation","CheckCollision",GenerateRequest(scene));ADD_FAILURE()<<"A calibrated display mesh is not a declared collision model";}catch(const std::exception& error){failure=error.what();}EXPECT_NE(Text::npos,failure.find("CAM_HEAD_COLLISION_BOXES_REQUIRED"));
    calibration=CalibrateTestHead(scene);calibration["tcpLocalPosition"]=Array{0.,0.,20.};Invoke(&scene,"CamHead","Configure",calibration);
    EXPECT_EQ("Stale",SimulationReport(scene).at("validity").To<Text>());ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(calibrated,Head(scene));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_FALSE(Head(scene).at("calibrated").To<bool>());ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(originalHead,Head(scene));EXPECT_EQ(originalReport,SimulationReport(scene));
    // Cancellation and revision fencing still apply to an actual collision
    // model; importing a visual mesh may not substitute for that model.
    RunCollision(scene);const auto beforeCancel=SimulationReport(scene);auto cancel=Invoke(&scene,"CamSimulation","CheckCollision",GenerateRequest(scene));Invoke(&scene,"CamSimulation","CancelTask",{{"epoch",cancel.at("epoch")},{"taskId",cancel.at("taskId")}});
    EXPECT_EQ("Cancelled",Wait(scene,cancel).at("status").To<Text>());EXPECT_EQ(beforeCancel,SimulationReport(scene));
    const auto beforeChange=Read(scene);const auto setupID=*ID::from_string(beforeChange.at("activeSetupId").To<Text>());auto settings=beforeChange.at("setups").To<Array>()[0].To<Map>().at("rapidSettings").To<Map>();settings["collisionThresholdMm"]=settings.at("collisionThresholdMm").To<double>()+.123;
    const auto stale=Invoke(&scene,"CamSimulation","CheckCollision",GenerateRequest(scene));ASSERT_EQ("Running",stale.at("status").To<Text>());
    const auto undo=scene.Database().BeginUndoCommand("Change collision threshold during check");auto& tx=scene.Database().BeginTransaction("Change collision threshold during check");tx.ModifyComponent(setupID,CFiveAxisSetupComponent::S_ClassName,{{"RapidSettings",settings}});ASSERT_TRUE(scene.Database().CommitTransaction(tx));undo->End();
    EXPECT_EQ("Superseded",Wait(scene,stale).at("status").To<Text>());
}
TEST(FiveAxisCollision, LeadsAndFixtureParticipationUseSceneGeometryAndInvalidation)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);ZeroGenerationPolicies(scene,ids);
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,{ids[0]},{{"patch",Map{{"leadIn",Lead("Line",.2)},{"leadOut",Lead("Arc",.2)}}}}));
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>());
    File fixture(BRepPrimAPI_MakeBox(gp_Pnt(-100,-100,11),200,200,100).Shape());auto import=GenerateRequest(scene);import["sourcePath"]=fixture.Utf8();
    const auto imported=Wait(scene,Invoke(&scene,"CamFixture","Import",import));ASSERT_EQ("Completed",imported.at("status").To<Text>());
    // Fixtures invalidate generation input and must be included after regeneration.
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>());
    RunCollision(scene);const auto report=SimulationReport(scene);const auto fixtureID=imported.at("result").To<Map>().at("fixtureId");
    bool leadIn=false,leadOut=false,hitFixture=false;
    for(const auto& f:report.at("frames").To<Array>()){leadIn|=f.To<Map>().at("role").To<Text>()=="LeadIn";leadOut|=f.To<Map>().at("role").To<Text>()=="LeadOut";}
    for(const auto& r:report.at("segments").To<Array>())hitFixture|=r.To<Map>().at("obstacleId")==fixtureID;
    EXPECT_TRUE(leadIn);EXPECT_TRUE(leadOut);EXPECT_TRUE(hitFixture);
    auto update=GenerateRequest(scene);update["fixtureId"]=fixtureID;update["visible"]=false;Invoke(&scene,"CamFixture","Update",update);EXPECT_EQ(report,SimulationReport(scene));
    update=GenerateRequest(scene);update["fixtureId"]=fixtureID;update["collisionEnabled"]=false;Invoke(&scene,"CamFixture","Update",update);EXPECT_EQ("Stale",SimulationReport(scene).at("validity").To<Text>());
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>());RunCollision(scene);
    for(const auto& r:SimulationReport(scene).at("segments").To<Array>())EXPECT_NE(fixtureID,r.To<Map>().at("obstacleId"));
    Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{ids[0]},{}));EXPECT_EQ("Stale",SimulationReport(scene).at("validity").To<Text>());
}
