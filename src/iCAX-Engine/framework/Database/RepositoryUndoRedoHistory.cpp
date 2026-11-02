#include "pch.h"
#include "RepositoryUndoRedoHistory.h"
#include "IMetaRegistry.h"
#include <iterator>


namespace
{
    constexpr size_t MAX_UNDO_STEPS_COUNT = 40;
}

namespace iCAX
{
    namespace Database
    {
    class CRepositoryUndoRedoHistoryScope final : public iCAX::Database::IRepositoryUndoScope
    {
    public:
        explicit CRepositoryUndoRedoHistoryScope(IN iCAX::Database::CRepositoryUndoRedoHistory& History_,
            std::function<void(const COperationBatch&)> Rollback_)
            : m_History(History_)
            , m_bCompleted(false)
            , m_Rollback(std::move(Rollback_))
        {
        }

        ~CRepositoryUndoRedoHistoryScope() override
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
        void EndWithoutHistory() override
        {
            if(m_bCompleted)return;
            m_History.EndCommand(false);m_bCompleted=true;
        }

        bool IsCompleted() const override
        {
            return m_bCompleted;
        }

        void Cancel() override
        {
            if (m_bCompleted) return;
            // Replay notifies observers synchronously; reentrant End/Cancel
            // must not publish the command we are discarding.
            m_bCompleted = true;
            try { m_History.CancelCommand(m_Rollback); }
            catch (...) { m_bCompleted = false; throw; }
        }

        void IncludeObservableProperty(const iCAX::Data::uuid& EntityID_,
            const std::string& Class_, const std::string& Property_) override
        {
            if (m_bCompleted) throw std::logic_error("Undo scope is already completed");
            m_History.IncludeObservableProperty(EntityID_, Class_, Property_);
        }

    private:
        iCAX::Database::CRepositoryUndoRedoHistory& m_History;
        bool m_bCompleted;
        std::function<void(const COperationBatch&)> m_Rollback;
    };
    }
}

struct iCAX::Database::CRepositoryUndoRedoHistory::CHistoryStep final
{
    iCAX::Data::uuid ID;
    std::string Name;
    COperationBatch Batch;
};

iCAX::Database::CRepositoryUndoRedoHistory::CRepositoryUndoRedoHistory(IN std::shared_ptr<IMetaRegistry> pMetaRegistry_)
    : m_pMetaRegistry(std::move(pMetaRegistry_))
{
    if (!m_pMetaRegistry)
    {
        throw std::invalid_argument("Repository undo/redo history requires a meta registry");
    }
}

std::unique_ptr<iCAX::Database::IRepositoryUndoScope> iCAX::Database::CRepositoryUndoRedoHistory::BeginCommand(IN const std::string& strName_,
    std::function<void(const COperationBatch&)> Rollback_)
{
    if (strName_.empty())
    {
        throw std::invalid_argument("Undo command name cannot be empty");
    }
    if (m_pCommandBuilder)
    {
        throw std::runtime_error("Nested repository undo scopes are not supported");
    }

    m_pCommandBuilder = std::make_unique<COperationBatchBuilder>(EOperationBatchKind::UserCommand, strName_);
    m_RetainedDerivedEntities.clear();
    m_CommandBatches.clear();
    m_IncludedObservableProperties.clear();
    return std::make_unique<CRepositoryUndoRedoHistoryScope>(*this, std::move(Rollback_));
}

bool iCAX::Database::CRepositoryUndoRedoHistory::IsRecording() const
{
    return m_pCommandBuilder != nullptr;
}

void iCAX::Database::CRepositoryUndoRedoHistory::HandleCommittedOperationBatch(IN const COperationBatch& Batch_)
{
    // 撤销记录边界之外发生的可撤销修改会让旧历史失效。
    // 边界之内的提交则追加到当前 undo step，保持操作顺序。
    if (IsRecording())
    {
        RecordCommittedOperationBatch(Batch_);
    }
    else
    {
        ClearForCommittedOperationBatch(Batch_);
    }
}

