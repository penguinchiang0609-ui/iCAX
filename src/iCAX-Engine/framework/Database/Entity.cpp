#include "pch.h"
#include "Entity.h"
#include "Repository.h"
#include "IRepository.h"
#include "IMetaRegistry.h"
#include "ModifyFilter.h"


namespace
{
    std::shared_ptr<iCAX::Database::IMetaRegistry> RequireMetaRegistryForEntity(IN const iCAX::Database::IEntity& Entity_)
    {
        auto _pRepository = Entity_.GetRepository();
        if (!_pRepository)
        {
            throw std::runtime_error("Entity has no repository");
        }

        auto _pMeta = _pRepository->GetMetaRegistry();
        if (!_pMeta)
        {
            throw std::runtime_error("Entity repository has no meta registry");
        }
        return _pMeta;
    }
}

//!< 构造函数
iCAX::Database::CEntity::CEntity(IN std::shared_ptr<IRepository> pRepository_, IN const iCAX::Data::uuid& UID_)
    : m_pRepository(pRepository_)
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
bool iCAX::Database::CEntity::AddComponent(IN const std::string& strClassName_, OUT std::shared_ptr<CComponentBase>& pComponent_, OUT std::string& strError_)
{
    pComponent_.reset();
    strError_.clear();
    if (m_mapComponents.contains(strClassName_))
    {
        strError_ = "Component already exists: " + strClassName_;
        return false;
    }

    if (!CModifyFilter::AllowAttachComponent(*this, strClassName_, strError_))
    {
        return false;
    }

    std::shared_ptr<CComponentBase> _pComponent;
    try
    {
        auto _pMeta = RequireMetaRegistryForEntity(*this);
        _pComponent = _pMeta->CreateByName(strClassName_, shared_from_this());
    }
    catch (const std::exception& _Exception)
    {
        strError_ = _Exception.what();
        return false;
    }
    catch (...)
    {
        strError_ = "Component creation threw a non-standard exception";
        return false;
    }

    if (!_pComponent)
    {
        strError_ = "Component creator returned null: " + strClassName_;
        return false;
    }

    auto _NewProperties = _pComponent->GetProperties();
    TriggerEntityChanging(EntityEventArgs::kAddComponent, strClassName_, {}, _NewProperties, _pComponent);
    m_mapComponents[strClassName_] = _pComponent;
    m_ComponentClasses.push_back(strClassName_);
    _pComponent->AddObserver(shared_from_this());//!< 侦听目标组件的属性事件
    TriggerEntityChanged(EntityEventArgs::kAddComponent, strClassName_, {}, _NewProperties, _pComponent);
    pComponent_ = _pComponent;
    return true;
}

//!< 移除组件
bool iCAX::Database::CEntity::RemoveComponent(IN const std::string& strClassName_, OUT std::string& strError_)
{
    strError_.clear();
    auto _Ite = m_mapComponents.find(strClassName_);
    if (_Ite == m_mapComponents.end())
    {
        return true;
    }

    if (!CModifyFilter::AllowRemoveComponent(*this, strClassName_, strError_))
    {
        return false;
    }

    auto _pCompoennt = _Ite->second;
    auto _PreviousProperties = _pCompoennt->GetProperties();
    _Ite->second->RemoveObserver(shared_from_this());//!< 移除对目标组件的属性事件侦听
    try
    {
        TriggerEntityChanging(EntityEventArgs::kRemoveComponent, strClassName_, _PreviousProperties, {}, _pCompoennt);
    }
    catch (const std::exception& _Exception)
    {
        _pCompoennt->AddObserver(shared_from_this());
        strError_ = _Exception.what();
        return false;
    }
    catch (...)
    {
        _pCompoennt->AddObserver(shared_from_this());
        strError_ = "Entity remove component changing event threw a non-standard exception";
        return false;
    }
    m_mapComponents.erase(_Ite);
    m_ComponentClasses.erase(std::remove(m_ComponentClasses.begin(), m_ComponentClasses.end(), strClassName_), m_ComponentClasses.end());
    TriggerEntityChanged(EntityEventArgs::kRemoveComponent, strClassName_, _PreviousProperties, {}, _pCompoennt);
    return true;
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
    auto _pMeta = RequireMetaRegistryForEntity(*this);
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
        std::string _strError;
        if (!RemoveComponent(*_Ite, _strError))
        {
            throw std::runtime_error(_strError.empty() ? "Cleanup component failed" : _strError);
        }
    }
}

