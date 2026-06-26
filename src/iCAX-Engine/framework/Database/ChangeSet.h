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
        * @details
        *   Kind 描述一组操作的来源语义。它不等价于撤销还原步骤，Undo 由 BeginUndoCommand/End 决定。
        */
        enum class EOperationBatchKind
        {
            LoadBaseline, //!< 静默加载基线数据，不进入历史或日志。
            Transaction,  //!< 事务作用域。
            UserCommand,  //!< 普通用户命令或批量命令。
            Replay        //!< Undo/Redo/快速保存恢复等回放操作。
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

        struct _DATABASE_EXP CChangeComponentState final
        {
            CChangeComponentKey Key;
            bool PreviousEnabled = true;
            bool NewEnabled = true;
        };

        /*
        * @brief 冻结后的批量变更结果
        * @details
        *   CChangeSet 是 OperationBatch 派生出的“净变更摘要”。
        *   它适合用于批量事件、版本更新和派生字段失效，不保留真实操作顺序。
        *   撤销、重做、事务回滚和快速保存必须使用 OperationBatch。
        */
        struct _DATABASE_EXP CChangeSet final
        {
            EOperationBatchKind Kind = EOperationBatchKind::UserCommand;
            std::string Name;

            std::vector<CChangeEntity> CreatedEntities;
            std::vector<CChangeEntity> DeletedEntities;
            std::vector<CChangeComponent> AddedComponents;
            std::vector<CChangeComponent> RemovedComponents;
            std::vector<CChangeProperty> ModifiedProperties;
            std::vector<CChangeComponentState> ModifiedComponentStates;

            bool IsEmpty() const;
        };

        /*
        * @brief 批量变更合并器
        * @details
        *   Builder 会合并同一实体/组件/字段上的多次变化，只保留批量结束后的净效果。
        *   因此它不能用作回放日志。
        */
        class _DATABASE_EXP CChangeSetBuilder final
        {
        public:
            /*
            * @brief 构造 ChangeSet 合并器。
            * @param [in] Kind_ 批次语义。
            * @param [in] strName_ 批次名称。
            */
            CChangeSetBuilder(IN EOperationBatchKind Kind_, IN const std::string& strName_);
            ~CChangeSetBuilder() = default;

        public:
            /*
            * @brief 记录新增实体。
            */
            void RecordAddEntity(IN const CChangeEntityKey& Key_);
            /*
            * @brief 记录删除实体。
            */
            void RecordDeleteEntity(IN const CChangeEntityKey& Key_);
            /*
            * @brief 记录新增组件。
            */
            void RecordAddComponent(IN const CChangeComponentKey& Key_, IN const iCAX::Data::PropertySet& NewProperties_);
            /*
            * @brief 记录移除组件。
            */
            void RecordRemoveComponent(IN const CChangeComponentKey& Key_, IN const iCAX::Data::PropertySet& PreviousProperties_);
            /*
            * @brief 记录修改组件。
            */
            void RecordModifyComponent(IN const CChangeComponentKey& Key_, IN const iCAX::Data::PropertySet& PreviousProperties_, IN const iCAX::Data::PropertySet& NewProperties_);
            /*
            * @brief 记录组件启用状态变化。
            */
            void RecordComponentEnabledState(IN const CChangeComponentKey& Key_, IN const bool bPreviousEnabled_, IN const bool bNewEnabled_);

            /*
            * @brief 生成净变更摘要。
            */
            CChangeSet Build() const;

        private:
            void EraseEntityChanges(IN const CChangeEntityKey& Key_);
            void EraseComponentChanges(IN const CChangeComponentKey& Key_);

        private:
            EOperationBatchKind m_Kind;
            std::string m_strName;
            std::map<CChangeEntityKey, CChangeEntity> m_CreatedEntities;
            std::map<CChangeEntityKey, CChangeEntity> m_DeletedEntities;
            std::map<CChangeComponentKey, CChangeComponent> m_AddedComponents;
            std::map<CChangeComponentKey, CChangeComponent> m_RemovedComponents;
            std::map<CChangePropertyKey, CChangeProperty> m_ModifiedProperties;
            std::map<CChangeComponentKey, CChangeComponentState> m_ModifiedComponentStates;
        };

    }
}
