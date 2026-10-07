#include "pch.h"
#include "ComponentBase.h"
#include "Entity.h"
#include "IEntity.h"
#include "ComponentMask.h"
#include "MaskRegistry.h"
#include "IRepository.h"
#include "IMetaRegistry.h"
#include "ModifyFilter.h"


namespace
{
    bool ValidateWritableValueProperty(
        IN iCAX::Database::IMetaRegistry& Meta_,
        IN const std::string& strComponentClass_,
        IN const std::string& strPropertyName_,
        OUT std::string& strError_)
    {
        if (!Meta_.HasTypeByName(strComponentClass_))
        {
            strError_ = "Component type is not registered: " + strComponentClass_;
            return false;
        }

        if (!Meta_.HasPropertyByName(strComponentClass_, strPropertyName_))
        {
            strError_ = std::format("{}.{} is not registered in component meta", strComponentClass_, strPropertyName_);
            return false;
        }

        if (Meta_.IsDerivedPropertyByName(strComponentClass_, strPropertyName_))
        {
            strError_ = std::format("{}.{} is a derived property and cannot be set", strComponentClass_, strPropertyName_);
            return false;
        }

        return true;
    }
}

//!< 构造函数
iCAX::Database::CComponentBase::CComponentBase(IN std::shared_ptr<IEntity> pEntity_)
    : m_pEntity(pEntity_)
    , m_bEnable(true)
    , m_bDeleted(false)
    , m_nComponentChangeNotificationSuppressionDepth(0)
{
}

//!< 析构函数
iCAX::Database::CComponentBase::~CComponentBase()
{
}

//!< 设置属性
bool iCAX::Database::CComponentBase::SetProperty(IN const std::string& strPropertyName_, IN const PropertyValue& NewValue_, OUT std::string& strError_)
{
    strError_.clear();
    auto _pMeta = ResolveMetaRegistryForComponent(*this);
    if (!ValidateWritableValueProperty(*_pMeta, GetComponentClass(), strPropertyName_, strError_))
    {
        return false;
    }

    auto _Previous = GetProperty(strPropertyName_);
    if (_Previous == NewValue_)
    {
        return true;
    }

    PropertySet _PreviousProperties = { { strPropertyName_, _Previous } };
    PropertySet _NewProperties = { { strPropertyName_, NewValue_ } };
    if (!CModifyFilter::AllowModifyComponent(*this, _NewProperties, strError_))
    {
        return false;
    }

    TriggerComponentChanging(ComponentEventArgs::kModifyComponent, _PreviousProperties, _NewProperties);
    {
        ComponentChangeNotificationSuppressor _Suppressor(this);
        try
        {
            OnSetProperty(strPropertyName_, NewValue_);
        }
        catch (const std::exception& _Exception)
        {
            strError_ = _Exception.what();
            return false;
        }
        catch (...)
        {
            strError_ = "Component property setter threw a non-standard exception";
            return false;
        }
    }
    TriggerComponentChanged(ComponentEventArgs::kModifyComponent, _PreviousProperties, _NewProperties);
    return true;
}

bool iCAX::Database::CComponentBase::SetProperty(IN const std::string& strPropertyName_, IN const PropertyValue& NewValue_)
{
    std::string _strError;
    return SetProperty(strPropertyName_, NewValue_, _strError);
}

//!< 设置属性集
bool iCAX::Database::CComponentBase::SetProperties(IN const PropertySet& Properties_, OUT std::string& strError_)
{
    return SetPropertiesImpl(Properties_, nullptr, strError_);
}

bool iCAX::Database::CComponentBase::SetPropertiesOwned(
    IN PropertySet&& Properties_, OUT std::string& strError_)
{
    return SetPropertiesImpl(Properties_, &Properties_, strError_);
}

