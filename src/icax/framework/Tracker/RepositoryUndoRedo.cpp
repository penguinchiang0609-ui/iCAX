#include "pch.h"
#include "RepositoryUndoRedo.h"
#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include "../Database/IMetaRegistry.h"
#include "Data/uuid.h"

namespace
{
    struct ComponentOperationKey final
    {
        iCAX::Data::uuid DomainID;
        iCAX::Data::uuid EntityID;
        std::string ComponentClass;

        bool operator<(IN const ComponentOperationKey& Other_) const noexcept
        {
            if (DomainID != Other_.DomainID)
            {
                return DomainID < Other_.DomainID;
            }
            if (EntityID != Other_.EntityID)
            {
                return EntityID < Other_.EntityID;
            }
            return ComponentClass < Other_.ComponentClass;
        }
    };

    std::optional<iCAX::Database::OperationUnit> FilterTransactionalModifyProperties(IN const iCAX::Database::OperationUnit& Operation_)
    {
        if (Operation_.nType != iCAX::Database::RepositoryEventArgs::kModifyComponent)
        {
            return Operation_;
        }

        auto _pMeta = iCAX::Database::GetGlobalMetaRegistry();
        iCAX::Database::OperationUnit _Filtered = Operation_;
        _Filtered.PreviousProperties.clear();
        _Filtered.NewProperties.clear();

        for (const auto& [_strName, _NewValue] : Operation_.NewProperties)
        {
            if (!_pMeta->HasPropertyByName(Operation_.strClassName, _strName))
            {
                if (_pMeta->HasTypeByName(Operation_.strClassName))
                {
                    continue;
                }
            }
            else if (_pMeta->GetPropertyChangePolicyByName(Operation_.strClassName, _strName) != iCAX::Database::EPropertyChangePolicy::Transactional)
            {
                continue;
            }

            auto _ItePrevious = Operation_.PreviousProperties.find(_strName);
            if (_ItePrevious != Operation_.PreviousProperties.end())
            {
                _Filtered.PreviousProperties.emplace(_strName, _ItePrevious->second);
            }
            _Filtered.NewProperties.emplace(_strName, _NewValue);
        }

        if (_Filtered.NewProperties.empty())
        {
            return std::nullopt;
        }
        return _Filtered;
    }

    std::list<iCAX::Database::OperationUnit> BuildOperationsFromChangeSet(IN const iCAX::Database::CChangeSet& ChangeSet_)
    {
        std::list<iCAX::Database::OperationUnit> _Operations;

        for (const auto& _Change : ChangeSet_.CreatedEntities)
        {
            iCAX::Database::OperationUnit _Operation;
            _Operation.nType = iCAX::Database::RepositoryEventArgs::kAddEntity;
            _Operation.DomainID = _Change.Key.DomainID;
            _Operation.EntityID = _Change.Key.EntityID;
            _Operations.push_back(_Operation);
        }

        for (const auto& _Change : ChangeSet_.RemovedComponents)
        {
            iCAX::Database::OperationUnit _Operation;
            _Operation.nType = iCAX::Database::RepositoryEventArgs::kRemoveComponent;
            _Operation.DomainID = _Change.Key.DomainID;
            _Operation.EntityID = _Change.Key.EntityID;
            _Operation.strClassName = _Change.Key.ComponentClass;
            _Operation.PreviousProperties = _Change.PreviousProperties;
            _Operations.push_back(_Operation);
        }

        for (const auto& _Change : ChangeSet_.AddedComponents)
        {
            iCAX::Database::OperationUnit _Operation;
            _Operation.nType = iCAX::Database::RepositoryEventArgs::kAddComponent;
            _Operation.DomainID = _Change.Key.DomainID;
            _Operation.EntityID = _Change.Key.EntityID;
            _Operation.strClassName = _Change.Key.ComponentClass;
            _Operation.NewProperties = _Change.NewProperties;
            _Operations.push_back(_Operation);
        }

        std::map<ComponentOperationKey, iCAX::Database::OperationUnit> _ModifyOperations;
        for (const auto& _Change : ChangeSet_.ModifiedProperties)
        {
            ComponentOperationKey _Key{ _Change.Key.DomainID, _Change.Key.EntityID, _Change.Key.ComponentClass };
            auto& _Operation = _ModifyOperations[_Key];
            _Operation.nType = iCAX::Database::RepositoryEventArgs::kModifyComponent;
            _Operation.DomainID = _Change.Key.DomainID;
            _Operation.EntityID = _Change.Key.EntityID;
            _Operation.strClassName = _Change.Key.ComponentClass;
            _Operation.PreviousProperties[_Change.Key.PropertyName] = _Change.PreviousValue;
            _Operation.NewProperties[_Change.Key.PropertyName] = _Change.NewValue;
        }

        for (auto& [_, _Operation] : _ModifyOperations)
        {
            _Operations.push_back(_Operation);
        }

        for (const auto& _Change : ChangeSet_.DeletedEntities)
        {
            iCAX::Database::OperationUnit _Operation;
            _Operation.nType = iCAX::Database::RepositoryEventArgs::kDeleteEntity;
            _Operation.DomainID = _Change.Key.DomainID;
            _Operation.EntityID = _Change.Key.EntityID;
            _Operations.push_back(_Operation);
        }

        return _Operations;
    }
}

