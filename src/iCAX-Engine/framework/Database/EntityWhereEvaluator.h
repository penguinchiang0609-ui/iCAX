#pragma once

#include "EntityWhere.h"

#include <map>
#include <memory>
#include <set>
#include <string>

namespace iCAX::Database
{
    class CRepository;
    class IEntity;

    /*
    * @brief Database 内部统一的 Where 求值器。
    * @details Query、EntityView、Update 和 Delete 必须复用该求值器，保证选择语义完全一致。
    */
    class CEntityWhereEvaluator final
    {
    public:
        struct SDependencySet final
        {
            bool bEntityExistence = false;
            std::set<std::string> ComponentPresence;
            std::set<std::string> ComponentState;
            std::map<std::string, std::set<std::string>> Properties;
        };

        struct SEvaluationResult final
        {
            bool bMatches = false;
            std::map<iCAX::Data::uuid, SDependencySet> Dependencies;
        };

        CEntityWhereEvaluator(
            IN std::shared_ptr<CRepository> pRepository_,
            IN SEntityWhere Where_,
            IN iCAX::Data::ObjectMap Parameters_);

        const SEntityWhere& GetWhere() const;
        const iCAX::Data::ObjectMap& GetParameters() const;
        SEvaluationResult EvaluateEntity(IN const iCAX::Data::uuid& EntityID_) const;

    private:
        bool EvaluateNode(
            IN const SEntityWhereNode& Node_,
            IN const IEntity& Entity_,
            IN OUT std::map<iCAX::Data::uuid, SDependencySet>& Dependencies_) const;

    private:
        std::weak_ptr<CRepository> m_pRepository;
        SEntityWhere m_Where;
        iCAX::Data::ObjectMap m_Parameters;
    };
}
