namespace
{
    using TravelQuery=iCAX::GeometryServices::SurfaceTravelQuery;
    using TravelKind=iCAX::GeometryServices::SurfaceTravelKind;
    std::shared_ptr<const iCAX::GeometryData::BRepModel> TravelModel(Scene& scene,const TopoDS_Shape& shape){
        File file(shape);iCAX::CAD::SCadBRepImportRequest r;r.SourcePath=file.Path;
        return std::make_shared<iCAX::GeometryData::BRepModel>(scene.Services().Resolve<iCAX::CAD::ICadBRepImportService>()->Load(r).Geometry);
    }
    TravelQuery FlatTravel(Scene& scene){
        TravelQuery q;q.Obstacles.push_back({"sheet",TravelModel(scene,BRepBuilderAPI_MakeFace(gp_Pln(gp::XOY()),-50,50,-50,50).Face())});
        q.Start={{-20,0,0},{0,0,1}};q.End={{20,0,0},{0,0,1}};q.EnvelopeRadius=1;q.EnvelopeLength=10;return q;
    }
    auto TravelService(Scene& scene){return scene.Services().Resolve<iCAX::GeometryServices::ISurfaceTravelService>();}
    Map RapidConfig(Scene& scene,double radius=1,double length=10){
        auto s=Read(scene).at("setups").To<Array>()[0].To<Map>().at("rapidSettings").To<Map>();s["envelopeRadiusMm"]=radius;s["envelopeLengthMm"]=length;
        auto request=GenerateRequest(scene);request["settings"]=s;return Invoke(&scene,"CamRapid","Configure",request);
    }
    Map RapidRequest(Scene& scene,bool preview=true,const Text& strategy="Quick"){
        auto request=GenerateRequest(scene);const auto generated=Generated(scene);request["geometryRevision"]=generated.at("revision");
        if(preview){const auto t=generated.at("transitions").To<Array>()[0].To<Map>();request["from"]=t.at("from");request["to"]=t.at("to");request["policy"]=Map{{"strategy",strategy},{"marginMm",.2},{"liftHeightMm",10.}};}return request;
    }
    Map SafeRapidRequest(Scene& scene){auto request=RapidRequest(scene,false);request["safe"]=true;return request;}
    Map PreviewTravel(Scene& scene,const Text& strategy="Quick"){
        const auto task=Wait(scene,Invoke(&scene,"CamRapid","Preview",RapidRequest(scene,true,strategy)));
        EXPECT_EQ("Completed",task.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);return task;
    }
}
TEST(FiveAxisRapidGeometry, FlyFollowsCurvedBRepInsteadOfConnectingEndpoints)
{
    Scene scene;auto q=FlatTravel(scene);q.Obstacles[0].Geometry=TravelModel(scene,BRepPrimAPI_MakeSphere(30).Shape());
    const double z=std::sqrt(500.);q.Start={{-20,0,z},{-20/30.,0,z/30.}};q.End={{20,0,z},{20/30.,0,z/30.}};
    q.Kind=TravelKind::Follow;q.EnvelopeRadius=.2;q.EnvelopeLength=5;
    const auto r=TravelService(scene)->Plan({q},{}).front();ASSERT_EQ("Clear",r.Status)<<r.Reason;ASSERT_GT(r.Poses.size(),5u);
    double maxZ=0;for(std::size_t i=0;i<r.Poses.size();++i){const auto p=r.Poses[i].Point;EXPECT_NEAR(31,std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]),1e-6);EXPECT_NEAR(1,r.Heights[i],1e-10);maxZ=std::max(maxZ,p[2]);}
    EXPECT_GT(maxZ,30.9);EXPECT_GE(r.CertifiedClearance,q.Clearance);EXPECT_GT(r.DistanceQueries,r.Poses.size());
}
TEST(FiveAxisRapidGeometry, FrogAddsSmoothSurfaceHeightProfileAndQuickRetractsTraversesApproaches)
{
    Scene scene;auto q=FlatTravel(scene);q.Kind=TravelKind::Arch;const auto frog=TravelService(scene)->Plan({q},{}).front();
    ASSERT_EQ("Clear",frog.Status)<<frog.Reason;EXPECT_NEAR(1,frog.Heights.front(),1e-9);EXPECT_NEAR(1,frog.Heights.back(),1e-9);EXPECT_NEAR(11,*std::max_element(frog.Heights.begin(),frog.Heights.end()),1e-9);
    for(std::size_t i=0;i<frog.Poses.size();++i){EXPECT_NEAR(0,frog.SurfacePoints[i][2],1e-9);EXPECT_NEAR(frog.Heights[i],frog.Poses[i].Point[2],1e-9);}
    q.Kind=TravelKind::Retract;const auto quick=TravelService(scene)->Plan({q},{}).front();ASSERT_EQ("Clear",quick.Status)<<quick.Reason;ASSERT_EQ(4u,quick.Poses.size());
    EXPECT_NEAR(-20,quick.Poses[1].Point[0],1e-9);EXPECT_GT(quick.Poses[1].Point[2],quick.Poses.front().Point[2]);EXPECT_NEAR(20,quick.Poses[2].Point[0],1e-9);EXPECT_EQ(quick.Poses.front().Point[2],quick.Poses.back().Point[2]);
}
TEST(FiveAxisRapidGeometry, FullEnvelopeCatchesThinMidSegmentObstacleAndQuickClearsIt)
{
    Scene scene;auto q=FlatTravel(scene);q.Start.Point={-10,0,0};q.End.Point={10,0,0};q.Kind=TravelKind::Follow;
    q.Obstacles.push_back({"clamp",TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(-.07,-.1,5),.14,.2,1).Shape())});
    const auto collision=TravelService(scene)->Plan({q},{}).front();EXPECT_EQ("Collision",collision.Status);EXPECT_EQ("clamp",collision.Obstacle);
    // The TCP at z=1 misses the clamp at z=5; the 10 mm head must catch it.
    q.Kind=TravelKind::Retract;const auto safe=TravelService(scene)->Plan({q},{}).front();EXPECT_EQ("Clear",safe.Status)<<safe.Reason;EXPECT_GE(safe.CertifiedClearance,q.Clearance);
    q.Kind=TravelKind::Arch;q.LiftHeight=15;EXPECT_EQ("Clear",TravelService(scene)->Plan({q},{}).front().Status);
}
TEST(FiveAxisRapidGeometry, FrogBridgesHolesWithoutSurfaceProjectionAndStillChecksItsWholeEnvelope)
{
    Scene scene;auto q=FlatTravel(scene);
    const auto sheet=BRepBuilderAPI_MakeFace(gp_Pln(gp::XOY()),-50,50,-50,50).Face();
    q.Obstacles[0].Geometry=TravelModel(scene,BRepAlgoAPI_Cut(sheet,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,0,-1),gp::DZ()),6,2).Shape()).Shape());
    q.Kind=TravelKind::Arch;
    const auto hop=TravelService(scene)->Plan({q},{}).front();ASSERT_EQ("Clear",hop.Status)<<hop.Reason;
    ASSERT_GT(hop.Poses.size(),5u);EXPECT_NEAR(11,*std::max_element(hop.Heights.begin(),hop.Heights.end()),1e-8);
    for(std::size_t i=0;i<hop.Poses.size();++i){EXPECT_NEAR(0,hop.SurfacePoints[i][1],1e-8);EXPECT_NEAR(0,hop.SurfacePoints[i][2],1e-8);}
    q.Obstacles.push_back({"overhead-clamp",TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(-.1,-.1,14),.2,.2,2).Shape())});
    const auto raised=TravelService(scene)->Plan({q},{}).front();ASSERT_EQ("Clear",raised.Status)<<raised.Reason;
    EXPECT_GT(*std::max_element(raised.Heights.begin(),raised.Heights.end()),11.);
    q.Obstacles.push_back({"blocked-departure",TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(-20.1,-.1,1),.2,.2,2).Shape())});
    const auto blocked=TravelService(scene)->Plan({q},{}).front();EXPECT_EQ("Collision",blocked.Status);EXPECT_EQ("blocked-departure",blocked.Obstacle);
}
TEST(FiveAxisRapidGeometry, MissingEnvelopeCancellationAndInvalidInputsNeverPass)
{
    Scene scene;auto q=FlatTravel(scene);q.EnvelopeRadius=q.EnvelopeLength=0;auto service=TravelService(scene);
    const auto missing=service->Plan({q},{}).front();EXPECT_EQ("Unchecked",missing.Status);EXPECT_EQ("TRAVEL_ENVELOPE_REQUIRED",missing.Reason);
    EXPECT_THROW(service->Plan({q},[]{return true;}),std::exception);
    q.Clearance=-1;EXPECT_THROW(service->Plan({q},{}),std::exception);q.Clearance=.3;q.Obstacles[0].Transform[0]=2;
    EXPECT_THROW(service->Plan({q},{}),std::exception);
}
TEST(FiveAxisRapidGeometry, EveryBezierPatchParticipatesInClearanceBounds)
{
    Scene scene;NCollection_Array2<gp_Pnt> poles(1,3,1,2);
    for(int u=1;u<=3;++u)for(int v=1;v<=2;++v)poles.SetValue(u,v,gp_Pnt((u-1)*10.,v==1?-10.:10.,u==3?10.:0.));
    NCollection_Array1<double> uk(1,3),vk(1,2);uk.SetValue(1,0);uk.SetValue(2,1);uk.SetValue(3,2);vk.SetValue(1,0);vk.SetValue(2,1);
    NCollection_Array1<int> um(1,3),vm(1,2);um.SetValue(1,2);um.SetValue(2,1);um.SetValue(3,2);vm.Init(2);
    occ::handle<Geom_BSplineSurface> surface=new Geom_BSplineSurface(poles,uk,vk,um,vm,1,1);
    TravelQuery q;q.Obstacles.push_back({"creased-sheet",TravelModel(scene,BRepBuilderAPI_MakeFace(surface,1e-7).Face())});
    q.Start=q.End={{15,0,5},{0,0,1}};q.FollowHeight=.01;q.EnvelopeRadius=.5;q.EnvelopeLength=2;
    auto service=TravelService(scene);const auto blocked=service->Plan({q},{}).front();
    EXPECT_EQ("Collision",blocked.Status)<<blocked.Reason;EXPECT_EQ("creased-sheet",blocked.Obstacle);
    q.Start.Point[2]=q.End.Point[2]=15;
    EXPECT_EQ("Clear",service->Plan({q},{}).front().Status);
}
TEST(FiveAxisRapidGeometry, MeshBoxBatchMatchesSingleConnectionChecksAndJoinsOnCancellation)
{
    using namespace iCAX::GeometryServices;Scene scene;
    auto mesh=std::make_shared<CollisionMesh>();mesh->Vertices={{-100,-100,0},{100,-100,0},{100,100,0},{-100,100,0}};mesh->Triangles={{0,1,2},{0,2,3}};
    auto environment=std::make_shared<RapidCollisionGeometry>();CollisionBox box;box.Center={0,0,2};box.HalfExtents={.3,.3,2};environment->Boxes.push_back(box);
    TravelObstacle obstacle;obstacle.Id="sheet";obstacle.Mesh=mesh;environment->Obstacles.push_back(obstacle);
    std::vector<RapidPathPlanningQuery> queries(24);
    for(std::size_t i=0;i<queries.size();++i){auto& q=queries[i];q.Collision=environment;q.Clearance=.1;q.Geometry.Kind=RapidPathKind::Arch;q.Geometry.LiftHeight=3;q.Geometry.Start={{double(i)-12,-10,0},{0,0,1}};q.Geometry.End={{double(i)-12,10,0},{0,0,1}};}
    queries[5].Geometry.Start.Point[2]=-1;
    const auto service=scene.Services().Resolve<IRapidPathPlanningService>();std::vector<RapidPathPlanningResult> serial;
    for(const auto& q:queries)serial.push_back(service->Plan({q}).front());
    std::mutex mutex;std::set<std::thread::id> threads;
    const auto parallel=service->Plan(queries,[&]{std::lock_guard lock(mutex);threads.insert(std::this_thread::get_id());return false;});
    ASSERT_EQ(serial.size(),parallel.size());if(std::thread::hardware_concurrency()>2)EXPECT_GT(threads.size(),1u);
    for(std::size_t i=0;i<queries.size();++i){const auto& a=serial[i];const auto& b=parallel[i];EXPECT_EQ(a.Generated,b.Generated);EXPECT_EQ(a.Status,b.Status);EXPECT_EQ(a.Reason,b.Reason);EXPECT_EQ(a.Method,b.Method);ASSERT_EQ(a.Poses.size(),b.Poses.size());
        for(std::size_t j=0;j<a.Poses.size();++j){EXPECT_EQ(a.Poses[j].Point,b.Poses[j].Point);EXPECT_EQ(a.Poses[j].Direction,b.Poses[j].Direction);}if(b.Status=="Clear")EXPECT_GE(b.CertifiedClearance,queries[i].Clearance);
    }
    EXPECT_FALSE(parallel[5].Generated);EXPECT_EQ("RAPID_ENDPOINT_BLOCKED",parallel[5].Reason);
    std::atomic_size_t checks=0;EXPECT_THROW(service->Plan(queries,[&]{return ++checks>20;}),std::exception);
    auto invalid=queries;invalid[3].Clearance=-1;EXPECT_THROW(service->Plan(invalid),std::exception);
    EXPECT_EQ(queries.size(),service->Plan(queries).size());
}
TEST(FiveAxisRapidGeometry, LargeBatchChecksEveryConnectionBeyond256)
{
    Scene scene;auto q=FlatTravel(scene);q.Kind=TravelKind::Retract;
    std::vector<TravelQuery> queries(479,q);
    for(std::size_t i=0;i<queries.size();++i){
        const auto y=-20.+40.*i/(queries.size()-1);
        queries[i].Start.Point[1]=queries[i].End.Point[1]=y;
    }
    queries[256].FollowHeight=.01;queries.back().FollowHeight=.01;
    const auto results=TravelService(scene)->Plan(queries,{});
    ASSERT_EQ(queries.size(),results.size());
    for(std::size_t i=0;i<results.size();++i){
        EXPECT_EQ(i==256||i==results.size()-1?"Collision":"Clear",results[i].Status)<<i;
        EXPECT_GT(results[i].DistanceQueries,0u)<<i;
        if(results[i].Status=="Clear"){
            EXPECT_GE(results[i].CertifiedClearance,queries[i].Clearance)<<i;
            ASSERT_FALSE(results[i].Poses.empty());EXPECT_EQ(queries[i].Start.Point[1],results[i].Poses.front().Point[1])<<i;
        }
    }
    // Cancellation during bulk work must return no partial "successful" batch.
    unsigned checks=0;EXPECT_THROW(TravelService(scene)->Plan(queries,[&]{return ++checks>1;}),std::exception);
}
TEST(FiveAxisRapidGeometry, ReusedGeometryChecksNewPosesAndInvalidatesOnPlacementAndSourceChanges)
{
    Scene scene;auto q=FlatTravel(scene);q.Kind=TravelKind::Retract;auto service=TravelService(scene);
    ASSERT_EQ("Clear",service->Plan({q},{}).front().Status);
    q.Start.Point[2]=q.End.Point[2]=-2;
    ASSERT_EQ("Collision",service->Plan({q},{}).front().Status);
    q.Obstacles[0].Transform[11]=50;
    ASSERT_EQ("Clear",service->Plan({q},{}).front().Status);
    q.Obstacles[0].Transform[11]=0;
    q.Obstacles[0].Geometry=TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(-50,-50,-3),100,100,30).Shape());
    ASSERT_EQ("Collision",service->Plan({q},{}).front().Status);
}
TEST(FiveAxisRapidGeometry, CurvedFrogBoundsRemainConservativeAfterRigidPlacement)
{
    Scene scene;auto q=FlatTravel(scene);q.Kind=TravelKind::Arch;q.LiftHeight=2;q.EnvelopeRadius=1;q.EnvelopeLength=5;
    q.Obstacles[0].Geometry=TravelModel(scene,BRepPrimAPI_MakeSphere(30).Shape());
    const double z=std::sqrt(500.);q.Start={{-20,0,z},{-20/30.,0,z/30.}};q.End={{20,0,z},{20/30.,0,z/30.}};
    // A tilted and translated curved obstacle must use bounds in setup space.
    q.Obstacles[0].Transform={1,0,0,70,0,0,-1,13,0,1,0,-9,0,0,0,1};
    for(auto* pose:{&q.Start,&q.End}){
        const auto p=pose->Point,d=pose->Direction;pose->Point={p[0]+70,13-p[2],p[1]-9};pose->Direction={d[0],-d[2],d[1]};
    }
    const auto r=TravelService(scene)->Plan({q},{}).front();ASSERT_EQ("Clear",r.Status)<<r.Reason;EXPECT_GE(r.CertifiedClearance,q.Clearance);
    using namespace iCAX::GeometryServices;RigidMotionCollisionQuery check;check.Obstacles=q.Obstacles;check.Solids={{0,q.EnvelopeLength,q.EnvelopeRadius,q.EnvelopeRadius}};
    RigidMotion::Quaternion rotation{0,0,0,1};TravelPoint direction{0,0,1};std::vector<RigidMotionPose> poses;
    for(const auto& p:r.Poses){rotation=RigidMotion::Multiply(RigidMotion::Between(direction,p.Direction),rotation);poses.push_back({p.Point,rotation});direction=p.Direction;}
    for(std::size_t i=1;i<poses.size();++i)check.Segments.push_back({poses[i-1],poses[i],q.Clearance});
    const auto verified=scene.Services().Resolve<IRigidMotionCollisionService>()->Check(check,{});
    ASSERT_EQ(check.Segments.size(),verified.Segments.size());for(const auto& segment:verified.Segments)EXPECT_EQ("Clear",segment.Status)<<segment.Reason;
    // Moving the head inside the curved solid must still fail at departure.
    q.Start.Point=RigidMotion::Add(q.Start.Point,RigidMotion::Scale(q.Start.Direction,-3));
    const auto blocked=TravelService(scene)->Plan({q},{}).front();EXPECT_EQ("Collision",blocked.Status);EXPECT_EQ("TRAVEL_ENDPOINT_CLEARANCE_VIOLATION",blocked.Reason);
}
TEST(FiveAxisRapidGeometry, OppositeSidesRouteOutsideSolidAndEveryMotionPassesIndependentCheck)
{
    Scene scene;auto q=FlatTravel(scene);q.Kind=TravelKind::Retract;
    q.Obstacles[0].Geometry=TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(-20,-20,-2),40,40,2).Shape());
    q.Start={{0,0,0},{0,0,1}};q.End={{0,0,-2},{0,0,-1}};
    const auto r=TravelService(scene)->Plan({q},{}).front();ASSERT_EQ("Clear",r.Status)<<r.Reason;ASSERT_GT(r.Poses.size(),4u);
    bool outside=false;for(const auto& p:r.Poses)outside|=std::abs(p.Point[0])>20||std::abs(p.Point[1])>20;EXPECT_TRUE(outside);
    using namespace iCAX::GeometryServices;RigidMotionCollisionQuery check;check.Obstacles=q.Obstacles;check.Solids={{0,q.EnvelopeLength,q.EnvelopeRadius,q.EnvelopeRadius}};
    RigidMotion::Quaternion rotation{0,0,0,1};TravelPoint direction{0,0,1};std::vector<RigidMotionPose> poses;
    for(const auto& p:r.Poses){rotation=RigidMotion::Multiply(RigidMotion::Between(direction,p.Direction),rotation);poses.push_back({p.Point,rotation});direction=p.Direction;}
    for(std::size_t i=1;i<poses.size();++i)check.Segments.push_back({poses[i-1],poses[i],q.Clearance});
    const auto verified=scene.Services().Resolve<IRigidMotionCollisionService>()->Check(check,{});
    for(const auto& segment:verified.Segments)EXPECT_EQ("Clear",segment.Status)<<segment.Reason;
}
TEST(FiveAxisRapidGeometry, BlockedWithdrawalCannotBecomeSafeByIncreasingRetraction)
{
    Scene scene;auto q=FlatTravel(scene);q.Kind=TravelKind::Retract;
    q.Obstacles.push_back({"ceiling",TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(-50,-50,5),100,100,2).Shape())});
    const auto r=TravelService(scene)->Plan({q},{}).front();EXPECT_EQ("Collision",r.Status);EXPECT_NE("Clear",r.Status);EXPECT_EQ("ceiling",r.Obstacle);
}
TEST(FiveAxisRapidGeometry, AFreeFaceFullyInsideTheHeadIsACollision)
{
    Scene scene;auto q=FlatTravel(scene);q.Kind=TravelKind::Retract;
    q.Obstacles.push_back({"free-face",TravelModel(scene,BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(-20,0,5),gp_Dir(0,0,1)),-.1,.1,-.1,.1).Face())});
    const auto r=TravelService(scene)->Plan({q},{}).front();EXPECT_EQ("Collision",r.Status);EXPECT_EQ("free-face",r.Obstacle);
}
TEST(FiveAxisRapidGeometry, ActualAxialHeadAndCalibratedImportedEnvelopeAreCheckedWithoutManualCylinder)
{
    Scene scene;auto q=FlatTravel(scene);q.EnvelopeRadius=q.EnvelopeLength=0;
    q.EnvelopeSolids={{0,16,0,7},{16,16,7,11},{32,24,12,12}};
    EXPECT_EQ("Clear",TravelService(scene)->Plan({q},{}).front().Status);
    q.EnvelopeSolids.clear();q.EnvelopeBody=TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(-1,-1,10),2,2,20).Shape());q.EnvelopeCalibration[11]=-10;
    EXPECT_EQ("Clear",TravelService(scene)->Plan({q},{}).front().Status);
    q.EnvelopeCalibration[11]=-20;const auto blocked=TravelService(scene)->Plan({q},{}).front();EXPECT_EQ("Collision",blocked.Status);
}
TEST(FiveAxisRapidGeometry, ContinuousCheckFindsObstacleBetweenSamplesAndSolidContainment)
{
    Scene scene;auto q=FlatTravel(scene);q.Kind=TravelKind::Follow;q.EnvelopeRadius=.001;q.EnvelopeLength=5;q.Clearance=.01;
    q.Obstacles.push_back({"thin",TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(.369,-.0005,3),.002,.001,.1).Shape())});
    const auto result=TravelService(scene)->Plan({q},{}).front();ASSERT_EQ("Collision",result.Status);EXPECT_EQ("thin",result.Obstacle);
    EXPECT_EQ(0,result.CertifiedClearance);
    for(const auto& pose:result.Poses)EXPECT_GT(std::abs(pose.Point[0]-.37),.05); // no displayed sample is in collision
    EXPECT_GT(result.DistanceQueries,result.Poses.size());
    q.Obstacles[1]={"enclosure",TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(-100,-100,-10),200,200,200).Shape())};
    const auto contained=TravelService(scene)->Plan({q},{}).front();EXPECT_EQ("Collision",contained.Status);EXPECT_EQ("enclosure",contained.Obstacle);
}
TEST(FiveAxisRapid, PreviewDoesNotMutateAndManualResultIsUndoableAndEmbedded)
{
    Scene scene;PrepareOrderPart(scene);RapidConfig(scene);const auto before=Generated(scene);const auto history=scene.Database().GetUndoArray().size();
    const auto task=PreviewTravel(scene);EXPECT_EQ(before,Generated(scene));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    auto apply=GenerateRequest(scene);apply["taskId"]=task.at("taskId");const auto saved=Invoke(&scene,"CamRapid","Apply",apply);
    const auto after=Generated(scene);const auto t=after.at("transitions").To<Array>()[0].To<Map>();EXPECT_EQ("Setup",t.at("frame").To<Text>());EXPECT_EQ("ToolTip",t.at("referencePoint").To<Text>());EXPECT_FALSE(t.at("laser").To<bool>());
    EXPECT_EQ("Generated",t.at("geometryStatus").To<Text>());EXPECT_EQ("Clear",t.at("validation").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(t);EXPECT_EQ("Unchecked",t.at("machineValidation").To<Text>());EXPECT_FALSE(t.at("productionQualified").To<bool>());
    EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());EXPECT_EQ(saved,Invoke(&scene,"CamRapid","Apply",apply));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before,Generated(scene));ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(after,Generated(scene));
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Rapid";
    const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(after,Generated(reopened));std::filesystem::remove(path);
}
TEST(FiveAxisRapidGeometry, SafeExteriorAlwaysClearsTheWholeAssemblyBeforeTraverse)
{
    Scene scene;auto q=FlatTravel(scene);q.Kind=TravelKind::SafeExterior;q.LiftHeight=.1;
    q.Obstacles.push_back({"tall-clamp",TravelModel(scene,BRepPrimAPI_MakeBox(gp_Pnt(100,-5,0),5,10,80).Shape())});
    const auto result=TravelService(scene)->Plan({q},{}).front();ASSERT_EQ("Clear",result.Status)<<result.Reason;
    ASSERT_GE(result.Poses.size(),4u);EXPECT_NEAR(1.,result.Poses.front().Point[2],1e-8);EXPECT_NEAR(1.,result.Poses.back().Point[2],1e-8);
    for(std::size_t i=1;i+1<result.Poses.size();++i)EXPECT_GT(result.Poses[i].Point[2],90.);
}
TEST(FiveAxisRapidGeometry, SafeExteriorTurnsOpposedDirectionsOutsideTheObstacle)
{
    Scene scene;auto q=FlatTravel(scene);q.Kind=TravelKind::SafeExterior;q.End.Direction={0,0,-1};
    const auto result=TravelService(scene)->Plan({q},{}).front();ASSERT_EQ("Clear",result.Status)<<result.Reason;
    EXPECT_GT(result.Poses.size(),4u);EXPECT_EQ(q.Start.Direction,result.Poses.front().Direction);EXPECT_EQ(q.End.Direction,result.Poses.back().Direction);
}
TEST(FiveAxisRapid, AllTravelStrategiesRemainSelectableAndPersisted)
{
    Scene scene;PrepareOrderPart(scene);
    auto settings=Read(scene).at("setups").To<Array>().front().To<Map>().at("rapidSettings").To<Map>();EXPECT_EQ("Quick",settings.at("strategy").To<Text>());
    double estimate=-1;
    for(const auto* strategy:{"Fly","Frog","Quick"}){
        settings["strategy"]=Text(strategy);auto request=GenerateRequest(scene);request["settings"]=settings;Invoke(&scene,"CamRapid","Configure",request);
        EXPECT_EQ(strategy,Read(scene).at("setups").To<Array>().front().To<Map>().at("rapidSettings").To<Map>().at("strategy").To<Text>());
        const auto sorted=Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Automatic"));
        const auto report=sorted.at("optimization").To<Map>();EXPECT_EQ("HeightPenalizedStraightLineTravelEstimate",report.at("costScope").To<Text>());
        const auto cost=report.at("afterCost").To<double>();EXPECT_GT(cost,0);if(estimate<0)estimate=cost;else EXPECT_NEAR(estimate,cost,1e-8);
        EXPECT_EQ(settings,Read(scene).at("setups").To<Array>().front().To<Map>().at("rapidSettings").To<Map>());
        const auto task=PreviewTravel(scene,strategy);EXPECT_EQ(strategy,task.at("result").To<Map>().at("connections").To<Map>().begin()->second.To<Map>().at("policy").To<Map>().at("strategy").To<Text>());
    }
}
TEST(FiveAxisRapid, AutomaticResultsPreserveHistoryAndGeometryWhileChangesInvalidateSafety)
{
    Scene scene;PrepareOrderPart(scene);RapidConfig(scene);const auto before=Generated(scene);const auto history=scene.Database().GetUndoArray().size();
    const auto task=Wait(scene,Invoke(&scene,"CamRapid","Generate",SafeRapidRequest(scene)));ASSERT_EQ("Completed",task.at("status").To<Text>());
    const auto after=Generated(scene);EXPECT_EQ(before.at("revision"),after.at("revision"));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    EXPECT_GT(std::stoul(after.at("rapidGeneratedCount").To<Text>()),0u);EXPECT_EQ(after.at("rapidGeneratedCount"),after.at("rapidClearCount"));RapidConfig(scene,2,10);
    EXPECT_EQ("0",Generated(scene).at("rapidClearCount").To<Text>());EXPECT_EQ("Current",Generated(scene).at("status").To<Text>());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(after,Generated(scene));
    auto ids=OrderedIDs(after);std::reverse(ids.begin(),ids.end());Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Manual",ids));EXPECT_EQ("0",Generated(scene).at("rapidClearCount").To<Text>());
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamRapid","Generate",SafeRapidRequest(scene))).at("status").To<Text>());
    const auto reversed=Generated(scene);ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(after,Generated(scene));
    ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(reversed,Generated(scene));
}
TEST(FiveAxisRapid, RepeatedGenerationReusesGeometryAndOnlyCalculatesMissingConnections)
{
    Scene scene;PrepareOrderPart(scene);const auto preview=PreviewTravel(scene);
    auto apply=GenerateRequest(scene);apply["taskId"]=preview.at("taskId");Invoke(&scene,"CamRapid","Apply",apply);
    const auto count=Generated(scene).at("transitions").To<Array>().size();ASSERT_GT(count,1u);
    const auto first=Wait(scene,Invoke(&scene,"CamRapid","Generate",SafeRapidRequest(scene)));
    ASSERT_EQ("Completed",first.at("status").To<Text>());const auto result=first.at("result").To<Map>();
    EXPECT_EQ(std::to_string(count),result.at("generatedCount").To<Text>());EXPECT_EQ(result.at("generatedCount"),result.at("clearCount"));EXPECT_EQ("1",result.at("reusedCount").To<Text>());
    EXPECT_EQ(std::to_string(count-1),result.at("calculatedCount").To<Text>());
    const auto state=Read(scene),before=Generated(scene);const auto history=scene.Database().GetUndoArray().size();
    const auto request=SafeRapidRequest(scene),repeat=Invoke(&scene,"CamRapid","Generate",request);
    ASSERT_EQ("Completed",repeat.at("status").To<Text>());const auto reused=repeat.at("result").To<Map>();
    EXPECT_EQ(std::to_string(count),reused.at("reusedCount").To<Text>());EXPECT_EQ("0",reused.at("calculatedCount").To<Text>());
    EXPECT_EQ(result.at("connections"),reused.at("connections"));EXPECT_EQ(before,Generated(scene));EXPECT_EQ(state,Read(scene));
    EXPECT_EQ(history,scene.Database().GetUndoArray().size());EXPECT_EQ(repeat,Invoke(&scene,"CamRapid","Generate",request));
    RapidConfig(scene,2,10);
    const auto changed=Wait(scene,Invoke(&scene,"CamRapid","Generate",SafeRapidRequest(scene))).at("result").To<Map>();
    EXPECT_EQ("0",changed.at("reusedCount").To<Text>());EXPECT_EQ(std::to_string(count),changed.at("calculatedCount").To<Text>());
}
TEST(FiveAxisRapid, OneCutPoseOnlyInvalidatesItsConnectionsAndReusesOtherExactCurves)
{
    Scene scene;PrepareOrderPart(scene);auto ids=OrderedIDs(Generated(scene));ASSERT_GE(ids.size(),4u);
    Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Manual",ids));
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamRapid","Generate",RapidRequest(scene,false))).at("status").To<Text>());
    const auto before=Generated(scene);const auto count=before.at("transitions").To<Array>().size();const auto id=ids[1];
    const auto setupID=ID::from_string(Read(scene).at("activeSetupId").To<Text>()).value();const auto stored=std::dynamic_pointer_cast<CFiveAxisSetupComponent>(scene.Database().GetEntity(setupID)->GetComponent(CFiveAxisSetupComponent::S_ClassName));
    ASSERT_TRUE(stored);
    EXPECT_FALSE(stored->GetGeneratedToolpaths().empty());EXPECT_FALSE(stored->GetGeneratedRapidPaths().empty());ASSERT_TRUE(stored->GetGeneratedToolpaths().contains("operations"));
    const auto paths=Read(scene).at("intentToolpaths").To<Array>();const auto path=std::find_if(paths.begin(),paths.end(),[&](const auto& value){return value.To<Map>().at("id")==id;});ASSERT_NE(paths.end(),path);
    const auto operations=stored->GetGeneratedToolpaths().at("operations").To<Array>();const auto operation=std::find_if(operations.begin(),operations.end(),[&](const auto& value){return value.To<Map>().at("intentId")==id;});ASSERT_NE(operations.end(),operation);
    EXPECT_EQ(path->To<Map>().at("curveRef").To<Map>().at("revision"),operation->To<Map>().at("intentFingerprint"));EXPECT_FALSE(operation->To<Map>().at("parts").To<Array>().empty());
    Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,id,"Fixed",{.03,0.,1.},"Workpiece"));
    const auto stale=Generated(scene);EXPECT_EQ(ids,OrderedIDs(stale));EXPECT_EQ(std::to_string(count-2),stale.at("rapidGeneratedCount").To<Text>());
    for(const auto& value:stale.at("transitions").To<Array>()){const auto t=value.To<Map>();
        EXPECT_EQ(t.at("from")==id||t.at("to")==id?"Stale":"Generated",t.at("geometryStatus").To<Text>());}
    const auto generated=Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene)));ASSERT_EQ("Completed",generated.at("status").To<Text>());
    const auto result=generated.at("result").To<Map>();EXPECT_EQ("1",result.at("calculatedCount").To<Text>());EXPECT_EQ(std::to_string(ids.size()-1),result.at("reusedCount").To<Text>());
    for(const auto& other:ids)if(other!=id){const auto a=AxisOperation(before,other),b=AxisOperation(Generated(scene),other);EXPECT_EQ(a.at("curveData"),b.at("curveData"));EXPECT_EQ(a.at("parts"),b.at("parts"));}
    const auto rapid=Wait(scene,Invoke(&scene,"CamRapid","Generate",RapidRequest(scene,false)));ASSERT_EQ("Completed",rapid.at("status").To<Text>());
    EXPECT_EQ("2",rapid.at("result").To<Map>().at("calculatedCount").To<Text>());EXPECT_EQ(std::to_string(count-2),rapid.at("result").To<Map>().at("reusedCount").To<Text>());
    // Undo/redo restores authored inputs; certificates for both endpoint states
    // remain usable after the corresponding single cut is regenerated.
    ASSERT_TRUE(scene.Database().Undo());GenerateAxis(scene);
    auto reused=Wait(scene,Invoke(&scene,"CamRapid","Generate",RapidRequest(scene,false)));EXPECT_EQ("0",reused.at("result").To<Map>().at("calculatedCount").To<Text>());
    ASSERT_TRUE(scene.Database().Redo());GenerateAxis(scene);
    reused=Wait(scene,Invoke(&scene,"CamRapid","Generate",RapidRequest(scene,false)));EXPECT_EQ("0",reused.at("result").To<Map>().at("calculatedCount").To<Text>());
}
TEST(FiveAxisRapid, InteriorPoseWithFixedEndpointsKeepsEveryRapidAfterRegeneration)
{
    Scene scene;PrepareOrderPart(scene);const auto ids=OrderedIDs(Generated(scene));const auto id=ids[1];
    Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Manual",ids));
    Invoke(&scene,"CamCut","SetType",EditRequest(scene,{id},{{"cutType",Text("Contour")}}));GenerateAxis(scene);
    const auto op=AxisOperation(Generated(scene),id);
    for(const auto* endpoint:{"start","end"}){
        const auto axis=op.at(Text(endpoint)=="start"?"setupStartDirection":"setupEndDirection").To<Array>();
        Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,Text(endpoint),{},axis));
    }
    GenerateAxis(scene);ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamRapid","Generate",RapidRequest(scene,false))).at("status").To<Text>());
    const auto before=Generated(scene),definition=DefinitionItem(scene,id);const auto spans=definition.at("sourceSpans").To<Array>();const auto span=spans.front().To<Map>();
    const Map anchor{{"port",definition.at("start").To<Map>().at("port")},{"segmentId",span.at("segmentId")},{"parameter",(span.at("first").To<double>()+span.at("last").To<double>())/2}};
    const auto edited=Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,NewAxisPoint(),anchor,{.05,0.,1.}));
    const auto patch=edited.at("statePatch").To<Map>();EXPECT_FALSE(patch.contains("transitionState"));
    // Pose edits publish operation validity only. Their response must not run
    // the ordering solver just to rebuild rapid presentation data.
    EXPECT_FALSE(patch.contains("transitionStates"));EXPECT_FALSE(patch.contains("operationOrder"));
    EXPECT_EQ(Array{id},patch.at("invalidatedIntentIds").To<Array>());
    GenerateAxis(scene);const auto after=Generated(scene);EXPECT_EQ(before.at("rapidGeneratedCount"),after.at("rapidGeneratedCount"));
    const auto rapid=Wait(scene,Invoke(&scene,"CamRapid","Generate",RapidRequest(scene,false)));ASSERT_EQ("Completed",rapid.at("status").To<Text>());
    EXPECT_EQ("0",rapid.at("result").To<Map>().at("calculatedCount").To<Text>());
    // A real obstacle transform still revokes every environmental certificate.
    const auto state=Read(scene),placement=state.at("placements").To<Array>().front().To<Map>();auto transform=placement.at("transform").To<Array>();transform[3]=5.;
    auto move=GenerateRequest(scene);move["placementId"]=placement.at("id");move["transform"]=transform;Invoke(&scene,"CamPlacement","SetTransform",move);
    EXPECT_EQ("0",Generated(scene).at("rapidClearCount").To<Text>());
}
TEST(FiveAxisRapid, UncalibratedImportedHeadCannotGenerateQualifiedRapids)
{
    Scene scene;PrepareOrderPart(scene);
    File head(BRepPrimAPI_MakeBox(gp_Pnt(-1,-1,0),2,2,20).Shape());
    auto import=GenerateRequest(scene);import["sourcePath"]=head.Utf8();
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamHead","Import",import)).at("status").To<Text>());
    const auto before=Generated(scene);EXPECT_THROW(Invoke(&scene,"CamRapid","Generate",SafeRapidRequest(scene)),std::exception);EXPECT_EQ(before,Generated(scene));
    EXPECT_THROW(Invoke(&scene,"CamSimulation","CheckCollision",GenerateRequest(scene)),std::exception);
}
TEST(FiveAxisRapid, StalePreviewAndCancelledJobsCannotPublish)
{
    Scene scene;PrepareOrderPart(scene);RapidConfig(scene);const auto preview=PreviewTravel(scene);auto apply=GenerateRequest(scene);apply["taskId"]=preview.at("taskId");
    RapidConfig(scene,2,10);const auto before=Generated(scene);EXPECT_THROW(Invoke(&scene,"CamRapid","Apply",apply),std::exception);
    apply["expected"]=Read(scene).at("expected");EXPECT_THROW(Invoke(&scene,"CamRapid","Apply",apply),std::exception);EXPECT_EQ(before,Generated(scene));
    const auto task=Invoke(&scene,"CamRapid","Generate",RapidRequest(scene,false));Invoke(&scene,"CamRapid","CancelTask",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});
    EXPECT_EQ("Cancelled",Wait(scene,task).at("status").To<Text>());EXPECT_EQ(before,Generated(scene));
}
TEST(FiveAxisRapid, CancellingReadOnlyPreviewDoesNotBlockFixtureImport)
{
    Scene scene;PrepareOrderPart(scene);RapidConfig(scene);File fixture(BRepPrimAPI_MakeBox(10,10,10).Shape());
    auto import=GenerateRequest(scene);import["sourcePath"]=fixture.Utf8();
    const auto preview=Invoke(&scene,"CamRapid","Preview",RapidRequest(scene));
    Invoke(&scene,"CamRapid","CancelTask",{{"epoch",preview.at("epoch")},{"taskId",preview.at("taskId")}});
    const auto task=Invoke(&scene,"CamFixture","Import",import);
    EXPECT_EQ("Completed",Wait(scene,task).at("status").To<Text>());
    EXPECT_EQ("Cancelled",Wait(scene,preview).at("status").To<Text>());
    EXPECT_EQ(1u,Read(scene).at("fixtures").To<Array>().size());
}