//!< 构造函数
iCAX::Tracker::CRepositoryUndoRedo::CRepositoryUndoRedo(IN std::shared_ptr<iCAX::Database::IRepository> pRepository_)
    : m_bHalt(true)
{
    m_pRepository = pRepository_;
    for (const auto& _DomainID : pRepository_->GetDomainIDs())
    {
        auto _pDomainUndoRedo = std::make_shared<CDomainUndoRedo>(m_pRepository.lock()->GetDomain(_DomainID));
        m_mapURInstancePtrs[_DomainID] = _pDomainUndoRedo;
    }
}

//!< 析构函数
iCAX::Tracker::CRepositoryUndoRedo::~CRepositoryUndoRedo()
{
}

//!< 获取仓储
std::weak_ptr<iCAX::Database::IRepository> iCAX::Tracker::CRepositoryUndoRedo::GetRepository() const
{
    return m_pRepository;
}

//!< 开始记录撤销还原
bool iCAX::Tracker::CRepositoryUndoRedo::BeginStep(IN const iCAX::Data::uuid& DomainID_, IN const std::string& strName_)
{
    if (strName_.empty())
    {
        return false;
    }
    if (IsRecording())
    {
        return false;
    }

    auto _Ite = m_mapURInstancePtrs.find(DomainID_);
    if (_Ite == m_mapURInstancePtrs.end())
    {
        return false;
    }
    if (_Ite->second->BeginStep(strName_))
    {
        m_CurrentInstancePtr = std::weak_ptr<CDomainUndoRedo>(_Ite->second);
        return true;
    }

    return false;
}

//!< 停止撤销还原
bool iCAX::Tracker::CRepositoryUndoRedo::EndStep()
{
    if (m_CurrentInstancePtr.expired())
    {
        return false;
    }
    auto _pStep = m_CurrentInstancePtr.lock()->EndStep();
    if (_pStep.expired())
    {
        return false;
    }

    //!< 如果该撤销还原影响到其他实例，则其他实例中也记录
    for (auto& _DomainID : _pStep.lock()->lstDomain)
    {
        if (_DomainID == m_CurrentInstancePtr.lock()->GetDomainID())
        {
            continue;
        }
        auto _IteOther = m_mapURInstancePtrs.find(_DomainID);
        if (_IteOther == m_mapURInstancePtrs.end())
        {
            continue;//对应实例已经释放，无需处理
        }
        _IteOther->second->PushUndo(_pStep.lock());
    }

    m_CurrentInstancePtr = std::weak_ptr<CDomainUndoRedo>();

    return true;
}

