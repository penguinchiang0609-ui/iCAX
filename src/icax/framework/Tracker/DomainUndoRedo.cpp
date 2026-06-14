#include "pch.h"
#include "DomainUndoRedo.h"
#include <algorithm>
#include "Data/uuid.h"

const int MAX_UNDO_STEPS_COUNT = 40;//!< 最大的回退步数

//!< 构造函数
iCAX::Tracker::CDomainUndoRedo::CDomainUndoRedo(IN std::shared_ptr<iCAX::Database::IDomain> pDomain_)
{
    m_pDomain = pDomain_;
}

//!< 析构函数
iCAX::Tracker::CDomainUndoRedo::~CDomainUndoRedo()
{
}

void iCAX::Tracker::CDomainUndoRedo::PushUndo(IN std::shared_ptr<UROperationStep> pStep_)
{
    m_UndoStack.push_back(pStep_);
    m_RedoStack.clear();

    if (m_UndoStack.size() > MAX_UNDO_STEPS_COUNT)
    {
        m_UndoStack.pop_front();//!< 从底部推出旧的步骤
    }
}

//!< 开始记录撤销还原
bool iCAX::Tracker::CDomainUndoRedo::BeginStep(const std::string& strName_)
{
    if (!m_strCurrStepName.empty() || strName_.empty())
    {
        return false;
    }
    m_strCurrStepName = strName_;
    return true;
}

//!< 停止撤销还原
std::weak_ptr<iCAX::Tracker::UROperationStep> iCAX::Tracker::CDomainUndoRedo::EndStep()
{
    if (m_strCurrStepName.empty())
    {
        return std::shared_ptr<UROperationStep>();
    }

    if (m_CurrOperationUnits.empty())
    {
        m_strCurrStepName.clear();
        return std::shared_ptr<UROperationStep>();
    }

    std::shared_ptr<UROperationStep> _pStep = std::make_shared<UROperationStep>();

    _pStep->ID = iCAX::Data::GenerateNewUUID();
    _pStep->strName = m_strCurrStepName;
    _pStep->Operation = m_CurrOperationUnits;
    std::list<iCAX::Data::uuid> _lstOwners;
    std::for_each(m_CurrOperationUnits.begin(), m_CurrOperationUnits.end(), [&](const iCAX::Database::OperationUnit& _Operation)
        {
            _lstOwners.push_back(_Operation.DomainID);
        });
    auto _IteLast = std::unique(_lstOwners.begin(), _lstOwners.end());
    _lstOwners.erase(_IteLast, _lstOwners.end());
    _pStep->lstDomain = _lstOwners;

    m_UndoStack.push_back(_pStep);
    m_RedoStack.clear();

    if (m_UndoStack.size() > MAX_UNDO_STEPS_COUNT)
    {
        m_UndoStack.pop_front();//!< 从底部推出旧的步骤
    }

    m_CurrOperationUnits.clear();
    m_strCurrStepName.clear();
    return std::weak_ptr<UROperationStep>(_pStep);
}

//!< 是否正在记录
bool iCAX::Tracker::CDomainUndoRedo::IsRecording() const
{
    return !m_strCurrStepName.empty();
}

//!< 是否可以重做
bool iCAX::Tracker::CDomainUndoRedo::CanRedo() const
{
    return !IsRecording() && !m_RedoStack.empty();
}

//!< 是否可以撤销
bool iCAX::Tracker::CDomainUndoRedo::CanUndo() const
{
    return !IsRecording() && !m_UndoStack.empty();
}

