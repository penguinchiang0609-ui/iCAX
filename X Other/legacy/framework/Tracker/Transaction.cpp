#include "pch.h"
#include "Transaction.h"
#include "../Database/Domain.h"
#include "../Database/IMetaRegistry.h"
//!< 构造函数
iCAX::Tracker::CTransaction::CTransaction(IN std::shared_ptr<iCAX::Database::IRepository> pRepository_)
    : m_pRepository(pRepository_)
    , m_bCommitSuccess(false)
{
}

//!< 析构函数
iCAX::Tracker::CTransaction::~CTransaction()
{
}

//!< 创建实体
void iCAX::Tracker::CTransaction::CreateEntity(IN const iCAX::Data::uuid& DomainID_, IN const iCAX::Data::uuid& EntityID_)
{
    Operation _Operation = { Operation::kCreateEntity, DomainID_, EntityID_ };
    m_Operations.push_back(_Operation);
}

//!< 释放实体
void iCAX::Tracker::CTransaction::DisposeEntity(IN const iCAX::Data::uuid& DomainID_, IN const iCAX::Data::uuid& EntityID_)
{
    Operation _Operation = { Operation::kDisposeEntity, DomainID_, EntityID_ };
    m_Operations.push_back(_Operation);
}

//!< 附加组件
void iCAX::Tracker::CTransaction::AttachComponent(IN const iCAX::Data::uuid& DomainID_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const iCAX::Data::PropertySet& Properties_)
{
    Operation _Operation = { Operation::kAttachComponent, DomainID_, EntityID_, strClassName_, Properties_ };
    m_Operations.push_back(_Operation);
}

//!< 移除组件
void iCAX::Tracker::CTransaction::DetachComponent(IN const iCAX::Data::uuid& DomainID_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_)
{
    Operation _Operation = { Operation::kDetachComponent, DomainID_, EntityID_,  strClassName_, {} };
    m_Operations.push_back(_Operation);
}

//!< 替换组件
void iCAX::Tracker::CTransaction::ModifyComponent(IN const iCAX::Data::uuid& DomainID_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const iCAX::Data::PropertySet& Properties_)
{
    Operation _Operation = { Operation::kModifyComponent, DomainID_, EntityID_, strClassName_, Properties_ };
    m_Operations.push_back(_Operation);
}

//!< 提交事务
void iCAX::Tracker::CTransaction::OnCommit()
{
    auto _pRepository = m_pRepository.lock();
    if (!_pRepository)
    {
        m_bCommitSuccess = false;
        return;
    }

    m_ActualOperations.clear();
    auto _pScope = _pRepository->BeginTransaction("TrackerTransaction");
    auto _Cancel = [&]()
    {
        try
        {
            if (_pScope && !_pScope->IsCompleted())
            {
                _pScope->Cancel();
            }
        }
        catch (...)
        {
        }
        m_bCommitSuccess = false;
    };

    try
    {
        for (Operation& _Operation : m_Operations)
        {
            switch (_Operation.nType)
            {
            case Operation::kCreateEntity:
            {
                auto _pDomain = _pRepository->GetDomain(_Operation.DomainID);
                if (!_pDomain)
                {
                    _Cancel();
                    return;
                }
                _pDomain->CreateEntity(_Operation.EntityID);
                break;
            }
            case Operation::kDisposeEntity:
            {
                auto _pDomain = _pRepository->GetDomain(_Operation.DomainID);
                if (!_pDomain)
                {
                    _Cancel();
                    return;
                }
                _pDomain->DeleteEntity(_Operation.EntityID);
                break;
            }
            case Operation::kAttachComponent:
            {
                auto _pDomain = _pRepository->GetDomain(_Operation.DomainID);
                if (!_pDomain)
                {
                    _Cancel();
                    return;
                }

                auto _pEntity = _pDomain->GetEntity(_Operation.EntityID);
                if (!_pEntity)
                {
                    _Cancel();
                    return;
                }

                std::string _strError;
                if (!iCAX::Database::GetGlobalMetaRegistry()->AllowAttachByName(*_pEntity.get(), _Operation.ClassType, _strError))
                {
                    _Cancel();
                    return;
                }

                auto _pComponent = _pEntity->AddComponent(_Operation.ClassType);
                if (!_Operation.Properties.empty())
                {
                    if (!iCAX::Database::GetGlobalMetaRegistry()->AllowModify(*_pComponent, _Operation.Properties, _strError))
                    {
                        _Cancel();
                        return;
                    }
                    _pComponent->SetProperties(_Operation.Properties);
                }
                break;
            }
            case Operation::kDetachComponent:
            {
                auto _pDomain = _pRepository->GetDomain(_Operation.DomainID);
                if (!_pDomain)
                {
                    _Cancel();
                    return;
                }

                auto _pEntity = _pDomain->GetEntity(_Operation.EntityID);
                if (!_pEntity)
                {
                    _Cancel();
                    return;
                }

                std::string _strError;
                if (!iCAX::Database::GetGlobalMetaRegistry()->AllowRemoveByName(*_pEntity.get(), _Operation.ClassType, _strError))
                {
                    _Cancel();
                    return;
                }

                _pEntity->RemoveComponent(_Operation.ClassType);
                break;
            }
            case Operation::kModifyComponent:
            {
                auto _pDomain = _pRepository->GetDomain(_Operation.DomainID);
                if (!_pDomain)
                {
                    _Cancel();
                    return;
                }

                auto _pEntity = _pDomain->GetEntity(_Operation.EntityID);
                if (!_pEntity)
                {
                    _Cancel();
                    return;
                }

                auto _pComponent = _pEntity->GetComponent(_Operation.ClassType);
                if (!_pComponent)
                {
                    _Cancel();
                    return;
                }

                std::string _strError;
                if (!iCAX::Database::GetGlobalMetaRegistry()->AllowModify(*_pComponent, _Operation.Properties, _strError))
                {
                    _Cancel();
                    return;
                }
                _pComponent->SetProperties(_Operation.Properties);
                break;
            }
            default:
            {
                break;
            }
            }
        }

        _pScope->Commit();
        m_Operations.clear();
        m_bCommitSuccess = true;
    }
    catch (...)
    {
        _Cancel();
    }
}

