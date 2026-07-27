#pragma once

#include "Database.h"

#include "Data/uuid.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace iCAX::Database
{
    /*
    * @brief EntityView 成员集合变化事件参数。
    * @details
    *   事件只描述成员 Entity ID 集合的净变化，不描述 Entity 内容变化，
    *   也不包含 Scene、产品 View 或前端通知协议等上层概念。
    */
    struct _DATABASE_EXP EntityViewEventArgs final
    {
        uint64_t nPreviousRevision = 0;                         //!< 变化前的成员 Revision。
        uint64_t nRevision = 0;                                 //!< 变化后的成员 Revision。
        std::vector<iCAX::Data::uuid> AddedEntityIDs;            //!< 本次净加入的 Entity ID，按 UUID 稳定排序。
        std::vector<iCAX::Data::uuid> RemovedEntityIDs;          //!< 本次净移除的 Entity ID，按 UUID 稳定排序。
    };

    /*
    * @brief EntityView 成员集合变化事件监听者。
    */
    class _DATABASE_EXP IEntityViewEventListener
    {
    public:
        IEntityViewEventListener() = default;
        virtual ~IEntityViewEventListener() = default;

        /*
        * @brief EntityView 已完成成员集合更新。
        * @details 回调发生时，GetRevision/GetEntityIDs 已经可以读取到新状态。
        */
        virtual void OnEntityViewChanged(
            IN void* pSender_,
            IN const EntityViewEventArgs& Args_) = 0;
    };

    /*
    * @brief EntityView 成员集合变化事件发布者。
    */
    class _DATABASE_EXP IEntityViewEventPublisher
    {
    public:
        IEntityViewEventPublisher() = default;
        virtual ~IEntityViewEventPublisher() = default;

        virtual void AddObserver(
            IN std::shared_ptr<IEntityViewEventListener> Observer_) = 0;
        virtual void RemoveObserver(
            IN std::shared_ptr<IEntityViewEventListener> Observer_) = 0;
    };
}