//!< 重做
void iCAX::Tracker::CDomainUndoRedo::Redo(IN const bool& bFake_)
{
    if (m_RedoStack.empty())
        return;

    // Get the last undone operation and push it back to the undo stack
    std::shared_ptr<UROperationStep> _pStep = m_RedoStack.back();
    m_RedoStack.pop_back();
    m_UndoStack.push_back(_pStep);

    // Notify the redo action (reapply the operation)
    if (!bFake_)
    {
        if (m_pDomain.expired())
        {
            return;
        }
        auto _pRepository = m_pDomain.lock()->GetRepository(); 
        if (!_pRepository)
        {
            return;
        }
        for (auto _Ite = _pStep->Operation.begin(); _Ite != _pStep->Operation.end(); _Ite++)
        {
            //std::shared_ptr<iCAX::Database::IRepository> _pRepository = m_pDomain->GetRepository();
            switch (_Ite->nType)
            {
            case iCAX::Database::RepositoryEventArgs::kModifyComponent:
            {
                auto _pDomain = _pRepository->GetDomain(_Ite->DomainID);
                if (_pDomain)
                {
                    _pDomain->GetEntity(_Ite->EntityID)->GetComponent(_Ite->strClassName)->SetProperties(_Ite->NewProperties);
                }
                break;
            }
            case iCAX::Database::RepositoryEventArgs::kAddComponent:
            {
                auto _pDomain = _pRepository->GetDomain(_Ite->DomainID);
                if (_pDomain)
                {
                    auto _pComponent = _pDomain->GetEntity(_Ite->EntityID)->AddComponent(_Ite->strClassName);
                    if (!_Ite->NewProperties.empty())
                    {
                        _pComponent->SetProperties(_Ite->NewProperties);
                    }
                }
                break;
            }
            case iCAX::Database::RepositoryEventArgs::kRemoveComponent:
            {
                auto _pDomain = _pRepository->GetDomain(_Ite->DomainID);
                if (_pDomain)
                {
                    _pDomain->GetEntity(_Ite->EntityID)->RemoveComponent(_Ite->strClassName);
                }
                break;
            }
            case iCAX::Database::RepositoryEventArgs::kAddEntity:
            {
                auto _pDomain = _pRepository->GetDomain(_Ite->DomainID);
                if (_pDomain)
                {
                    _pDomain->CreateEntity(_Ite->EntityID);
                }
                break;
            }
            case iCAX::Database::RepositoryEventArgs::kDeleteEntity:
            {
                auto _pDomain = _pRepository->GetDomain(_Ite->DomainID);
                if (_pDomain)
                {
                    _pDomain->DeleteEntity(_Ite->EntityID);
                }
                break;
            }
            default:
                break;
            }
        }
    }

    return;
}

//!< 撤销
void iCAX::Tracker::CDomainUndoRedo::Undo(IN const bool& bFake_)
{
    if (m_UndoStack.empty())
        return;

    // Get the last operation and push it to the redo stack
    std::shared_ptr<UROperationStep> _pStep = m_UndoStack.back();
    m_UndoStack.pop_back();
    m_RedoStack.push_back(_pStep);

    // Notify the undo action (reverse the operation)
    if (!bFake_)
    {
        if (m_pDomain.expired())
        {
            return;
        }
        auto _pRepository = m_pDomain.lock()->GetRepository();
        if (!_pRepository)
        {
            return;
        }
        for (auto _Ite = _pStep->Operation.rbegin(); _Ite != _pStep->Operation.rend(); _Ite++)
        {
            switch (_Ite->nType)
            {
            case iCAX::Database::RepositoryEventArgs::kModifyComponent:
            {
                auto _pDomain = _pRepository->GetDomain(_Ite->DomainID);
                if (_pDomain)
                {
                    _pDomain->GetEntity(_Ite->EntityID)->GetComponent(_Ite->strClassName)->SetProperties(_Ite->PreviousProperties);
                }
                break;
            }
            case iCAX::Database::RepositoryEventArgs::kAddComponent:
            {
                auto _pDomain = _pRepository->GetDomain(_Ite->DomainID);
                if (_pDomain)
                {
                    _pDomain->GetEntity(_Ite->EntityID)->RemoveComponent(_Ite->strClassName);
                }
                break;
            }
            case iCAX::Database::RepositoryEventArgs::kRemoveComponent:
            {
                auto _pDomain = _pRepository->GetDomain(_Ite->DomainID);
                if (_pDomain)
                {
                    _pDomain->GetEntity(_Ite->EntityID)->AddComponent(_Ite->strClassName)->SetProperties(_Ite->PreviousProperties);
                }
                break;
            }
            case iCAX::Database::RepositoryEventArgs::kAddEntity:
            {
                auto _pDomain = _pRepository->GetDomain(_Ite->DomainID);
                if (_pDomain)
                {
                    _pDomain->DeleteEntity(_Ite->EntityID);
                }
                break;
            }
            case iCAX::Database::RepositoryEventArgs::kDeleteEntity:
            {
                auto _pDomain = _pRepository->GetDomain(_Ite->DomainID);
                if (_pDomain)
                {
                    _pDomain->CreateEntity(_Ite->EntityID);
                }
                break;
            }
            default:
                break;
            }
        }
    }

    return;
}