void iCAX::Database::CRepositoryUndoRedoHistory::Clear()
{
    m_RetainedDerivedEntities.clear();
    m_pCommandBuilder.reset();
    m_CommandBatches.clear();
    m_IncludedObservableProperties.clear();
    m_UndoStack.clear();
    m_RedoStack.clear();
}

void iCAX::Database::CRepositoryUndoRedoHistory::ClearForCommittedOperationBatch(IN const COperationBatch& Batch_)
{
    // 只有 Transactional 操作会影响 undo/redo 历史。
    // Observable/Silent/Derived 等非撤销字段不应清空用户已有撤销栈。
    auto _UndoableBatch = FilterTransactionalOperationBatch(Batch_, GetMetaRegistry());
    if (_UndoableBatch.IsEmpty())
    {
        return;
    }

    m_UndoStack.clear();
    m_RedoStack.clear();
}

bool iCAX::Database::CRepositoryUndoRedoHistory::CanUndo() const
{
    return CanMoveStep(true);
}

bool iCAX::Database::CRepositoryUndoRedoHistory::CanRedo() const
{
    return CanMoveStep(false);
}

const iCAX::Database::COperationBatch& iCAX::Database::CRepositoryUndoRedoHistory::GetUndoOperationBatch() const
{
    return m_UndoStack.back()->Batch;
}

const iCAX::Database::COperationBatch& iCAX::Database::CRepositoryUndoRedoHistory::GetRedoOperationBatch() const
{
    return m_RedoStack.back()->Batch;
}

std::string iCAX::Database::CRepositoryUndoRedoHistory::GetUndoStepName() const
{
    return m_UndoStack.back()->Name;
}

bool iCAX::Database::CRepositoryUndoRedoHistory::MoveUndoToRedo()
{
    return MoveStep(true);
}

bool iCAX::Database::CRepositoryUndoRedoHistory::MoveRedoToUndo()
{
    return MoveStep(false);
}

std::vector<std::tuple<iCAX::Data::uuid, std::string>> iCAX::Database::CRepositoryUndoRedoHistory::GetUndoArray() const
{
    return GetStepArray(m_UndoStack);
}

std::vector<std::tuple<iCAX::Data::uuid, std::string>> iCAX::Database::CRepositoryUndoRedoHistory::GetRedoArray() const
{
    return GetStepArray(m_RedoStack);
}

void iCAX::Database::CRepositoryUndoRedoHistory::EndCommand(bool RecordHistory_)
{
    if (!m_pCommandBuilder)
    {
        return;
    }

    auto _Batch = RecordHistory_ ? BuildCurrentCommand() : COperationBatch{};
    m_RetainedDerivedEntities.clear();
    m_pCommandBuilder.reset();
    m_CommandBatches.clear();
    m_IncludedObservableProperties.clear();
    if (_Batch.IsEmpty()) return;
    auto _pStep = std::make_shared<CHistoryStep>();
    _pStep->ID = iCAX::Data::GenerateNewUUID();
    _pStep->Name = _Batch.Name;
    _pStep->Batch = std::move(_Batch);
    PushStep(_pStep);
}

iCAX::Database::COperationBatch iCAX::Database::CRepositoryUndoRedoHistory::BuildCurrentCommand(bool RetainDerived_) const
{
    auto _Batch = m_pCommandBuilder->Build();
    std::size_t _OperationCount = 0;
    for (const auto& _Committed : m_CommandBatches)
        _OperationCount += _Committed->Operations.size();
    _Batch.Operations.reserve(_OperationCount);
    for (const auto& _Committed : m_CommandBatches)
    {
        auto _Transactional = FilterUndoCommandOperationBatch(*_Committed, GetMetaRegistry(),
            m_IncludedObservableProperties);
        _Batch.Operations.insert(_Batch.Operations.end(),
            std::make_move_iterator(_Transactional.Operations.begin()),
            std::make_move_iterator(_Transactional.Operations.end()));
    }
    if (RetainDerived_)
        std::erase_if(_Batch.Operations, [this](const auto& operation) {
            return m_RetainedDerivedEntities.contains(operation.EntityID);
        });
    return _Batch;
}

