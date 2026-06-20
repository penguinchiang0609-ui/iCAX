#include "pch.h"
#include "RepositoryHistory.h"
#include "ChangeLog.h"
#include "IMetaRegistry.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <utility>

namespace
{
    constexpr size_t MAX_UNDO_STEPS_COUNT = 40;
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
};

iCAX::Database::CRepositoryHistory::CRepositoryHistory(IN std::shared_ptr<IMetaRegistry> pMetaRegistry_)
    : m_pMetaRegistry(std::move(pMetaRegistry_))
{
}

std::unique_ptr<iCAX::Database::IRepositoryUndoScope> iCAX::Database::CRepositoryHistory::BeginCommand(IN const std::string& strName_)
{
    if (strName_.empty())
    {
        throw std::invalid_argument("Undo command name cannot be empty");
    }
    if (m_pCommandBuilder)
    {
        throw std::runtime_error("Nested repository undo scopes are not supported");
    }

    m_pCommandBuilder = std::make_unique<CChangeSetBuilder>(EChangeScopeKind::UserCommand, strName_);
    return std::make_unique<CRepositoryHistoryScope>(*this);
}

bool iCAX::Database::CRepositoryHistory::IsRecording() const
{
    return m_pCommandBuilder != nullptr;
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
    m_UndoStack.clear();
    m_RedoStack.clear();
}

void iCAX::Database::CRepositoryHistory::ClearForCommittedChangeSet(IN const CChangeSet& ChangeSet_)
{
    auto _UndoableChangeSet = FilterTransactionalChangeSet(ChangeSet_, GetMetaRegistry());
    if (_UndoableChangeSet.IsEmpty())
    {
        return;
    }

    m_UndoStack.clear();
    m_RedoStack.clear();
}

bool iCAX::Database::CRepositoryHistory::CanUndo() const
{
    return CanMoveStep(true);
}

bool iCAX::Database::CRepositoryHistory::CanRedo() const
{
    return CanMoveStep(false);
}

const iCAX::Database::CChangeSet& iCAX::Database::CRepositoryHistory::GetUndoChangeSet() const
{
    return m_UndoStack.back()->ChangeSet;
}

const iCAX::Database::CChangeSet& iCAX::Database::CRepositoryHistory::GetRedoChangeSet() const
{
    return m_RedoStack.back()->ChangeSet;
}

std::string iCAX::Database::CRepositoryHistory::GetUndoStepName() const
{
    return m_UndoStack.back()->Name;
}

bool iCAX::Database::CRepositoryHistory::MoveUndoToRedo()
{
    return MoveStep(true);
}

bool iCAX::Database::CRepositoryHistory::MoveRedoToUndo()
{
    return MoveStep(false);
}

std::vector<std::tuple<iCAX::Data::uuid, std::string>> iCAX::Database::CRepositoryHistory::GetUndoArray() const
{
    return GetStepArray(m_UndoStack);
}

std::vector<std::tuple<iCAX::Data::uuid, std::string>> iCAX::Database::CRepositoryHistory::GetRedoArray() const
{
    return GetStepArray(m_RedoStack);
}

void iCAX::Database::CRepositoryHistory::EndCommand()
{
    if (!m_pCommandBuilder)
    {
        return;
    }

    auto _pBuilder = std::move(m_pCommandBuilder);

    auto _ChangeSet = FilterTransactionalChangeSet(_pBuilder->Build(), GetMetaRegistry());
    if (_ChangeSet.IsEmpty())
    {
        return;
    }

    auto _pStep = std::make_shared<CHistoryStep>();
    _pStep->ID = iCAX::Data::GenerateNewUUID();
    _pStep->Name = _ChangeSet.Name;
    _pStep->ChangeSet = _ChangeSet;
    PushStep(_pStep);
}

const iCAX::Database::IMetaRegistry& iCAX::Database::CRepositoryHistory::GetMetaRegistry() const
{
    return m_pMetaRegistry ? *m_pMetaRegistry : *GetGlobalMetaRegistry();
}

void iCAX::Database::CRepositoryHistory::RecordCommittedChangeSet(IN const CChangeSet& ChangeSet_)
{
    if (!m_pCommandBuilder || ChangeSet_.IsEmpty())
    {
        return;
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
        auto& _Properties = _ModifiedProperties[{ _Change.Key.EntityID, _Change.Key.ComponentClass }];
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
}

void iCAX::Database::CRepositoryHistory::PushStep(IN std::shared_ptr<CHistoryStep> pStep_)
{
    if (!pStep_)
    {
        return;
    }

    m_UndoStack.push_back(pStep_);
    if (m_UndoStack.size() > MAX_UNDO_STEPS_COUNT)
    {
        m_UndoStack.pop_front();
    }
    m_RedoStack.clear();
}

bool iCAX::Database::CRepositoryHistory::CanMoveStep(IN const bool bUndo_) const
{
    const auto& _Stack = bUndo_ ? m_UndoStack : m_RedoStack;
    return !_Stack.empty() && _Stack.back() != nullptr;
}

bool iCAX::Database::CRepositoryHistory::MoveStep(IN const bool bUndo_)
{
    if (!CanMoveStep(bUndo_))
    {
        return false;
    }

    auto& _FromStack = bUndo_ ? m_UndoStack : m_RedoStack;
    auto& _ToStack = bUndo_ ? m_RedoStack : m_UndoStack;
    auto _pStep = _FromStack.back();
    _FromStack.pop_back();
    _ToStack.push_back(_pStep);
    return true;
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
