#pragma once

#include "JoltPhysicsScene.h"

#include <memory>
#include <vector>

namespace iCAX
{
    namespace JoltPhysics
    {
        /*
        * @brief 物理场景目录。
        * @details
        *   目录用于一个 project/runtime 内管理多个独立 PhysicsScene。它不是全局单例。
        */
        class _JOLT_PHYSICS_EXP CJoltPhysicsSceneCatalog final
        {
        public:
            CJoltPhysicsSceneCatalog();
            ~CJoltPhysicsSceneCatalog();

            CJoltPhysicsSceneCatalog(const CJoltPhysicsSceneCatalog&) = delete;
            CJoltPhysicsSceneCatalog& operator=(const CJoltPhysicsSceneCatalog&) = delete;

        public:
            CJoltPhysicsScene& CreateScene(PhysicsSceneID nSceneID_);
            bool DestroyScene(PhysicsSceneID nSceneID_);
            CJoltPhysicsScene* FindScene(PhysicsSceneID nSceneID_) noexcept;
            const CJoltPhysicsScene* FindScene(PhysicsSceneID nSceneID_) const noexcept;
            std::vector<PhysicsSceneID> ListSceneIDs() const;
            void Clear() noexcept;

        private:
            class Impl;
            std::unique_ptr<Impl> m_pImpl;
        };
    }
}
