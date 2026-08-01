#pragma once

#include "EntityViewPDO.h"

#include "Data/Variant.h"
#include "Database/EntityWhere.h"

#include <cstddef>
#include <memory>

namespace iCAX
{
    namespace Database
    {
        class IRepository;
    }

    namespace PDO
    {
        class IPDOHub;
    }

    namespace View
    {
        using EntityViewID = iCAX::Data::uuid;

        struct _ENTITY_VIEW_RUNTIME_EXP SEntityViewHandle final
        {
            EntityViewID ViewID;
            uint64_t nRevision = 0;
            iCAX::PDO::PDOID nPDOID = 0;
            uint32_t nPDOLayoutVersion = kEntityViewPDOLayoutVersion;
            uint64_t nPDOPayloadSize = 0;
            uint32_t nMaxEntityCount = 0;
        };

        /*
        * @brief 当前 Scene 持有的 EntityView 及其公开 PDO 集合。
        * @details
        *   它是 Scene 的运行期子对象，不是 Service；不跨 Scene 保存状态。
        *   帧内只由 Database EntityView 维护成员状态；每帧末尾由 Scene 统一调用
        *   Publish 写入 PDO，再由 PDOHub 在帧边界一次性对外交换。
        *   Scene 必须在 Repository 和 PDOHub 之前销毁本对象。
        */
        class _ENTITY_VIEW_RUNTIME_EXP CEntityViewSet final
        {
        public:
            CEntityViewSet(
                IN iCAX::Database::IRepository& Repository_,
                IN iCAX::PDO::IPDOHub& PDOHub_);
            ~CEntityViewSet();

            CEntityViewSet(const CEntityViewSet&) = delete;
            CEntityViewSet& operator=(const CEntityViewSet&) = delete;

            SEntityViewHandle GetOrCreate(
                IN const iCAX::Database::SEntityWhere& Where_,
                IN const iCAX::Data::ObjectMap& Parameters_ = {});

            /*
            * @brief 释放一次 GetOrCreate 取得的使用权。
            * @details 使用计数归零时释放物化视图及其公开 PDO 端口。
            */
            bool Release(IN const EntityViewID& ViewID_);

            /*
            * @brief 在当前帧末尾把所有 EntityView 的完整成员快照写入各自 PDO。
            */
            void Publish();

            /*
            * @brief 释放当前 Scene 尚未归还的所有 EntityView 使用权和 PDO。
            */
            void Clear() noexcept;

            size_t Size() const noexcept;

        private:
            struct SImpl;
            std::unique_ptr<SImpl> m_pImpl;
        };
    }
}