//!< 获取域ID
iCAX::Data::uuid iCAX::Tracker::CDomainUndoRedo::GetDomainID() const
{
    return m_pDomain.expired() ? iCAX::Data::uuid() : m_pDomain.lock()->GetID();
}

//!< 获取当前操作列表
const std::list<iCAX::Database::OperationUnit>& iCAX::Tracker::CDomainUndoRedo::GetCurrentOperations() const
{
    return m_CurrOperationUnits;
}

//!< 获取当前操作列表
std::list<iCAX::Database::OperationUnit>& iCAX::Tracker::CDomainUndoRedo::GetCurrentOperations()
{
    return m_CurrOperationUnits;
}

//!< 获取栈顶撤销
std::weak_ptr<iCAX::Tracker::UROperationStep> iCAX::Tracker::CDomainUndoRedo::PeekUndo() const
{
    return m_UndoStack.empty() ? std::weak_ptr<UROperationStep>() : std::weak_ptr<UROperationStep>(m_UndoStack.back());
}

//!< 获取栈顶重做
std::weak_ptr<iCAX::Tracker::UROperationStep> iCAX::Tracker::CDomainUndoRedo::PeekRedo() const
{
    return m_RedoStack.empty() ? std::weak_ptr<UROperationStep>() : std::weak_ptr<UROperationStep>(m_RedoStack.back());
}

//!< 批量记录
void iCAX::Tracker::CDomainUndoRedo::BatchRecord(IN const std::list<iCAX::Database::OperationUnit>& Operations_)
{
    m_CurrOperationUnits.insert(m_CurrOperationUnits.end(), Operations_.begin(), Operations_.end());
}

//!< 操作后事件
void iCAX::Tracker::CDomainUndoRedo::Record(IN const iCAX::Database::OperationUnit& Operation_)
{
    m_CurrOperationUnits.push_back(Operation_);
}

//!< 获取撤销步骤列表
std::vector<std::tuple<iCAX::Data::uuid, std::string>> iCAX::Tracker::CDomainUndoRedo::GetUndoArray() const
{
    std::vector<std::tuple<iCAX::Data::uuid, std::string>> _Array;
    for (auto _Ite = m_UndoStack.rbegin(); _Ite != m_UndoStack.rend(); _Ite++)
    {
        _Array.push_back({ (*_Ite)->ID, (*_Ite)->strName });
    }
    return _Array;
}

//!< 获取重做步骤列表
std::vector<std::tuple<iCAX::Data::uuid, std::string>> iCAX::Tracker::CDomainUndoRedo::GetRedoArray() const
{
    std::vector<std::tuple<iCAX::Data::uuid, std::string>> _Array;
    for (auto _Ite = m_RedoStack.rbegin(); _Ite != m_RedoStack.rend(); _Ite++)
    {
        _Array.push_back({ (*_Ite)->ID, (*_Ite)->strName });
    }
    return _Array;
}
