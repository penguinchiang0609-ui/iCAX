#pragma once

#include "ColliderServiceExport.h"
#include "ColliderData/ColliderData.h"
#include "Data/uuid.h"
#include "Services/IService.h"

#include <vector>

namespace iCAX
{
    namespace Collider
    {
        /*
        * @brief 碰撞服务接口。
        * @details
        *   ColliderService 负责 project/scene 级 collider 生命周期、raycast 和 simulation step。
        *   它不规定底层物理引擎，Jolt/Bullet/自研 BVH 都可以实现该接口。
        */
        class _COLLIDER_SERVICE_EXP IColliderService : public iCAX::Services::IService
        {
        public:
            ~IColliderService() override = default;

        public:
            virtual bool CreateScene(
                IN const iCAX::Data::uuid& ProjectID_,
                IN ColliderSceneID nSceneID_) = 0;

            virtual bool DestroyScene(
                IN const iCAX::Data::uuid& ProjectID_,
                IN ColliderSceneID nSceneID_) = 0;

            virtual void DestroyProject(IN const iCAX::Data::uuid& ProjectID_) = 0;

            virtual bool HasScene(
                IN const iCAX::Data::uuid& ProjectID_,
                IN ColliderSceneID nSceneID_) const = 0;

            virtual std::vector<ColliderSceneID> ListSceneIDs(IN const iCAX::Data::uuid& ProjectID_) const = 0;

            virtual ColliderID CreateBoxCollider(
                IN const iCAX::Data::uuid& ProjectID_,
                IN ColliderSceneID nSceneID_,
                IN const SBoxColliderData& Collider_) = 0;

            virtual ColliderID CreateSphereCollider(
                IN const iCAX::Data::uuid& ProjectID_,
                IN ColliderSceneID nSceneID_,
                IN const SSphereColliderData& Collider_) = 0;

            virtual ColliderID CreateTriangleMeshCollider(
                IN const iCAX::Data::uuid& ProjectID_,
                IN ColliderSceneID nSceneID_,
                IN const STriangleMeshColliderData& Collider_) = 0;

            virtual bool DestroyCollider(
                IN const iCAX::Data::uuid& ProjectID_,
                IN ColliderSceneID nSceneID_,
                IN ColliderID nColliderID_) = 0;

            virtual bool SetColliderTransform(
                IN const iCAX::Data::uuid& ProjectID_,
                IN ColliderSceneID nSceneID_,
                IN ColliderID nColliderID_,
                IN const SColliderTransform& Transform_,
                IN bool bActivate_) = 0;

            virtual bool GetColliderTransform(
                IN const iCAX::Data::uuid& ProjectID_,
                IN ColliderSceneID nSceneID_,
                IN ColliderID nColliderID_,
                OUT SColliderTransform& Transform_) const = 0;

            virtual void Step(
                IN const iCAX::Data::uuid& ProjectID_,
                IN ColliderSceneID nSceneID_,
                IN float nDeltaSeconds_,
                IN int nCollisionSteps_) = 0;

            virtual SColliderRaycastHit Raycast(
                IN const iCAX::Data::uuid& ProjectID_,
                IN ColliderSceneID nSceneID_,
                IN const SColliderRaycastRequest& Request_) const = 0;
        };
    }
}
