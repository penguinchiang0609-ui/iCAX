#include "pch.h"
#include "ComponentBase.h"
#include "IEntity.h"
#include "ComponentMask.h"
#include "MaskRegistry.h"
#include "IRepository.h"
#include "IMetaRegistry.h"

#include <exception>
#include <vector>

//!< 构造函数
iCAX::Database::CComponentBase::CComponentBase(IN std::shared_ptr<IEntity> pEntity_)
    : m_pEntity(pEntity_)
    , m_bEnable(false)
    , m_bDeleted(false)
    , m_nComponentChangeNotificationSuppressionDepth(0)
{
}

//!< 析构函数
iCAX::Database::CComponentBase::~CComponentBase()
{
}

//!< 设置属性
void iCAX::Database::CComponentBase::SetProperty(IN const std::string& strPropertyName_, IN const PropertyValue& NewValue_)
{
    auto _pMeta = ResolveMetaRegistryForComponent(*this);
    if (_pMeta->IsDerivedPropertyByName(GetComponentClass(), strPropertyName_))
    {
        throw std::runtime_error(std::format("{}.{} is a derived property and cannot be set", GetComponentClass(), strPropertyName_));
    }

    auto _Previous = GetProperty(strPropertyName_);
    if (_Previous == NewValue_)
    {
        return;
    }

    TriggerComponentChanging(ComponentEventArgs::kModifyComponent, { {strPropertyName_, _Previous} }, { {strPropertyName_, NewValue_}});
    {
        ComponentChangeNotificationSuppressor _Suppressor(this);
        OnSetProperty(strPropertyName_, NewValue_);
    }
    TriggerComponentChanged(ComponentEventArgs::kModifyComponent, { {strPropertyName_, _Previous} }, { {strPropertyName_, NewValue_} });
}

//!< 设置属性集
void iCAX::Database::CComponentBase::SetProperties(IN const PropertySet& Properties_)
{
    auto _pMeta = ResolveMetaRegistryForComponent(*this);
    for (auto& [_strName, _] : Properties_)
    {
        if (_pMeta->IsDerivedPropertyByName(GetComponentClass(), _strName))
        {
            throw std::runtime_error(std::format("{}.{} is a derived property and cannot be set", GetComponentClass(), _strName));
        }
    }

    PropertySet _PrivousProperties;
    PropertySet _NewProperties;
    for (auto& [_strName, _Value] : Properties_)
    {
        auto _Previous = GetProperty(_strName);
        if (_Previous != _Value)
        {
            _PrivousProperties[_strName] = _Previous;
            _NewProperties[_strName] = _Value;
        }
    }
    if (_PrivousProperties.empty())
    {
        return;
    }
    TriggerComponentChanging(ComponentEventArgs::kModifyComponent, _PrivousProperties, _NewProperties);
    std::vector<std::pair<std::string, PropertyValue>> _AppliedProperties;
    {
        ComponentChangeNotificationSuppressor _Suppressor(this);
        try
        {
            for (auto& [_strName, _Value] : _NewProperties)
            {
                OnSetProperty(_strName, _Value);
                _AppliedProperties.emplace_back(_strName, _PrivousProperties.at(_strName));
            }
        }
        catch (...)
        {
            for (auto _Ite = _AppliedProperties.rbegin(); _Ite != _AppliedProperties.rend(); ++_Ite)
            {
                try
                {
                    OnSetProperty(_Ite->first, _Ite->second);
                }
                catch (...)
                {
                }
            }
            throw;
        }
    }
    TriggerComponentChanged(ComponentEventArgs::kModifyComponent, _PrivousProperties, _NewProperties);
}

//!< 获取属性
iCAX::Data::PropertySet iCAX::Database::CComponentBase::GetProperties() const
{
    PropertySet _Set;
    auto _pMeta = ResolveMetaRegistryForComponent(*this);
    const auto _strComponentClass = GetComponentClass();
    const bool _bRegisteredComponent = _pMeta->HasTypeByName(_strComponentClass);
    auto _vecNames = GetPropertyNameArray();
    for (auto& _strName : _vecNames)
    {
        if (_bRegisteredComponent)
        {
            if (!_pMeta->HasPropertyByName(_strComponentClass, _strName))
            {
                continue;
            }
            if (_pMeta->GetPropertyKindByName(_strComponentClass, _strName) != EPropertyKind::Value)
            {
                continue;
            }
        }
        _Set[_strName] = GetProperty(_strName);
    }
    return _Set;
}

//!< 获取所在实体
std::shared_ptr<iCAX::Database::IEntity> iCAX::Database::CComponentBase::GetEntity() const
{
    return m_pEntity.lock();
}

//!< 启用
void iCAX::Database::CComponentBase::Enable()
{
    if (m_bEnable)
    {
        return;
    }
    TriggerComponentChanging(ComponentEventArgs::kEnableComponent, {}, {});
    m_bEnable = true;
    TriggerComponentChanged(ComponentEventArgs::kEnableComponent, {}, {});

}

