#pragma once

#include "IRepository.h"
#include "OperationLog.h"
#include "Data/uuid.h"

#include <deque>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace iCAX
{
    namespace Database
    {
        class IMetaRegistry;

        /*
        * @brief Repository 外挂式撤销重做历史记录器
        * @details
        *   只维护撤销重做相关状态，不侵入 Repository、Entity、Component。
        */
        class _DATABASE_EXP CRepositoryUndoRedoHistory final
        {
        public:
            CRepositoryUndoRedoHistory() = delete;

            /*
            * @brief 构造历史记录器。
            * @param [in] pMetaRegistry_ 字段 meta 注册表；必须显式传入。
            */
            explicit CRepositoryUndoRedoHistory(IN std::shared_ptr<IMetaRegistry> pMetaRegistry_);
            ~CRepositoryUndoRedoHistory() = default;

            CRepositoryUndoRedoHistory(IN const CRepositoryUndoRedoHistory&) = delete;
            CRepositoryUndoRedoHistory& operator=(IN const CRepositoryUndoRedoHistory&) = delete;

            /*
            * @brief 开始记录一个撤销还原步骤。
            * @param [in] strName_ 步骤名称，不允许为空。
            * @return 撤销记录作用域；调用 End 或析构时结束记录。
            */
            std::unique_ptr<IRepositoryUndoScope> BeginCommand(IN const std::string& strName_);

            /*
            * @brief 判断当前是否正在记录撤销还原步骤。
            * @return true 表示 BeginCommand 后尚未 End。
            */
            bool IsRecording() const;

            /*
            * @brief 处理 Repository 已提交的操作批次。
            * @param [in] Batch_ 已提交的有序操作批次。
            * @details
            *   若当前正在记录撤销命令，则追加到当前命令构建器；
            *   否则如果批次包含可撤销字段，会清空旧 undo/redo 历史，避免历史分叉。
            */
            void HandleCommittedOperationBatch(IN const COperationBatch& Batch_);

            /*
            * @brief 清空撤销记录器状态。
            */
            void Clear();

            /*
            * @brief 根据外部提交批次决定是否清空历史。
            * @param [in] Batch_ 已提交的有序操作批次。
            */
            void ClearForCommittedOperationBatch(IN const COperationBatch& Batch_);

            /*
            * @brief 判断是否可以撤销。
            * @return true 表示 undo 栈存在可用步骤。
            */
            bool CanUndo() const;

            /*
            * @brief 判断是否可以重做。
            * @return true 表示 redo 栈存在可用步骤。
            */
            bool CanRedo() const;

            /*
            * @brief 获取当前待撤销的操作批次。
            * @return undo 栈顶操作批次。
            */
            const COperationBatch& GetUndoOperationBatch() const;

            /*
            * @brief 获取当前待重做的操作批次。
            * @return redo 栈顶操作批次。
            */
            const COperationBatch& GetRedoOperationBatch() const;

            /*
            * @brief 获取当前待撤销步骤名称。
            * @return undo 栈顶步骤名称。
            */
            std::string GetUndoStepName() const;

            /*
            * @brief 将 undo 栈顶步骤移动到 redo 栈。
            * @return true 表示移动成功。
            */
            bool MoveUndoToRedo();

            /*
            * @brief 将 redo 栈顶步骤移动到 undo 栈。
            * @return true 表示移动成功。
            */
            bool MoveRedoToUndo();

            /*
            * @brief 获取 undo 步骤摘要数组。
            * @return 从近到远排列的步骤 ID 与名称。
            */
            std::vector<std::tuple<iCAX::Data::uuid, std::string>> GetUndoArray() const;

            /*
            * @brief 获取 redo 步骤摘要数组。
            * @return 从近到远排列的步骤 ID 与名称。
            */
            std::vector<std::tuple<iCAX::Data::uuid, std::string>> GetRedoArray() const;

        private:
            /*
            * @brief 获取用于字段过滤的 meta 注册表。
            * @return 当前记录器使用的 meta 注册表引用。
            */
            const IMetaRegistry& GetMetaRegistry() const;
            struct CHistoryStep;

            /*
            * @brief 结束当前撤销命令并生成历史步骤。
            * @details
            *   EndCommand 会按 meta 过滤出 Transactional 操作，保留操作顺序后压入 undo 栈。
            */
            void EndCommand();

            /*
            * @brief 将已提交批次记录到当前撤销命令。
            * @param [in] Batch_ 已提交的有序操作批次。
            */
            void RecordCommittedOperationBatch(IN const COperationBatch& Batch_);

            /*
            * @brief 压入一个历史步骤。
            * @param [in] pStep_ 待压入步骤；空指针会被忽略。
            */
            void PushStep(IN std::shared_ptr<CHistoryStep> pStep_);

            /*
            * @brief 判断指定方向是否可以移动历史步骤。
            * @param [in] bUndo_ true 表示 undo -> redo，false 表示 redo -> undo。
            * @return true 表示源栈存在可移动步骤。
            */
            bool CanMoveStep(IN const bool bUndo_) const;

            /*
            * @brief 移动指定方向的历史步骤。
            * @param [in] bUndo_ true 表示 undo -> redo，false 表示 redo -> undo。
            * @return true 表示移动成功。
            */
            bool MoveStep(IN const bool bUndo_);

            /*
            * @brief 将历史栈转换为上层可展示的摘要数组。
            * @param [in] Stack_ 目标历史栈。
            * @return 从近到远排列的步骤 ID 与名称。
            */
            std::vector<std::tuple<iCAX::Data::uuid, std::string>> GetStepArray(IN const std::deque<std::shared_ptr<CHistoryStep>>& Stack_) const;

            friend class CRepositoryUndoRedoHistoryScope;

        private:
            std::shared_ptr<IMetaRegistry> m_pMetaRegistry;
            std::unique_ptr<COperationBatchBuilder> m_pCommandBuilder;
            std::deque<std::shared_ptr<CHistoryStep>> m_UndoStack;
            std::deque<std::shared_ptr<CHistoryStep>> m_RedoStack;
        };
    }
}