//!< 回滚事务
void iCAX::Tracker::CTransaction::OnRollback()
{
    if (m_pRepository.expired())
    {
        return;//!< 代码保护
    }

    for (auto _Ite = m_ActualOperations.rbegin(); _Ite != m_ActualOperations.rend(); _Ite++)
    {
        switch (_Ite->nType)
        {
        case iCAX::Database::RepositoryEventArgs::kModifyComponent:
        {
            auto _pDomain = m_pRepository.lock()->GetDomain(_Ite->DomainID);
            if (_pDomain)
            {
                _pDomain->GetEntity(_Ite->EntityID)->GetComponent(_Ite->strClassName)->SetProperties(_Ite->PreviousProperties);
            }
            break;
        }
        case iCAX::Database::RepositoryEventArgs::kAddComponent:
        {
            auto _pDomain = m_pRepository.lock()->GetDomain(_Ite->DomainID);
            if (_pDomain)
            {
                _pDomain->GetEntity(_Ite->EntityID)->RemoveComponent(_Ite->strClassName);
            }
            break;
        }
        case iCAX::Database::RepositoryEventArgs::kRemoveComponent:
        {
            auto _pDomain = m_pRepository.lock()->GetDomain(_Ite->DomainID);
            if (_pDomain)
            {
                _pDomain->GetEntity(_Ite->EntityID)->AddComponent(_Ite->strClassName)->SetProperties(_Ite->PreviousProperties);
            }
            break;
        }
        case iCAX::Database::RepositoryEventArgs::kAddEntity:
        {
            auto _pDomain = m_pRepository.lock()->GetDomain(_Ite->DomainID);
            if (_pDomain)
            {
                _pDomain->DeleteEntity(_Ite->EntityID);
            }
            break;
        }
        case iCAX::Database::RepositoryEventArgs::kDeleteEntity:
        {
            auto _pDomain = m_pRepository.lock()->GetDomain(_Ite->DomainID);
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
    m_ActualOperations.clear();
}

//!< 是否提交成功
bool iCAX::Tracker::CTransaction::IsCommitSuccess() const
{
    return m_bCommitSuccess;
}

//!< 获取实际操作列表
const std::list<iCAX::Database::OperationUnit>& iCAX::Tracker::CTransaction::GetActualOperations() const
{
    return m_ActualOperations;
}

//!< 修改前事件
void iCAX::Tracker::CTransaction::OnRepositoryChanging(IN void* pSender_, IN const iCAX::Database::RepositoryEventArgs& Args_)
{
    if (Args_.nType == iCAX::Database::RepositoryEventArgs::kBatchChanged)
    {
        return;
    }

    if (Args_.nType != iCAX::Database::RepositoryEventArgs::kModifyComponent)
    {
        m_ActualOperations.push_back(Args_);//!< write-log-ahead 先写日志，此处借鉴于数据库日志机制
        return;
    }

    auto _pMeta = iCAX::Database::GetGlobalMetaRegistry();
    iCAX::Database::RepositoryEventArgs _Filtered = Args_;
    _Filtered.PreviousProperties.clear();
    _Filtered.NewProperties.clear();

    for (const auto& [_strName, _NewValue] : Args_.NewProperties)
    {
        if (!_pMeta->HasPropertyByName(Args_.strClassName, _strName))
        {
            if (_pMeta->HasTypeByName(Args_.strClassName))
            {
                continue;
            }
        }
        else if (_pMeta->GetPropertyChangePolicyByName(Args_.strClassName, _strName) != iCAX::Database::EPropertyChangePolicy::Transactional)
        {
            continue;
        }

        auto _ItePrevious = Args_.PreviousProperties.find(_strName);
        if (_ItePrevious != Args_.PreviousProperties.end())
        {
            _Filtered.PreviousProperties.emplace(_strName, _ItePrevious->second);
        }
        _Filtered.NewProperties.emplace(_strName, _NewValue);
    }

    if (!_Filtered.NewProperties.empty())
    {
        m_ActualOperations.push_back(_Filtered);//!< write-log-ahead 先写日志，此处借鉴于数据库日志机制
    }
}

//!< 更改后事件
void iCAX::Tracker::CTransaction::OnRepositoryChanged(IN void* pSender_, IN const iCAX::Database::RepositoryEventArgs& Args_)
{
    //!< do-nothing
}