//!< 是否正在记录
bool iCAX::Tracker::CRepositoryUndoRedo::IsRecording() const
{
    return !m_CurrentInstancePtr.expired();
}

//!< 获取正在录制撤销还原的域
iCAX::Data::uuid iCAX::Tracker::CRepositoryUndoRedo::GetCurrentRecordingDomain() const
{
    return m_CurrentInstancePtr.expired() ? iCAX::Data::GenerateNilUUID() : m_CurrentInstancePtr.lock()->GetDomainID();
}

//!< 是否可以重做
bool iCAX::Tracker::CRepositoryUndoRedo::CanRedo(IN const iCAX::Data::uuid& DomainID_) const
{
    auto _Ite = m_mapURInstancePtrs.find(DomainID_);
    if (_Ite == m_mapURInstancePtrs.end())
    {
        return false;
    }
    return _Ite->second->CanRedo();
}

//!< 是否可以撤销
bool iCAX::Tracker::CRepositoryUndoRedo::CanUndo(IN const iCAX::Data::uuid& DomainID_) const
{
    auto _Ite = m_mapURInstancePtrs.find(DomainID_);
    if (_Ite == m_mapURInstancePtrs.end())
    {
        return false;
    }
    return _Ite->second->CanUndo();
}

//!<  获取撤销步骤列表
std::vector<std::tuple<iCAX::Data::uuid, std::string>> iCAX::Tracker::CRepositoryUndoRedo::GetUndoArray(IN const iCAX::Data::uuid& DomainID_) const
{
    auto _Ite = m_mapURInstancePtrs.find(DomainID_);
    if (_Ite == m_mapURInstancePtrs.end())
    {
        return {};
    }
    return _Ite->second->GetUndoArray();
}

//!< 获取重做步骤列表
std::vector<std::tuple<iCAX::Data::uuid, std::string>> iCAX::Tracker::CRepositoryUndoRedo::GetRedoArray(IN const iCAX::Data::uuid& DomainID_) const
{
    auto _Ite = m_mapURInstancePtrs.find(DomainID_);
    if (_Ite == m_mapURInstancePtrs.end())
    {
        return {};
    }
    return _Ite->second->GetRedoArray();
}

//!< 重做
bool iCAX::Tracker::CRepositoryUndoRedo::Redo(IN const iCAX::Data::uuid& DomainID_)
{
    if (IsRecording())
    {
        return false;
    }
    auto _Ite = m_mapURInstancePtrs.find(DomainID_);
    if (_Ite == m_mapURInstancePtrs.end())
    {
        return false;
    }
    std::weak_ptr<UROperationStep> _StepPtr = _Ite->second->PeekRedo();
    if (_StepPtr.expired())
    {
        return false;
    }

    UROperationStep* _pStep = _StepPtr.lock().get();

    std::for_each(_pStep->lstDomain.begin(), _pStep->lstDomain.end(), [&](auto& _DomainID)
    {
        if (_DomainID != DomainID_)
        {
            std::map<iCAX::Data::uuid, std::shared_ptr<CDomainUndoRedo>>::iterator _IteOther = m_mapURInstancePtrs.find(_DomainID);
            if (_IteOther == m_mapURInstancePtrs.end())
            {
                return;// 实例已经释放，无需处理
            }

            _IteOther->second->Redo(true);
        }
    });
    _Ite->second->Redo();

    return true;

}