bool iCAX::Database::CComponentBase::SetPropertiesImpl(IN const PropertySet& Properties_,
    IN PropertySet* pOwnedProperties_, OUT std::string& strError_)
{
    const bool _Profile = GetComponentClass() == "CGenerationRunComponent"
        && GetEnvironmentVariableA("ICAX_PROFILE_DISASSEMBLY", nullptr, 0) != 0;
    auto _ProfileLast = std::chrono::steady_clock::now();
    const auto _Mark = [&](const char* Phase_) {
        if (!_Profile) return;
        const auto now = std::chrono::steady_clock::now();
        std::fprintf(stderr, "Disassembly/generation-properties/%s %.3f s\n", Phase_,
            std::chrono::duration<double>(now - _ProfileLast).count());
        _ProfileLast = now;
    };
    strError_.clear();
    auto _pMeta = ResolveMetaRegistryForComponent(*this);
    for (auto& [_strName, _] : Properties_)
    {
        if (!ValidateWritableValueProperty(*_pMeta, GetComponentClass(), _strName, strError_))
        {
            return false;
        }
    }

    _Mark("validate");
    PropertySet _PrivousProperties;
    PropertySet _NewProperties;
    for (auto _Ite = Properties_.begin(); _Ite != Properties_.end(); )
    {
        const auto _Current = _Ite++;
        const auto& [_strName, _Value] = *_Current;
        auto _Previous = GetProperty(_strName);
        if (_Previous != _Value)
        {
            _PrivousProperties.emplace(_strName, std::move(_Previous));
            if (pOwnedProperties_)
                _NewProperties.insert(pOwnedProperties_->extract(_Current));
            else
                _NewProperties.emplace(_strName, _Value);
        }
    }
    _Mark("select");
    if (_PrivousProperties.empty())
    {
        return true;
    }

    if (!CModifyFilter::AllowModifyComponent(*this, _NewProperties, strError_))
    {
        return false;
    }

    _Mark("filter");
    TriggerComponentChanging(ComponentEventArgs::kModifyComponent, _PrivousProperties, _NewProperties);
    _Mark("changing");
    std::vector<std::string> _AppliedProperties;
    _AppliedProperties.reserve(_NewProperties.size());
    {
        ComponentChangeNotificationSuppressor _Suppressor(this);
        try
        {
            for (auto& [_strName, _Value] : _NewProperties)
            {
                const auto _SetterStart = _Profile ? std::chrono::steady_clock::now()
                    : std::chrono::steady_clock::time_point{};
                OnSetProperty(_strName, _Value);
                _AppliedProperties.push_back(_strName);
                if (_Profile)
                    std::fprintf(stderr, "Disassembly/generation-properties/field %s %.3f s\n",
                        _strName.c_str(), std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - _SetterStart).count());
            }
        }
        catch (...)
        {
            for (auto _Ite = _AppliedProperties.rbegin(); _Ite != _AppliedProperties.rend(); ++_Ite)
            {
                try
                {
                    OnSetProperty(*_Ite, _PrivousProperties.at(*_Ite));
                }
                catch (...)
                {
                }
            }
            if (const auto _pException = std::current_exception())
            {
                try
                {
                    std::rethrow_exception(_pException);
                }
                catch (const std::exception& _Exception)
                {
                    strError_ = _Exception.what();
                }
                catch (...)
                {
                    strError_ = "Component property setter threw a non-standard exception";
                }
            }
            return false;
        }
    }
    _Mark("set");
    if (!pOwnedProperties_
        || !TryTriggerComponentChangedOwned(std::move(_PrivousProperties), std::move(_NewProperties)))
        TriggerComponentChanged(ComponentEventArgs::kModifyComponent, _PrivousProperties, _NewProperties);
    _Mark("changed");
    return true;
}

bool iCAX::Database::CComponentBase::SetProperties(IN const PropertySet& Properties_)
{
    std::string _strError;
    return SetProperties(Properties_, _strError);
}

bool iCAX::Database::CComponentBase::InitializeNewComponentProperties(
    IN const PropertySet& Properties_, OUT std::string& strError_)
{
    strError_.clear();
    auto meta = ResolveMetaRegistryForComponent(*this);
    const auto componentClass = GetComponentClass();
    PropertySet changed;
    std::vector<std::pair<std::string, PropertyValue>> previous;
    previous.reserve(Properties_.size());
    for (const auto& [name, value] : Properties_)
    {
        if (!ValidateWritableValueProperty(*meta, componentClass, name, strError_))
            return false;
        auto oldValue = GetProperty(name);
        if (oldValue != value)
        {
            changed.emplace(name, value);
            previous.emplace_back(name, std::move(oldValue));
        }
    }
    if (changed.empty()) return true;
    if (!CModifyFilter::AllowModifyComponent(*this, changed, strError_))
        return false;

    // An initial add has no independent modify event. On any setter failure
    // the composite-entity transaction removes the entire new entity.
    ComponentChangeNotificationSuppressor suppressor(this);
    try
    {
        for (const auto& [name, value] : changed)
            OnSetProperty(name, value);
    }
    catch (const std::exception& exception)
    {
        for (auto entry = previous.rbegin(); entry != previous.rend(); ++entry)
        {
            try { OnSetProperty(entry->first, entry->second); }
            catch (...) {}
        }
        strError_ = exception.what();
        return false;
    }
    catch (...)
    {
        for (auto entry = previous.rbegin(); entry != previous.rend(); ++entry)
        {
            try { OnSetProperty(entry->first, entry->second); }
            catch (...) {}
        }
        strError_ = "Component property setter threw a non-standard exception";
        return false;
    }
    return true;
}

