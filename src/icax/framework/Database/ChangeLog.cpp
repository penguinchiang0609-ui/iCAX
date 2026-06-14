#include "pch.h"
#include "ChangeLog.h"
#include "IMetaRegistry.h"
#include "Data/VariantSerializer.h"

#include <cctype>
#include <stdexcept>

namespace
{
    using namespace iCAX::Data;
    using namespace iCAX::Database;

    std::string ToString(IN const EChangeScopeKind Kind_)
    {
        switch (Kind_)
        {
        case EChangeScopeKind::LoadBaseline:
            return "LoadBaseline";
        case EChangeScopeKind::Replay:
            return "Replay";
        case EChangeScopeKind::UserCommand:
        default:
            return "UserCommand";
        }
    }

    EChangeScopeKind ToScopeKind(IN const std::string& strKind_)
    {
        if (strKind_ == "LoadBaseline")
        {
            return EChangeScopeKind::LoadBaseline;
        }
        if (strKind_ == "Replay")
        {
            return EChangeScopeKind::Replay;
        }
        return EChangeScopeKind::UserCommand;
    }

    const Variant& Require(IN const ObjectMap& Map_, IN const std::string& strName_)
    {
        auto _Ite = Map_.find(strName_);
        if (_Ite == Map_.end())
        {
            throw std::runtime_error("Missing ChangeSet field: " + strName_);
        }
        return _Ite->second;
    }

    ObjectMap AsObject(IN const Variant& Value_)
    {
        return Value_.To<ObjectMap>();
    }

    VariantArray AsArray(IN const Variant& Value_)
    {
        return Value_.To<VariantArray>();
    }

    bool ReadBool(IN const ObjectMap& Map_, IN const std::string& strName_, IN const bool bDefault_)
    {
        auto _Ite = Map_.find(strName_);
        if (_Ite == Map_.end())
        {
            return bDefault_;
        }
        return _Ite->second.To<bool>();
    }

    PropertySet ReadProperties(IN const ObjectMap& Map_, IN const std::string& strName_)
    {
        auto _Ite = Map_.find(strName_);
        if (_Ite == Map_.end())
        {
            return {};
        }
        return _Ite->second.To<ObjectMap>();
    }

    bool IsBlank(IN const std::string& Text_)
    {
        for (const auto _Char : Text_)
        {
            if (!std::isspace(static_cast<unsigned char>(_Char)))
            {
                return false;
            }
        }
        return true;
    }

    bool ShouldKeepProperty(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_, IN const bool bRequirePersistent_)
    {
        auto _pMeta = GetGlobalMetaRegistry();
        if (!_pMeta->HasTypeByName(strComponentClass_))
        {
            return true;
        }
        if (!_pMeta->HasPropertyByName(strComponentClass_, strPropertyName_))
        {
            return false;
        }
        if (_pMeta->GetPropertyKindByName(strComponentClass_, strPropertyName_) != EPropertyKind::Value)
        {
            return false;
        }
        if (_pMeta->GetPropertyChangePolicyByName(strComponentClass_, strPropertyName_) != EPropertyChangePolicy::Transactional)
        {
            return false;
        }
        if (bRequirePersistent_ && _pMeta->GetPropertyPersistenceByName(strComponentClass_, strPropertyName_) != EPropertyPersistence::Persistent)
        {
            return false;
        }
        return true;
    }

    PropertySet FilterProperties(IN const std::string& strComponentClass_, IN const PropertySet& Properties_, IN const bool bRequirePersistent_)
    {
        PropertySet _Result;
        for (const auto& [_strName, _Value] : Properties_)
        {
            if (ShouldKeepProperty(strComponentClass_, _strName, bRequirePersistent_))
            {
                _Result[_strName] = _Value;
            }
        }
        return _Result;
    }

