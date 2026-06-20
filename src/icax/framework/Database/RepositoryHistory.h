#pragma once

#include "ChangeSet.h"
#include "Data/uuid.h"

#include <deque>
#include <map>
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
        * @brief Repository 外挂式历史记录器
        * @details
        *   只维护撤销重做相关状态，不侵入 Repository、Entity、Component。
        */
        class _DATABASE_EXP CRepositoryHistory final
        {
        public:
            CRepositoryHistory() = default;
            explicit CRepositoryHistory(IN std::shared_ptr<IMetaRegistry> pMetaRegistry_);
            ~CRepositoryHistory() = default;

            CRepositoryHistory(IN const CRepositoryHistory&) = delete;
            CRepositoryHistory& operator=(IN const CRepositoryHistory&) = delete;

            std::unique_ptr<IRepositoryUndoScope> BeginCommand(IN const std::string& strName_);
            bool IsRecording() const;

            void HandleCommittedChangeSet(IN const CChangeSet& ChangeSet_);
            void Clear();
            void ClearForCommittedChangeSet(IN const CChangeSet& ChangeSet_);

            bool CanUndo() const;
            bool CanRedo() const;
            const CChangeSet& GetUndoChangeSet() const;
            const CChangeSet& GetRedoChangeSet() const;
            std::string GetUndoStepName() const;
            bool MoveUndoToRedo();
            bool MoveRedoToUndo();

            std::vector<std::tuple<iCAX::Data::uuid, std::string>> GetUndoArray() const;
            std::vector<std::tuple<iCAX::Data::uuid, std::string>> GetRedoArray() const;

        private:
            const IMetaRegistry& GetMetaRegistry() const;
            struct CHistoryStep;

            void EndCommand();
            void RecordCommittedChangeSet(IN const CChangeSet& ChangeSet_);
            void PushStep(IN std::shared_ptr<CHistoryStep> pStep_);
            bool CanMoveStep(IN const bool bUndo_) const;
            bool MoveStep(IN const bool bUndo_);
            std::vector<std::tuple<iCAX::Data::uuid, std::string>> GetStepArray(IN const std::deque<std::shared_ptr<CHistoryStep>>& Stack_) const;

            friend class CRepositoryHistoryScope;

        private:
            std::shared_ptr<IMetaRegistry> m_pMetaRegistry;
            std::unique_ptr<CChangeSetBuilder> m_pCommandBuilder;
            std::deque<std::shared_ptr<CHistoryStep>> m_UndoStack;
            std::deque<std::shared_ptr<CHistoryStep>> m_RedoStack;
        };
    }
}
