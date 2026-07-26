#pragma once

// Entity 选择条件的结构化表达与类型安全构造入口。

#include "Database.h"
#include "ComponentBase.h"

#include "Data/Variant.h"
#include "Data/uuid.h"

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace iCAX::Database
{
    /*
    * @brief Entity 查询表达式节点类型。
    * @details
    *   查询是纯数据描述，可被规范化、缓存和增量维护。
    *   Where::From 接受的 lambda 只用于构造这些节点，不是对真实 Entity 执行的任意谓词。
    */
    enum class EEntityWhereNodeType
    {
        Constant,
        HasComponent,
        ComponentEnabled,
        PropertyComparison,
        All,
        Any,
        Not,
        Reference,
    };

    enum class EEntityWhereComparison
    {
        Equal,
        NotEqual,
        Less,
        LessOrEqual,
        Greater,
        GreaterOrEqual,
        Contains,
        StartsWith,
        EndsWith,
        In,
    };

    enum class EEntityValueOperandType
    {
        Literal,
        Parameter,
    };

    enum class EEntityReferenceMatch
    {
        Any,
        All,
    };

    struct _DATABASE_EXP SEntityValueOperand final
    {
        EEntityValueOperandType Type = EEntityValueOperandType::Literal;
        iCAX::Data::PropertyValue Literal;
        std::string ParameterName;

        bool operator==(IN const SEntityValueOperand& Other_) const = default;
        bool operator<(IN const SEntityValueOperand& Other_) const
        {
            return std::tie(Type, Literal, ParameterName)
                < std::tie(Other_.Type, Other_.Literal, Other_.ParameterName);
        }
    };

    /*
    * @brief 一棵声明式 Entity 查询表达式。
    * @details
    *   PropertyPath 的第一个片段是组件顶层字段名，其余片段使用 Variant 路径语法。
    *   Reference 从 uuid、uuid 字符串或它们的 VariantArray 中取得目标 Entity，
    *   再用唯一子节点匹配目标 Entity。
    */
    struct _DATABASE_EXP SEntityWhereNode final
    {
        EEntityWhereNodeType Type = EEntityWhereNodeType::Constant;
        bool bConstant = true;
        std::string ComponentClass;
        std::string PropertyPath;
        EEntityWhereComparison Comparison = EEntityWhereComparison::Equal;
        SEntityValueOperand Operand;
        EEntityReferenceMatch ReferenceMatch = EEntityReferenceMatch::Any;
        std::vector<SEntityWhereNode> Children;

        bool operator==(IN const SEntityWhereNode& Other_) const = default;
        bool operator<(IN const SEntityWhereNode& Other_) const
        {
            return std::tie(
                Type,
                bConstant,
                ComponentClass,
                PropertyPath,
                Comparison,
                Operand,
                ReferenceMatch,
                Children)
                < std::tie(
                    Other_.Type,
                    Other_.bConstant,
                    Other_.ComponentClass,
                    Other_.PropertyPath,
                    Other_.Comparison,
                    Other_.Operand,
                    Other_.ReferenceMatch,
                    Other_.Children);
        }
    };

    struct _DATABASE_EXP SEntityWhere final
    {
        SEntityWhereNode Root;

        bool operator==(IN const SEntityWhere& Other_) const = default;
        bool operator<(IN const SEntityWhere& Other_) const
        {
            return Root < Other_.Root;
        }
    };

    class CEntityWhereEntityExpression;

    /*
    * @brief Entity 查询的类型安全构造入口。
    * @details 组件类型由 TComponent::S_ClassName 提供；业务代码不需要手写组件类名。
    */
    class _DATABASE_EXP CEntityWhereBuilder final
    {
    public:
        using Entity = CEntityWhereEntityExpression;

        static SEntityValueOperand Literal(IN iCAX::Data::PropertyValue Value_);
        static SEntityValueOperand Parameter(IN std::string ParameterName_);

        static SEntityWhereNode Constant(IN bool bValue_);
        static SEntityWhereNode HasComponent(IN std::string ComponentClass_);
        static SEntityWhereNode ComponentEnabled(IN std::string ComponentClass_);
        static SEntityWhereNode Compare(
            IN std::string ComponentClass_,
            IN std::string PropertyPath_,
            IN EEntityWhereComparison Comparison_,
            IN SEntityValueOperand Operand_);
        static SEntityWhereNode All(IN std::vector<SEntityWhereNode> Children_);
        static SEntityWhereNode All(IN std::initializer_list<SEntityWhereNode> Children_);
        static SEntityWhereNode Any(IN std::vector<SEntityWhereNode> Children_);
        static SEntityWhereNode Any(IN std::initializer_list<SEntityWhereNode> Children_);
        static SEntityWhereNode Not(IN SEntityWhereNode Child_);
        static SEntityWhereNode Reference(
            IN std::string ComponentClass_,
            IN std::string PropertyPath_,
            IN SEntityWhereNode TargetPredicate_,
            IN EEntityReferenceMatch Match_ = EEntityReferenceMatch::Any);
        static SEntityWhere Build(IN SEntityWhereNode Root_);
        static SEntityWhere MatchAll();

        /*
        * @brief 执行一次类型安全表达式 lambda，并把返回的代理表达式构造成查询 AST。
        */
        template<typename TBuilder>
        static SEntityWhere From(IN TBuilder&& Builder_);

        static SEntityWhere Normalize(IN const SEntityWhere& Where_);
        static iCAX::Data::ObjectMap BindParameters(
            IN const SEntityWhere& Where_,
            IN const iCAX::Data::ObjectMap& Parameters_);

        template<typename TComponent>
        static SEntityWhereNode Has()
        {
            static_assert(
                std::is_base_of_v<CComponentBase, TComponent>,
                "TComponent must derive from CComponentBase");
            return HasComponent(TComponent::S_ClassName);
        }

        template<typename TComponent>
        static SEntityWhereNode Enabled()
        {
            static_assert(
                std::is_base_of_v<CComponentBase, TComponent>,
                "TComponent must derive from CComponentBase");
            return ComponentEnabled(TComponent::S_ClassName);
        }

        template<typename TComponent>
        static SEntityWhereNode Compare(
            IN std::string PropertyPath_,
            IN EEntityWhereComparison Comparison_,
            IN iCAX::Data::PropertyValue Literal_)
        {
            static_assert(
                std::is_base_of_v<CComponentBase, TComponent>,
                "TComponent must derive from CComponentBase");
            return Compare(
                TComponent::S_ClassName,
                std::move(PropertyPath_),
                Comparison_,
                Literal(std::move(Literal_)));
        }

        template<typename TComponent>
        static SEntityWhereNode CompareParameter(
            IN std::string PropertyPath_,
            IN EEntityWhereComparison Comparison_,
            IN std::string ParameterName_)
        {
            static_assert(
                std::is_base_of_v<CComponentBase, TComponent>,
                "TComponent must derive from CComponentBase");
            return Compare(
                TComponent::S_ClassName,
                std::move(PropertyPath_),
                Comparison_,
                Parameter(std::move(ParameterName_)));
        }

        template<typename TComponent>
        static SEntityWhereNode Reference(
            IN std::string PropertyPath_,
            IN SEntityWhereNode TargetPredicate_,
            IN EEntityReferenceMatch Match_ = EEntityReferenceMatch::Any)
        {
            static_assert(
                std::is_base_of_v<CComponentBase, TComponent>,
                "TComponent must derive from CComponentBase");
            return Reference(
                TComponent::S_ClassName,
                std::move(PropertyPath_),
                std::move(TargetPredicate_),
                Match_);
        }
    };

    namespace EntityWhereExpressionDetail
    {
        template<typename TValue>
        iCAX::Data::PropertyValue ToPropertyValue(IN TValue&& Value_)
        {
            using TDecayed = std::remove_cvref_t<TValue>;
            if constexpr (std::is_same_v<TDecayed, iCAX::Data::PropertyValue>)
            {
                return std::forward<TValue>(Value_);
            }
            else if constexpr (std::is_same_v<TDecayed, std::string_view>)
            {
                return iCAX::Data::PropertyValue(std::string(Value_));
            }
            else if constexpr (
                std::is_same_v<TDecayed, const char*>
                || std::is_same_v<TDecayed, char*>)
            {
                return iCAX::Data::PropertyValue(
                    std::string(Value_ ? Value_ : ""));
            }
            else if constexpr (
                std::is_array_v<std::remove_reference_t<TValue>>
                && std::is_same_v<
                    std::remove_cv_t<std::remove_extent_t<std::remove_reference_t<TValue>>>,
                    char>)
            {
                return iCAX::Data::PropertyValue(std::string(Value_));
            }
            else
            {
                return iCAX::Data::PropertyValue(std::forward<TValue>(Value_));
            }
        }
    }

    /*
    * @brief Lambda 查询中的右操作数代理。
    */
    class CEntityValueOperandExpression final
    {
    public:
        explicit CEntityValueOperandExpression(IN SEntityValueOperand Operand_)
            : m_Operand(std::move(Operand_))
        {
        }

        const SEntityValueOperand& GetOperand() const
        {
            return m_Operand;
        }

    private:
        SEntityValueOperand m_Operand;
    };

    /*
    * @brief Lambda 查询返回的布尔表达式代理。
    * @details 该类型不能转换为 bool，避免把构造期表达式误当作真实 Entity 条件分支。
    */
    class CEntityWhereExpression final
    {
    public:
        explicit CEntityWhereExpression(IN SEntityWhereNode Node_)
            : m_Node(std::move(Node_))
        {
        }

        explicit operator bool() const = delete;

        SEntityWhereNode TakeNode() &&
        {
            return std::move(m_Node);
        }

        const SEntityWhereNode& GetNode() const
        {
            return m_Node;
        }

        friend CEntityWhereExpression operator&&(
            IN CEntityWhereExpression Left_,
            IN CEntityWhereExpression Right_)
        {
            std::vector<SEntityWhereNode> _Children;
            _Children.reserve(2);
            _Children.push_back(std::move(Left_).TakeNode());
            _Children.push_back(std::move(Right_).TakeNode());
            return CEntityWhereExpression(
                CEntityWhereBuilder::All(std::move(_Children)));
        }

        friend CEntityWhereExpression operator||(
            IN CEntityWhereExpression Left_,
            IN CEntityWhereExpression Right_)
        {
            std::vector<SEntityWhereNode> _Children;
            _Children.reserve(2);
            _Children.push_back(std::move(Left_).TakeNode());
            _Children.push_back(std::move(Right_).TakeNode());
            return CEntityWhereExpression(
                CEntityWhereBuilder::Any(std::move(_Children)));
        }

        friend CEntityWhereExpression operator!(
            IN CEntityWhereExpression Expression_)
        {
            return CEntityWhereExpression(CEntityWhereBuilder::Not(
                std::move(Expression_).TakeNode()));
        }

    private:
        SEntityWhereNode m_Node;
    };

    /*
    * @brief Lambda 查询中的组件字段代理。
    */
    class CEntityWherePropertyExpression final
    {
    public:
        CEntityWherePropertyExpression(
            IN std::string ComponentClass_,
            IN std::string PropertyPath_)
            : m_ComponentClass(std::move(ComponentClass_))
            , m_PropertyPath(std::move(PropertyPath_))
        {
        }

        CEntityWhereExpression Equal(
            IN const CEntityValueOperandExpression& Right_) const
        {
            return Compare(EEntityWhereComparison::Equal, Right_);
        }

        CEntityWhereExpression NotEqual(
            IN const CEntityValueOperandExpression& Right_) const
        {
            return Compare(EEntityWhereComparison::NotEqual, Right_);
        }

        CEntityWhereExpression Less(
            IN const CEntityValueOperandExpression& Right_) const
        {
            return Compare(EEntityWhereComparison::Less, Right_);
        }

        CEntityWhereExpression LessOrEqual(
            IN const CEntityValueOperandExpression& Right_) const
        {
            return Compare(EEntityWhereComparison::LessOrEqual, Right_);
        }

        CEntityWhereExpression Greater(
            IN const CEntityValueOperandExpression& Right_) const
        {
            return Compare(EEntityWhereComparison::Greater, Right_);
        }

        CEntityWhereExpression GreaterOrEqual(
            IN const CEntityValueOperandExpression& Right_) const
        {
            return Compare(EEntityWhereComparison::GreaterOrEqual, Right_);
        }

        CEntityWhereExpression Contains(
            IN const CEntityValueOperandExpression& Right_) const
        {
            return Compare(EEntityWhereComparison::Contains, Right_);
        }

        CEntityWhereExpression StartsWith(
            IN const CEntityValueOperandExpression& Right_) const
        {
            return Compare(EEntityWhereComparison::StartsWith, Right_);
        }

        CEntityWhereExpression EndsWith(
            IN const CEntityValueOperandExpression& Right_) const
        {
            return Compare(EEntityWhereComparison::EndsWith, Right_);
        }

        CEntityWhereExpression In(
            IN const CEntityValueOperandExpression& Right_) const
        {
            return Compare(EEntityWhereComparison::In, Right_);
        }

        template<typename TValue>
            requires (!std::is_same_v<
                std::remove_cvref_t<TValue>,
                CEntityValueOperandExpression>)
        CEntityWhereExpression Equal(IN TValue&& Right_) const
        {
            return Equal(MakeLiteral(std::forward<TValue>(Right_)));
        }

        template<typename TValue>
            requires (!std::is_same_v<
                std::remove_cvref_t<TValue>,
                CEntityValueOperandExpression>)
        CEntityWhereExpression NotEqual(IN TValue&& Right_) const
        {
            return NotEqual(MakeLiteral(std::forward<TValue>(Right_)));
        }

        template<typename TValue>
            requires (!std::is_same_v<
                std::remove_cvref_t<TValue>,
                CEntityValueOperandExpression>)
        CEntityWhereExpression Less(IN TValue&& Right_) const
        {
            return Less(MakeLiteral(std::forward<TValue>(Right_)));
        }

        template<typename TValue>
            requires (!std::is_same_v<
                std::remove_cvref_t<TValue>,
                CEntityValueOperandExpression>)
        CEntityWhereExpression LessOrEqual(IN TValue&& Right_) const
        {
            return LessOrEqual(MakeLiteral(std::forward<TValue>(Right_)));
        }

        template<typename TValue>
            requires (!std::is_same_v<
                std::remove_cvref_t<TValue>,
                CEntityValueOperandExpression>)
        CEntityWhereExpression Greater(IN TValue&& Right_) const
        {
            return Greater(MakeLiteral(std::forward<TValue>(Right_)));
        }

        template<typename TValue>
            requires (!std::is_same_v<
                std::remove_cvref_t<TValue>,
                CEntityValueOperandExpression>)
        CEntityWhereExpression GreaterOrEqual(IN TValue&& Right_) const
        {
            return GreaterOrEqual(MakeLiteral(std::forward<TValue>(Right_)));
        }

        template<typename TValue>
            requires (!std::is_same_v<
                std::remove_cvref_t<TValue>,
                CEntityValueOperandExpression>)
        CEntityWhereExpression Contains(IN TValue&& Right_) const
        {
            return Contains(MakeLiteral(std::forward<TValue>(Right_)));
        }

        template<typename TValue>
            requires (!std::is_same_v<
                std::remove_cvref_t<TValue>,
                CEntityValueOperandExpression>)
        CEntityWhereExpression StartsWith(IN TValue&& Right_) const
        {
            return StartsWith(MakeLiteral(std::forward<TValue>(Right_)));
        }

        template<typename TValue>
            requires (!std::is_same_v<
                std::remove_cvref_t<TValue>,
                CEntityValueOperandExpression>)
        CEntityWhereExpression EndsWith(IN TValue&& Right_) const
        {
            return EndsWith(MakeLiteral(std::forward<TValue>(Right_)));
        }

        template<typename TValue>
            requires (!std::is_same_v<
                std::remove_cvref_t<TValue>,
                CEntityValueOperandExpression>)
        CEntityWhereExpression In(IN TValue&& Right_) const
        {
            return In(MakeLiteral(std::forward<TValue>(Right_)));
        }

        CEntityWhereExpression operator==(
            IN const CEntityValueOperandExpression& Right_) const
        {
            return Equal(Right_);
        }

        CEntityWhereExpression operator!=(
            IN const CEntityValueOperandExpression& Right_) const
        {
            return NotEqual(Right_);
        }

        CEntityWhereExpression operator<(
            IN const CEntityValueOperandExpression& Right_) const
        {
            return Less(Right_);
        }

        CEntityWhereExpression operator<=(
            IN const CEntityValueOperandExpression& Right_) const
        {
            return LessOrEqual(Right_);
        }

        CEntityWhereExpression operator>(
            IN const CEntityValueOperandExpression& Right_) const
        {
            return Greater(Right_);
        }

        CEntityWhereExpression operator>=(
            IN const CEntityValueOperandExpression& Right_) const
        {
            return GreaterOrEqual(Right_);
        }

        template<typename TValue>
            requires (!std::is_same_v<
                std::remove_cvref_t<TValue>,
                CEntityValueOperandExpression>)
        CEntityWhereExpression operator==(IN TValue&& Right_) const
        {
            return Equal(std::forward<TValue>(Right_));
        }

        template<typename TValue>
            requires (!std::is_same_v<
                std::remove_cvref_t<TValue>,
                CEntityValueOperandExpression>)
        CEntityWhereExpression operator!=(IN TValue&& Right_) const
        {
            return NotEqual(std::forward<TValue>(Right_));
        }

        template<typename TValue>
            requires (!std::is_same_v<
                std::remove_cvref_t<TValue>,
                CEntityValueOperandExpression>)
        CEntityWhereExpression operator<(IN TValue&& Right_) const
        {
            return Less(std::forward<TValue>(Right_));
        }

        template<typename TValue>
            requires (!std::is_same_v<
                std::remove_cvref_t<TValue>,
                CEntityValueOperandExpression>)
        CEntityWhereExpression operator<=(IN TValue&& Right_) const
        {
            return LessOrEqual(std::forward<TValue>(Right_));
        }

        template<typename TValue>
            requires (!std::is_same_v<
                std::remove_cvref_t<TValue>,
                CEntityValueOperandExpression>)
        CEntityWhereExpression operator>(IN TValue&& Right_) const
        {
            return Greater(std::forward<TValue>(Right_));
        }

        template<typename TValue>
            requires (!std::is_same_v<
                std::remove_cvref_t<TValue>,
                CEntityValueOperandExpression>)
        CEntityWhereExpression operator>=(IN TValue&& Right_) const
        {
            return GreaterOrEqual(std::forward<TValue>(Right_));
        }

    private:
        CEntityWhereExpression Compare(
            IN EEntityWhereComparison Comparison_,
            IN const CEntityValueOperandExpression& Right_) const
        {
            return CEntityWhereExpression(CEntityWhereBuilder::Compare(
                m_ComponentClass,
                m_PropertyPath,
                Comparison_,
                Right_.GetOperand()));
        }

        template<typename TValue>
        static CEntityValueOperandExpression MakeLiteral(IN TValue&& Value_)
        {
            return CEntityValueOperandExpression(
                CEntityWhereBuilder::Literal(
                    EntityWhereExpressionDetail::ToPropertyValue(
                        std::forward<TValue>(Value_))));
        }

    private:
        std::string m_ComponentClass;
        std::string m_PropertyPath;
    };

    class CEntityWhereReferenceExpression final
    {
    public:
        CEntityWhereReferenceExpression(
            IN std::string ComponentClass_,
            IN std::string PropertyPath_)
            : m_ComponentClass(std::move(ComponentClass_))
            , m_PropertyPath(std::move(PropertyPath_))
        {
        }

        template<typename TBuilder>
        CEntityWhereExpression Any(IN TBuilder&& Builder_) const;

        template<typename TBuilder>
        CEntityWhereExpression All(IN TBuilder&& Builder_) const;

    private:
        template<typename TBuilder>
        CEntityWhereExpression Build(
            IN TBuilder&& Builder_,
            IN EEntityReferenceMatch Match_) const;

    private:
        std::string m_ComponentClass;
        std::string m_PropertyPath;
    };

    /*
    * @brief Where::From lambda 接收的假 Entity。
    * @details 所有方法只生成 AST，不读取 Repository 或真实 Entity。
    */
    class CEntityWhereEntityExpression final
    {
    public:
        CEntityWhereExpression Constant(IN const bool bValue_) const
        {
            return CEntityWhereExpression(
                CEntityWhereBuilder::Constant(bValue_));
        }

        template<typename TComponent>
        CEntityWhereExpression Has() const
        {
            return CEntityWhereExpression(
                CEntityWhereBuilder::Has<TComponent>());
        }

        template<typename TComponent>
        CEntityWhereExpression Enabled() const
        {
            return CEntityWhereExpression(
                CEntityWhereBuilder::Enabled<TComponent>());
        }

        template<typename TComponent>
        CEntityWherePropertyExpression Field(
            IN std::string PropertyPath_) const
        {
            static_assert(
                std::is_base_of_v<CComponentBase, TComponent>,
                "TComponent must derive from CComponentBase");
            return CEntityWherePropertyExpression(
                TComponent::S_ClassName,
                std::move(PropertyPath_));
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

        template<typename TComponent>
        CEntityWhereReferenceExpression Ref(
            IN std::string PropertyPath_) const
        {
            static_assert(
                std::is_base_of_v<CComponentBase, TComponent>,
                "TComponent must derive from CComponentBase");
            return CEntityWhereReferenceExpression(
                TComponent::S_ClassName,
                std::move(PropertyPath_));
        }

        template<typename TComponent>
        CEntityWhereReferenceExpression Reference(
            IN std::string PropertyPath_) const
        {
            return Ref<TComponent>(std::move(PropertyPath_));
        }
    };

    template<typename TBuilder>
    CEntityWhereExpression CEntityWhereReferenceExpression::Any(
        IN TBuilder&& Builder_) const
    {
        return Build(std::forward<TBuilder>(Builder_), EEntityReferenceMatch::Any);
    }

    template<typename TBuilder>
    CEntityWhereExpression CEntityWhereReferenceExpression::All(
        IN TBuilder&& Builder_) const
    {
        return Build(std::forward<TBuilder>(Builder_), EEntityReferenceMatch::All);
    }

    template<typename TBuilder>
    CEntityWhereExpression CEntityWhereReferenceExpression::Build(
        IN TBuilder&& Builder_,
        IN const EEntityReferenceMatch Match_) const
    {
        static_assert(
            std::is_invocable_v<TBuilder, CEntityWhereEntityExpression&>,
            "Reference lambda must accept an Entity where expression");
        using TResult = std::invoke_result_t<
            TBuilder,
            CEntityWhereEntityExpression&>;
        static_assert(
            std::is_same_v<std::remove_cvref_t<TResult>, CEntityWhereExpression>,
            "Reference lambda must return a CEntityWhereExpression");

        CEntityWhereEntityExpression _Target;
        auto _TargetExpression = std::invoke(
            std::forward<TBuilder>(Builder_),
            _Target);
        return CEntityWhereExpression(CEntityWhereBuilder::Reference(
            m_ComponentClass,
            m_PropertyPath,
            std::move(_TargetExpression).TakeNode(),
            Match_));
    }

    template<typename TBuilder>
    SEntityWhere CEntityWhereBuilder::From(IN TBuilder&& Builder_)
    {
        static_assert(
            std::is_invocable_v<TBuilder, CEntityWhereEntityExpression&>,
            "Where lambda must accept an Entity where expression");
        using TResult = std::invoke_result_t<
            TBuilder,
            CEntityWhereEntityExpression&>;
        static_assert(
            std::is_same_v<std::remove_cvref_t<TResult>, CEntityWhereExpression>,
            "Where lambda must return a CEntityWhereExpression");

        CEntityWhereEntityExpression _Entity;
        auto _Expression = std::invoke(
            std::forward<TBuilder>(Builder_),
            _Entity);
        return Build(std::move(_Expression).TakeNode());
    }

}
