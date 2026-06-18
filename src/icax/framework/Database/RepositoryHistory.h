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
        /*
        * @brief Repository 外挂式历史记录器
        * @details
        *   只维护撤销重做相关状态，不侵入 Repository、Entity、Component。
        */
        class _DATABASE_EXP CRepositoryHistory final
        {
        public:
            CRepositoryHistory() = default;
            ~CRepositoryHistory() = default;

            CRepositoryHistory(IN const CRepositoryHistory&) = delete;
            CRepositoryHistory& operator=(IN const CRepositoryHistory&) = delete;

            std::unique_ptr<IRepositoryUndoScope> BeginCommand(IN const iCAX::Data::uuid& RepositoryID_, IN const std::string& strName_);
            bool IsRecording() const;
            iCAX::Data::uuid GetCurrentCommandDomain() const;

            void HandleCommittedChangeSet(IN const CChangeSet& ChangeSet_);
            void Clear();
            void ClearForCommittedChangeSet(IN const CChangeSet& ChangeSet_);

            bool CanUndo(IN const iCAX::Data::uuid& DomainID_) const;
            bool CanRedo(IN const iCAX::Data::uuid& DomainID_) const;
            const CChangeSet& GetUndoChangeSet(IN const iCAX::Data::uuid& DomainID_) const;
            const CChangeSet& GetRedoChangeSet(IN const iCAX::Data::uuid& DomainID_) const;
            std::string GetUndoStepName(IN const iCAX::Data::uuid& DomainID_) const;
            bool MoveUndoToRedo(IN const iCAX::Data::uuid& DomainID_);
            bool MoveRedoToUndo(IN const iCAX::Data::uuid& DomainID_);

            std::vector<std::tuple<iCAX::Data::uuid, std::string>> GetUndoArray(IN const iCAX::Data::uuid& DomainID_) const;
            std::vector<std::tuple<iCAX::Data::uuid, std::string>> GetRedoArray(IN const iCAX::Data::uuid& DomainID_) const;

        private:
            struct CHistoryStep;

            void EndCommand();
            void RecordCommittedChangeSet(IN const CChangeSet& ChangeSet_);
            void PushStep(IN std::shared_ptr<CHistoryStep> pStep_);
            bool CanMoveStep(IN const bool bUndo_) const;
            bool MoveStep(IN const bool bUndo_);
            std::vector<std::tuple<iCAX::Data::uuid, std::string>> GetStepArray(IN const std::deque<std::shared_ptr<CHistoryStep>>& Stack_) const;

            friend class CRepositoryHistoryScope;

        private:
            std::unique_ptr<CChangeSetBuilder> m_pCommandBuilder;
            iCAX::Data::uuid m_CurrentCommandDomainID;
            std::deque<std::shared_ptr<CHistoryStep>> m_UndoStack;
            std::deque<std::shared_ptr<CHistoryStep>> m_RedoStack;
        };
    }
}