void iCAX::Database::CRepositoryUndoRedoHistory::CancelCommand(
    const std::function<void(const COperationBatch&)>& Rollback_)
{
    if (!m_pCommandBuilder) return;
    const auto batch = BuildCurrentCommand(false);
    if (!batch.IsEmpty()) {
        if (!Rollback_) throw std::logic_error("Undo cancellation requires its repository rollback handler");
        // Keep the pending command until rollback succeeds, allowing a caller
        // to retry safely if a replay validator rejects the inverse batch.
        Rollback_(batch);
    }
    m_pCommandBuilder.reset();
    m_CommandBatches.clear();
    m_IncludedObservableProperties.clear();
    m_RetainedDerivedEntities.clear();
}

void iCAX::Database::CRepositoryUndoRedoHistory::RetainDerivedEntity(const iCAX::Data::uuid& EntityID_)
{
    if (!IsRecording()) throw std::logic_error("Derived receipt retention requires an active undo command");
    m_RetainedDerivedEntities.insert(EntityID_);
}

void iCAX::Database::CRepositoryUndoRedoHistory::IncludeObservableProperty(
    const iCAX::Data::uuid& EntityID_, const std::string& Class_, const std::string& Property_)
{
    if (!m_pCommandBuilder) throw std::logic_error("No active undo command");
    const auto& meta = GetMetaRegistry();
    if (!meta.HasTypeByName(Class_) || !meta.HasPropertyByName(Class_, Property_)
        || meta.GetPropertyKindByName(Class_, Property_) != EPropertyKind::Value
        || meta.GetPropertyChangePolicyByName(Class_, Property_) != EPropertyChangePolicy::Observable
        || meta.GetPropertyPersistenceByName(Class_, Property_) != EPropertyPersistence::Persistent)
        throw std::invalid_argument("Undo inclusion requires a persistent Observable value property");
    m_IncludedObservableProperties.emplace(EntityID_, Class_, Property_);
}

const iCAX::Database::IMetaRegistry& iCAX::Database::CRepositoryUndoRedoHistory::GetMetaRegistry() const
{
    if (!m_pMetaRegistry)
    {
        throw std::runtime_error("Repository undo/redo history has no meta registry");
    }
    return *m_pMetaRegistry;
}

void iCAX::Database::CRepositoryUndoRedoHistory::RecordCommittedOperationBatch(IN const COperationBatch& Batch_)
{
    if (!m_pCommandBuilder || Batch_.IsEmpty())
    {
        return;
    }

    // The public const-reference entry point must freeze an independent
    // snapshot immediately; its caller still owns and may reuse Batch_.
    m_CommandBatches.push_back(std::make_shared<const COperationBatch>(Batch_));
}

void iCAX::Database::CRepositoryUndoRedoHistory::HandleSharedCommittedOperationBatch(
    IN std::shared_ptr<const COperationBatch> pBatch_)
{
    if (!pBatch_ || pBatch_->IsEmpty()) return;
    if (IsRecording())
        m_CommandBatches.push_back(std::move(pBatch_));
    else
        ClearForCommittedOperationBatch(*pBatch_);
}

void iCAX::Database::CRepositoryUndoRedoHistory::PushStep(IN std::shared_ptr<CHistoryStep> pStep_)
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
    // 新的用户步骤会形成新的历史分支，旧 redo 栈必须失效。
    m_RedoStack.clear();
}

bool iCAX::Database::CRepositoryUndoRedoHistory::CanMoveStep(IN const bool bUndo_) const
{
    const auto& _Stack = bUndo_ ? m_UndoStack : m_RedoStack;
    return !_Stack.empty() && _Stack.back() != nullptr;
}

bool iCAX::Database::CRepositoryUndoRedoHistory::MoveStep(IN const bool bUndo_)
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

std::vector<std::tuple<iCAX::Data::uuid, std::string>> iCAX::Database::CRepositoryUndoRedoHistory::GetStepArray(IN const std::deque<std::shared_ptr<CHistoryStep>>& Stack_) const
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
