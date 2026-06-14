#include "pch.h"
#include "ChangeSet.h"

#include <iterator>

namespace
{
    bool MatchDomain(IN const iCAX::Database::CChangeEntityKey& Key_, IN const iCAX::Data::uuid& DomainID_)
    {
        return Key_.DomainID == DomainID_;
    }

    bool MatchDomain(IN const iCAX::Database::CChangeComponentKey& Key_, IN const iCAX::Data::uuid& DomainID_)
    {
        return Key_.DomainID == DomainID_;
    }

    bool MatchDomain(IN const iCAX::Database::CChangePropertyKey& Key_, IN const iCAX::Data::uuid& DomainID_)
    {
        return Key_.DomainID == DomainID_;
    }

    bool MatchEntity(IN const iCAX::Database::CChangeComponentKey& Key_, IN const iCAX::Database::CChangeEntityKey& EntityKey_)
    {
        return Key_.DomainID == EntityKey_.DomainID
            && Key_.EntityID == EntityKey_.EntityID;
    }

    bool MatchEntity(IN const iCAX::Database::CChangePropertyKey& Key_, IN const iCAX::Database::CChangeEntityKey& EntityKey_)
    {
        return Key_.DomainID == EntityKey_.DomainID
            && Key_.EntityID == EntityKey_.EntityID;
    }

    bool MatchComponent(IN const iCAX::Database::CChangePropertyKey& Key_, IN const iCAX::Database::CChangeComponentKey& ComponentKey_)
    {
        return Key_.DomainID == ComponentKey_.DomainID
            && Key_.EntityID == ComponentKey_.EntityID
            && Key_.ComponentClass == ComponentKey_.ComponentClass;
    }
}

bool iCAX::Database::CChangeEntityKey::operator==(IN const CChangeEntityKey& Other_) const noexcept
{
    return DomainID == Other_.DomainID
        && EntityID == Other_.EntityID;
}

bool iCAX::Database::CChangeEntityKey::operator<(IN const CChangeEntityKey& Other_) const noexcept
{
    if (DomainID != Other_.DomainID)
    {
        return DomainID < Other_.DomainID;
    }
    return EntityID < Other_.EntityID;
}

bool iCAX::Database::CChangeComponentKey::operator==(IN const CChangeComponentKey& Other_) const noexcept
{
    return DomainID == Other_.DomainID
        && EntityID == Other_.EntityID
        && ComponentClass == Other_.ComponentClass;
}

bool iCAX::Database::CChangeComponentKey::operator<(IN const CChangeComponentKey& Other_) const noexcept
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

bool iCAX::Database::CChangePropertyKey::operator==(IN const CChangePropertyKey& Other_) const noexcept
{
    return DomainID == Other_.DomainID
        && EntityID == Other_.EntityID
        && ComponentClass == Other_.ComponentClass
        && PropertyName == Other_.PropertyName;
}

bool iCAX::Database::CChangePropertyKey::operator<(IN const CChangePropertyKey& Other_) const noexcept
{
    if (DomainID != Other_.DomainID)
    {
        return DomainID < Other_.DomainID;
    }
    if (EntityID != Other_.EntityID)
    {
        return EntityID < Other_.EntityID;
    }
    if (ComponentClass != Other_.ComponentClass)
    {
        return ComponentClass < Other_.ComponentClass;
    }
    return PropertyName < Other_.PropertyName;
}

bool iCAX::Database::CChangeSet::IsEmpty() const
{
    return CreatedDomains.empty()
        && DeletedDomains.empty()
        && CreatedEntities.empty()
        && DeletedEntities.empty()
        && AddedComponents.empty()
        && RemovedComponents.empty()
        && ModifiedProperties.empty();
}

iCAX::Database::CChangeSetBuilder::CChangeSetBuilder(IN EChangeScopeKind Kind_, IN const std::string& strName_)
    : m_Kind(Kind_)
    , m_strName(strName_)
{
}

void iCAX::Database::CChangeSetBuilder::RecordAddDomain(IN const iCAX::Data::uuid& DomainID_, IN const bool bPersistent_)
{
    if (m_Kind == EChangeScopeKind::LoadBaseline)
    {
        return;
    }

    if (m_DeletedDomains.erase(DomainID_) > 0)
    {
        return;
    }
    m_CreatedDomains[DomainID_] = { DomainID_, bPersistent_ };
}