//!< 获取所在仓储
std::shared_ptr<iCAX::Database::IRepository> iCAX::Database::CEntity::GetRepository() const
{
    if (m_pRepository.expired())
    {
        return nullptr;
    }
    return m_pRepository.lock();
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
    std::optional<EntityEventArgs> _Args;
    const auto _GetArgs = [&]() -> const EntityEventArgs& {
        if (!_Args)
        {
            _Args.emplace();
            _Args->nType = nType_;
            _Args->EntityID = GetID();
            _Args->strClassName = strClassName_;
            _Args->PreviousProperties = Previous_;
            _Args->NewProperties = New_;
            _Args->pComponent = pComponent_;
            _Args->pEntity = shared_from_this();
        }
        return *_Args;
    };
    auto _pCoreListener = std::dynamic_pointer_cast<IEntityEventListener>(GetRepository());
    const auto _Repository = std::dynamic_pointer_cast<CRepository>(GetRepository());
    if (_Repository && (_Repository->IsOperationBatchActive() || _Repository->IsRepositoryEventSuppressed()))
    {
        // The repository intentionally ignores changing notifications during
        // a batch. Avoid materializing property snapshots just to discard them.
    }
    else if (_pCoreListener)
    {
        _pCoreListener->OnEntityChanging(this, _GetArgs());
    }

    // 遍历观察者列表
    for (auto _Ite = m_Observers.begin(); _Ite != m_Observers.end(); )
    {
        // 检查 是否还有效
        if (auto _Observer = _Ite->lock())
        {
            if (_Observer == _pCoreListener)
            {
                ++_Ite;
                continue;
            }

            try
            {
                _Observer->OnEntityChanging(this, _GetArgs());
            }
            catch (...)
            {
                // Entity observers are notification-only. Validation must use ModifyFilter.
            }
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
    std::optional<EntityEventArgs> _Args;
    const auto _GetArgs = [&]() -> const EntityEventArgs& {
        if (!_Args)
        {
            _Args.emplace();
            _Args->nType = nType_;
            _Args->EntityID = GetID();
            _Args->strClassName = strClassName_;
            _Args->PreviousProperties = Previous_;
            _Args->NewProperties = New_;
            _Args->pComponent = pComponent_;
            _Args->pEntity = shared_from_this();
        }
        return *_Args;
    };
    auto _pCoreListener = std::dynamic_pointer_cast<IEntityEventListener>(GetRepository());
    const auto _Repository = std::dynamic_pointer_cast<CRepository>(GetRepository());
    if (_Repository && _Repository->IsRepositoryEventSuppressed())
    {
    }
    else if (_Repository && _Repository->IsOperationBatchActive())
    {
        // Internal batch propagation borrows values synchronously. The
        // repository still records independent owning event/fact snapshots.
        _Repository->TriggerRepositoryChanged(static_cast<RepositoryEventArgs::EventType>(nType_),
            GetID(), strClassName_, Previous_, New_, pComponent_, shared_from_this());
    }
    else if (_pCoreListener)
    {
        _pCoreListener->OnEntityChanged(this, _GetArgs());
    }

    // 遍历观察者列表
    for (auto _Ite = m_Observers.begin(); _Ite != m_Observers.end(); )
    {
        // 检查 是否还有效
        if (auto _Observer = _Ite->lock())
        {
            if (_Observer == _pCoreListener)
            {
                ++_Ite;
                continue;
            }

            try
            {
                _Observer->OnEntityChanged(this, _GetArgs());
            }
            catch (...)
            {
                // Entity observers are notification-only. Validation must use ModifyFilter.
            }
            ++_Ite;
        }
        else
        {
            // 如果 已失效，移除它
            _Ite = m_Observers.erase(_Ite);
        }
    }
}

bool iCAX::Database::CEntity::CanTransferChangedProperties() const
{
    const auto repository = std::dynamic_pointer_cast<CRepository>(GetRepository());
    return repository && repository->IsOperationBatchActive()
        && !repository->IsRepositoryEventSuppressed();
}

bool iCAX::Database::CEntity::HasExternalChangedObservers() const
{
    const auto core = std::dynamic_pointer_cast<IEntityEventListener>(GetRepository());
    return std::any_of(m_Observers.begin(), m_Observers.end(), [&](const auto& weak) {
        const auto observer = weak.lock();
        return observer && observer != core;
    });
}

bool iCAX::Database::CEntity::TryTriggerEntityChangedOwned(IN const std::string& Class_,
    IN PropertySet&& Previous_, IN PropertySet&& New_,
    IN std::shared_ptr<CComponentBase> pComponent_)
{
    const auto repository = std::dynamic_pointer_cast<CRepository>(GetRepository());
    if (!repository || !repository->IsOperationBatchActive()
        || repository->IsRepositoryEventSuppressed()) return false;
    const auto core = std::dynamic_pointer_cast<IEntityEventListener>(repository);
    std::optional<EntityEventArgs> args;
    if (HasExternalChangedObservers())
    {
        args.emplace();
        args->nType = EntityEventArgs::kModifyComponent;
        args->EntityID = GetID();
        args->strClassName = Class_;
        args->PreviousProperties = Previous_;
        args->NewProperties = New_;
        args->pComponent = pComponent_;
        args->pEntity = shared_from_this();
    }
    repository->RecordOwnedComponentModification(GetID(), Class_,
        std::move(Previous_), std::move(New_), pComponent_, shared_from_this());
    // No borrowed references into the repository's event vector survive this
    // call: an observer may append more facts and force that vector to grow.
    for (auto it = m_Observers.begin(); it != m_Observers.end(); )
    {
        if (auto observer = it->lock())
        {
            if (observer != core)
            {
                try
                {
                    observer->OnEntityChanged(this, args.value());
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
