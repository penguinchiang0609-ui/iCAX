#include "pch.h"
#include "Repository.h"
#include "Domain.h"
#include "MetaRegistry.h"

//!< 构造函数
iCAX::Database::CRepository::CRepository(IN const iCAX::Data::uuid& UID_)
{
    m_UID = UID_;
    m_pVerisonTable = std::make_shared<VersionTable>();
}

//!< 析构函数
iCAX::Database::CRepository::~CRepository()
{
}

//! 获取ID
const iCAX::Data::uuid& iCAX::Database::CRepository::GetID() const
{
    return m_UID;
}

//! 初始化
void iCAX::Database::CRepository::Initialzie()
{
    auto _pDomain = std::make_shared<CDomain>(shared_from_this(), m_UID, true);
    _pDomain->Initialize();
    _pDomain->AddObserver(shared_from_this());
    m_mapDomains[m_UID] = _pDomain;
}

//!< 创造域
std::shared_ptr<iCAX::Database::IDomain> iCAX::Database::CRepository::CreateDomain(IN const iCAX::Data::uuid& ID_, IN const bool& bPersistent_)
{
    if (ID_ == m_UID)
    {
        return GetDomain(m_UID);
    }
    auto _Ite = m_mapDomains.find(ID_);
    if (_Ite != m_mapDomains.end())
    {
        throw std::runtime_error("Domain already exists: " + to_string(ID_));
    }
    auto _pDomain = std::make_shared<CDomain>(shared_from_this(), ID_, bPersistent_);
    
    TriggerRepositoryChanging(RepositoryEventArgs::kAddDomain, ID_, {}, {}, {}, {}, {}, {}, _pDomain);
    m_mapDomains[ID_] = _pDomain;
    TriggerRepositoryChanged(RepositoryEventArgs::kAddDomain, ID_, {}, {}, {}, {}, {}, {}, _pDomain);

    _pDomain->AddObserver(shared_from_this());
    return _pDomain;
}

//!< 是否包含域
bool iCAX::Database::CRepository::HasDomain(IN const iCAX::Data::uuid& ID_) const
{
    return m_mapDomains.find(ID_) != m_mapDomains.end();
}

//!< 获取域
std::shared_ptr<iCAX::Database::IDomain> iCAX::Database::CRepository::GetDomain(IN const iCAX::Data::uuid& ID_)
{
    auto _Ite = m_mapDomains.find(ID_);
    if (_Ite != m_mapDomains.end())
    {
        return _Ite->second;
    }

    return nullptr;
}


//!< 移除域
void iCAX::Database::CRepository::DeleteDomain(IN const iCAX::Data::uuid& ID_)
{
    if (ID_ == m_UID)
    {
        return;//!< 元域不得删除
    }
    auto _Ite = m_mapDomains.find(ID_);
    if (_Ite == m_mapDomains.end())
    {
        return;
    }
    _Ite->second->CleanUp();
    _Ite->second->RemoveObserver(shared_from_this());
    auto _pDomain = _Ite->second;
    TriggerRepositoryChanging(RepositoryEventArgs::kDeleteDomain, ID_, {}, {}, {}, {}, {}, {}, _pDomain);
    m_mapDomains.erase(_Ite);
    TriggerRepositoryChanged(RepositoryEventArgs::kDeleteDomain, ID_, {}, {}, {}, {}, {}, {}, _pDomain);
}

//!< 域数量
int iCAX::Database::CRepository::DomainCount() const
{
    return (int)m_mapDomains.size();
}

//!< 获取域ID列表
std::vector<iCAX::Data::uuid> iCAX::Database::CRepository::GetDomainIDs() const
{
    std::vector<iCAX::Data::uuid> _vecTemp;
    for (auto& [_ID, _] : m_mapDomains)
    {
        _vecTemp.push_back(_ID);
    }
    return _vecTemp;
}

//!< 获取MetaEntity
std::shared_ptr<iCAX::Database::IEntity> iCAX::Database::CRepository::GetMetaEntity()
{
    return GetDomain(m_UID)->GetMetaEntity();
}

//! 清空
void iCAX::Database::CRepository::CleanUp(IN const bool& bForced_/* = false*/)
{
    auto _vecIDs = GetDomainIDs();
    for (auto _Ite = _vecIDs.begin(); _Ite != _vecIDs.end(); _Ite++)
    {
        if (*_Ite == m_UID)
        {
            _vecIDs.erase(_Ite);
            break;
        }
    }

    for (auto& _ID : _vecIDs)
    {
        DeleteDomain(_ID);
    }

    if (bForced_)
    {
        //! 最后删除本体实体
        auto _Ite = m_mapDomains.find(m_UID);
        if (_Ite == m_mapDomains.end())
        {
            return;
        }
        _Ite->second->CleanUp();
        _Ite->second->RemoveObserver(shared_from_this());
        auto _pDomain = _Ite->second;
        TriggerRepositoryChanging(RepositoryEventArgs::kDeleteDomain, m_UID, {}, {}, {}, {}, {}, {}, _pDomain);
        m_mapDomains.erase(_Ite);
        TriggerRepositoryChanged(RepositoryEventArgs::kDeleteDomain, m_UID, {}, {}, {}, {}, {}, {}, _pDomain);
    }
}

