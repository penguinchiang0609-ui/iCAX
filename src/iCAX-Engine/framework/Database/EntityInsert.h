#pragma once

#include "Database.h"
#include "EntityUpdate.h"

#include <optional>

namespace iCAX::Database
{
    /*
    * @brief 创建一个 Entity 的结构化表达。
    * @details EntityID 省略时由 Database 生成；Initializer 只允许包含 Add 组件操作。
    */
    struct _DATABASE_EXP SEntityInsert final
    {
        std::optional<SEntityValueOperand> EntityID;
        SEntityUpdate Initializer;
    };

    struct _DATABASE_EXP SEntityInsertResult final
    {
        iCAX::Data::uuid EntityID;
    };
}
