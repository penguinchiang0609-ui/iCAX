#include "pch.h"
#include "DerivedProperty.h"

#include "ComponentBase.h"
#include "IEntity.h"
#include "IMetaRegistry.h"
#include "Repository.h"

bool iCAX::Database::CPropertyKey::operator==(IN const CPropertyKey& Other_) const noexcept
{
    return EntityID == Other_.EntityID
        && ComponentClass == Other_.ComponentClass
        && PropertyName == Other_.PropertyName;
}

std::size_t iCAX::Database::CPropertyKeyHash::operator()(IN const CPropertyKey& Key_) const noexcept
{
    std::size_t _Seed = std::hash<iCAX::Data::uuid>{}(Key_.EntityID);
    _Seed ^= std::hash<std::string>{}(Key_.ComponentClass) + 0x9e3779b97f4a7c15ULL + (_Seed << 6) + (_Seed >> 2);
    _Seed ^= std::hash<std::string>{}(Key_.PropertyName) + 0x9e3779b97f4a7c15ULL + (_Seed << 6) + (_Seed >> 2);
    return _Seed;
}

iCAX::Database::CDerivedPropertyContext::CDerivedPropertyContext(IN CDerivedPropertyManager& Manager_, IN CRepository& Repository_, IN const CPropertyKey& Target_)
    : m_Manager(Manager_)
    , m_Repository(Repository_)
    , m_Target(Target_)
{
}

iCAX::Data::PropertyValue iCAX::Database::CDerivedPropertyContext::GetProperty(IN const CPropertyKey& Key_)
{
    auto _pMeta = m_Repository.GetMetaRegistry();
    if (!_pMeta)
    {
        throw std::runtime_error("Repository has no meta registry");
    }
    if (!_pMeta->HasTypeByName(Key_.ComponentClass))
    {
        throw std::runtime_error("Derived property depends on unregistered component type: " + Key_.ComponentClass);
    }
    if (!_pMeta->HasPropertyByName(Key_.ComponentClass, Key_.PropertyName))
    {
        throw std::runtime_error("Derived property depends on unregistered component property: " + Key_.ComponentClass + "." + Key_.PropertyName);
    }
    if (_pMeta->GetPropertyKindByName(Key_.ComponentClass, Key_.PropertyName) == EPropertyKind::Value
        && _pMeta->GetPropertyChangePolicyByName(Key_.ComponentClass, Key_.PropertyName) == EPropertyChangePolicy::Silent)
    {
        throw std::runtime_error(std::format("Derived property {}.{} cannot depend on silent value property {}.{}", m_Target.ComponentClass, m_Target.PropertyName, Key_.ComponentClass, Key_.PropertyName));
    }

    auto _pEntity = m_Repository.GetEntity(Key_.EntityID);
    if (!_pEntity)
    {
        m_Manager.AddDependency(m_Target, Key_);
        return iCAX::Data::PropertyValue::Nil;
    }

    auto _pComponent = _pEntity->GetComponent(Key_.ComponentClass);
    if (!_pComponent)
    {
        m_Manager.AddDependency(m_Target, Key_);
        return iCAX::Data::PropertyValue::Nil;
    }

    m_Manager.AddDependency(m_Target, Key_);
    return _pComponent->GetProperty(Key_.PropertyName);
}

iCAX::Data::PropertyValue iCAX::Database::CDerivedPropertyContext::GetProperty(IN const CComponentBase& Component_, IN const std::string& strPropertyName_)
{
    auto _pEntity = Component_.GetEntity();
    if (!_pEntity)
    {
        return iCAX::Data::PropertyValue::Nil;
    }

    return GetProperty({ _pEntity->GetID(), Component_.GetComponentClass(), strPropertyName_ });
}

iCAX::Data::PropertyValue iCAX::Database::CDerivedPropertyContext::GetProperty(IN const iCAX::Data::uuid& EntityID_, IN const std::string& strComponentClass_, IN const std::string& strPropertyName_)
{
    return GetProperty({ EntityID_, strComponentClass_, strPropertyName_ });
}

const iCAX::Database::CPropertyKey& iCAX::Database::CDerivedPropertyContext::GetTarget() const
{
    return m_Target;
}

iCAX::Database::CDerivedPropertyManager::CDerivedPropertyManager(IN CRepository& Repository_)
    : m_Repository(Repository_)
{
}

iCAX::Data::PropertyValue iCAX::Database::CDerivedPropertyManager::Evaluate(IN const CComponentBase& Component_, IN const std::string& strPropertyName_, IN const DerivedPropertyEvaluator& Evaluator_)
{
    if (!Evaluator_)
    {
        throw std::runtime_error(std::format("Derived property {}.{} has no evaluator", Component_.GetComponentClass(), strPropertyName_));
    }

    auto _Key = MakeKey(Component_, strPropertyName_);
    auto& _Slot = m_Caches[_Key];

    if (_Slot.State == EDerivedPropertyState::Clean)
    {
        return _Slot.Value;
    }

    if (_Slot.State == EDerivedPropertyState::Computing)
    {
        _Slot.State = EDerivedPropertyState::Error;
        throw std::runtime_error(std::format("Circular derived property dependency detected at {}.{}", _Key.ComponentClass, _Key.PropertyName));
    }

    ClearDependencies(_Key);
    _Slot.State = EDerivedPropertyState::Computing;

    try
    {
        CDerivedPropertyContext _Context(*this, m_Repository, _Key);
        _Slot.Value = Evaluator_(_Context, Component_);
        _Slot.State = EDerivedPropertyState::Clean;
        return _Slot.Value;
    }
    catch (...)
    {
        _Slot.State = EDerivedPropertyState::Error;
        throw;
    }
}