//!< 获取属性
iCAX::Data::PropertySet iCAX::Database::CComponentBase::GetProperties() const
{
    PropertySet _Set;
    auto _pMeta = ResolveMetaRegistryForComponent(*this);
    const auto _strComponentClass = GetComponentClass();
    if (!_pMeta->HasTypeByName(_strComponentClass))
    {
        throw std::runtime_error("Component type is not registered: " + _strComponentClass);
    }
    auto _vecNames = GetPropertyNameArray();
    for (auto& _strName : _vecNames)
    {
        if (!_pMeta->HasPropertyByName(_strComponentClass, _strName))
        {
            throw std::runtime_error("Component property is not registered: " + _strComponentClass + "." + _strName);
        }
        if (_pMeta->GetPropertyKindByName(_strComponentClass, _strName) != EPropertyKind::Value)
        {
            continue;
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

//!< 获取组件版本
uint64_t iCAX::Database::CComponentBase::Version() const
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
    std::optional<ComponentEventArgs> _Args;
    const auto _GetArgs = [&]() -> const ComponentEventArgs& {
        if (!_Args)
        {
            _Args.emplace();
            _Args->nType = nEventType_;
            _Args->strClassName = GetComponentClass();
            _Args->PreviousProperties = Previous_;
            _Args->NewProperties = New_;
            _Args->pComponent = shared_from_this();
        }
        return *_Args;
    };
    auto _pCoreListener = std::dynamic_pointer_cast<IComponentEventListener>(GetEntity());
    if (const auto _Entity = std::dynamic_pointer_cast<CEntity>(GetEntity()))
        _Entity->TriggerEntityChanging(static_cast<EntityEventArgs::EventType>(nEventType_),
            GetComponentClass(), Previous_, New_, shared_from_this());
    else if (_pCoreListener)
    {
        _pCoreListener->OnComponentChanging(this, _GetArgs());
    }

    //!< 遍历观察者列表
    for (auto _Ite = m_Observers.begin(); _Ite != m_Observers.end(); )
    {
        //!< 检查 是否还有效
        if (auto _Observer = _Ite->lock())
        {
            if (_Observer == _pCoreListener)
            {
                ++_Ite;
                continue;
            }

            try
            {
                _Observer->OnComponentChanging(this, _GetArgs());
            }
            catch (...)
            {
                // Component observers are notification-only. Validation must use ModifyFilter.
            }
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
    std::optional<ComponentEventArgs> _Args;
    const auto _GetArgs = [&]() -> const ComponentEventArgs& {
        if (!_Args)
        {
            _Args.emplace();
            _Args->nType = nEventType_;
            _Args->strClassName = GetComponentClass();
            _Args->PreviousProperties = Previous_;
            _Args->NewProperties = New_;
            _Args->pComponent = shared_from_this();
        }
        return *_Args;
    };
    auto _pCoreListener = std::dynamic_pointer_cast<IComponentEventListener>(GetEntity());
    if (const auto _Entity = std::dynamic_pointer_cast<CEntity>(GetEntity()))
        _Entity->TriggerEntityChanged(static_cast<EntityEventArgs::EventType>(nEventType_),
            GetComponentClass(), Previous_, New_, shared_from_this());
    else if (_pCoreListener)
    {
        _pCoreListener->OnComponentChanged(this, _GetArgs());
    }

    //!< 遍历观察者列表
    for (auto _Ite = m_Observers.begin(); _Ite != m_Observers.end(); )
    {
        //!< 检查 是否还有效
        if (auto _Observer = _Ite->lock())
        {
            if (_Observer == _pCoreListener)
            {
                ++_Ite;
                continue;
            }

            try
            {
                _Observer->OnComponentChanged(this, _GetArgs());
            }
            catch (...)
            {
                // Component observers are notification-only. Validation must use ModifyFilter.
            }
            ++_Ite;
        }
        else
        {
            //!< 如果 已失效，移除它
            _Ite = m_Observers.erase(_Ite);
        }
    }
}

bool iCAX::Database::CComponentBase::TryTriggerComponentChangedOwned(
    IN PropertySet&& Previous_, IN PropertySet&& New_)
{
    const auto entity = std::dynamic_pointer_cast<CEntity>(GetEntity());
    if (!entity || !entity->CanTransferChangedProperties()) return false;
    const auto core = std::dynamic_pointer_cast<IComponentEventListener>(entity);
    const bool hasComponentObservers = std::any_of(m_Observers.begin(), m_Observers.end(),
        [&](const auto& weak) {
            const auto observer = weak.lock();
            return observer && observer != core;
        });
    std::optional<ComponentEventArgs> args;
    // An entity callback may add a component observer or modify this component
    // reentrantly. Prepare its independent snapshot before ownership transfers,
    // even when no component observer has been registered yet.
    if (hasComponentObservers || entity->HasExternalChangedObservers())
    {
        args.emplace();
        args->nType = ComponentEventArgs::kModifyComponent;
        args->strClassName = GetComponentClass();
        args->PreviousProperties = Previous_;
        args->NewProperties = New_;
        args->pComponent = shared_from_this();
    }
    if (!entity->TryTriggerEntityChangedOwned(GetComponentClass(),
        std::move(Previous_), std::move(New_), shared_from_this())) return false;
    for (auto it = m_Observers.begin(); it != m_Observers.end(); )
    {
        if (auto observer = it->lock())
        {
            if (observer != core)
            {
                try
                {
                    observer->OnComponentChanged(this, args.value());
                }
                catch (...)
                {
                    // Notification-only failures cannot undo committed data.
                }
            }
            ++it;
        }
        else
            it = m_Observers.erase(it);
    }
    return true;
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
