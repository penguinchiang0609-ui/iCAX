#pragma once

#include "DatabaseLanguage.h"

#include "Database/EntityUpdate.h"
#include "Database/EntityWhere.h"
#include "Database/EntityInsert.h"
#include "Database/EntityQuery.h"
#include "Database/IRepository.h"

#include <string_view>
#include <vector>

namespace iCAX::DatabaseLanguage
{
    enum class EEntityStatementType
    {
        Query,
        Insert,
        Update,
        Delete,
    };

    struct _DATABASE_LANGUAGE_EXP SEntityStatement final
    {
        EEntityStatementType Type = EEntityStatementType::Query;
        iCAX::Database::SEntityWhere Where;
        iCAX::Database::SEntityQuery Query;
        iCAX::Database::SEntityInsert Insert;
        iCAX::Database::SEntityUpdate Update;
    };

    struct _DATABASE_LANGUAGE_EXP SEntityExecutionResult final
    {
        EEntityStatementType Type = EEntityStatementType::Query;
        std::vector<iCAX::Data::uuid> EntityIDs;
        iCAX::Database::SEntityQueryResult Query;
        iCAX::Database::SEntityInsertResult Insert;
        iCAX::Database::SEntityMutationResult Mutation;
    };

    /*
    * @brief 受控 Lambda 字符串 DSL。
    * @details 只解析声明式语法，不执行任意脚本，结果与 C++ Lambda Builder 使用同一结构。
    */
    class _DATABASE_LANGUAGE_EXP CEntityLambda final
    {
    public:
        static iCAX::Database::SEntityWhere ParseWhere(
            IN std::string_view Text_);

        static iCAX::Database::SEntityUpdate ParseUpdate(
            IN std::string_view Text_);
    };

    /*
    * @brief 面向 Entity/Component 的类 SQL 语言入口。
    */
    class _DATABASE_LANGUAGE_EXP CEntitySql final
    {
    public:
        static iCAX::Database::SEntityWhere ParseWhere(
            IN std::string_view Text_);

        static SEntityStatement Parse(IN std::string_view Text_);

        static SEntityExecutionResult Execute(
            IN iCAX::Database::IRepository& Repository_,
            IN std::string_view Text_,
            IN const iCAX::Data::ObjectMap& Parameters_ = {});
    };
}
