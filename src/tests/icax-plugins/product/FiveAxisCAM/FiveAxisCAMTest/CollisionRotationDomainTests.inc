#include <FiveAxisCAM/FiveAxisCollisionRotation.inc>
TEST(FiveAxisCollisionRotation, CanonicalTransportRetainsActualAxesIncludingTheAntipodeWithBoundedNumericalChange)
{
    namespace M=iCAX::GeometryServices::RigidMotion;M::Quaternion rotation{0,0,0,1};M::Vector previous{0,0,1};double observed=0;
    const std::vector<M::Vector> axes{{.1,.2,.9},{-.0873,.82976,.55125},{0,.98513,.17182},{1e-10,2e-10,-1},{0,0,-1},{1e-10,0,-1},{0,0,1},{.4,-.2,.8}};
    for(const auto& raw:axes){const auto axis=M::Unit(raw);rotation=CollisionRotation::Step(previous,axis,rotation,&observed);EXPECT_LT(M::Length(M::Sub(M::Rotate(rotation,{0,0,1}),axis)),2e-13);previous=axis;}
    EXPECT_LE(observed,CollisionRotation::MaximumStepCorrectionRad);EXPECT_GT(observed,0.);
}
TEST(FiveAxisCollisionRotation, ALocalCoplanarExcursionRestoresTheExactActualBoundaryAndDownstreamPoses)
{
    namespace M=iCAX::GeometryServices::RigidMotion;const auto axis=M::Unit(M::Vector{.17,.83,.52});auto start=CollisionRotation::Canonical(M::Multiply(CollisionRotation::Between({0,0,1},axis),{0,0,std::sin(.37/2),std::cos(.37/2)}),axis);
    const auto tangent=M::Unit(M::Cross(axis,{1,0,0}));auto rotation=start;auto previous=axis;
    for(double angle:{0.,.05,.1,.05,0.}){const auto current=M::Unit(M::Add(M::Scale(axis,std::cos(angle)),M::Scale(tangent,std::sin(angle))));rotation=CollisionRotation::Step(previous,current,rotation);previous=current;}
    EXPECT_EQ(start,rotation);
    auto original=start;for(const auto& next:std::vector<M::Vector>{{0,1,0},{.2,.8,.3},{0,0,1}}){const auto current=M::Unit(next);original=CollisionRotation::Step(previous,current,original);rotation=CollisionRotation::Step(previous,current,rotation);EXPECT_EQ(original,rotation);previous=current;}
}
TEST(FiveAxisCollisionRotation, DifferentPhysicalRollsRemainDistinctAndDoNotReuseAnOccupiedVolumeWitness)
{
    namespace G=iCAX::GeometryServices;namespace M=G::RigidMotion;Scene scene;const auto service=scene.Services().Resolve<G::IRigidMotionCollisionService>();ASSERT_TRUE(service);
    const auto first=CollisionRotation::Canonical({0,0,0,1},{0,0,1}),second=CollisionRotation::Canonical({0,0,std::sin(M_PI/4),std::cos(M_PI/4)},{0,0,1});ASSERT_NE(first,second);
    const auto tiny=CollisionRotation::Canonical({0,0,std::sin(1e-8/2),std::cos(1e-8/2)},{0,0,1});EXPECT_NE(first,tiny);
    auto mesh=std::make_shared<G::CollisionMesh>();for(int bits=0;bits<8;++bits)mesh->Vertices.push_back({2+(bits&1?.05:-.05),bits&2?.05:-.05,10+(bits&4?.05:-.05)});
    mesh->Triangles={{0,2,1},{1,2,3},{4,5,6},{5,7,6},{0,1,4},{1,5,4},{2,6,3},{3,6,7},{0,4,2},{2,4,6},{1,3,5},{3,7,5}};mesh->ClosedVolumes={{0,1,2,3,4,5,6,7,8,9,10,11}};
    G::RigidMotionCollisionQuery query;G::CollisionBox box;box.Center={2,0,10};box.HalfExtents={.025,.025,.025};query.Boxes={box};G::TravelObstacle obstacle;obstacle.Id="occupied-own-mesh";obstacle.Mesh=mesh;query.Obstacles.push_back(obstacle);
    query.Segments={{{{0,0,0},first},{{0,0,0},first},.1,true},{{{0,0,0},second},{{0,0,0},second},.1,true}};
    const auto checked=service->Check(query,{});ASSERT_EQ(2u,checked.Segments.size());EXPECT_EQ("Collision",checked.Segments[0].Status);ASSERT_TRUE(checked.Segments[0].Witness);EXPECT_TRUE(checked.Segments[0].Witness->Containment);EXPECT_EQ(0u,checked.Segments[0].Witness->BoxIndex);EXPECT_EQ("Clear",checked.Segments[1].Status);EXPECT_FALSE(checked.Segments[1].Witness);
}