//!< 撤销
bool iCAX::Tracker::CRepositoryUndoRedo::Undo(IN const iCAX::Data::uuid& DomainID_)
{
    if (IsRecording())
    {
        return false;
    }

    auto _Ite = m_mapURInstancePtrs.find(DomainID_);
    if (_Ite == m_mapURInstancePtrs.end())
    {
        return false;
    }
    std::weak_ptr<UROperationStep> _StepPtr = _Ite->second->PeekUndo();
    if (_StepPtr.expired())
    {
        return false;
    }
    UROperationStep* _pStep = _StepPtr.lock().get();
    std::for_each(_pStep->lstDomain.begin(), _pStep->lstDomain.end(), [&](auto& _InstanceID)
        {
            if (_InstanceID != DomainID_)
            {
                std::map<iCAX::Data::uuid, std::shared_ptr<CDomainUndoRedo>>::iterator _IteOther = m_mapURInstancePtrs.find(_InstanceID);
                if (_IteOther == m_mapURInstancePtrs.end())
                {
                    return;//!< 实例已经释放，无需处理
                }
                _IteOther->second->Undo(true);
            }
        });

    _Ite->second->Undo();
    return true;
}

//!< 挂起
void iCAX::Tracker::CRepositoryUndoRedo::Halt(IN bool bHalt_)
{
    m_bHalt = bHalt_;
}

//!< 批量记录
void iCAX::Tracker::CRepositoryUndoRedo::BatchRecord(IN const std::list<iCAX::Database::OperationUnit>& Operations_)
{
    if (m_CurrentInstancePtr.expired())
    {
        return;
    }

    std::list<iCAX::Database::OperationUnit> _TransactionalOperations;
    for (const auto& _Operation : Operations_)
    {
        if (auto _Filtered = FilterTransactionalModifyProperties(_Operation))
        {
            _TransactionalOperations.push_back(*_Filtered);
        }
    }

    m_CurrentInstancePtr.lock()->BatchRecord(_TransactionalOperations);
}

//!< 修改前事件
void iCAX::Tracker::CRepositoryUndoRedo::OnRepositoryChanging(IN void* pSender_, IN const iCAX::Database::RepositoryEventArgs& Args_)
{
}

//!< 更改后事件
void iCAX::Tracker::CRepositoryUndoRedo::OnRepositoryChanged(IN void* pSender_, IN const iCAX::Database::RepositoryEventArgs& Args_)
{
    if (Args_.nType == iCAX::Database::RepositoryEventArgs::kBatchChanged && Args_.pChangeSet)
    {
        for (const auto& _Domain : Args_.pChangeSet->CreatedDomains)
        {
            if (auto _pDomain = m_pRepository.lock()->GetDomain(_Domain.DomainID))
            {
                m_mapURInstancePtrs[_Domain.DomainID] = std::make_shared<CDomainUndoRedo>(_pDomain);
            }
        }

        for (const auto& _Domain : Args_.pChangeSet->DeletedDomains)
        {
            m_mapURInstancePtrs.erase(_Domain.DomainID);
        }

        if (m_bHalt || Args_.pChangeSet->Kind != iCAX::Database::EChangeScopeKind::UserCommand || m_CurrentInstancePtr.expired())
        {
            return;
        }

        BatchRecord(BuildOperationsFromChangeSet(*Args_.pChangeSet));
        return;
    }

    if (Args_.nType == iCAX::Database::RepositoryEventArgs::kAddDomain)
    {
        //!< 创建域撤销还原
        auto _pDomainUndoRedo = std::make_shared<CDomainUndoRedo>(m_pRepository.lock()->GetDomain(Args_.DomainID));
        m_mapURInstancePtrs[Args_.DomainID] = _pDomainUndoRedo;
    }
    else if (Args_.nType == iCAX::Database::RepositoryEventArgs::kDeleteDomain)
    {
        auto _Ite = m_mapURInstancePtrs.find(Args_.DomainID);
        if (_Ite == m_mapURInstancePtrs.end())
        {
            return;//!< 代码保护
        }
        m_mapURInstancePtrs.erase(_Ite);
    }

    if (m_bHalt)//!< 屏蔽侦听
    {
        return;
    }

    if (m_CurrentInstancePtr.expired())
    {
        return;
    }

    auto _Filtered = FilterTransactionalModifyProperties(Args_);
    if (!_Filtered)
    {
        return;
    }

    m_CurrentInstancePtr.lock()->Record(*_Filtered);
}
