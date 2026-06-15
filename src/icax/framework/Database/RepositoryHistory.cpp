#include "pch.h"
#include "RepositoryHistory.h"
#include "ChangeLog.h"

#include <algorithm>
#include <map>
#include <stdexcept>

namespace
{
    constexpr size_t MAX_UNDO_STEPS_COUNT = 40;

    void AddDomainIDOnce(IN OUT std::vector<iCAX::Data::uuid>& DomainIDs_, IN const iCAX::Data::uuid& DomainID_)
    {
        if (DomainID_.is_nil())
        {
            return;
        }
        if (std::find(DomainIDs_.begin(), DomainIDs_.end(), DomainID_) == DomainIDs_.end())
        {
            DomainIDs_.push_back(DomainID_);
        }
    }

    std::vector<iCAX::Data::uuid> CollectAffectedDomainIDs(IN const iCAX::Database::CChangeSet& ChangeSet_, IN const iCAX::Data::uuid& OwnerDomainID_)
    {
        std::vector<iCAX::Data::uuid> _DomainIDs;
        AddDomainIDOnce(_DomainIDs, OwnerDomainID_);

        for (const auto& _Change : ChangeSet_.CreatedDomains)
        {
            AddDomainIDOnce(_DomainIDs, _Change.DomainID);
        }
        for (const auto& _Change : ChangeSet_.DeletedDomains)
        {
            AddDomainIDOnce(_DomainIDs, _Change.DomainID);
        }
        for (const auto& _Change : ChangeSet_.CreatedEntities)
        {
            AddDomainIDOnce(_DomainIDs, _Change.Key.DomainID);
        }
        for (const auto& _Change : ChangeSet_.DeletedEntities)
        {
            AddDomainIDOnce(_DomainIDs, _Change.Key.DomainID);
        }
        for (const auto& _Change : ChangeSet_.AddedComponents)
        {
            AddDomainIDOnce(_DomainIDs, _Change.Key.DomainID);
        }
        for (const auto& _Change : ChangeSet_.RemovedComponents)
        {
            AddDomainIDOnce(_DomainIDs, _Change.Key.DomainID);
        }
        for (const auto& _Change : ChangeSet_.ModifiedProperties)
        {
            AddDomainIDOnce(_DomainIDs, _Change.Key.DomainID);
        }

        return _DomainIDs;
    }
}

namespace iCAX
{
    namespace Database
    {
    class CRepositoryHistoryScope final : public iCAX::Database::IRepositoryUndoScope
    {
    public:
        explicit CRepositoryHistoryScope(IN iCAX::Database::CRepositoryHistory& History_)
            : m_History(History_)
            , m_bCompleted(false)
        {
        }

        ~CRepositoryHistoryScope() override
        {
            if (!m_bCompleted)
            {
                try
                {
                    End();
                }
                catch (...)
                {
                }
            }
        }

        void End() override
        {
            if (m_bCompleted)
            {
                return;
            }
            m_History.EndCommand();
            m_bCompleted = true;
        }

        bool IsCompleted() const override
        {
            return m_bCompleted;
        }

    private:
        iCAX::Database::CRepositoryHistory& m_History;
        bool m_bCompleted;
    };
    }
}

struct iCAX::Database::CRepositoryHistory::CHistoryStep final
{
    iCAX::Data::uuid ID;
    std::string Name;
    CChangeSet ChangeSet;
    std::vector<iCAX::Data::uuid> DomainIDs;
};

std::unique_ptr<iCAX::Database::IRepositoryUndoScope> iCAX::Database::CRepositoryHistory::BeginCommand(IN const iCAX::Data::uuid& DomainID_, IN const std::string& strName_)
{
    if (strName_.empty())
    {
        throw std::invalid_argument("Undo command name cannot be empty");
    }
    if (m_pCommandBuilder)
    {
        throw std::runtime_error("Nested repository undo scopes are not supported");
    }

    m_CurrentCommandDomainID = DomainID_;
    m_pCommandBuilder = std::make_unique<CChangeSetBuilder>(EChangeScopeKind::UserCommand, strName_);
    return std::make_unique<CRepositoryHistoryScope>(*this);
}

bool iCAX::Database::CRepositoryHistory::IsRecording() const
{
    return m_pCommandBuilder != nullptr;
}

iCAX::Data::uuid iCAX::Database::CRepositoryHistory::GetCurrentCommandDomain() const
{
    return m_pCommandBuilder ? m_CurrentCommandDomainID : iCAX::Data::GenerateNilUUID();
}

void iCAX::Database::CRepositoryHistory::HandleCommittedChangeSet(IN const CChangeSet& ChangeSet_)
{
    if (IsRecording())
    {
        RecordCommittedChangeSet(ChangeSet_);
    }
    else
    {
        ClearForCommittedChangeSet(ChangeSet_);
    }
}

void iCAX::Database::CRepositoryHistory::Clear()
{
    m_pCommandBuilder.reset();
    m_CurrentCommandDomainID = iCAX::Data::GenerateNilUUID();
    m_UndoStacks.clear();
    m_RedoStacks.clear();
}

