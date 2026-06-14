#include "pch.h"
#include "Domain.h"

//!< 构造函数
iCAX::Database::CDomain::CDomain(IN std::shared_ptr<IRepository> pRepository_, IN const iCAX::Data::uuid& UID_, IN const bool& bPersistent_)
{
    m_pRepository = pRepository_;
    m_UID = UID_;
    m_bPersistent = bPersistent_;
}

//!< 析构函数
iCAX::Database::CDomain::~CDomain()
{
}

//!< 获取ID
iCAX::Data::uuid iCAX::Database::CDomain::GetID() const
{
    return m_UID;
}

void iCAX::Database::CDomain::Initialize()
{
    auto _pEntity = std::make_shared<CEntity>(shared_from_this(), m_UID);
    _pEntity->AddObserver(shared_from_this());
    m_mapEntities[m_UID] = _pEntity;

    m_pEntitesView = std::make_shared<CEntitiesView>(shared_from_this());
    this->AddObserver(m_pEntitesView);
}

//!< 创建实体
std::shared_ptr<iCAX::Database::IEntity> iCAX::Database::CDomain::CreateEntity(IN const iCAX::Data::uuid& ID_)
{
    if (ID_ == m_UID)
    {
        return GetEntity(m_UID);
    }
    auto _Ite = m_mapEntities.find(ID_);
    if (_Ite != m_mapEntities.end())
    {
        throw std::runtime_error("Entity already exists");
    }
    auto _pEntity = std::make_shared<CEntity>(shared_from_this(), ID_);

    TriggerDomainChanging(DomainEventArgs::kAddEntity, ID_, {}, {}, {}, {}, _pEntity);
    m_mapEntities[ID_] = _pEntity;
    _pEntity->AddObserver(shared_from_this());//!< 增加监控
    TriggerDomainChanged(DomainEventArgs::kAddEntity, ID_, {}, {}, {}, {}, _pEntity);
    return _pEntity;
}

//!< 获取实体
std::shared_ptr<iCAX::Database::IEntity> iCAX::Database::CDomain::GetEntity(IN const iCAX::Data::uuid& ID_) const
{
    auto _Ite = m_mapEntities.find(ID_);
    if (_Ite != m_mapEntities.end())
    {
        return _Ite->second;
    }
 
    return nullptr;
}

//!< 是否包含实体
bool iCAX::Database::CDomain::HasEntity(IN const iCAX::Data::uuid& ID_) const
{
    return m_mapEntities.find(ID_) != m_mapEntities.end();
}

//!< 删除实体
void iCAX::Database::CDomain::DeleteEntity(IN const iCAX::Data::uuid& ID_)
{
    if (ID_ == m_UID)
    {
        return;//!< MetaEntity不允许删除
    }
    auto _Ite = m_mapEntities.find(ID_);
    if (_Ite == m_mapEntities.end())
    {
        return;
    }
    _Ite->second->Cleanup();//!< 清除所有组件
    _Ite->second->RemoveObserver(shared_from_this());//!< 移除监控
    auto _pEntity = _Ite->second;
    TriggerDomainChanging(DomainEventArgs::kDeleteEntity, ID_, {}, {}, {}, {}, _pEntity);
    m_mapEntities.erase(_Ite);
    TriggerDomainChanged(DomainEventArgs::kDeleteEntity, ID_, {}, {}, {}, {}, _pEntity);
}

//!< 筛选实体
std::vector<std::shared_ptr<iCAX::Database::IEntity>> iCAX::Database::CDomain::FilterEntities(IN std::function<bool(std::shared_ptr<IEntity>)> funcWhere_) const
{
    std::vector<std::shared_ptr<IEntity>> _vecTemp;

    for (auto& [_, _pEntity] : m_mapEntities)
    {
        if (funcWhere_(_pEntity))
        {
            _vecTemp.push_back(_pEntity);
        }
    }

    return _vecTemp;
}

//!< 实体数量
int iCAX::Database::CDomain::EntityCount() const
{
    return (int)m_mapEntities.size();
}

//!< 获取实体的ID列表
std::vector<iCAX::Data::uuid> iCAX::Database::CDomain::GetEntityIDs() const
{
    std::vector<iCAX::Data::uuid> _vecTemp;
    for (auto& [_ID, _] : m_mapEntities)
    {
        _vecTemp.push_back(_ID);
    }
    return _vecTemp;
}