void iCAX::Database::CChangeSetBuilder::RecordDeleteDomain(IN const iCAX::Data::uuid& DomainID_, IN const bool bPersistent_)
{
    if (m_Kind == EChangeScopeKind::LoadBaseline)
    {
        return;
    }

    if (m_CreatedDomains.erase(DomainID_) > 0)
    {
        EraseDomainChanges(DomainID_);
        return;
    }

    m_DeletedDomains[DomainID_] = { DomainID_, bPersistent_ };
}

void iCAX::Database::CChangeSetBuilder::RecordAddEntity(IN const CChangeEntityKey& Key_)
{
    if (m_Kind == EChangeScopeKind::LoadBaseline)
    {
        return;
    }

    if (m_DeletedEntities.erase(Key_) > 0)
    {
        return;
    }
    m_CreatedEntities[Key_] = { Key_ };
}

void iCAX::Database::CChangeSetBuilder::RecordDeleteEntity(IN const CChangeEntityKey& Key_)
{
    if (m_Kind == EChangeScopeKind::LoadBaseline)
    {
        return;
    }

    if (m_CreatedEntities.erase(Key_) > 0)
    {
        EraseEntityChanges(Key_);
        return;
    }

    m_DeletedEntities[Key_] = { Key_ };
}

void iCAX::Database::CChangeSetBuilder::RecordAddComponent(IN const CChangeComponentKey& Key_, IN const iCAX::Data::PropertySet& NewProperties_)
{
    if (m_Kind == EChangeScopeKind::LoadBaseline)
    {
        return;
    }

    m_AddedComponents[Key_] = { Key_, {}, NewProperties_ };
}

void iCAX::Database::CChangeSetBuilder::RecordRemoveComponent(IN const CChangeComponentKey& Key_, IN const iCAX::Data::PropertySet& PreviousProperties_)
{
    if (m_Kind == EChangeScopeKind::LoadBaseline)
    {
        return;
    }

    if (m_AddedComponents.erase(Key_) > 0)
    {
        EraseComponentChanges(Key_);
        return;
    }

    auto _Snapshot = PreviousProperties_;
    for (auto _Ite = m_ModifiedProperties.begin(); _Ite != m_ModifiedProperties.end(); ++_Ite)
    {
        if (MatchComponent(_Ite->first, Key_))
        {
            _Snapshot[_Ite->first.PropertyName] = _Ite->second.PreviousValue;
        }
    }

    EraseComponentChanges(Key_);
    m_RemovedComponents[Key_] = { Key_, _Snapshot, {} };
}

void iCAX::Database::CChangeSetBuilder::RecordModifyComponent(IN const CChangeComponentKey& Key_, IN const iCAX::Data::PropertySet& PreviousProperties_, IN const iCAX::Data::PropertySet& NewProperties_)
{
    if (m_Kind == EChangeScopeKind::LoadBaseline)
    {
        return;
    }

    if (auto _IteAdded = m_AddedComponents.find(Key_); _IteAdded != m_AddedComponents.end())
    {
        for (const auto& [_strPropertyName, _NewValue] : NewProperties_)
        {
            _IteAdded->second.NewProperties[_strPropertyName] = _NewValue;
        }
        return;
    }

    if (m_RemovedComponents.contains(Key_))
    {
        return;
    }

    for (const auto& [_strPropertyName, _NewValue] : NewProperties_)
    {
        auto _ItePrevious = PreviousProperties_.find(_strPropertyName);
        if (_ItePrevious == PreviousProperties_.end())
        {
            continue;
        }

        CChangePropertyKey _PropertyKey{ Key_.DomainID, Key_.EntityID, Key_.ComponentClass, _strPropertyName };
        auto _IteChange = m_ModifiedProperties.find(_PropertyKey);
        if (_IteChange == m_ModifiedProperties.end())
        {
            if (_ItePrevious->second == _NewValue)
            {
                continue;
            }
            m_ModifiedProperties[_PropertyKey] = { _PropertyKey, _ItePrevious->second, _NewValue };
        }
        else
        {
            _IteChange->second.NewValue = _NewValue;
            if (_IteChange->second.PreviousValue == _IteChange->second.NewValue)
            {
                m_ModifiedProperties.erase(_IteChange);
            }
        }
    }
}