    CChangeSet FilterChangeSet(IN const CChangeSet& ChangeSet_, IN const bool bRequirePersistent_, IN const std::function<bool(const uuid&)>& DomainPersistentResolver_)
    {
        CChangeSet _Result;
        _Result.Kind = ChangeSet_.Kind;
        _Result.Name = ChangeSet_.Name;

        std::map<uuid, bool> _DomainPersistence;
        for (const auto& _Change : ChangeSet_.CreatedDomains)
        {
            _DomainPersistence[_Change.DomainID] = _Change.bPersistent;
        }
        for (const auto& _Change : ChangeSet_.DeletedDomains)
        {
            _DomainPersistence[_Change.DomainID] = _Change.bPersistent;
        }

        auto _IsPersistentDomain = [&](const uuid& DomainID_) -> bool
        {
            if (!bRequirePersistent_)
            {
                return true;
            }
            auto _Ite = _DomainPersistence.find(DomainID_);
            if (_Ite != _DomainPersistence.end())
            {
                return _Ite->second;
            }
            return !DomainPersistentResolver_ || DomainPersistentResolver_(DomainID_);
        };

        for (const auto& _Change : ChangeSet_.CreatedDomains)
        {
            if (_IsPersistentDomain(_Change.DomainID))
            {
                _Result.CreatedDomains.push_back(_Change);
            }
        }
        for (const auto& _Change : ChangeSet_.DeletedDomains)
        {
            if (_IsPersistentDomain(_Change.DomainID))
            {
                _Result.DeletedDomains.push_back(_Change);
            }
        }
        for (const auto& _Change : ChangeSet_.CreatedEntities)
        {
            if (_IsPersistentDomain(_Change.Key.DomainID))
            {
                _Result.CreatedEntities.push_back(_Change);
            }
        }
        for (const auto& _Change : ChangeSet_.DeletedEntities)
        {
            if (_IsPersistentDomain(_Change.Key.DomainID))
            {
                _Result.DeletedEntities.push_back(_Change);
            }
        }

        for (const auto& _Change : ChangeSet_.AddedComponents)
        {
            if (!_IsPersistentDomain(_Change.Key.DomainID))
            {
                continue;
            }
            CChangeComponent _Filtered = _Change;
            _Filtered.PreviousProperties = FilterProperties(_Change.Key.ComponentClass, _Change.PreviousProperties, bRequirePersistent_);
            _Filtered.NewProperties = FilterProperties(_Change.Key.ComponentClass, _Change.NewProperties, bRequirePersistent_);
            _Result.AddedComponents.push_back(_Filtered);
        }

        for (const auto& _Change : ChangeSet_.RemovedComponents)
        {
            if (!_IsPersistentDomain(_Change.Key.DomainID))
            {
                continue;
            }
            CChangeComponent _Filtered = _Change;
            _Filtered.PreviousProperties = FilterProperties(_Change.Key.ComponentClass, _Change.PreviousProperties, bRequirePersistent_);
            _Filtered.NewProperties = FilterProperties(_Change.Key.ComponentClass, _Change.NewProperties, bRequirePersistent_);
            _Result.RemovedComponents.push_back(_Filtered);
        }

        for (const auto& _Change : ChangeSet_.ModifiedProperties)
        {
            if (_IsPersistentDomain(_Change.Key.DomainID)
                && ShouldKeepProperty(_Change.Key.ComponentClass, _Change.Key.PropertyName, bRequirePersistent_))
            {
                _Result.ModifiedProperties.push_back(_Change);
            }
        }

        return _Result;
    }

    ObjectMap ToVariantObject(IN const CChangeDomain& Change_)
    {
        return {
            { "domainID", Variant(Change_.DomainID) },
            { "persistent", Variant(Change_.bPersistent) }
        };
    }

    CChangeDomain ToChangeDomain(IN const Variant& Value_)
    {
        auto _Object = AsObject(Value_);
        return {
            Require(_Object, "domainID").To<uuid>(),
            ReadBool(_Object, "persistent", true)
        };
    }

    ObjectMap ToVariantObject(IN const CChangeEntityKey& Key_)
    {
        return {
            { "domainID", Variant(Key_.DomainID) },
            { "entityID", Variant(Key_.EntityID) }
        };
    }

    CChangeEntityKey ToChangeEntityKey(IN const ObjectMap& Object_)
    {
        return {
            Require(Object_, "domainID").To<uuid>(),
            Require(Object_, "entityID").To<uuid>()
        };
    }

    ObjectMap ToVariantObject(IN const CChangeEntity& Change_)
    {
        return ToVariantObject(Change_.Key);
    }

    CChangeEntity ToChangeEntity(IN const Variant& Value_)
    {
        return { ToChangeEntityKey(AsObject(Value_)) };
    }

    ObjectMap ToVariantObject(IN const CChangeComponentKey& Key_)
    {
        return {
            { "domainID", Variant(Key_.DomainID) },
            { "entityID", Variant(Key_.EntityID) },
            { "componentClass", Variant(Key_.ComponentClass) }
        };
    }

    CChangeComponentKey ToChangeComponentKey(IN const ObjectMap& Object_)
    {
        return {
            Require(Object_, "domainID").To<uuid>(),
            Require(Object_, "entityID").To<uuid>(),
            Require(Object_, "componentClass").To<std::string>()
        };
    }

    ObjectMap ToVariantObject(IN const CChangeComponent& Change_)
    {
        auto _Object = ToVariantObject(Change_.Key);
        _Object["previousProperties"] = Variant(Change_.PreviousProperties);
        _Object["newProperties"] = Variant(Change_.NewProperties);
        return _Object;
    }