//! 清空
void iCAX::Database::CDomain::CleanUp(IN const bool& bForced_/* = false*/)
{
    auto _vecIDs = GetEntityIDs();
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
        DeleteEntity(_ID);
    }

    if (bForced_)
    {
        //! 最后删除本体实体
        auto _Ite = m_mapEntities.find(m_UID);
        if (_Ite == m_mapEntities.end())
        {
            return;
        }
        _Ite->second->Cleanup();//!< 清除所有组件
        auto _pEntity = _Ite->second;
        TriggerDomainChanging(DomainEventArgs::kDeleteEntity, m_UID, {}, {}, {}, {}, _pEntity);
        _Ite->second->RemoveObserver(shared_from_this());//!< 移除监控
        m_mapEntities.erase(_Ite);
        TriggerDomainChanged(DomainEventArgs::kDeleteEntity, m_UID, {}, {}, {}, {}, _pEntity);
    }
}

//! 是否有效
bool iCAX::Database::CDomain::IsValid()
{
    return HasEntity(m_UID);
}

//!< 组件修改前事件
void iCAX::Database::CDomain::OnEntityChanging(IN void* pSender_, IN const EntityEventArgs& Args_)
{
    TriggerDomainChanging((DomainEventArgs::EventType)(Args_.nType), Args_.EntityID, Args_.strClassName, Args_.PreviousProperties, Args_.NewProperties, Args_.pComponent, Args_.pEntity);
}

//!< 组件更改后事件
void iCAX::Database::CDomain::OnEntityChanged(IN void* pSender_, IN const EntityEventArgs& Args_)
{
    TriggerDomainChanged((DomainEventArgs::EventType)(Args_.nType), Args_.EntityID, Args_.strClassName, Args_.PreviousProperties, Args_.NewProperties, Args_.pComponent, Args_.pEntity);
}

//!< 添加观察者
void iCAX::Database::CDomain::AddObserver(IN std::shared_ptr<IDomainEventListener> Observer_)
{
    m_Observers.push_back(Observer_);
}

//!< 移除观察者
void iCAX::Database::CDomain::RemoveObserver(IN std::shared_ptr<IDomainEventListener> Observer_)
{
    m_Observers.remove_if([&](const std::weak_ptr<IDomainEventListener>& weakObserver)
    {
        return weakObserver.expired() || weakObserver.lock() == Observer_;
    });
}

//!< 前触发
void iCAX::Database::CDomain::TriggerDomainChanging(IN const DomainEventArgs::EventType& nType_, IN const iCAX::Data::uuid& ID_, IN const std::string& strClassName_, IN const PropertySet& Previous_, IN const PropertySet& New_, IN std::shared_ptr<CComponentBase> pComponent_, IN std::shared_ptr<IEntity> pEntity_)
{
    // 遍历观察者列表
    for (auto _Ite = m_Observers.begin(); _Ite != m_Observers.end(); )
    {
        if (auto _Observer = _Ite->lock())
        {
            _Observer->OnDomainChanging(this, { nType_, GetID(), ID_, strClassName_, Previous_, New_, pComponent_, pEntity_, shared_from_this()});
            ++_Ite;
        }
        else
        {
            _Ite = m_Observers.erase(_Ite);
        }
    }
}

//!< 后触发
void iCAX::Database::CDomain::TriggerDomainChanged(IN const DomainEventArgs::EventType& nType_, IN const iCAX::Data::uuid& ID_, IN const std::string& strClassName_, IN const PropertySet& Previous_, IN const PropertySet& New_, IN std::shared_ptr<CComponentBase> pComponent_, IN std::shared_ptr<IEntity> pEntity_)
{
    // 遍历观察者列表
    for (auto _Ite = m_Observers.begin(); _Ite != m_Observers.end(); )
    {
        // 检查 是否还有效
        if (auto _Observer = _Ite->lock())
        {
            _Observer->OnDomainChanged(this, { nType_, GetID(),  ID_, strClassName_, Previous_, New_, pComponent_, pEntity_, shared_from_this() });
            ++_Ite;
        }
        else
        {
            // 如果 已失效，移除它
            _Ite = m_Observers.erase(_Ite);
        }
    }
}

//!< 获取所在仓储
std::shared_ptr<iCAX::Database::IRepository> iCAX::Database::CDomain::GetRepository() const
{
    if (m_pRepository.expired())
    {
        return nullptr;
    }
    return m_pRepository.lock();
}

//!< 获取MetaEntity
std::shared_ptr<iCAX::Database::IEntity> iCAX::Database::CDomain::GetMetaEntity() const
{
    return GetEntity(m_UID);
}

//!< 获取实体视图
iCAX::Database::IEntitiesView& iCAX::Database::CDomain::GetView() const
{
    return *m_pEntitesView;
}

//! 是否是持久化的
bool iCAX::Database::CDomain::IsPersistent()
{
    return m_bPersistent;
}

