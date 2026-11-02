namespace {
    using namespace iCAX::GeometryServices;
    CollisionTransform CollisionIdentity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
    CollisionMesh BoxCollisionMesh(CollisionVector center,CollisionVector half){
        CollisionMesh mesh;for(int bits=0;bits<8;++bits)mesh.Vertices.push_back({center[0]+(bits&1?half[0]:-half[0]),center[1]+(bits&2?half[1]:-half[1]),center[2]+(bits&4?half[2]:-half[2])});
        mesh.Triangles={{0,2,3},{0,3,1},{4,5,7},{4,7,6},{0,1,5},{0,5,4},{2,6,7},{2,7,3},{0,4,6},{0,6,2},{1,3,7},{1,7,5}};return mesh;
    }
}
TEST(CoalDistance,ExactSeparationDistinguishesThresholdFromOverlapAndClosedMeshContainment)
{
    Scene scene;const auto service=scene.Services().Resolve<ICollisionDistanceService>();const auto head=service->PrepareBoxes({CollisionBox{{0,0,0},{.1,.1,.1}}});
    const auto fixed=service->PrepareMesh(BoxCollisionMesh({0,0,0},{1,1,1}));auto pose=CollisionIdentity();pose[3]=1.3;
    EXPECT_EQ(0,service->LowerBound(fixed,head,pose,.3).LowerBound);const auto nearby=service->Separation(fixed,head,pose);
    EXPECT_FALSE(nearby.Overlapping);EXPECT_NEAR(.2,nearby.LowerBound,2e-6);EXPECT_NEAR(.2,nearby.UpperBound,2e-6);
    pose[3]=0;EXPECT_EQ(0,service->LowerBound(fixed,head,pose,0).LowerBound);const auto contained=service->Separation(fixed,head,pose);
    EXPECT_TRUE(contained.Overlapping);EXPECT_EQ(0,contained.LowerBound);EXPECT_EQ(0,contained.UpperBound);
    // Tessellators may duplicate shared vertices for different face normals.
    auto duplicated=BoxCollisionMesh({0,0,0},{1,1,1});const auto original=duplicated.Vertices;duplicated.Vertices.clear();
    for(auto& triangle:duplicated.Triangles)for(auto& index:triangle){const auto point=original[index];index=static_cast<unsigned>(duplicated.Vertices.size());duplicated.Vertices.push_back(point);}
    EXPECT_TRUE(service->Separation(service->PrepareMesh(duplicated),head,pose).Overlapping);
    pose[3]=5;EXPECT_FALSE(service->Separation(fixed,head,pose).Overlapping);EXPECT_THROW(service->Separation(fixed,head,pose,[]{return true;}),std::runtime_error);
}
TEST(CoalDistance,ClosedMeshCavitiesAndOpenMeshHolesRetainTheirOwnGeometry)
{
    Scene scene;const auto service=scene.Services().Resolve<ICollisionDistanceService>();const auto head=service->PrepareBoxes({CollisionBox{{0,0,0},{.1,.1,.1}}});
    auto cavity=BoxCollisionMesh({0,0,0},{2,2,2});const auto inner=BoxCollisionMesh({0,0,0},{1,1,1});const auto offset=static_cast<unsigned>(cavity.Vertices.size());
    cavity.Vertices.insert(cavity.Vertices.end(),inner.Vertices.begin(),inner.Vertices.end());for(auto triangle:inner.Triangles){for(auto& index:triangle)index+=offset;cavity.Triangles.push_back(triangle);}
    const auto fixed=service->PrepareMesh(cavity);auto pose=CollisionIdentity();const auto lower=service->LowerBound(fixed,head,pose,0).LowerBound;
    EXPECT_GT(lower,0);EXPECT_LE(lower,.900001);const auto exact=service->Separation(fixed,head,pose);EXPECT_FALSE(exact.Overlapping);EXPECT_NEAR(.9,exact.LowerBound,2e-6);EXPECT_NEAR(.9,exact.UpperBound,2e-6);
    pose[3]=1.5;EXPECT_TRUE(service->Separation(fixed,head,pose).Overlapping);
    CollisionMesh ring{{{-10,-10,0},{10,-10,0},{10,10,0},{-10,10,0},{-2,-2,0},{2,-2,0},{2,2,0},{-2,2,0}},
        {{0,1,5},{0,5,4},{1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7}},0};
    const auto opening=service->PrepareMesh(ring);pose=CollisionIdentity();EXPECT_GT(service->Separation(opening,head,pose).LowerBound,1.89);
    pose[3]=5;EXPECT_TRUE(service->Separation(opening,head,pose).Overlapping);
    // An explicitly open mesh occupies its triangles, not an invented solid.
    auto open=BoxCollisionMesh({0,0,0},{1,1,1});open.Triangles.erase(open.Triangles.begin(),open.Triangles.begin()+2);pose=CollisionIdentity();
    EXPECT_FALSE(service->Separation(service->PrepareMesh(open),head,pose).Overlapping);
}
TEST(CoalDistance,DeclaredClosedVolumesPreserveSeamsUnionAndMixedOpenSurfaces)
{
    Scene scene;const auto service=scene.Services().Resolve<ICollisionDistanceService>();auto mesh=BoxCollisionMesh({-.25,0,0},{1,1,1});const auto second=BoxCollisionMesh({.25,0,0},{1,1,1});
    const auto offset=static_cast<unsigned>(mesh.Vertices.size());mesh.Vertices.insert(mesh.Vertices.end(),second.Vertices.begin(),second.Vertices.end());
    for(auto triangle:second.Triangles){for(auto& index:triangle)index+=offset;mesh.Triangles.push_back(triangle);}
    mesh.ClosedVolumes={{0,1,2,3,4,5,6,7,8,9,10,11},{12,13,14,15,16,17,18,19,20,21,22,23}};
    const auto head=service->PrepareBoxes({CollisionBox{{0,0,0},{.05,.05,.05}}});EXPECT_TRUE(service->Separation(service->PrepareMesh(mesh),head,CollisionIdentity()).Overlapping);
    // The source topology declares a solid even when independently evaluated
    // face boundary coordinates have tiny numerical seams. Coordinates remain.
    auto seam=BoxCollisionMesh({0,0,0},{1,1,1});const auto original=seam.Vertices;seam.Vertices.clear();
    for(auto& triangle:seam.Triangles)for(auto& index:triangle){auto point=original[index];point[0]+=static_cast<double>(seam.Vertices.size())*1e-14;index=static_cast<unsigned>(seam.Vertices.size());seam.Vertices.push_back(point);}
    seam.ClosedVolumes={{0,1,2,3,4,5,6,7,8,9,10,11}};const auto start=static_cast<unsigned>(seam.Vertices.size());seam.Vertices.insert(seam.Vertices.end(),{{100,100,100},{101,100,100},{100,101,100}});seam.Triangles.push_back({start,start+1,start+2});
    EXPECT_EQ(0,service->LowerBound(service->PrepareMesh(seam),head,CollisionIdentity(),0).LowerBound);
    seam.ClosedVolumes[0].push_back(99);EXPECT_THROW(service->PrepareMesh(seam),std::invalid_argument);
}
TEST(CoalDistance,WitnessIdentifiesTheActualCompoundBoxAndItsSurfacePair)
{
    Scene scene;const auto service=scene.Services().Resolve<ICollisionDistanceService>();const auto fixed=service->PrepareMesh(BoxCollisionMesh({0,0,0},{1,1,1}));
    auto body=service->PrepareBoxes({CollisionBox{{10,0,0},{.1,.1,.1}},CollisionBox{{1.3,0,0},{.1,.1,.1}}});
    const auto separated=service->Separation(fixed,body,CollisionIdentity());ASSERT_TRUE(separated.Witness);const auto& w=*separated.Witness;
    EXPECT_EQ(1u,w.MovingPartIndex);EXPECT_EQ(0u,w.FixedPartIndex);EXPECT_FALSE(w.Containment);EXPECT_NEAR(1,w.PointOnFixed[0],1e-6);EXPECT_NEAR(1.2,w.PointOnMoving[0],1e-6);EXPECT_NEAR(.2,w.Separation,1e-6);EXPECT_NEAR(.2,w.SurfaceDistance,1e-6);
    double squared=0;for(int i=0;i<3;++i)squared+=std::pow(w.PointOnFixed[i]-w.PointOnMoving[i],2);EXPECT_NEAR(std::sqrt(squared),w.SurfaceDistance,1e-9);
    body=service->PrepareBoxes({CollisionBox{{10,0,0},{.1,.1,.1}},CollisionBox{{.95,0,0},{.1,.1,.1}}});const auto intersecting=service->Separation(fixed,body,CollisionIdentity());ASSERT_TRUE(intersecting.Overlapping);ASSERT_TRUE(intersecting.Witness);
    EXPECT_EQ(1u,intersecting.Witness->MovingPartIndex);EXPECT_FALSE(intersecting.Witness->Containment);EXPECT_EQ(0,intersecting.Witness->Separation);EXPECT_NEAR(1,intersecting.Witness->PointOnFixed[0],1e-6);
    EXPECT_TRUE(std::all_of(intersecting.Witness->PointOnMoving.begin(),intersecting.Witness->PointOnMoving.end(),[](auto n){return std::isfinite(n);}));
    const auto shared=service->PrepareBoxes({CollisionBox{{0,0,0},{.1,.1,.1}}});auto translated=CollisionIdentity();translated[3]=1.2;const auto reused=service->Separation(shared,shared,translated);ASSERT_TRUE(reused.Witness);
    EXPECT_NEAR(.1,reused.Witness->PointOnFixed[0],1e-6);EXPECT_NEAR(1.1,reused.Witness->PointOnMoving[0],1e-6);
}
TEST(CoalDistance,ContainedBoxWitnessDoesNotUseAnUnrelatedNearestCompoundPart)
{
    Scene scene;const auto service=scene.Services().Resolve<ICollisionDistanceService>();const auto fixed=service->PrepareMesh(BoxCollisionMesh({0,0,0},{1,1,1}));
    // Part zero has the smallest SURFACE gap (.2). Part one is actually inside
    // the occupied solid and must own the collision witness, despite its .9 gap.
    const auto body=service->PrepareBoxes({CollisionBox{{1.3,0,0},{.1,.1,.1}},CollisionBox{{0,0,0},{.1,.1,.1}}});const auto result=service->Separation(fixed,body,CollisionIdentity());ASSERT_TRUE(result.Overlapping);ASSERT_TRUE(result.Witness);
    const auto& w=*result.Witness;EXPECT_EQ(1u,w.MovingPartIndex);EXPECT_TRUE(w.Containment);EXPECT_EQ(0,w.Separation);EXPECT_NEAR(.9,w.SurfaceDistance,1e-6);
    EXPECT_LE(*std::max_element(w.PointOnMoving.begin(),w.PointOnMoving.end()),.100001);EXPECT_GE(*std::min_element(w.PointOnMoving.begin(),w.PointOnMoving.end()),-.100001);
}
TEST(CoalDistance,ConservativeDistancesRespectTransformsAndContainment)
{
    Scene scene;auto service=scene.Services().Resolve<ICollisionDistanceService>();
    auto wall=service->PrepareBoxes({CollisionBox{{0,0,0},{1,20,20}}});
    auto body=service->PrepareFrusta({{0,2,1,1}});auto pose=CollisionIdentity();pose[3]=5;
    const auto clear=service->LowerBound(wall,body,pose,.3);
    EXPECT_NEAR(clear.LowerBound,3,2e-6);EXPECT_GT(clear.NarrowPhaseQueries,0u);
    pose[3]=0;EXPECT_EQ(service->LowerBound(wall,body,pose,.3).LowerBound,0);
    pose[3]=2.1;EXPECT_EQ(service->LowerBound(wall,body,pose,.3).LowerBound,0);
    pose[0]=2;EXPECT_THROW(service->LowerBound(wall,body,pose,.3),std::invalid_argument);
    EXPECT_THROW(service->PrepareBoxes({CollisionBox{{0,0,0},{-1,1,1}}}),std::invalid_argument);
    EXPECT_THROW(service->PrepareBoxes({CollisionBox{{0,0,0},{1,1,1}}},[]{return true;}),std::runtime_error);
}
TEST(CoalDistance,FrustaEncloseCircularCrossSectionsAndQueryEveryBody)
{
    Scene scene;auto service=scene.Services().Resolve<ICollisionDistanceService>();
    auto wall=service->PrepareBoxes({CollisionBox{{0,0,0},{1,20,20}}});
    auto body=service->PrepareFrusta({{0,5,2,1}});auto pose=CollisionIdentity();pose[3]=5;
    auto result=service->LowerBound(wall,body,pose,.3);
    EXPECT_LE(result.LowerBound,2);EXPECT_GT(result.LowerBound,1.99);
    auto compound=service->PrepareFrusta({{100,2,1,1},{0,5,2,1}});
    EXPECT_NEAR(service->LowerBound(wall,compound,pose,.3).LowerBound,result.LowerBound,1e-6);
    // Rotation about Z must not open gaps in the conservative circular envelope.
    const double angle=.023;pose[0]=pose[5]=std::cos(angle);pose[1]=-std::sin(angle);pose[4]=std::sin(angle);
    EXPECT_LE(service->LowerBound(wall,body,pose,.3).LowerBound,2);
    EXPECT_THROW(service->LowerBound(wall,body,pose,.3,[]{return true;}),std::runtime_error);
}
TEST(CoalDistance,PreparedModelsSupportConcurrentReadQueries)
{
    Scene scene;auto service=scene.Services().Resolve<ICollisionDistanceService>();
    auto wall=service->PrepareBoxes({CollisionBox{{0,0,0},{1,20,20}}});auto body=service->PrepareFrusta({{0,2,1,1}});
    std::vector<std::future<double>> tasks;
    for(int i=0;i<8;++i)tasks.push_back(std::async(std::launch::async,[&,i]{auto pose=CollisionIdentity();pose[3]=5+i;return service->LowerBound(wall,body,pose,.3).LowerBound;}));
    for(int i=0;i<8;++i)EXPECT_NEAR(tasks[i].get(),3+i,2e-6);
}
TEST(CoalDistance,PointHullSupportExcludesEmptyCornersAndPreservesPadding)
{
    Scene scene;auto service=scene.Services().Resolve<ICollisionDistanceService>();
    CollisionHull triangle{{{0,0,0},{10,0,0},{0,10,0}},.1};
    auto fixed=service->PrepareVolumes({CollisionBox{{100,100,100},{1,1,1}}},{triangle});
    auto moving=service->PrepareFrusta({{0,2,.2,.2}});auto pose=CollisionIdentity();pose[3]=pose[7]=9;
    const auto separated=service->LowerBound(fixed,moving,pose,.3);
    EXPECT_NEAR(separated.LowerBound,8/std::sqrt(2.)-.3,3e-6);
    pose[3]=pose[7]=2;EXPECT_EQ(service->LowerBound(fixed,moving,pose,.3).LowerBound,0);
    triangle.Padding=-1;EXPECT_THROW(service->PrepareVolumes({},{triangle}),std::invalid_argument);
    EXPECT_THROW(service->PrepareVolumes({},{{{},0}}),std::invalid_argument);
}
TEST(CoalDistance,LargePointHullsWithoutNeighborTopologyWorkInBothOrders)
{
    Scene scene;auto service=scene.Services().Resolve<ICollisionDistanceService>();
    CollisionHull hull;hull.Padding=.05;
    for(int ring=0;ring<2;++ring)for(int i=0;i<64;++i){const auto a=i*2*3.141592653589793/64;hull.Points.push_back({2*std::cos(a),2*std::sin(a),ring?2.:-2.});}
    const auto body=service->PrepareVolumes({},{hull}),box=service->PrepareBoxes({CollisionBox{{0,0,0},{1,1,1}}});
    auto pose=CollisionIdentity();pose[3]=10;
    EXPECT_GT(service->LowerBound(box,body,pose,.1).LowerBound,6.9);
    EXPECT_GT(service->LowerBound(body,box,pose,.1).LowerBound,6.9);
    EXPECT_GT(service->LowerBound(body,body,pose,.1).LowerBound,5.8);
    pose[3]=0;EXPECT_EQ(service->LowerBound(body,body,pose,.1).LowerBound,0);
    EXPECT_EQ(service->LowerBound(body,box,pose,.1).LowerBound,0);
}
TEST(CoalDistance,MeshBVHPreservesHolesPaddingAndSharedQueries)
{
    Scene scene;auto service=scene.Services().Resolve<ICollisionDistanceService>();
    CollisionMesh ring{{{-10,-10,0},{10,-10,0},{10,10,0},{-10,10,0},{-2,-2,0},{2,-2,0},{2,2,0},{-2,2,0}},
        {{0,1,5},{0,5,4},{1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7}},.05};
    const auto mesh=service->PrepareMesh(ring),distantWall=service->PrepareBoxes({CollisionBox{{100,100,100},{1,1,1}}});
    const auto fixed=service->Combine({distantWall,mesh}),head=service->PrepareFrusta({{0,2,.2,.2}});
    auto pose=CollisionIdentity();const auto throughHole=service->LowerBound(fixed,head,pose,.3).LowerBound;
    EXPECT_GT(throughHole,1.7);EXPECT_LE(throughHole,1.750001);
    pose[3]=5;EXPECT_EQ(service->LowerBound(fixed,head,pose,.3).LowerBound,0);
    pose[11]=3;const auto above=service->LowerBound(fixed,head,pose,.3).LowerBound;
    EXPECT_GT(above,2.9);EXPECT_LE(above,2.950001);
    std::vector<std::future<double>> tasks;
    for(int i=0;i<8;++i)tasks.push_back(std::async(std::launch::async,[&,i]{auto p=CollisionIdentity();p[3]=5;p[11]=3+i;return service->LowerBound(fixed,head,p,.3).LowerBound;}));
    for(int i=0;i<8;++i){const auto lower=tasks[i].get();EXPECT_GT(lower,2.9+i);EXPECT_LE(lower,2.950001+i);}
    EXPECT_NEAR(service->LowerBound(fixed,head,pose,.3).LowerBound,above,1e-9);
    ring.Triangles[0][0]=99;EXPECT_THROW(service->PrepareMesh(ring),std::invalid_argument);
    ring.Triangles[0][0]=0;EXPECT_THROW(service->PrepareMesh(ring,[]{return true;}),std::runtime_error);
}
TEST(CoalDistance,MeshClearanceRefinesPastASmallBoundingVolumeGap)
{
    Scene scene;auto service=scene.Services().Resolve<ICollisionDistanceService>();
    CollisionMesh plane{{{-10,-10,0},{10,-10,0},{10,10,0},{-10,10,0}},{{0,1,2},{0,2,3}},.01};
    const auto fixed=service->PrepareMesh(plane),head=service->PrepareFrusta({{0,1,.1,.1}});auto pose=CollisionIdentity();
    // A horizontal BV gap is only .1 mm, while the actual vertical clearance
    // is almost 1 mm. Stopping BV traversal at its default .001 mm cannot
    // answer a caller asking to certify .3 mm clearance.
    pose[3]=10.2;pose[11]=1.;const auto result=service->LowerBound(fixed,head,pose,.3);
    EXPECT_GT(result.LowerBound,.3);EXPECT_LE(result.LowerBound,1.01);
    pose[11]=.1;EXPECT_EQ(service->LowerBound(fixed,head,pose,.3).LowerBound,0);
}
TEST(CoalDistance,SolverPairEvictionPreservesMeshClearanceAndCollisionWitnesses)
{
    Scene scene;const auto service=scene.Services().Resolve<ICollisionDistanceService>();
    const auto fixed=service->PrepareMesh(BoxCollisionMesh({0,0,0},{1,1,1}));
    const auto first=service->PrepareBoxes({CollisionBox{{0,0,0},{.1,.1,.1}}});auto pose=CollisionIdentity();pose[3]=1.3;
    const auto before=service->Separation(fixed,first,pose);ASSERT_FALSE(before.Overlapping);ASSERT_TRUE(before.Witness);EXPECT_NEAR(.2,before.LowerBound,2e-6);
    // Keep every model alive so this exercises distinct geometry pairs rather
    // than allocator reuse of a released geometry's address.
    std::vector<CollisionDistanceModelRef> models;models.reserve(520);
    for(unsigned i=0;i<520;++i){
        const double half=.1+double(i%5)*.001;models.push_back(service->PrepareBoxes({CollisionBox{{0,0,0},{half,half,half}}}));
        const auto lower=service->LowerBound(fixed,models.back(),pose,.01);EXPECT_GT(lower.LowerBound,.19);EXPECT_LE(lower.LowerBound,.200001);
        const auto separated=service->Separation(fixed,models.back(),pose);EXPECT_FALSE(separated.Overlapping);EXPECT_NEAR(.3-half,separated.LowerBound,2e-6);
    }
    const auto revisited=service->Separation(fixed,first,pose);ASSERT_FALSE(revisited.Overlapping);ASSERT_TRUE(revisited.Witness);
    EXPECT_NEAR(before.LowerBound,revisited.LowerBound,1e-9);EXPECT_NEAR(.2,revisited.Witness->Separation,1e-6);
    const auto lower=service->LowerBound(fixed,first,pose,.01);EXPECT_GT(lower.LowerBound,.19);EXPECT_LE(lower.LowerBound,.200001);
    pose[3]=.95;EXPECT_EQ(0,service->LowerBound(fixed,first,pose,.01).LowerBound);const auto collision=service->Separation(fixed,first,pose);
    ASSERT_TRUE(collision.Overlapping);ASSERT_TRUE(collision.Witness);EXPECT_EQ(0,collision.LowerBound);EXPECT_EQ(0,collision.Witness->Separation);EXPECT_FALSE(collision.Witness->Containment);
    EXPECT_EQ(0u,collision.Witness->FixedPartIndex);EXPECT_EQ(0u,collision.Witness->MovingPartIndex);EXPECT_NEAR(1,collision.Witness->PointOnFixed[0],1e-6);
}
TEST(CoalDistance,CancelledQueriesReuseTheSamePairAcrossClearAndCollidingPoses)
{
    Scene scene;const auto service=scene.Services().Resolve<ICollisionDistanceService>();
    const auto fixed=service->PrepareMesh(BoxCollisionMesh({0,0,0},{1,1,1})),head=service->PrepareBoxes({CollisionBox{{0,0,0},{.1,.1,.1}}});
    auto pose=CollisionIdentity();pose[3]=.95;ASSERT_TRUE(service->Separation(fixed,head,pose).Overlapping);
    pose[3]=1.3;
    // Cancellation follows a completed narrow-phase call, while containment
    // still needs checking. The next call must discard that partial result.
    unsigned checks=0;EXPECT_THROW(service->LowerBound(fixed,head,pose,.01,[&]{return ++checks>=3;}),std::runtime_error);EXPECT_GE(checks,3u);
    checks=0;EXPECT_THROW(service->Separation(fixed,head,pose,[&]{return ++checks>=3;}),std::runtime_error);EXPECT_GE(checks,3u);
    for(unsigned i=0;i<8;++i){const bool collision=i%2==0;pose[3]=collision?.95:1.3;
        const auto lower=service->LowerBound(fixed,head,pose,.01);if(collision)EXPECT_EQ(0,lower.LowerBound);else{EXPECT_GT(lower.LowerBound,.19);EXPECT_LE(lower.LowerBound,.200001);}
        const auto separated=service->Separation(fixed,head,pose);EXPECT_EQ(collision,separated.Overlapping);ASSERT_TRUE(separated.Witness);
        EXPECT_EQ(0u,separated.Witness->FixedPartIndex);EXPECT_EQ(0u,separated.Witness->MovingPartIndex);EXPECT_FALSE(separated.Witness->Containment);
        if(collision){EXPECT_EQ(0,separated.LowerBound);EXPECT_EQ(0,separated.UpperBound);EXPECT_EQ(0,separated.Witness->Separation);}
        else{EXPECT_NEAR(.2,separated.LowerBound,2e-6);EXPECT_NEAR(.2,separated.UpperBound,2e-6);EXPECT_NEAR(.2,separated.Witness->Separation,1e-6);EXPECT_NEAR(1.2,separated.Witness->PointOnMoving[0],1e-6);}
        EXPECT_NEAR(1,separated.Witness->PointOnFixed[0],1e-6);
        EXPECT_TRUE(std::all_of(separated.Witness->PointOnMoving.begin(),separated.Witness->PointOnMoving.end(),[](double value){return std::isfinite(value);}));
    }
}
