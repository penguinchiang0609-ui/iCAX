#pragma once

#include "Database.h"
#include "EntityWhere.h"
#include "IEntityViewEvent.h"

#include "Data/Variant.h"
#include "Data/uuid.h"

#include <cstdint>
#include <vector>

namespace iCAX::Database
{
    /*
    * @brief Repository 派生的 Entity 成员物化视图。
    * @details
    *   Where 和绑定参数在创建后不可变。成员集合由 Repository 事件增量维护，
    *   Revision 只在最终 Entity ID 集合实际变化时递增。
    */
    class _DATABASE_EXP IEntityView
        : public IEntityViewEventPublisher
    {
    public:
        IEntityView() = default;
        virtual ~IEntityView() = default;

        virtual const SEntityWhere& GetWhere() const = 0;
        virtual const iCAX::Data::ObjectMap& GetParameters() const = 0;
        virtual uint64_t GetRevision() const = 0;
        virtual std::vector<iCAX::Data::uuid> GetEntityIDs() const = 0;
        virtual bool Contains(IN const iCAX::Data::uuid& EntityID_) const = 0;
    };
}
