#include "pch.h"
#include "Entity.h"
#include "GenericComponent.h"
#include "IRepository.h"
#include "IMetaRegistry.h"

//!< 构造函数
iCAX::Database::CEntity::CEntity(IN std::shared_ptr<IDomain> pDomain_, IN const iCAX::Data::uuid& UID_)
    : m_pDomain(pDomain_)
    , m_UID(UID_)
{
}

//!< 析构函数
iCAX::Database::CEntity::~CEntity()
{
}

//!< 获取ID
const iCAX::Data::uuid& iCAX::Database::CEntity::GetID() const
{
    return m_UID;
}

//!< 添加组件
std::shared_ptr<iCAX::Database::CComponentBase> iCAX::Database::CEntity::AddComponent(IN const std::string& strClassName_)
{
    if (m_mapComponents.contains(strClassName_))
    {
        throw std::runtime_error("Component already exists: " + strClassName_);
    }
    std::shared_ptr<CComponentBase> _pComponent = GetGlobalMetaRegistry()->CreateByName(strClassName_, shared_from_this());
    auto _NewProperties = _pComponent->GetProperties();
    TriggerEntityChanging(EntityEventArgs::kAddComponent, strClassName_, {}, _NewProperties, _pComponent);
    m_mapComponents[strClassName_] = _pComponent;
    m_ComponentClasses.push_back(strClassName_);
    _pComponent->AddObserver(shared_from_this());//!< 侦听目标组件的属性事件
    TriggerEntityChanged(EntityEventArgs::kAddComponent, strClassName_, {}, _NewProperties, _pComponent);
    return _pComponent;
}

//!< 移除组件
void iCAX::Database::CEntity::RemoveComponent(IN const std::string& strClassName_)
{
    auto _Ite = m_mapComponents.find(strClassName_);
    if (_Ite == m_mapComponents.end())
    {
        return;
    }
    auto _pCompoennt = _Ite->second;
    auto _PreviousProperties = _pCompoennt->GetProperties();
    _Ite->second->RemoveObserver(shared_from_this());//!< 移除对目标组件的属性事件侦听
    TriggerEntityChanging(EntityEventArgs::kRemoveComponent, strClassName_, _PreviousProperties, {}, _pCompoennt);
    m_mapComponents.erase(_Ite);
    m_ComponentClasses.erase(std::remove(m_ComponentClasses.begin(), m_ComponentClasses.end(), strClassName_), m_ComponentClasses.end());
    TriggerEntityChanged(EntityEventArgs::kRemoveComponent, strClassName_, _PreviousProperties, {}, _pCompoennt);
}

//!< 获取组件
std::shared_ptr<iCAX::Database::CComponentBase> iCAX::Database::CEntity::GetComponent(IN const std::string& strClassName_) const
{
    auto _Ite = m_mapComponents.find(strClassName_);
    if (_Ite != m_mapComponents.end())
    {
        return _Ite->second;
    }

    return nullptr;
}

//!< 获取组件列表
std::vector<std::shared_ptr<iCAX::Database::CComponentBase>> iCAX::Database::CEntity::GetComponents(IN const std::string& strClassName_) const
{
    auto _pMeta = GetGlobalMetaRegistry();
    std::vector<std::shared_ptr<iCAX::Database::CComponentBase>> _vecResult;
    for (auto & [_Key, _pComponent] : m_mapComponents)
    {
        if (_pMeta->IsInheritance(_Key, strClassName_))
        {
            _vecResult.push_back(_pComponent);
        }
    }

    return _vecResult;
}

//!< 是否含有指定组件
bool iCAX::Database::CEntity::HasComponent(IN const std::string& strClassName_) const
{
    return m_mapComponents.contains(strClassName_);
}

//!< 获取组件类型列表
std::vector<std::string> iCAX::Database::CEntity::GetComponentClasses() const
{
    return m_ComponentClasses;
}

//!< 获取组件数量
int iCAX::Database::CEntity::ComponentsCount() const
{
    return (int)m_mapComponents.size();
}

//!< 移除所有组件
void iCAX::Database::CEntity::Cleanup()
{
    auto _vecClasses = m_ComponentClasses;
    for (auto _Ite = _vecClasses.rbegin(); _Ite != _vecClasses.rend(); _Ite++)
    {
        RemoveComponent(*_Ite);
    }
}

//!< 获取所在域
std::shared_ptr<iCAX::Database::IDomain> iCAX::Database::CEntity::GetDomain() const
{
    if (m_pDomain.expired())
    {
        return nullptr;
    }
    return m_pDomain.lock();
}

//!< 添加观察者
void iCAX::Database::CEntity::AddObserver(IN std::shared_ptr<IEntityEventListener> Observer_)
{
    m_Observers.push_back(Observer_);
}

//!< 移除观察者
void iCAX::Database::CEntity::RemoveObserver(IN std::shared_ptr<IEntityEventListener> Observer_)
{
    m_Observers.remove_if([&](const std::weak_ptr<IEntityEventListener>& weakObserver)
        {
            return weakObserver.expired() || weakObserver.lock() == Observer_;
        });
}

//!< 前触发
void iCAX::Database::CEntity::TriggerEntityChanging(IN const EntityEventArgs::EventType& nType_, IN const std::string& strClassName_, IN const PropertySet& Previous_, IN const PropertySet& New_, IN std::shared_ptr<CComponentBase> pComponent_)
{
    // 遍历观察者列表
    for (auto _Ite = m_Observers.begin(); _Ite != m_Observers.end(); )
    {
        // 检查 是否还有效
        if (auto _Observer = _Ite->lock())
        {
            _Observer->OnEntityChanging(this, { nType_, GetID(), strClassName_, Previous_, New_, pComponent_, shared_from_this()});
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
void iCAX::Database::CEntity::TriggerEntityChanged(IN const EntityEventArgs::EventType& nType_, IN const std::string& strClassName_, IN const PropertySet& Previous_, IN const PropertySet& New_, IN std::shared_ptr<CComponentBase> pComponent_)
{
    // 遍历观察者列表
    for (auto _Ite = m_Observers.begin(); _Ite != m_Observers.end(); )
    {
        // 检查 是否还有效
        if (auto _Observer = _Ite->lock())
        {
            _Observer->OnEntityChanged(this, { nType_, GetID(),  strClassName_, Previous_, New_, pComponent_, shared_from_this() });
            ++_Ite;
        }
        else
        {
            // 如果 已失效，移除它
            _Ite = m_Observers.erase(_Ite);
        }
    }
}

//!< 属性修改前事件
void iCAX::Database::CEntity::OnComponentChanging(IN void* pSender_, IN const ComponentEventArgs& Args_)
{
    TriggerEntityChanging((EntityEventArgs::EventType)(Args_.nType), Args_.strClassName, Args_.PreviousProperties, Args_.NewProperties, Args_.pComponent);
}

//!< 属性更改后事件
void iCAX::Database::CEntity::OnComponentChanged(IN void* pSender_, IN const ComponentEventArgs& Args_)
{
    TriggerEntityChanged((EntityEventArgs::EventType)(Args_.nType), Args_.strClassName, Args_.PreviousProperties, Args_.NewProperties, Args_.pComponent);
}
