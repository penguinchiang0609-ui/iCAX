#pragma once
#include "Database.h"
#include "Data/Variant.h"
#include "Data/uuid.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace iCAX
{
    namespace Database
    {
        class CComponentBase;
        class CRepository;

        /*
        * @brief 字段唯一标识
        * @details 用于派生字段缓存和依赖图，精确到 Entity/Component/Property。
        */
        struct _DATABASE_EXP CPropertyKey final
        {
            iCAX::Data::uuid EntityID;
            std::string ComponentClass;
            std::string PropertyName;

            bool operator==(IN const CPropertyKey& Other_) const noexcept;
        };

        struct _DATABASE_EXP CPropertyKeyHash final
        {
            std::size_t operator()(IN const CPropertyKey& Key_) const noexcept;
        };

        /*
        * @brief 派生字段状态
        * @details
        *   Dirty 表示需要重新计算；Computing 用于检测递归依赖；Error 表示上次计算失败。
        */
        enum class EDerivedPropertyState
        {
            Dirty,
            Clean,
            Computing,
            Error
        };

        class CDerivedPropertyContext;
        /*
        * @brief 派生字段计算函数。
        * @param [in,out] CDerivedPropertyContext& 计算上下文，用于读取并登记依赖。
        * @param [in] CComponentBase& 目标组件。
        * @return 派生字段值。
        */
        using DerivedPropertyEvaluator = std::function<iCAX::Data::PropertyValue(CDerivedPropertyContext&, const CComponentBase&)>;

        /*
        * @brief 派生字段计算上下文
        * @remark Evaluator 必须通过本上下文读取依赖字段，从而让 Database 自动记录依赖关系。
        */
        class _DATABASE_EXP CDerivedPropertyContext final
        {
        public:
            /*
            * @brief 构造派生字段计算上下文。
            * @param [in,out] Manager_ 派生字段管理器。
            * @param [in,out] Repository_ 当前 Repository。
            * @param [in] Target_ 正在计算的派生字段 key。
            */
            CDerivedPropertyContext(IN class CDerivedPropertyManager& Manager_, IN CRepository& Repository_, IN const CPropertyKey& Target_);
            ~CDerivedPropertyContext() = default;

        public:
            /*
            * @brief 读取依赖字段。
            * @param [in] Key_ 依赖字段 key。
            * @return 字段值。
            * @details 会自动把 Key_ 记录为 Target 的依赖。
            */
            iCAX::Data::PropertyValue GetProperty(IN const CPropertyKey& Key_);

            /*
            * @brief 读取指定组件上的依赖字段。
            */
            iCAX::Data::PropertyValue GetProperty(IN const CComponentBase& Component_, IN const std::string& strPropertyName_);

            /*
            * @brief 按 EntityID/ComponentClass/PropertyName 读取依赖字段。
            */
            iCAX::Data::PropertyValue GetProperty(IN const iCAX::Data::uuid& EntityID_, IN const std::string& strComponentClass_, IN const std::string& strPropertyName_);

            /*
            * @brief 获取当前正在计算的派生字段 key。
            */
            const CPropertyKey& GetTarget() const;

        private:
            class CDerivedPropertyManager& m_Manager;
            CRepository& m_Repository;
            CPropertyKey m_Target;
        };

        /*
        * @brief 派生字段管理器
        * @details
        *   管理器维护派生字段缓存和 Source->Derived 依赖图。
        *   当源字段变化时，Repository 调用 Invalidate 递归置脏所有依赖它的派生字段。
        */
        class _DATABASE_EXP CDerivedPropertyManager final
        {
        private:
            struct CacheSlot final
            {
                EDerivedPropertyState State = EDerivedPropertyState::Dirty;
                iCAX::Data::PropertyValue Value;
            };

        public:
            /*
            * @brief 构造派生字段管理器。
            */
            CDerivedPropertyManager(IN CRepository& Repository_);
            ~CDerivedPropertyManager() = default;

        public:
            /*
            * @brief 求值派生字段。
            * @param [in] Component_ 目标组件。
            * @param [in] strPropertyName_ 派生字段名称。
            * @param [in] Evaluator_ 计算器。
            * @return 派生字段值。
            * @details
            *   Clean 时直接返回缓存；Dirty 时创建 Context 重新计算并记录依赖。
            *   Computing 状态用于避免递归依赖导致无限计算。
            */
            iCAX::Data::PropertyValue Evaluate(IN const CComponentBase& Component_, IN const std::string& strPropertyName_, IN const DerivedPropertyEvaluator& Evaluator_);

            /*
            * @brief 登记派生字段依赖。
            * @param [in] Derived_ 派生字段 key。
            * @param [in] Source_ 源字段 key。
            */
            void AddDependency(IN const CPropertyKey& Derived_, IN const CPropertyKey& Source_);

            /*
            * @brief 使依赖指定源字段的派生字段递归过期。
            */
            void Invalidate(IN const CPropertyKey& Source_);

            /*
            * @brief 移除组件相关的缓存和依赖边。
            */
            void RemoveComponent(IN const iCAX::Data::uuid& EntityID_, IN const std::string& strComponentClass_);

            /*
            * @brief 清空全部缓存和依赖图。
            */
            void Clear();

        private:
            /*
            * @brief 根据组件和字段名生成 key。
            */
            CPropertyKey MakeKey(IN const CComponentBase& Component_, IN const std::string& strPropertyName_) const;

            /*
            * @brief 清理某个派生字段旧依赖。
            */
            void ClearDependencies(IN const CPropertyKey& Derived_);

            /*
            * @brief 递归置脏依赖 Source_ 的派生字段。
            */
            void InvalidateRecursive(IN const CPropertyKey& Source_, IN OUT std::unordered_set<CPropertyKey, CPropertyKeyHash>& Visited_);

            /*
            * @brief 判断 key 是否属于指定组件。
            */
            bool MatchComponent(IN const CPropertyKey& Key_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strComponentClass_) const;

        private:
            CRepository& m_Repository;
            std::unordered_map<CPropertyKey, CacheSlot, CPropertyKeyHash> m_Caches;
            std::unordered_map<CPropertyKey, std::unordered_set<CPropertyKey, CPropertyKeyHash>, CPropertyKeyHash> m_SourceToDerived;
            std::unordered_map<CPropertyKey, std::unordered_set<CPropertyKey, CPropertyKeyHash>, CPropertyKeyHash> m_DerivedToSources;
        };
    }
}