iCAX::Database::CChangeSet iCAX::Database::CChangeSetBuilder::Build() const
{
    CChangeSet _Result;
    _Result.Kind = m_Kind;
    _Result.Name = m_strName;

    for (const auto& [_, _Change] : m_CreatedDomains)
    {
        _Result.CreatedDomains.push_back(_Change);
    }
    for (const auto& [_, _Change] : m_DeletedDomains)
    {
        _Result.DeletedDomains.push_back(_Change);
    }
    for (const auto& [_, _Change] : m_CreatedEntities)
    {
        _Result.CreatedEntities.push_back(_Change);
    }
    for (const auto& [_, _Change] : m_DeletedEntities)
    {
        _Result.DeletedEntities.push_back(_Change);
    }
    for (const auto& [_, _Change] : m_AddedComponents)
    {
        _Result.AddedComponents.push_back(_Change);
    }
    for (const auto& [_, _Change] : m_RemovedComponents)
    {
        _Result.RemovedComponents.push_back(_Change);
    }
    for (const auto& [_, _Change] : m_ModifiedProperties)
    {
        _Result.ModifiedProperties.push_back(_Change);
    }

    return _Result;
}

void iCAX::Database::CChangeSetBuilder::EraseDomainChanges(IN const iCAX::Data::uuid& DomainID_)
{
    for (auto _Ite = m_CreatedEntities.begin(); _Ite != m_CreatedEntities.end(); )
    {
        _Ite = MatchDomain(_Ite->first, DomainID_) ? m_CreatedEntities.erase(_Ite) : std::next(_Ite);
    }
    for (auto _Ite = m_DeletedEntities.begin(); _Ite != m_DeletedEntities.end(); )
    {
        _Ite = MatchDomain(_Ite->first, DomainID_) ? m_DeletedEntities.erase(_Ite) : std::next(_Ite);
    }
    for (auto _Ite = m_AddedComponents.begin(); _Ite != m_AddedComponents.end(); )
    {
        _Ite = MatchDomain(_Ite->first, DomainID_) ? m_AddedComponents.erase(_Ite) : std::next(_Ite);
    }
    for (auto _Ite = m_RemovedComponents.begin(); _Ite != m_RemovedComponents.end(); )
    {
        _Ite = MatchDomain(_Ite->first, DomainID_) ? m_RemovedComponents.erase(_Ite) : std::next(_Ite);
    }
    for (auto _Ite = m_ModifiedProperties.begin(); _Ite != m_ModifiedProperties.end(); )
    {
        _Ite = MatchDomain(_Ite->first, DomainID_) ? m_ModifiedProperties.erase(_Ite) : std::next(_Ite);
    }
}

void iCAX::Database::CChangeSetBuilder::EraseEntityChanges(IN const CChangeEntityKey& Key_)
{
    for (auto _Ite = m_AddedComponents.begin(); _Ite != m_AddedComponents.end(); )
    {
        _Ite = MatchEntity(_Ite->first, Key_) ? m_AddedComponents.erase(_Ite) : std::next(_Ite);
    }
    for (auto _Ite = m_RemovedComponents.begin(); _Ite != m_RemovedComponents.end(); )
    {
        _Ite = MatchEntity(_Ite->first, Key_) ? m_RemovedComponents.erase(_Ite) : std::next(_Ite);
    }
    for (auto _Ite = m_ModifiedProperties.begin(); _Ite != m_ModifiedProperties.end(); )
    {
        _Ite = MatchEntity(_Ite->first, Key_) ? m_ModifiedProperties.erase(_Ite) : std::next(_Ite);
    }
}

void iCAX::Database::CChangeSetBuilder::EraseComponentChanges(IN const CChangeComponentKey& Key_)
{
    for (auto _Ite = m_ModifiedProperties.begin(); _Ite != m_ModifiedProperties.end(); )
    {
        _Ite = MatchComponent(_Ite->first, Key_) ? m_ModifiedProperties.erase(_Ite) : std::next(_Ite);
    }
}
