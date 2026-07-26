#pragma once

#include "Database.h"
#include "EntityWhere.h"

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace iCAX::Database
{
    enum class EComponentUpdateType
    {
        Modify,
        Add,
        Remove,
    };

    /*
    * @brief 对一个组件执行的结构化 Entity 修改。
    * @details
    *   Modify 的 Properties 是字段补丁；Add 的 Properties 是初始化值；
    *   Remove 不允许携带 Properties 或 Enabled。
    */
    struct _DATABASE_EXP SComponentUpdate final
    {
        EComponentUpdateType Type = EComponentUpdateType::Modify;
        std::string ComponentClass;
        std::map<std::string, SEntityValueOperand> Properties;
        std::optional<bool> Enabled;

        bool operator==(IN const SComponentUpdate& Other_) const = default;
    };

    struct _DATABASE_EXP SEntityUpdate final
    {
        std::vector<SComponentUpdate> Components;

        bool operator==(IN const SEntityUpdate& Other_) const = default;
    };

    struct _DATABASE_EXP SEntityMutationResult final
    {
        uint64_t MatchedCount = 0;
        uint64_t ChangedCount = 0;

        bool operator==(IN const SEntityMutationResult& Other_) const = default;
    };

    class CEntityUpdateEntityExpression;

    class _DATABASE_EXP CEntityUpdateBuilder final
    {
    public:
        using Entity = CEntityUpdateEntityExpression;

        static SComponentUpdate ModifyComponent(
            IN std::string ComponentClass_,
            IN std::map<std::string, SEntityValueOperand> Properties_ = {},
            IN std::optional<bool> Enabled_ = std::nullopt);

        static SComponentUpdate AddComponent(
            IN std::string ComponentClass_,
            IN std::map<std::string, SEntityValueOperand> InitialProperties_ = {},
            IN bool bEnabled_ = true);

        static SComponentUpdate RemoveComponent(IN std::string ComponentClass_);

        static SEntityUpdate Build(IN std::vector<SComponentUpdate> Components_);
        static SEntityUpdate Build(IN std::initializer_list<SComponentUpdate> Components_);

        template<typename TComponent>
        static SComponentUpdate Modify(
            IN std::map<std::string, SEntityValueOperand> Properties_ = {},
            IN std::optional<bool> Enabled_ = std::nullopt)
        {
            static_assert(
                std::is_base_of_v<CComponentBase, TComponent>,
                "TComponent must derive from CComponentBase");
            return ModifyComponent(
                TComponent::S_ClassName,
                std::move(Properties_),
                Enabled_);
        }

        template<typename TComponent>
        static SComponentUpdate Add(
            IN std::map<std::string, SEntityValueOperand> InitialProperties_ = {},
            IN bool bEnabled_ = true)
        {
            static_assert(
                std::is_base_of_v<CComponentBase, TComponent>,
                "TComponent must derive from CComponentBase");
            return AddComponent(
                TComponent::S_ClassName,
                std::move(InitialProperties_),
                bEnabled_);
        }

        template<typename TComponent>
        static SComponentUpdate Remove()
        {
            static_assert(
                std::is_base_of_v<CComponentBase, TComponent>,
                "TComponent must derive from CComponentBase");
            return RemoveComponent(TComponent::S_ClassName);
        }

        template<typename TBuilder>
        static SEntityUpdate From(IN TBuilder&& Builder_);
    };

    class CEntityComponentUpdateExpression final
    {
    public:
        CEntityComponentUpdateExpression(
            IN std::vector<SComponentUpdate>& Components_,
            IN size_t nIndex_)
            : m_pComponents(&Components_)
            , m_nIndex(nIndex_)
        {
        }

        CEntityComponentUpdateExpression& Set(
            IN std::string PropertyName_,
            IN const CEntityValueOperandExpression& Value_)
        {
            Component().Properties.insert_or_assign(
                std::move(PropertyName_),
                Value_.GetOperand());
            return *this;
        }

        template<typename TValue>
            requires (!std::is_same_v<
                std::remove_cvref_t<TValue>,
                CEntityValueOperandExpression>)
        CEntityComponentUpdateExpression& Set(
            IN std::string PropertyName_,
            IN TValue&& Value_)
        {
            return Set(
                std::move(PropertyName_),
                CEntityValueOperandExpression(CEntityWhereBuilder::Literal(
                    EntityWhereExpressionDetail::ToPropertyValue(
                        std::forward<TValue>(Value_)))));
        }

        CEntityComponentUpdateExpression& Enabled(IN const bool bEnabled_)
        {
            if (Component().Type == EComponentUpdateType::Remove)
            {
                throw std::logic_error(
                    "Removed component cannot define an enabled state");
            }
            Component().Enabled = bEnabled_;
            return *this;
        }

    private:
        SComponentUpdate& Component()
        {
            return m_pComponents->at(m_nIndex);
        }

    private:
        std::vector<SComponentUpdate>* m_pComponents = nullptr;
        size_t m_nIndex = 0;
    };

    /*
    * @brief Update::From lambda 接收的假 Entity。
    * @details Lambda 在构造期执行一次，所有方法只记录结构化修改，不读取或修改真实 Entity。
    */
    class CEntityUpdateEntityExpression final
    {
    public:
        explicit CEntityUpdateEntityExpression(
            IN std::vector<SComponentUpdate>& Components_)
            : m_pComponents(&Components_)
        {
        }

        template<typename TComponent>
        CEntityComponentUpdateExpression Modify()
        {
            static_assert(
                std::is_base_of_v<CComponentBase, TComponent>,
                "TComponent must derive from CComponentBase");
            return Append(CEntityUpdateBuilder::Modify<TComponent>());
        }

        template<typename TComponent>
        CEntityComponentUpdateExpression Add()
        {
            static_assert(
                std::is_base_of_v<CComponentBase, TComponent>,
                "TComponent must derive from CComponentBase");
            return Append(CEntityUpdateBuilder::Add<TComponent>());
        }

        template<typename TComponent>
        void Remove()
        {
            static_assert(
                std::is_base_of_v<CComponentBase, TComponent>,
                "TComponent must derive from CComponentBase");
            m_pComponents->push_back(CEntityUpdateBuilder::Remove<TComponent>());
        }

        CEntityValueOperandExpression Parameter(
            IN std::string ParameterName_) const
        {
            return CEntityValueOperandExpression(
                CEntityWhereBuilder::Parameter(std::move(ParameterName_)));
        }

        template<typename TValue>
        CEntityValueOperandExpression Literal(IN TValue&& Value_) const
        {
            return CEntityValueOperandExpression(
                CEntityWhereBuilder::Literal(
                    EntityWhereExpressionDetail::ToPropertyValue(
                        std::forward<TValue>(Value_))));
        }

    private:
        CEntityComponentUpdateExpression Append(IN SComponentUpdate Component_)
        {
            m_pComponents->push_back(std::move(Component_));
            return CEntityComponentUpdateExpression(
                *m_pComponents,
                m_pComponents->size() - 1);
        }

    private:
        std::vector<SComponentUpdate>* m_pComponents = nullptr;
    };

    template<typename TBuilder>
    SEntityUpdate CEntityUpdateBuilder::From(IN TBuilder&& Builder_)
    {
        static_assert(
            std::is_invocable_v<TBuilder, CEntityUpdateEntityExpression&>,
            "Update lambda must accept an Entity update expression");
        static_assert(
            std::is_void_v<std::invoke_result_t<
                TBuilder,
                CEntityUpdateEntityExpression&>>,
            "Update lambda must return void");

        std::vector<SComponentUpdate> _Components;
        CEntityUpdateEntityExpression _Entity(_Components);
        std::invoke(std::forward<TBuilder>(Builder_), _Entity);
        return Build(std::move(_Components));
    }
}
