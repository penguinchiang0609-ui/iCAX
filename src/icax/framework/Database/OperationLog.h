#pragma once

#include "IRepositoryEvent.h"

#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Database
    {
        class IMetaRegistry;

        /*
        * @brief Repository 中的一条原子操作。
        * @details
        *   Operation 是撤销、重做、事务回滚和快速保存回放的最小记录单元。
        *   它按实际发生顺序写入 COperationBatch，不做字段合并；字段合并只发生在 ChangeSet 摘要层。
        */
        struct _DATABASE_EXP CRepositoryOperation final
        {
            RepositoryEventArgs::EventType Type = RepositoryEventArgs::kModifyComponent; //!< 操作类型。
            iCAX::Data::uuid EntityID;                                                   //!< 目标实体 ID。
            std::string ComponentClass;                                                   //!< 目标组件类型；实体级操作为空。
            iCAX::Data::PropertySet PreviousProperties;                                   //!< 操作前字段快照；修改和移除组件时使用。
            iCAX::Data::PropertySet NewProperties;                                        //!< 操作后字段快照；修改和添加组件时使用。

            /*
            * @brief 判断操作是否为空。
            * @return true 表示该操作没有可回放内容，应从批次中跳过。
            */
            bool IsEmpty() const;
        };

        /*
        * @brief 按真实发生顺序冻结的一组 Repository 操作。
        * @details
        *   COperationBatch 是日志事实来源。Undo 通过反向 Batch 回放；Redo 和快速保存恢复通过原 Batch 回放。
        *   如果需要对外通知批量净变更，应通过 BuildChangeSetFromOperationBatch 派生 ChangeSet。
        */
        struct _DATABASE_EXP COperationBatch final
        {
            EChangeScopeKind Kind = EChangeScopeKind::UserCommand;        //!< 批次语义，如普通用户命令、事务、回放等。
            std::string Name;                                              //!< 批次名称，用于日志、撤销步骤和调试展示。
            std::vector<CRepositoryOperation> Operations;                  //!< 有序操作列表，顺序不可重排。

            /*
            * @brief 判断批次是否为空。
            * @return true 表示没有任何有效操作。
            */
            bool IsEmpty() const;
        };

        /*
        * @brief 有序操作批次构建器。
        * @details
        *   与 CChangeSetBuilder 不同，本构建器只追加，不合并。它用于保留用户真实操作顺序。
        */
        class _DATABASE_EXP COperationBatchBuilder final
        {
        public:
            /*
            * @brief 构造批次构建器。
            * @param [in] Kind_ 批次语义。
            * @param [in] strName_ 批次名称。
            */
            COperationBatchBuilder(IN EChangeScopeKind Kind_, IN const std::string& strName_);
            ~COperationBatchBuilder() = default;

            /*
            * @brief 记录一条 Repository 事件。
            * @param [in] Args_ Repository 事件参数。
            */
            void RecordRepositoryEvent(IN const RepositoryEventArgs& Args_);

            /*
            * @brief 追加一条操作。
            * @param [in] Operation_ 待追加操作；空操作会被忽略。
            */
            void AppendOperation(IN const CRepositoryOperation& Operation_);

            /*
            * @brief 追加另一个批次中的全部操作。
            * @param [in] Batch_ 待追加批次；原有操作顺序会被保留。
            */
            void AppendBatch(IN const COperationBatch& Batch_);

            /*
            * @brief 冻结并返回当前批次。
            * @return 当前已收集的有序操作批次。
            */
            COperationBatch Build() const;

        private:
            COperationBatch m_Batch;
        };

        /*
        * @brief 将一条 Repository 事件转换为单操作批次。
        * @param [in] Args_ Repository 事件参数。
        * @param [in] Kind_ 批次语义。
        * @param [in] strName_ 批次名称。
        * @return 包含 0 或 1 条操作的批次。
        */
        COperationBatch MakeOperationBatchFromRepositoryEvent(IN const RepositoryEventArgs& Args_, IN EChangeScopeKind Kind_ = EChangeScopeKind::UserCommand, IN const std::string& strName_ = std::string());

        /*
        * @brief 从有序操作批次生成净变更摘要。
        * @param [in] Batch_ 有序操作批次。
        * @return 合并后的 ChangeSet，用于批量事件、版本更新和派生字段失效。
        * @remark 返回值不保留真实操作顺序，不应作为 Undo/Redo/Replay 的依据。
        */
        CChangeSet BuildChangeSetFromOperationBatch(IN const COperationBatch& Batch_);

        /*
        * @brief 过滤出可参与事务和撤销还原的操作。
        * @param [in] Batch_ 原始有序批次。
        * @return 仅包含 Transactional 值字段和结构性操作的批次。
        */
        COperationBatch FilterTransactionalOperationBatch(IN const COperationBatch& Batch_);

        /*
        * @brief 过滤出可参与事务和撤销还原的操作。
        * @param [in] Batch_ 原始有序批次。
        * @param [in] Meta_ 字段 meta 注册表。
        * @return 仅包含 Transactional 值字段和结构性操作的批次。
        */
        COperationBatch FilterTransactionalOperationBatch(IN const COperationBatch& Batch_, IN const IMetaRegistry& Meta_);

        /*
        * @brief 过滤出可写入快速保存日志的操作。
        * @param [in] Batch_ 原始有序批次。
        * @return 仅包含 Persistent + Transactional 值字段和结构性操作的批次。
        */
        COperationBatch FilterPersistentOperationBatch(IN const COperationBatch& Batch_);

        /*
        * @brief 过滤出可写入快速保存日志的操作。
        * @param [in] Batch_ 原始有序批次。
        * @param [in] Meta_ 字段 meta 注册表。
        * @return 仅包含 Persistent + Transactional 值字段和结构性操作的批次。
        */
        COperationBatch FilterPersistentOperationBatch(IN const COperationBatch& Batch_, IN const IMetaRegistry& Meta_);

        /*
        * @brief 生成反向操作批次。
        * @param [in] Batch_ 原始有序批次。
        * @param [in] strName_ 反向批次名称；为空时根据原批次名称自动生成。
        * @return 可用于撤销或事务回滚的反向批次。
        * @details
        *   反向批次会倒序遍历原操作，并交换每条操作的前后状态。
        *   这保证了字段存在顺序约束时，回滚路径仍然合法。
        */
        COperationBatch MakeInverseOperationBatch(IN const COperationBatch& Batch_, IN const std::string& strName_ = std::string());

        /*
        * @brief 将操作批次转换为 Variant。
        * @param [in] Batch_ 操作批次。
        * @return 可序列化的 Variant 对象。
        */
        iCAX::Data::Variant OperationBatchToVariant(IN const COperationBatch& Batch_);

        /*
        * @brief 从 Variant 还原操作批次。
        * @param [in] Value_ Variant 对象。
        * @return 还原后的操作批次。
        */
        COperationBatch OperationBatchFromVariant(IN const iCAX::Data::Variant& Value_);

        /*
        * @brief 序列化操作批次。
        * @param [in] Batch_ 操作批次。
        * @return 字符串形式的序列化结果。
        */
        std::string SerializeOperationBatch(IN const COperationBatch& Batch_);

        /*
        * @brief 反序列化操作批次。
        * @param [in] Text_ 字符串形式的序列化内容。
        * @return 还原后的操作批次。
        */
        COperationBatch DeserializeOperationBatch(IN const std::string& Text_);

        /*
        * @brief 追加式操作日志文件。
        * @details
        *   每行保存一个 COperationBatch。读取时遇到损坏行会停止，用于保留崩溃前最后一条完整日志。
        */
        class _DATABASE_EXP COperationBatchJournal final
        {
        public:
            COperationBatchJournal() = default;
            ~COperationBatchJournal();

            COperationBatchJournal(IN const COperationBatchJournal&) = delete;
            COperationBatchJournal& operator=(IN const COperationBatchJournal&) = delete;

            /*
            * @brief 打开日志文件。
            * @param [in] strPath_ 日志文件路径。
            * @param [in] bTruncate_ true 表示截断旧内容，false 表示追加写入。
            */
            void Open(IN const std::string& strPath_, IN const bool bTruncate_);

            /*
            * @brief 关闭日志文件，并刷新已写入内容。
            */
            void Close();

            /*
            * @brief 判断日志文件是否已打开。
            * @return true 表示当前可以追加写入。
            */
            bool IsOpen() const;

            /*
            * @brief 获取当前打开的日志路径。
            * @return 当前日志路径；未打开时为空字符串。
            */
            const std::string& GetPath() const;

            /*
            * @brief 追加一个操作批次。
            * @param [in] Batch_ 待写入批次；空批次会被忽略。
            */
            void Append(IN const COperationBatch& Batch_);

            /*
            * @brief 读取日志文件中的所有完整操作批次。
            * @param [in] strPath_ 日志文件路径。
            * @return 按文件顺序读取到的操作批次数组。
            */
            std::vector<COperationBatch> ReadAll(IN const std::string& strPath_) const;

        private:
            std::string m_strPath;
            std::ofstream m_Stream;
        };
    }
}
