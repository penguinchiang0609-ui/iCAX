#pragma once

#include "JoltColliderServiceExport.h"
#include "ColliderService/ColliderService.h"
#include "JoltPhysics/JoltPhysicsSceneCatalog.h"
#include "Services/ServicesHelper.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace iCAX
{
    namespace JoltColliderService
    {
        /*
        * @brief 基于 JoltPhysics 的 ColliderService 实现。
        * @details
        *   服务按 ProjectID 保存独立 scene catalog。一个 Project 下可以有多个 ColliderScene，
        *   每个 scene 对应一套独立 Jolt PhysicsSystem，适合主视口、预览视口、仿真视口并存。
        */
        class _JOLT_COLLIDER_SERVICE_EXP CJoltColliderService final : public iCAX::Collider::IColliderService
        {
            AUTO_REGIST_SERVICE(iCAX::Collider::IColliderService, CJoltColliderService)

        public:
            CJoltColliderService();
            ~CJoltColliderService() override;

            CJoltColliderService(IN const CJoltColliderService&) = delete;
            CJoltColliderService& operator=(IN const CJoltColliderService&) = delete;

        public:
            void OnLoad() override;
            void OnUnload() override;

            bool CreateScene(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Collider::ColliderSceneID nSceneID_) override;

            bool DestroyScene(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Collider::ColliderSceneID nSceneID_) override;

            void DestroyProject(IN const iCAX::Data::uuid& ProjectID_) override;

            bool HasScene(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Collider::ColliderSceneID nSceneID_) const override;

            std::vector<iCAX::Collider::ColliderSceneID> ListSceneIDs(IN const iCAX::Data::uuid& ProjectID_) const override;

            iCAX::Collider::ColliderID CreateBoxCollider(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Collider::ColliderSceneID nSceneID_,
                IN const iCAX::Collider::SBoxColliderData& Collider_) override;

            iCAX::Collider::ColliderID CreateSphereCollider(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Collider::ColliderSceneID nSceneID_,
                IN const iCAX::Collider::SSphereColliderData& Collider_) override;

            iCAX::Collider::ColliderID CreateTriangleMeshCollider(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Collider::ColliderSceneID nSceneID_,
                IN const iCAX::Collider::STriangleMeshColliderData& Collider_) override;

            bool DestroyCollider(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Collider::ColliderSceneID nSceneID_,
                IN iCAX::Collider::ColliderID nColliderID_) override;

            bool SetColliderTransform(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Collider::ColliderSceneID nSceneID_,
                IN iCAX::Collider::ColliderID nColliderID_,
                IN const iCAX::Collider::SColliderTransform& Transform_,
                IN bool bActivate_) override;

            bool GetColliderTransform(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Collider::ColliderSceneID nSceneID_,
                IN iCAX::Collider::ColliderID nColliderID_,
                OUT iCAX::Collider::SColliderTransform& Transform_) const override;

            void Step(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Collider::ColliderSceneID nSceneID_,
                IN float nDeltaSeconds_,
                IN int nCollisionSteps_) override;

            iCAX::Collider::SColliderRaycastHit Raycast(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Collider::ColliderSceneID nSceneID_,
                IN const iCAX::Collider::SColliderRaycastRequest& Request_) const override;

        private:
            iCAX::JoltPhysics::CJoltPhysicsScene* FindSceneNoLock(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Collider::ColliderSceneID nSceneID_);

            const iCAX::JoltPhysics::CJoltPhysicsScene* FindSceneNoLock(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Collider::ColliderSceneID nSceneID_) const;

            iCAX::JoltPhysics::CJoltPhysicsScene& RequireSceneNoLock(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Collider::ColliderSceneID nSceneID_);

            const iCAX::JoltPhysics::CJoltPhysicsScene& RequireSceneNoLock(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Collider::ColliderSceneID nSceneID_) const;

            static void ValidateProjectAndScene(
                IN const iCAX::Data::uuid& ProjectID_,
                IN iCAX::Collider::ColliderSceneID nSceneID_);

        private:
            mutable std::mutex m_Mutex;
            std::unordered_map<iCAX::Data::uuid, std::unique_ptr<iCAX::JoltPhysics::CJoltPhysicsSceneCatalog>> m_Projects;
        };
    }
}
