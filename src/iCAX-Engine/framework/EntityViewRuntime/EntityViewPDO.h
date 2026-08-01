#pragma once

#include "EntityViewRuntimeExport.h"

#include "Data/uuid.h"
#include "PDO/PDODecl.h"

#include <cstdint>
#include <vector>

namespace iCAX
{
    namespace PDO
    {
        class IPDOSlot;
    }

    namespace View
    {
        inline constexpr uint32_t kEntityViewPDOLayoutVersion = 1;
        inline constexpr uint32_t kDefaultEntityViewCapacity = 16384;

        /*
        * @brief EntityView 当前成员快照的固定布局头。
        * @details
        *   PDODecl::nVersion 表示布局版本；nRevision 表示 EntityView 内容修订。
        *   头后紧跟 MaxEntityCount 个 16-byte UUID 槽位，未使用部分始终填零。
        */
        struct _ENTITY_VIEW_RUNTIME_EXP SEntityViewPDOHeader final
        {
            uint64_t nRevision = 0;
            uint32_t nEntityCount = 0;
            uint32_t nReserved = 0;
        };

        _ENTITY_VIEW_RUNTIME_EXP uint64_t GetEntityViewPDOPayloadSize(
            IN uint32_t nMaxEntityCount_);

        _ENTITY_VIEW_RUNTIME_EXP iCAX::PDO::PDODecl MakeEntityViewPDODecl(
            IN const iCAX::Data::uuid& ViewID_,
            IN uint32_t nMaxEntityCount_);

        /*
        * @brief 把完整 Entity ID 当前快照写入固定容量 PDO。
        * @return true 表示本次取得写缓冲并发布；false 表示相同/更新 revision 已在槽中或写缓冲暂不可用。
        * @throws std::length_error 成员数量超过固定容量；不会截断。
        */
        _ENTITY_VIEW_RUNTIME_EXP bool WriteEntityViewPDO(
            IN iCAX::PDO::IPDOSlot& Slot_,
            IN uint64_t nRevision_,
            IN const std::vector<iCAX::Data::uuid>& EntityIDs_);
    }
}