//!< 禁用
void iCAX::Database::CComponentBase::Disable()
{
    if (!m_bEnable)
    {
        return;
    }
    TriggerComponentChanging(ComponentEventArgs::kDisableComponent, {}, {});
    m_bEnable = false;
    TriggerComponentChanged(ComponentEventArgs::kDisableComponent, {}, {});

}

//!< 是否启用
bool iCAX::Database::CComponentBase::IsEnable() const
{
    return m_bEnable;
}

//!< 是否启用
size_t iCAX::Database::CComponentBase::Version() const
{
    auto _pEntity = m_pEntity.lock();
    if (!_pEntity)
    {
        return 0;
    }
    auto _pRepository = _pEntity->GetRepository();
    if (!_pRepository)
    {
        return 0;
    }

    return _pRepository->GetComponentVersion(_pEntity->GetID(), GetComponentClass());
}

//! 是否发生了更改
bool iCAX::Database::CComponentBase::IsChanged() const
{
    auto _pEntity = m_pEntity.lock();
    if (!_pEntity)
    {
        return false;
    }
    auto _pRepository = _pEntity->GetRepository();
    if (!_pRepository)
    {
        return false;
    }

    return _pRepository->IsComponentChanged(_pEntity->GetID(), GetComponentClass());
}

//! 删除
void iCAX::Database::CComponentBase::Delete()
{
    m_bDeleted = true;
}

//! 是否被标记删除
bool iCAX::Database::CComponentBase::IsDeleted() const
{
    return m_bDeleted;
}


//!< 添加观察者
void iCAX::Database::CComponentBase::AddObserver(IN std::shared_ptr<IComponentEventListener> Observer_)
{
    m_Observers.push_back(Observer_);
}

//!< 移除观察者
void iCAX::Database::CComponentBase::RemoveObserver(IN std::shared_ptr<IComponentEventListener> Observer_)
{
    m_Observers.remove_if([&](const std::weak_ptr<IComponentEventListener>& weakObserver)
        {
            return weakObserver.expired() || weakObserver.lock() == Observer_;
        });
}

//!< 预通知
void iCAX::Database::CComponentBase::TriggerComponentChanging(IN const ComponentEventArgs::EventType& nEventType_, IN const PropertySet& Previous_, IN const PropertySet& New_)
{
    //!< 遍历观察者列表
    for (auto _Ite = m_Observers.begin(); _Ite != m_Observers.end(); )
    {
        //!< 检查 是否还有效
        if (auto _Observer = _Ite->lock())
        {
            _Observer->OnComponentChanging(this, { nEventType_, GetComponentClass(), Previous_, New_, shared_from_this()});
            ++_Ite;
        }
        else
        {
            //!< 如果 已失效，移除它
            _Ite = m_Observers.erase(_Ite);
        }
    }
}

//!< 后通知
void iCAX::Database::CComponentBase::TriggerComponentChanged(IN const ComponentEventArgs::EventType& nEventType_, IN const PropertySet& Previous_, IN const PropertySet& New_)
{
    //!< 遍历观察者列表
    for (auto _Ite = m_Observers.begin(); _Ite != m_Observers.end(); )
    {
        //!< 检查 是否还有效
        if (auto _Observer = _Ite->lock())
        {
            _Observer->OnComponentChanged(this, { nEventType_, GetComponentClass(), Previous_, New_, shared_from_this() });
            ++_Ite;
        }
        else
        {
            //!< 如果 已失效，移除它
            _Ite = m_Observers.erase(_Ite);
        }
    }
}

//!< 构造函数
iCAX::Database::CComponentBase::ComponentChangeNotifier::ComponentChangeNotifier(IN CComponentBase* pBase_, IN ComponentEventArgs::EventType nType_, IN const PropertySet& Previous_, IN const PropertySet& New_)
    : pBase(pBase_)
    , nType(nType_)
    , Previous(Previous_)
    , New(New_)
    , bEnabled(!pBase_->IsComponentChangeNotificationSuppressed())
    , nUncaughtExceptionCount(std::uncaught_exceptions())
{
    if (bEnabled)
    {
        pBase->TriggerComponentChanging(nType, Previous, New);
    }
}

//!< 析构函数
iCAX::Database::CComponentBase::ComponentChangeNotifier::~ComponentChangeNotifier()
{
    if (bEnabled && std::uncaught_exceptions() == nUncaughtExceptionCount)
    {
        pBase->TriggerComponentChanged(nType, Previous, New);
    }
}

iCAX::Database::CComponentBase::ComponentChangeNotificationSuppressor::ComponentChangeNotificationSuppressor(IN CComponentBase* pBase_)
    : pBase(pBase_)
{
    ++pBase->m_nComponentChangeNotificationSuppressionDepth;
}

iCAX::Database::CComponentBase::ComponentChangeNotificationSuppressor::~ComponentChangeNotificationSuppressor()
{
    --pBase->m_nComponentChangeNotificationSuppressionDepth;
}

bool iCAX::Database::CComponentBase::IsComponentChangeNotificationSuppressed() const
{
    return m_nComponentChangeNotificationSuppressionDepth > 0;
}
