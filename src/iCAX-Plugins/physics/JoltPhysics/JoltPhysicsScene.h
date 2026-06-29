#pragma once

#include "JoltPhysicsTypes.h"

#include <memory>

namespace iCAX
{
    namespace JoltPhysics
    {
        /*
        * @brief Jolt 物理场景。
        * @details
        *   一个物理场景拥有一套独立的 Jolt PhysicsSystem。多个显示场景、预览场景或导入场景
        *   应创建多个 CJoltPhysicsScene，避免 body/collider/raycast 相互污染。
        */
        class _JOLT_PHYSICS_EXP CJoltPhysicsScene final
        {
        public:
            CJoltPhysicsScene();
            ~CJoltPhysicsScene();

            CJoltPhysicsScene(const CJoltPhysicsScene&) = delete;
            CJoltPhysicsScene& operator=(const CJoltPhysicsScene&) = delete;

            CJoltPhysicsScene(CJoltPhysicsScene&&) noexcept;
            CJoltPhysicsScene& operator=(CJoltPhysicsScene&&) noexcept;

        public:
            void Initialize(PhysicsSceneID nSceneID_);
            void Shutdown() noexcept;
            bool IsInitialized() const noexcept;
            PhysicsSceneID GetSceneID() const noexcept;

            void SetGravity(const SPhysicsVec3& Gravity_);
            SPhysicsVec3 GetGravity() const;

            /*
            * @brief 创建盒体 collider/rigid body。
            * @param [in] Desc_ 盒体描述，HalfExtent 为半尺寸。
            * @return 当前 Scene 内的 body ID。
            */
            PhysicsBodyID CreateBoxBody(const SBoxBodyDesc& Desc_);

            /*
            * @brief 创建球体 collider/rigid body。
            * @param [in] Desc_ 球体描述。
            * @return 当前 Scene 内的 body ID。
            */
            PhysicsBodyID CreateSphereBody(const SSphereBodyDesc& Desc_);

            /*
            * @brief 创建三角网格 collider/rigid body。
            * @param [in] Desc_ 三角网格描述。顶点和索引只在调用期间读取。
            * @return 当前 Scene 内的 body ID。
            * @details Mesh collider 主要服务模型拾取和静态场景碰撞。动态复杂 mesh 成本较高，产品层应谨慎使用。
            */
            PhysicsBodyID CreateTriangleMeshBody(const STriangleMeshBodyDesc& Desc_);

            bool DestroyBody(PhysicsBodyID nBodyID_);
            bool SetBodyTransform(PhysicsBodyID nBodyID_, const SPhysicsTransform& Transform_, bool bActivate_);
            bool GetBodyTransform(PhysicsBodyID nBodyID_, SPhysicsTransform& Transform_) const;

            void Step(float nDeltaSeconds_, int nCollisionSteps_);
            SPhysicsRaycastHit Raycast(const SPhysicsRaycastRequest& Request_) const;

        private:
            class Impl;
            std::unique_ptr<Impl> m_pImpl;
        };
    }
}