    CChangeComponent ToChangeComponent(IN const Variant& Value_)
    {
        auto _Object = AsObject(Value_);
        return {
            ToChangeComponentKey(_Object),
            ReadProperties(_Object, "previousProperties"),
            ReadProperties(_Object, "newProperties")
        };
    }

    ObjectMap ToVariantObject(IN const CChangePropertyKey& Key_)
    {
        return {
            { "domainID", Variant(Key_.DomainID) },
            { "entityID", Variant(Key_.EntityID) },
            { "componentClass", Variant(Key_.ComponentClass) },
            { "propertyName", Variant(Key_.PropertyName) }
        };
    }

    CChangePropertyKey ToChangePropertyKey(IN const ObjectMap& Object_)
    {
        return {
            Require(Object_, "domainID").To<uuid>(),
            Require(Object_, "entityID").To<uuid>(),
            Require(Object_, "componentClass").To<std::string>(),
            Require(Object_, "propertyName").To<std::string>()
        };
    }

    ObjectMap ToVariantObject(IN const CChangeProperty& Change_)
    {
        auto _Object = ToVariantObject(Change_.Key);
        _Object["previousValue"] = Change_.PreviousValue;
        _Object["newValue"] = Change_.NewValue;
        return _Object;
    }

    CChangeProperty ToChangeProperty(IN const Variant& Value_)
    {
        auto _Object = AsObject(Value_);
        return {
            ToChangePropertyKey(_Object),
            Require(_Object, "previousValue"),
            Require(_Object, "newValue")
        };
    }

    template<typename T>
    VariantArray ToArray(IN const std::vector<T>& Changes_)
    {
        VariantArray _Array;
        for (const auto& _Change : Changes_)
        {
            _Array.emplace_back(ToVariantObject(_Change));
        }
        return _Array;
    }

    template<typename T, typename TReader>
    std::vector<T> FromArray(IN const ObjectMap& Object_, IN const std::string& strName_, IN TReader Reader_)
    {
        std::vector<T> _Result;
        auto _Ite = Object_.find(strName_);
        if (_Ite == Object_.end())
        {
            return _Result;
        }
        for (const auto& _Item : AsArray(_Ite->second))
        {
            _Result.push_back(Reader_(_Item));
        }
        return _Result;
    }
}

iCAX::Database::CChangeSet iCAX::Database::FilterTransactionalChangeSet(IN const CChangeSet& ChangeSet_)
{
    return FilterChangeSet(ChangeSet_, false, {});
}

iCAX::Database::CChangeSet iCAX::Database::FilterPersistentChangeSet(IN const CChangeSet& ChangeSet_, IN const std::function<bool(const iCAX::Data::uuid&)>& DomainPersistentResolver_)
{
    return FilterChangeSet(ChangeSet_, true, DomainPersistentResolver_);
}

iCAX::Database::CChangeSet iCAX::Database::MakeInverseChangeSet(IN const CChangeSet& ChangeSet_, IN const std::string& strName_)
{
    CChangeSet _Result;
    _Result.Kind = EChangeScopeKind::Replay;
    if (!strName_.empty())
    {
        _Result.Name = strName_;
    }
    else if (!ChangeSet_.Name.empty())
    {
        _Result.Name = "Undo " + ChangeSet_.Name;
    }
    else
    {
        _Result.Name = "Undo";
    }

    for (const auto& _Change : ChangeSet_.DeletedDomains)
    {
        _Result.CreatedDomains.push_back(_Change);
    }
    for (const auto& _Change : ChangeSet_.DeletedEntities)
    {
        _Result.CreatedEntities.push_back(_Change);
    }
    for (const auto& _Change : ChangeSet_.RemovedComponents)
    {
        _Result.AddedComponents.push_back({ _Change.Key, {}, _Change.PreviousProperties });
    }
    for (const auto& _Change : ChangeSet_.ModifiedProperties)
    {
        _Result.ModifiedProperties.push_back({ _Change.Key, _Change.NewValue, _Change.PreviousValue });
    }
    for (const auto& _Change : ChangeSet_.AddedComponents)
    {
        _Result.RemovedComponents.push_back({ _Change.Key, _Change.NewProperties, {} });
    }
    for (const auto& _Change : ChangeSet_.CreatedEntities)
    {
        _Result.DeletedEntities.push_back(_Change);
    }
    for (const auto& _Change : ChangeSet_.CreatedDomains)
    {
        _Result.DeletedDomains.push_back(_Change);
    }

    return _Result;
}