//! 是否有效
bool iCAX::Database::CRepository::IsValid()
{
    return HasDomain(m_UID);//! 如果缺少了本体域，则标明已经强制清空了，不再可用
}

//! 获取组件版本号
size_t iCAX::Database::CRepository::GetComponentVersion(IN const iCAX::Data::uuid& nDomainID_, IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const
{
    return m_pVerisonTable->GetVersion({ nDomainID_, nEntityID_, strComponentType_});;
}

//! 组件版本升级
void iCAX::Database::CRepository::BumpComponentVersion(IN const iCAX::Data::uuid& nDomainID_, IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const
{
    m_pVerisonTable->BumpVersion({ nDomainID_, nEntityID_, strComponentType_ });
}

//! 是否发生了更改
bool iCAX::Database::CRepository::IsComponentChanged(IN const iCAX::Data::uuid& nDomainID_, IN const iCAX::Data::uuid& nEntityID_, IN const std::string& strComponentType_) const
{
    return m_pVerisonTable->IsDirty({ nDomainID_, nEntityID_, strComponentType_ });
}

//! 清空修改标记位
void iCAX::Database::CRepository::ResetComponentChangedFlag()
{
    m_pVerisonTable->ClearDirty();
}

//!< 域修改前事件
void iCAX::Database::CRepository::OnDomainChanging(IN void* pSender_, IN const DomainEventArgs& Args_)
{
    TriggerRepositoryChanging((RepositoryEventArgs::EventType)(Args_.nType), Args_.DomainID, Args_.EntityID, Args_.strClassName, Args_.PreviousProperties, Args_.NewProperties, Args_.pComponent, Args_.pEntity, Args_.pDomain);
}

//!< 域更改后事件
void iCAX::Database::CRepository::OnDomainChanged(IN void* pSender_, IN const DomainEventArgs& Args_)
{
    if (Args_.nType == iCAX::Database::DomainEventArgs::kModifyComponent)
    {
        m_pVerisonTable->BumpVersion({ Args_.DomainID, Args_.EntityID, Args_.strClassName });
    }
    else if (Args_.nType == iCAX::Database::DomainEventArgs::kAddComponent)
    {
        m_pVerisonTable->Reset({ Args_.DomainID, Args_.EntityID, Args_.strClassName });
    }
    else if (Args_.nType == iCAX::Database::DomainEventArgs::kRemoveComponent)
    {
        m_pVerisonTable->Remove({ Args_.DomainID, Args_.EntityID, Args_.strClassName });
    }

    TriggerRepositoryChanged((RepositoryEventArgs::EventType)(Args_.nType), Args_.DomainID, Args_.EntityID, Args_.strClassName, Args_.PreviousProperties, Args_.NewProperties, Args_.pComponent, Args_.pEntity, Args_.pDomain);
}

//!< 添加观察者
void iCAX::Database::CRepository::AddObserver(IN std::shared_ptr<IRepositoryEventListener> Observer_)
{
    m_Observers.push_back(Observer_);
}

//!< 移除观察者
void iCAX::Database::CRepository::RemoveObserver(IN std::shared_ptr<IRepositoryEventListener> Observer_)
{
    m_Observers.remove_if([&](const std::weak_ptr<IRepositoryEventListener>& weakObserver)
        {
            return weakObserver.lock() == Observer_;
        });
}

//!< 前触发
void iCAX::Database::CRepository::TriggerRepositoryChanging(IN const RepositoryEventArgs::EventType& nType_, IN const iCAX::Data::uuid& DomainID_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const PropertySet& Previous_, IN const PropertySet& New_, IN std::shared_ptr<CComponentBase> pComponent_, IN std::shared_ptr<IEntity> pEntity_, IN std::shared_ptr<IDomain> pDomain_)
{
    // 遍历观察者列表
    for (auto _Ite = m_Observers.begin(); _Ite != m_Observers.end(); )
    {
        // 检查 是否还有效
        if (auto _Observer = _Ite->lock())
        {
            _Observer->OnRepositoryChanging(this, { nType_, GetID(), DomainID_, EntityID_, strClassName_, Previous_, New_, pComponent_, pEntity_, pDomain_, shared_from_this() });
            ++_Ite;
        }
        else
        {
            // 如果 已失效，移除它
            _Ite = m_Observers.erase(_Ite);
        }
    }
}

//!< 后触发
void iCAX::Database::CRepository::TriggerRepositoryChanged(IN const RepositoryEventArgs::EventType& nType_, IN const iCAX::Data::uuid& DomainID_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strClassName_, IN const PropertySet& Previous_, IN const PropertySet& New_, IN std::shared_ptr<CComponentBase> pComponent_, IN std::shared_ptr<IEntity> pEntity_, IN std::shared_ptr<IDomain> pDomain_)
{
    // 遍历观察者列表
    for (auto _Ite = m_Observers.begin(); _Ite != m_Observers.end(); )
    {
        // 检查 是否还有效
        if (auto _Observer = _Ite->lock())
        {
            _Observer->OnRepositoryChanged(this, { nType_, GetID(),  DomainID_, EntityID_, strClassName_, Previous_, New_, pComponent_, pEntity_, pDomain_, shared_from_this()});
            ++_Ite;
        }
        else
        {
            // 如果 已失效，移除它
            _Ite = m_Observers.erase(_Ite);
        }
    }
}