void iCAX::Database::CDerivedPropertyManager::AddDependency(IN const CPropertyKey& Derived_, IN const CPropertyKey& Source_)
{
    m_SourceToDerived[Source_].insert(Derived_);
    m_DerivedToSources[Derived_].insert(Source_);
}

void iCAX::Database::CDerivedPropertyManager::Invalidate(IN const CPropertyKey& Source_)
{
    std::unordered_set<CPropertyKey, CPropertyKeyHash> _Visited;
    InvalidateRecursive(Source_, _Visited);
}

void iCAX::Database::CDerivedPropertyManager::RemoveComponent(IN const iCAX::Data::uuid& EntityID_, IN const std::string& strComponentClass_)
{
    std::vector<CPropertyKey> _Keys;

    for (const auto& [_Key, _] : m_Caches)
    {
        if (MatchComponent(_Key, EntityID_, strComponentClass_))
        {
            _Keys.push_back(_Key);
        }
    }

    for (const auto& [_Key, _] : m_SourceToDerived)
    {
        if (MatchComponent(_Key, EntityID_, strComponentClass_))
        {
            _Keys.push_back(_Key);
        }
    }

    for (const auto& _Key : _Keys)
    {
        Invalidate(_Key);
        ClearDependencies(_Key);
        m_Caches.erase(_Key);
        m_SourceToDerived.erase(_Key);
    }

    for (auto& [_, _DerivedSet] : m_SourceToDerived)
    {
        for (auto _Ite = _DerivedSet.begin(); _Ite != _DerivedSet.end(); )
        {
            if (MatchComponent(*_Ite, EntityID_, strComponentClass_))
            {
                _Ite = _DerivedSet.erase(_Ite);
            }
            else
            {
                ++_Ite;
            }
        }
    }

    for (auto _Ite = m_SourceToDerived.begin(); _Ite != m_SourceToDerived.end(); )
    {
        if (_Ite->second.empty())
        {
            _Ite = m_SourceToDerived.erase(_Ite);
        }
        else
        {
            ++_Ite;
        }
    }

    for (auto _Ite = m_DerivedToSources.begin(); _Ite != m_DerivedToSources.end(); )
    {
        if (MatchComponent(_Ite->first, EntityID_, strComponentClass_))
        {
            _Ite = m_DerivedToSources.erase(_Ite);
            continue;
        }

        for (auto _SourceIte = _Ite->second.begin(); _SourceIte != _Ite->second.end(); )
        {
            if (MatchComponent(*_SourceIte, EntityID_, strComponentClass_))
            {
                _SourceIte = _Ite->second.erase(_SourceIte);
            }
            else
            {
                ++_SourceIte;
            }
        }

        if (_Ite->second.empty())
        {
            _Ite = m_DerivedToSources.erase(_Ite);
        }
        else
        {
            ++_Ite;
        }
    }
}

void iCAX::Database::CDerivedPropertyManager::Clear()
{
    m_Caches.clear();
    m_SourceToDerived.clear();
    m_DerivedToSources.clear();
}

iCAX::Database::CPropertyKey iCAX::Database::CDerivedPropertyManager::MakeKey(IN const CComponentBase& Component_, IN const std::string& strPropertyName_) const
{
    auto _pEntity = Component_.GetEntity();
    if (!_pEntity)
    {
        throw std::runtime_error(std::format("Component {} has no entity", Component_.GetComponentClass()));
    }

    return { _pEntity->GetID(), Component_.GetComponentClass(), strPropertyName_ };
}

void iCAX::Database::CDerivedPropertyManager::ClearDependencies(IN const CPropertyKey& Derived_)
{
    auto _Ite = m_DerivedToSources.find(Derived_);
    if (_Ite == m_DerivedToSources.end())
    {
        return;
    }

    for (const auto& _Source : _Ite->second)
    {
        auto _SourceIte = m_SourceToDerived.find(_Source);
        if (_SourceIte != m_SourceToDerived.end())
        {
            _SourceIte->second.erase(Derived_);
            if (_SourceIte->second.empty())
            {
                m_SourceToDerived.erase(_SourceIte);
            }
        }
    }

    m_DerivedToSources.erase(_Ite);
}

void iCAX::Database::CDerivedPropertyManager::InvalidateRecursive(IN const CPropertyKey& Source_, IN OUT std::unordered_set<CPropertyKey, CPropertyKeyHash>& Visited_)
{
    auto _Ite = m_SourceToDerived.find(Source_);
    if (_Ite == m_SourceToDerived.end())
    {
        return;
    }

    auto _DerivedSet = _Ite->second;
    for (const auto& _Derived : _DerivedSet)
    {
        if (!Visited_.insert(_Derived).second)
        {
            continue;
        }

        if (auto _CacheIte = m_Caches.find(_Derived); _CacheIte != m_Caches.end())
        {
            if (_CacheIte->second.State != EDerivedPropertyState::Computing)
            {
                _CacheIte->second.State = EDerivedPropertyState::Dirty;
            }
        }

        InvalidateRecursive(_Derived, Visited_);
    }
}

bool iCAX::Database::CDerivedPropertyManager::MatchComponent(IN const CPropertyKey& Key_, IN const iCAX::Data::uuid& EntityID_, IN const std::string& strComponentClass_) const
{
    return Key_.EntityID == EntityID_
        && Key_.ComponentClass == strComponentClass_;
}
