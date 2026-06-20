#pragma once
#include "Database.h"
#include "Data/uuid.h"
#include "Data/Variant.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Database
    {
        /*
        * @brief 批量变更语义
        */
        enum class EChangeScopeKind
        {
            LoadBaseline,
            Transaction,
            UserCommand,
            Replay
        };

        struct _DATABASE_EXP CChangeEntityKey final
        {
            iCAX::Data::uuid EntityID;

            bool operator==(IN const CChangeEntityKey& Other_) const noexcept;
            bool operator<(IN const CChangeEntityKey& Other_) const noexcept;
        };

        struct _DATABASE_EXP CChangeComponentKey final
        {
            iCAX::Data::uuid EntityID;
            std::string ComponentClass;

            bool operator==(IN const CChangeComponentKey& Other_) const noexcept;
            bool operator<(IN const CChangeComponentKey& Other_) const noexcept;
        };

        struct _DATABASE_EXP CChangePropertyKey final
        {
            iCAX::Data::uuid EntityID;
            std::string ComponentClass;
            std::string PropertyName;

            bool operator==(IN const CChangePropertyKey& Other_) const noexcept;
            bool operator<(IN const CChangePropertyKey& Other_) const noexcept;
        };

        struct _DATABASE_EXP CChangeEntity final
        {
            CChangeEntityKey Key;
        };

        struct _DATABASE_EXP CChangeComponent final
        {
            CChangeComponentKey Key;
            iCAX::Data::PropertySet PreviousProperties;
            iCAX::Data::PropertySet NewProperties;
        };

        struct _DATABASE_EXP CChangeProperty final
        {
            CChangePropertyKey Key;
            iCAX::Data::PropertyValue PreviousValue;
            iCAX::Data::PropertyValue NewValue;
        };

        /*
        * @brief 冻结后的批量变更结果
        */
        struct _DATABASE_EXP CChangeSet final
        {
            EChangeScopeKind Kind = EChangeScopeKind::UserCommand;
            std::string Name;

            std::vector<CChangeEntity> CreatedEntities;
            std::vector<CChangeEntity> DeletedEntities;
            std::vector<CChangeComponent> AddedComponents;
            std::vector<CChangeComponent> RemovedComponents;
            std::vector<CChangeProperty> ModifiedProperties;

            bool IsEmpty() const;
        };

        /*
        * @brief 批量变更合并器
        */
        class _DATABASE_EXP CChangeSetBuilder final
        {
        public:
            CChangeSetBuilder(IN EChangeScopeKind Kind_, IN const std::string& strName_);
            ~CChangeSetBuilder() = default;

        public:
            void RecordAddEntity(IN const CChangeEntityKey& Key_);
            void RecordDeleteEntity(IN const CChangeEntityKey& Key_);
            void RecordAddComponent(IN const CChangeComponentKey& Key_, IN const iCAX::Data::PropertySet& NewProperties_);
            void RecordRemoveComponent(IN const CChangeComponentKey& Key_, IN const iCAX::Data::PropertySet& PreviousProperties_);
            void RecordModifyComponent(IN const CChangeComponentKey& Key_, IN const iCAX::Data::PropertySet& PreviousProperties_, IN const iCAX::Data::PropertySet& NewProperties_);

            CChangeSet Build() const;

        private:
            void EraseEntityChanges(IN const CChangeEntityKey& Key_);
            void EraseComponentChanges(IN const CChangeComponentKey& Key_);

        private:
            EChangeScopeKind m_Kind;
            std::string m_strName;
            std::map<CChangeEntityKey, CChangeEntity> m_CreatedEntities;
            std::map<CChangeEntityKey, CChangeEntity> m_DeletedEntities;
            std::map<CChangeComponentKey, CChangeComponent> m_AddedComponents;
            std::map<CChangeComponentKey, CChangeComponent> m_RemovedComponents;
            std::map<CChangePropertyKey, CChangeProperty> m_ModifiedProperties;
        };

        /*
        * @brief Repository 批处理作用域
        */
        class _DATABASE_EXP IRepositoryChangeScope
        {
        public:
            IRepositoryChangeScope() = default;
            virtual ~IRepositoryChangeScope() = default;

            virtual void Commit() = 0;
            virtual void Cancel() = 0;
            virtual bool IsCompleted() const = 0;
        };

        /*
        * @brief Repository 撤销还原记录作用域
        */
        class _DATABASE_EXP IRepositoryUndoScope
        {
        public:
            IRepositoryUndoScope() = default;
            virtual ~IRepositoryUndoScope() = default;

            virtual void End() = 0;
            virtual bool IsCompleted() const = 0;
        };

    }
}