iCAX::Data::Variant iCAX::Database::ChangeSetToVariant(IN const CChangeSet& ChangeSet_)
{
    ObjectMap _Object;
    _Object["kind"] = Variant(ToString(ChangeSet_.Kind));
    _Object["name"] = Variant(ChangeSet_.Name);
    _Object["createdDomains"] = Variant(ToArray(ChangeSet_.CreatedDomains));
    _Object["deletedDomains"] = Variant(ToArray(ChangeSet_.DeletedDomains));
    _Object["createdEntities"] = Variant(ToArray(ChangeSet_.CreatedEntities));
    _Object["deletedEntities"] = Variant(ToArray(ChangeSet_.DeletedEntities));
    _Object["addedComponents"] = Variant(ToArray(ChangeSet_.AddedComponents));
    _Object["removedComponents"] = Variant(ToArray(ChangeSet_.RemovedComponents));
    _Object["modifiedProperties"] = Variant(ToArray(ChangeSet_.ModifiedProperties));
    return Variant(_Object);
}

iCAX::Database::CChangeSet iCAX::Database::ChangeSetFromVariant(IN const iCAX::Data::Variant& Value_)
{
    auto _Object = Value_.To<ObjectMap>();

    CChangeSet _Result;
    _Result.Kind = ToScopeKind(Require(_Object, "kind").To<std::string>());
    _Result.Name = Require(_Object, "name").To<std::string>();
    _Result.CreatedDomains = FromArray<CChangeDomain>(_Object, "createdDomains", ToChangeDomain);
    _Result.DeletedDomains = FromArray<CChangeDomain>(_Object, "deletedDomains", ToChangeDomain);
    _Result.CreatedEntities = FromArray<CChangeEntity>(_Object, "createdEntities", ToChangeEntity);
    _Result.DeletedEntities = FromArray<CChangeEntity>(_Object, "deletedEntities", ToChangeEntity);
    _Result.AddedComponents = FromArray<CChangeComponent>(_Object, "addedComponents", ToChangeComponent);
    _Result.RemovedComponents = FromArray<CChangeComponent>(_Object, "removedComponents", ToChangeComponent);
    _Result.ModifiedProperties = FromArray<CChangeProperty>(_Object, "modifiedProperties", ToChangeProperty);
    return _Result;
}

std::string iCAX::Database::SerializeChangeSet(IN const CChangeSet& ChangeSet_)
{
    return iCAX::Data::VariantSerializer::Serialize(ChangeSetToVariant(ChangeSet_));
}

iCAX::Database::CChangeSet iCAX::Database::DeserializeChangeSet(IN const std::string& Text_)
{
    return ChangeSetFromVariant(iCAX::Data::VariantSerializer::Deserialize(Text_));
}

iCAX::Database::CChangeSetJournal::~CChangeSetJournal()
{
    Close();
}

void iCAX::Database::CChangeSetJournal::Open(IN const std::string& strPath_, IN const bool bTruncate_)
{
    Close();

    auto _Mode = std::ios::out | std::ios::binary;
    _Mode |= bTruncate_ ? std::ios::trunc : std::ios::app;
    m_Stream.open(strPath_, _Mode);
    if (!m_Stream.is_open())
    {
        throw std::runtime_error("Failed to open change journal: " + strPath_);
    }
    m_strPath = strPath_;
}

void iCAX::Database::CChangeSetJournal::Close()
{
    if (m_Stream.is_open())
    {
        m_Stream.flush();
        m_Stream.close();
    }
    m_strPath.clear();
}

bool iCAX::Database::CChangeSetJournal::IsOpen() const
{
    return m_Stream.is_open();
}

const std::string& iCAX::Database::CChangeSetJournal::GetPath() const
{
    return m_strPath;
}

void iCAX::Database::CChangeSetJournal::Append(IN const CChangeSet& ChangeSet_)
{
    if (!m_Stream.is_open())
    {
        return;
    }

    if (ChangeSet_.IsEmpty())
    {
        return;
    }

    m_Stream << SerializeChangeSet(ChangeSet_) << '\n';
    m_Stream.flush();
    if (!m_Stream.good())
    {
        throw std::runtime_error("Failed to append change journal: " + m_strPath);
    }
}

std::vector<iCAX::Database::CChangeSet> iCAX::Database::CChangeSetJournal::ReadAll(IN const std::string& strPath_) const
{
    std::ifstream _Input(strPath_, std::ios::in | std::ios::binary);
    if (!_Input.is_open())
    {
        return {};
    }

    std::vector<CChangeSet> _Result;
    std::string _Line;
    while (std::getline(_Input, _Line))
    {
        if (IsBlank(_Line))
        {
            continue;
        }

        try
        {
            _Result.push_back(DeserializeChangeSet(_Line));
        }
        catch (...)
        {
            break;
        }
    }
    return _Result;
}