void iCAX::Database::CRepositoryHistory::ClearForCommittedChangeSet(IN const CChangeSet& ChangeSet_)
{
    auto _UndoableChangeSet = FilterTransactionalChangeSet(ChangeSet_);
    if (_UndoableChangeSet.IsEmpty())
    {
        return;
    }

    ClearHistoryForDomains(CollectAffectedDomainIDs(_UndoableChangeSet, iCAX::Data::GenerateNilUUID()));
}

bool iCAX::Database::CRepositoryHistory::CanUndo(IN const iCAX::Data::uuid& DomainID_) const
{
    return CanMoveStep(DomainID_, true);
}

bool iCAX::Database::CRepositoryHistory::CanRedo(IN const iCAX::Data::uuid& DomainID_) const
{
    return CanMoveStep(DomainID_, false);
}

const iCAX::Database::CChangeSet& iCAX::Database::CRepositoryHistory::GetUndoChangeSet(IN const iCAX::Data::uuid& DomainID_) const
{
    return m_UndoStacks.at(DomainID_).back()->ChangeSet;
}

const iCAX::Database::CChangeSet& iCAX::Database::CRepositoryHistory::GetRedoChangeSet(IN const iCAX::Data::uuid& DomainID_) const
{
    return m_RedoStacks.at(DomainID_).back()->ChangeSet;
}

std::string iCAX::Database::CRepositoryHistory::GetUndoStepName(IN const iCAX::Data::uuid& DomainID_) const
{
    return m_UndoStacks.at(DomainID_).back()->Name;
}

bool iCAX::Database::CRepositoryHistory::MoveUndoToRedo(IN const iCAX::Data::uuid& DomainID_)
{
    return MoveStep(DomainID_, true);
}

bool iCAX::Database::CRepositoryHistory::MoveRedoToUndo(IN const iCAX::Data::uuid& DomainID_)
{
    return MoveStep(DomainID_, false);
}

std::vector<std::tuple<iCAX::Data::uuid, std::string>> iCAX::Database::CRepositoryHistory::GetUndoArray(IN const iCAX::Data::uuid& DomainID_) const
{
    auto _Ite = m_UndoStacks.find(DomainID_);
    if (_Ite == m_UndoStacks.end())
    {
        return {};
    }
    return GetStepArray(_Ite->second);
}

std::vector<std::tuple<iCAX::Data::uuid, std::string>> iCAX::Database::CRepositoryHistory::GetRedoArray(IN const iCAX::Data::uuid& DomainID_) const
{
    auto _Ite = m_RedoStacks.find(DomainID_);
    if (_Ite == m_RedoStacks.end())
    {
        return {};
    }
    return GetStepArray(_Ite->second);
}

void iCAX::Database::CRepositoryHistory::EndCommand()
{
    if (!m_pCommandBuilder)
    {
        return;
    }

    auto _pBuilder = std::move(m_pCommandBuilder);
    auto _OwnerDomainID = m_CurrentCommandDomainID;
    m_CurrentCommandDomainID = iCAX::Data::GenerateNilUUID();

    auto _ChangeSet = FilterTransactionalChangeSet(_pBuilder->Build());
    if (_ChangeSet.IsEmpty())
    {
        return;
    }

    auto _pStep = std::make_shared<CHistoryStep>();
    _pStep->ID = iCAX::Data::GenerateNewUUID();
    _pStep->Name = _ChangeSet.Name;
    _pStep->ChangeSet = _ChangeSet;
    _pStep->DomainIDs = CollectAffectedDomainIDs(_ChangeSet, _OwnerDomainID);
    PushStep(_pStep);
}

void iCAX::Database::CRepositoryHistory::RecordCommittedChangeSet(IN const CChangeSet& ChangeSet_)
{
    if (!m_pCommandBuilder || ChangeSet_.IsEmpty())
    {
        return;
    }

    for (const auto& _Change : ChangeSet_.CreatedDomains)
    {
        m_pCommandBuilder->RecordAddDomain(_Change.DomainID, _Change.bPersistent);
    }
    for (const auto& _Change : ChangeSet_.CreatedEntities)
    {
        m_pCommandBuilder->RecordAddEntity(_Change.Key);
    }
    for (const auto& _Change : ChangeSet_.RemovedComponents)
    {
        m_pCommandBuilder->RecordRemoveComponent(_Change.Key, _Change.PreviousProperties);
    }
    for (const auto& _Change : ChangeSet_.AddedComponents)
    {
        m_pCommandBuilder->RecordAddComponent(_Change.Key, _Change.NewProperties);
    }

    std::map<CChangeComponentKey, std::pair<iCAX::Data::PropertySet, iCAX::Data::PropertySet>> _ModifiedProperties;
    for (const auto& _Change : ChangeSet_.ModifiedProperties)
    {
        auto& _Properties = _ModifiedProperties[{ _Change.Key.DomainID, _Change.Key.EntityID, _Change.Key.ComponentClass }];
        _Properties.first[_Change.Key.PropertyName] = _Change.PreviousValue;
        _Properties.second[_Change.Key.PropertyName] = _Change.NewValue;
    }
    for (const auto& [_Key, _Properties] : _ModifiedProperties)
    {
        m_pCommandBuilder->RecordModifyComponent(_Key, _Properties.first, _Properties.second);
    }

    for (const auto& _Change : ChangeSet_.DeletedEntities)
    {
        m_pCommandBuilder->RecordDeleteEntity(_Change.Key);
    }
    for (const auto& _Change : ChangeSet_.DeletedDomains)
    {
        m_pCommandBuilder->RecordDeleteDomain(_Change.DomainID, _Change.bPersistent);
    }
}

