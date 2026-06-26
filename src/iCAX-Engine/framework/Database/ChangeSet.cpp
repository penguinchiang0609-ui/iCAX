#include "pch.h"
#include "ChangeSet.h"

#include <iterator>

namespace
{
    bool MatchEntity(IN const iCAX::Database::CChangeComponentKey& Key_, IN const iCAX::Database::CChangeEntityKey& EntityKey_)
    {
        return Key_.EntityID == EntityKey_.EntityID;
    }

    bool MatchEntity(IN const iCAX::Database::CChangePropertyKey& Key_, IN const iCAX::Database::CChangeEntityKey& EntityKey_)
    {
        return Key_.EntityID == EntityKey_.EntityID;
    }

    bool MatchComponent(IN const iCAX::Database::CChangePropertyKey& Key_, IN const iCAX::Database::CChangeComponentKey& ComponentKey_)
    {
        return Key_.EntityID == ComponentKey_.EntityID
            && Key_.ComponentClass == ComponentKey_.ComponentClass;
    }
}

bool iCAX::Database::CChangeEntityKey::operator==(IN const CChangeEntityKey& Other_) const noexcept
{
    return EntityID == Other_.EntityID;
}

bool iCAX::Database::CChangeEntityKey::operator<(IN const CChangeEntityKey& Other_) const noexcept
{
    return EntityID < Other_.EntityID;
}

bool iCAX::Database::CChangeComponentKey::operator==(IN const CChangeComponentKey& Other_) const noexcept
{
    return EntityID == Other_.EntityID
        && ComponentClass == Other_.ComponentClass;
}

bool iCAX::Database::CChangeComponentKey::operator<(IN const CChangeComponentKey& Other_) const noexcept
{
    if (EntityID != Other_.EntityID)
    {
        return EntityID < Other_.EntityID;
    }
    return ComponentClass < Other_.ComponentClass;
}

bool iCAX::Database::CChangePropertyKey::operator==(IN const CChangePropertyKey& Other_) const noexcept
{
    return EntityID == Other_.EntityID
        && ComponentClass == Other_.ComponentClass
        && PropertyName == Other_.PropertyName;
}

bool iCAX::Database::CChangePropertyKey::operator<(IN const CChangePropertyKey& Other_) const noexcept
{
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
    return CreatedEntities.empty()
        && DeletedEntities.empty()
        && AddedComponents.empty()
        && RemovedComponents.empty()
        && ModifiedProperties.empty()
        && ModifiedComponentStates.empty();
}

iCAX::Database::CChangeSetBuilder::CChangeSetBuilder(IN EOperationBatchKind Kind_, IN const std::string& strName_)
    : m_Kind(Kind_)
    , m_strName(strName_)
{
}

void iCAX::Database::CChangeSetBuilder::RecordAddEntity(IN const CChangeEntityKey& Key_)
{
    if (m_Kind == EOperationBatchKind::LoadBaseline)
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
    if (m_Kind == EOperationBatchKind::LoadBaseline)
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
    if (m_Kind == EOperationBatchKind::LoadBaseline)
    {
        return;
    }

    EraseComponentChanges(Key_);
    m_AddedComponents[Key_] = { Key_, {}, NewProperties_ };
}

void iCAX::Database::CChangeSetBuilder::RecordRemoveComponent(IN const CChangeComponentKey& Key_, IN const iCAX::Data::PropertySet& PreviousProperties_)
{
    if (m_Kind == EOperationBatchKind::LoadBaseline)
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
    if (m_Kind == EOperationBatchKind::LoadBaseline)
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

        CChangePropertyKey _PropertyKey{ Key_.EntityID, Key_.ComponentClass, _strPropertyName };
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

void iCAX::Database::CChangeSetBuilder::RecordComponentEnabledState(IN const CChangeComponentKey& Key_, IN const bool bPreviousEnabled_, IN const bool bNewEnabled_)
{
    if (m_Kind == EOperationBatchKind::LoadBaseline || bPreviousEnabled_ == bNewEnabled_)
    {
        return;
    }

    if (m_AddedComponents.contains(Key_) || m_RemovedComponents.contains(Key_))
    {
        return;
    }

    auto _IteChange = m_ModifiedComponentStates.find(Key_);
    if (_IteChange == m_ModifiedComponentStates.end())
    {
        m_ModifiedComponentStates[Key_] = { Key_, bPreviousEnabled_, bNewEnabled_ };
        return;
    }

    _IteChange->second.NewEnabled = bNewEnabled_;
    if (_IteChange->second.PreviousEnabled == _IteChange->second.NewEnabled)
    {
        m_ModifiedComponentStates.erase(_IteChange);
    }
}

iCAX::Database::CChangeSet iCAX::Database::CChangeSetBuilder::Build() const
{
    CChangeSet _Result;
    _Result.Kind = m_Kind;
    _Result.Name = m_strName;

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
    for (const auto& [_, _Change] : m_ModifiedComponentStates)
    {
        _Result.ModifiedComponentStates.push_back(_Change);
    }

    return _Result;
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
    for (auto _Ite = m_ModifiedComponentStates.begin(); _Ite != m_ModifiedComponentStates.end(); )
    {
        _Ite = MatchEntity(_Ite->first, Key_) ? m_ModifiedComponentStates.erase(_Ite) : std::next(_Ite);
    }
}

void iCAX::Database::CChangeSetBuilder::EraseComponentChanges(IN const CChangeComponentKey& Key_)
{
    m_ModifiedComponentStates.erase(Key_);
    for (auto _Ite = m_ModifiedProperties.begin(); _Ite != m_ModifiedProperties.end(); )
    {
        _Ite = MatchComponent(_Ite->first, Key_) ? m_ModifiedProperties.erase(_Ite) : std::next(_Ite);
    }
}
