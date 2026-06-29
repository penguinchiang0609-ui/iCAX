#include <gtest/gtest.h>

#include <JoltPhysics/JoltPhysics.h>

#include <array>
#include <cmath>
#include <stdexcept>

using namespace iCAX::JoltPhysics;

namespace
{
    void ExpectNear(const SPhysicsVec3& Value_, const SPhysicsVec3& Expected_)
    {
        constexpr float kEpsilon = 0.001f;
        EXPECT_NEAR(Expected_.x, Value_.x, kEpsilon);
        EXPECT_NEAR(Expected_.y, Value_.y, kEpsilon);
        EXPECT_NEAR(Expected_.z, Value_.z, kEpsilon);
    }
}

TEST(JoltPhysicsSceneCatalogTest, CreatesIndependentScenes)
{
    CJoltPhysicsSceneCatalog _Catalog;
    CJoltPhysicsScene& _MainScene = _Catalog.CreateScene(1);
    CJoltPhysicsScene& _PreviewScene = _Catalog.CreateScene(2);

    EXPECT_TRUE(_MainScene.IsInitialized());
    EXPECT_TRUE(_PreviewScene.IsInitialized());
    EXPECT_NE(&_MainScene, &_PreviewScene);
    EXPECT_EQ(1, _MainScene.GetSceneID());
    EXPECT_EQ(2, _PreviewScene.GetSceneID());
    EXPECT_EQ(2u, _Catalog.ListSceneIDs().size());

    EXPECT_TRUE(_Catalog.DestroyScene(1));
    EXPECT_EQ(nullptr, _Catalog.FindScene(1));
    EXPECT_NE(nullptr, _Catalog.FindScene(2));
}

TEST(JoltPhysicsSceneTest, RaycastReturnsBoxOwnerAndSurfaceNormal)
{
    CJoltPhysicsSceneCatalog _Catalog;
    CJoltPhysicsScene& _Scene = _Catalog.CreateScene(1);

    SBoxBodyDesc _Box;
    _Box.nOwnerObjectID = 42;
    _Box.eMotionType = EPhysicsBodyMotionType::Static;
    _Box.HalfExtent = { 1.0f, 1.0f, 1.0f };
    const PhysicsBodyID _BodyID = _Scene.CreateBoxBody(_Box);
    ASSERT_NE(kInvalidPhysicsBodyID, _BodyID);

    SPhysicsRaycastRequest _Ray;
    _Ray.Origin = { 0.0f, 0.0f, 5.0f };
    _Ray.Direction = { 0.0f, 0.0f, -1.0f };
    _Ray.nMaxDistance = 100.0f;

    const SPhysicsRaycastHit _Hit = _Scene.Raycast(_Ray);
    ASSERT_TRUE(_Hit.bHit);
    EXPECT_EQ(_BodyID, _Hit.nBodyID);
    EXPECT_EQ(42, _Hit.nOwnerObjectID);
    ExpectNear(_Hit.Position, SPhysicsVec3{ 0.0f, 0.0f, 1.0f });
    ExpectNear(_Hit.Normal, SPhysicsVec3{ 0.0f, 0.0f, 1.0f });
    EXPECT_NEAR(4.0f, _Hit.nDistance, 0.001f);
}

TEST(JoltPhysicsSceneTest, RaycastCanHitTriangleMesh)
{
    CJoltPhysicsSceneCatalog _Catalog;
    CJoltPhysicsScene& _Scene = _Catalog.CreateScene(1);

    const std::array<SPhysicsVec3, 4> _Vertices = {
        SPhysicsVec3{ -1.0f, -1.0f, 0.0f },
        SPhysicsVec3{ 1.0f, -1.0f, 0.0f },
        SPhysicsVec3{ 1.0f, 1.0f, 0.0f },
        SPhysicsVec3{ -1.0f, 1.0f, 0.0f },
    };
    const std::array<uint32_t, 6> _Indices = { 0, 1, 2, 0, 2, 3 };

    STriangleMeshBodyDesc _Mesh;
    _Mesh.nOwnerObjectID = 9001;
    _Mesh.pVertices = _Vertices.data();
    _Mesh.nVertexCount = static_cast<uint32_t>(_Vertices.size());
    _Mesh.pIndices = _Indices.data();
    _Mesh.nIndexCount = static_cast<uint32_t>(_Indices.size());
    const PhysicsBodyID _BodyID = _Scene.CreateTriangleMeshBody(_Mesh);
    ASSERT_NE(kInvalidPhysicsBodyID, _BodyID);

    SPhysicsRaycastRequest _Ray;
    _Ray.Origin = { 0.0f, 0.0f, 10.0f };
    _Ray.Direction = { 0.0f, 0.0f, -1.0f };
    _Ray.nMaxDistance = 100.0f;

    const SPhysicsRaycastHit _Hit = _Scene.Raycast(_Ray);
    ASSERT_TRUE(_Hit.bHit);
    EXPECT_EQ(_BodyID, _Hit.nBodyID);
    EXPECT_EQ(9001, _Hit.nOwnerObjectID);
    ExpectNear(_Hit.Position, SPhysicsVec3{ 0.0f, 0.0f, 0.0f });
    EXPECT_NEAR(10.0f, _Hit.nDistance, 0.001f);
}

TEST(JoltPhysicsSceneTest, RejectsInvalidTriangleMesh)
{
    CJoltPhysicsSceneCatalog _Catalog;
    CJoltPhysicsScene& _Scene = _Catalog.CreateScene(1);

    STriangleMeshBodyDesc _Mesh;
    EXPECT_THROW(_Scene.CreateTriangleMeshBody(_Mesh), std::invalid_argument);

    const std::array<SPhysicsVec3, 1> _Vertices = { SPhysicsVec3{ 0.0f, 0.0f, 0.0f } };
    const std::array<uint32_t, 3> _Indices = { 0, 1, 2 };
    _Mesh.pVertices = _Vertices.data();
    _Mesh.nVertexCount = static_cast<uint32_t>(_Vertices.size());
    _Mesh.pIndices = _Indices.data();
    _Mesh.nIndexCount = static_cast<uint32_t>(_Indices.size());
    EXPECT_THROW(_Scene.CreateTriangleMeshBody(_Mesh), std::invalid_argument);
}
