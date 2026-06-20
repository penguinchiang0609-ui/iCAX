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
        */
        enum class EDerivedPropertyState
        {
            Dirty,
            Clean,
            Computing,
            Error
        };

        class CDerivedPropertyContext;
        using DerivedPropertyEvaluator = std::function<iCAX::Data::PropertyValue(CDerivedPropertyContext&, const CComponentBase&)>;

        /*
        * @brief 派生字段计算上下文
        * @remark Evaluator 必须通过本上下文读取依赖字段，从而让 Database 自动记录依赖关系。
        */
        class _DATABASE_EXP CDerivedPropertyContext final
        {
        public:
            CDerivedPropertyContext(IN class CDerivedPropertyManager& Manager_, IN CRepository& Repository_, IN const CPropertyKey& Target_);
            ~CDerivedPropertyContext() = default;

        public:
            iCAX::Data::PropertyValue GetProperty(IN const CPropertyKey& Key_);
            iCAX::Data::PropertyValue GetProperty(IN const CComponentBase& Component_, IN const std::string& strPropertyName_);
            iCAX::Data::PropertyValue GetProperty(IN const iCAX::Data::uuid& EntityID_, IN const std::string& strComponentClass_, IN const std::string& strPropertyName_);

            const CPropertyKey& GetTarget() const;

        private:
            class CDerivedPropertyManager& m_Manager;
            CRepository& m_Repository;
            CPropertyKey m_Target;
        };

        /*
        * @brief 派生字段管理器
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
            CDerivedPropertyManager(IN CRepository& Repository_);
            ~CDerivedPropertyManager() = default;

        public:
            iCAX::Data::PropertyValue Evaluate(IN const CComponentBase& Component_, IN const std::string& strPropertyName_, IN const DerivedPropertyEvaluator& Evaluator_);
            void AddDependency(IN const CPropertyKey& Derived_, IN const CPropertyKey& Source_);
            void Invalidate(IN const CPropertyKey& Source_);
            void RemoveComponent(IN const iCAX::Data::uuid& EntityID_, IN const std::string& strComponentClass_);
            void Clear();

        private:
            CPropertyKey MakeKey(IN const CComponentBase& Component_, IN const std::string& strPropertyName_) const;
            void ClearDependencies(IN const CPropertyKey& Derived_);
            void InvalidateRecursive(IN const CPropertyKey& Source_, IN OUT std::unordered_set<CPropertyKey, CPropertyKeyHash>& Visited_);
            bool MatchComponent(IN const CPropertyKey& Key_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strComponentClass_) const;

        private:
            CRepository& m_Repository;
            std::unordered_map<CPropertyKey, CacheSlot, CPropertyKeyHash> m_Caches;
            std::unordered_map<CPropertyKey, std::unordered_set<CPropertyKey, CPropertyKeyHash>, CPropertyKeyHash> m_SourceToDerived;
            std::unordered_map<CPropertyKey, std::unordered_set<CPropertyKey, CPropertyKeyHash>, CPropertyKeyHash> m_DerivedToSources;
        };
    }
}
