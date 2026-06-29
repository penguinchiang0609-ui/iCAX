#include <gtest/gtest.h>

#include <JoltColliderService/JoltColliderService.h>

namespace
{
    iCAX::Collider::SBoxColliderData MakeBox(IN iCAX::Collider::ColliderObjectID nOwnerID_, IN float nX_)
    {
        iCAX::Collider::SBoxColliderData _Box;
        _Box.nOwnerObjectID = nOwnerID_;
        _Box.Transform.Position = { nX_, 0.0f, 0.0f };
        _Box.HalfExtent = { 0.5f, 0.5f, 0.5f };
        return _Box;
    }

    iCAX::Collider::SColliderRaycastRequest MakeDownRay(IN float nX_)
    {
        iCAX::Collider::SColliderRaycastRequest _Request;
        _Request.Origin = { nX_, 0.0f, 5.0f };
        _Request.Direction = { 0.0f, 0.0f, -1.0f };
        _Request.nMaxDistance = 10.0f;
        return _Request;
    }
}

TEST(JoltColliderServiceTest, KeepsColliderScenesIsolatedByProject)
{
    iCAX::JoltColliderService::CJoltColliderService _Service;
    _Service.OnLoad();

    const auto _ProjectA = iCAX::Data::GenerateNewUUID();
    const auto _ProjectB = iCAX::Data::GenerateNewUUID();
    constexpr iCAX::Collider::ColliderSceneID _SceneID = 1;

    EXPECT_TRUE(_Service.CreateScene(_ProjectA, _SceneID));
    EXPECT_TRUE(_Service.CreateScene(_ProjectB, _SceneID));
    const auto _ColliderID = _Service.CreateBoxCollider(_ProjectA, _SceneID, MakeBox(101, 0.0f));
    EXPECT_NE(iCAX::Collider::kInvalidColliderID, _ColliderID);

    const auto _HitA = _Service.Raycast(_ProjectA, _SceneID, MakeDownRay(0.0f));
    EXPECT_TRUE(_HitA.bHit);
    EXPECT_EQ(101u, _HitA.nOwnerObjectID);

    const auto _HitB = _Service.Raycast(_ProjectB, _SceneID, MakeDownRay(0.0f));
    EXPECT_FALSE(_HitB.bHit);

    _Service.DestroyProject(_ProjectA);
    EXPECT_FALSE(_Service.HasScene(_ProjectA, _SceneID));
    EXPECT_TRUE(_Service.HasScene(_ProjectB, _SceneID));
}

TEST(JoltColliderServiceTest, KeepsColliderScenesIsolatedInsideProject)
{
    iCAX::JoltColliderService::CJoltColliderService _Service;
    _Service.OnLoad();

    const auto _ProjectID = iCAX::Data::GenerateNewUUID();
    constexpr iCAX::Collider::ColliderSceneID _MainSceneID = 1;
    constexpr iCAX::Collider::ColliderSceneID _PreviewSceneID = 2;

    ASSERT_TRUE(_Service.CreateScene(_ProjectID, _MainSceneID));
    ASSERT_TRUE(_Service.CreateScene(_ProjectID, _PreviewSceneID));
    ASSERT_NE(iCAX::Collider::kInvalidColliderID, _Service.CreateBoxCollider(_ProjectID, _MainSceneID, MakeBox(201, 0.0f)));
    ASSERT_NE(iCAX::Collider::kInvalidColliderID, _Service.CreateBoxCollider(_ProjectID, _PreviewSceneID, MakeBox(202, 4.0f)));

    EXPECT_TRUE(_Service.Raycast(_ProjectID, _MainSceneID, MakeDownRay(0.0f)).bHit);
    EXPECT_FALSE(_Service.Raycast(_ProjectID, _PreviewSceneID, MakeDownRay(0.0f)).bHit);
    EXPECT_TRUE(_Service.Raycast(_ProjectID, _PreviewSceneID, MakeDownRay(4.0f)).bHit);
}
