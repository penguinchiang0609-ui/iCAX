#pragma once

#include "Database.h"
#include "Data/Variant.h"
#include "EntityWhere.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace iCAX::Database
{
    enum class EEntityQueryFieldType
    {
        EntityID,
        Property,
    };

    struct _DATABASE_EXP SEntityQueryField final
    {
        EEntityQueryFieldType Type = EEntityQueryFieldType::EntityID;
        std::string ComponentClass;
        std::string PropertyPath;

        bool operator==(IN const SEntityQueryField& Other_) const = default;
    };

    enum class EEntityQueryAggregate
    {
        None,
        Count,
        Sum,
        Average,
        Minimum,
        Maximum,
    };

    /*
    * @brief SELECT 的一个输出列。
    * @details CountAll 只对 COUNT(*) 有效；普通字段和聚合字段都可通过 Alias 命名。
    */
    struct _DATABASE_EXP SEntityQueryProjection final
    {
        SEntityQueryField Field;
        EEntityQueryAggregate Aggregate = EEntityQueryAggregate::None;
        bool bCountAll = false;
        std::string Alias;

        bool operator==(IN const SEntityQueryProjection& Other_) const = default;
    };

    enum class EEntityQueryOrderDirection
    {
        Ascending,
        Descending,
    };

    /*
    * @brief ORDER BY 项。
    * @details bUseProjection 为 true 时按投影别名排序，否则按 Field 排序。
    */
    struct _DATABASE_EXP SEntityQueryOrder final
    {
        bool bUseProjection = false;
        std::string ProjectionAlias;
        SEntityQueryField Field;
        EEntityQueryOrderDirection Direction =
            EEntityQueryOrderDirection::Ascending;

        bool operator==(IN const SEntityQueryOrder& Other_) const = default;
    };

    /*
    * @brief Entity 查询的结构化表达。
    * @details Projections 的第一列固定为 ENTITYID。分组查询中该列为组内 EntityID 数组。
    *   Skip/Take 必须与 OrderBy 配合，操作数在执行时绑定为非负整数。
    */
    struct _DATABASE_EXP SEntityQuery final
    {
        SEntityWhere Where;
        std::vector<SEntityQueryProjection> Projections;
        std::vector<SEntityQueryField> GroupBy;
        std::vector<SEntityQueryOrder> OrderBy;
        std::optional<SEntityValueOperand> Skip;
        std::optional<SEntityValueOperand> Take;
    };

    /*
    * @brief 表格化 Entity 查询结果。
    * @details Rows 中每行的值与 Columns 一一对应；第一列固定名为 ENTITYID。
    *   TotalCount 是应用 Skip/Take 之前的结果行数。
    */
    struct _DATABASE_EXP SEntityQueryResult final
    {
        std::vector<std::string> Columns;
        std::vector<iCAX::Data::PropertyArray> Rows;
        std::vector<iCAX::Data::uuid> EntityIDs;
        std::uint64_t TotalCount = 0;
    };
}