void iCAX::Database::CRepositoryHistory::PushStep(IN std::shared_ptr<CHistoryStep> pStep_)
{
    if (!pStep_ || pStep_->DomainIDs.empty())
    {
        return;
    }

    RemoveStepFromStacks(pStep_);
    for (const auto& _DomainID : pStep_->DomainIDs)
    {
        auto& _UndoStack = m_UndoStacks[_DomainID];
        _UndoStack.push_back(pStep_);
        if (_UndoStack.size() > MAX_UNDO_STEPS_COUNT)
        {
            auto _pRemovedStep = _UndoStack.front();
            _UndoStack.pop_front();
            RemoveStepFromStacks(_pRemovedStep);
        }
        m_RedoStacks[_DomainID].clear();
    }
}

bool iCAX::Database::CRepositoryHistory::CanMoveStep(IN const iCAX::Data::uuid& DomainID_, IN const bool bUndo_) const
{
    const auto& _Stacks = bUndo_ ? m_UndoStacks : m_RedoStacks;
    auto _Ite = _Stacks.find(DomainID_);
    if (_Ite == _Stacks.end() || _Ite->second.empty())
    {
        return false;
    }

    const auto _pStep = _Ite->second.back();
    if (!_pStep)
    {
        return false;
    }

    for (const auto& _DomainID : _pStep->DomainIDs)
    {
        auto _IteDomain = _Stacks.find(_DomainID);
        if (_IteDomain == _Stacks.end() || _IteDomain->second.empty() || _IteDomain->second.back() != _pStep)
        {
            return false;
        }
    }
    return true;
}

bool iCAX::Database::CRepositoryHistory::MoveStep(IN const iCAX::Data::uuid& DomainID_, IN const bool bUndo_)
{
    if (!CanMoveStep(DomainID_, bUndo_))
    {
        return false;
    }

    auto& _FromStacks = bUndo_ ? m_UndoStacks : m_RedoStacks;
    auto& _ToStacks = bUndo_ ? m_RedoStacks : m_UndoStacks;
    auto _pStep = _FromStacks[DomainID_].back();

    for (const auto& _DomainID : _pStep->DomainIDs)
    {
        _FromStacks[_DomainID].pop_back();
        _ToStacks[_DomainID].push_back(_pStep);
    }
    return true;
}

void iCAX::Database::CRepositoryHistory::RemoveStepFromStacks(IN const std::shared_ptr<CHistoryStep>& pStep_)
{
    auto _Remove = [&](auto& Stacks_)
    {
        for (auto& [_DomainID, _Stack] : Stacks_)
        {
            _Stack.erase(std::remove(_Stack.begin(), _Stack.end(), pStep_), _Stack.end());
        }
    };

    _Remove(m_UndoStacks);
    _Remove(m_RedoStacks);
}

void iCAX::Database::CRepositoryHistory::ClearHistoryForDomains(IN const std::vector<iCAX::Data::uuid>& DomainIDs_)
{
    std::vector<std::shared_ptr<CHistoryStep>> _StepsToRemove;
    auto _Collect = [&](const auto& Stacks_)
    {
        for (const auto& _DomainID : DomainIDs_)
        {
            auto _Ite = Stacks_.find(_DomainID);
            if (_Ite == Stacks_.end())
            {
                continue;
            }

            for (const auto& _pStep : _Ite->second)
            {
                if (_pStep && std::find(_StepsToRemove.begin(), _StepsToRemove.end(), _pStep) == _StepsToRemove.end())
                {
                    _StepsToRemove.push_back(_pStep);
                }
            }
        }
    };

    _Collect(m_UndoStacks);
    _Collect(m_RedoStacks);

    for (const auto& _pStep : _StepsToRemove)
    {
        RemoveStepFromStacks(_pStep);
    }
}

std::vector<std::tuple<iCAX::Data::uuid, std::string>> iCAX::Database::CRepositoryHistory::GetStepArray(IN const std::deque<std::shared_ptr<CHistoryStep>>& Stack_) const
{
    std::vector<std::tuple<iCAX::Data::uuid, std::string>> _Array;
    for (auto _Ite = Stack_.rbegin(); _Ite != Stack_.rend(); ++_Ite)
    {
        if (*_Ite)
        {
            _Array.push_back({ (*_Ite)->ID, (*_Ite)->Name });
        }
    }
    return _Array;
}
