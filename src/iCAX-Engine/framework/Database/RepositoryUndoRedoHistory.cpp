#include "pch.h"
#include "RepositoryUndoRedoHistory.h"
#include "IMetaRegistry.h"


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
        explicit CRepositoryUndoRedoHistoryScope(IN iCAX::Database::CRepositoryUndoRedoHistory& History_)
            : m_History(History_)
            , m_bCompleted(false)
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

        bool IsCompleted() const override
        {
            return m_bCompleted;
        }

    private:
        iCAX::Database::CRepositoryUndoRedoHistory& m_History;
        bool m_bCompleted;
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

std::unique_ptr<iCAX::Database::IRepositoryUndoScope> iCAX::Database::CRepositoryUndoRedoHistory::BeginCommand(IN const std::string& strName_)
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
    return std::make_unique<CRepositoryUndoRedoHistoryScope>(*this);
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
    m_pCommandBuilder.reset();
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

void iCAX::Database::CRepositoryUndoRedoHistory::EndCommand()
{
    if (!m_pCommandBuilder)
    {
        return;
    }

    auto _pBuilder = std::move(m_pCommandBuilder);

    // End 时才做 meta 过滤：记录阶段保留原始顺序，结束阶段只剔除不可撤销字段。
    auto _Batch = FilterTransactionalOperationBatch(_pBuilder->Build(), GetMetaRegistry());
    if (_Batch.IsEmpty())
    {
        return;
    }

    auto _pStep = std::make_shared<CHistoryStep>();
    _pStep->ID = iCAX::Data::GenerateNewUUID();
    _pStep->Name = _Batch.Name;
    _pStep->Batch = _Batch;
    PushStep(_pStep);
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

    m_pCommandBuilder->AppendBatch(Batch_);
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
