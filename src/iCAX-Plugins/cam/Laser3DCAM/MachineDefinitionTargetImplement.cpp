#include "pch.h"
#include "MachineDefinitionTargetImplement.h"

#include <algorithm>

namespace iCAX::CAM::Commands::Internal
{
namespace
{
    iCAX::Data::uuid _VariantToUuid(IN const Variant& Value_)
    {
        if (Value_.Is<iCAX::Data::uuid>())
        {
            return Value_.To<iCAX::Data::uuid>();
        }

        if (Value_.Is<std::string>())
        {
            auto _Parsed = iCAX::Data::uuid::from_string(Value_.To<std::string>());
            return _Parsed.value_or(iCAX::Data::uuid());
        }

        return iCAX::Data::uuid();
    }

    bool _ContainsDefinitionID(IN const VariantArray& DefinitionIDs_, IN const iCAX::Data::uuid& DefinitionID_)
    {
        return std::any_of(DefinitionIDs_.begin(), DefinitionIDs_.end(), [&](IN const Variant& Value_) {
            return _VariantToUuid(Value_) == DefinitionID_;
        });
    }

    VariantArray _NormalizeExistingDefinitionIDs(IN iCAX::Database::IRepository& Repository_, IN const VariantArray& DefinitionIDs_)
    {
        VariantArray _Result;
        _Result.reserve(DefinitionIDs_.size());
        for (const auto& _Value : DefinitionIDs_)
        {
            const auto _ID = _VariantToUuid(_Value);
            if (_ID.is_nil())
            {
                continue;
            }

            auto _pEntity = Repository_.GetEntity(_ID);
            if (_pEntity && _GetComponent<iCAX::CAM::CMachineDefinitionComponent>(_pEntity))
            {
                _Result.emplace_back(_UuidToString(_ID));
            }
        }
        return _Result;
    }
}

std::shared_ptr<iCAX::CAM::CMachineDefinitionCatalogComponent> _GetOrCreateMachineDefinitionCatalog(
    IN iCAX::Database::IRepository& Repository_)
{
    return _GetOrCreateComponent<iCAX::CAM::CMachineDefinitionCatalogComponent>(Repository_);
}

std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CMachineDefinitionComponent>> _FindMachineDefinition(
    IN iCAX::Database::IRepository& Repository_,
    IN const iCAX::Data::uuid& DefinitionID_)
{
    if (DefinitionID_.is_nil())
    {
        return {};
    }

    auto _pEntity = Repository_.GetEntity(DefinitionID_);
    auto _pDefinition = _GetComponent<iCAX::CAM::CMachineDefinitionComponent>(_pEntity);
    if (!_pDefinition)
    {
        return {};
    }
    return { _pEntity, _pDefinition };
}

std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CMachineDefinitionComponent>> _CreateOrUpdateMachineDefinition(
    IN iCAX::Database::IRepository& Repository_,
    IN const iCAX::Data::uuid& DefinitionID_,
    IN const std::string& strName_,
    IN const std::string& strSourcePath_,
    IN const std::string& strMachineResourceID_,
    IN const iCAX::CAM::CMachineDescriptionResource& Description_)
{
    if (DefinitionID_.is_nil())
    {
        throw std::invalid_argument("CAM machine definition id cannot be empty");
    }
    if (strMachineResourceID_.empty())
    {
        throw std::invalid_argument("CAM machine definition requires resource id");
    }

    auto [_pEntity, _pDefinition] = _FindMachineDefinition(Repository_, DefinitionID_);
    if (!_pEntity)
    {
        std::string _strError;
        if (!Repository_.CreateEntity(DefinitionID_, _pEntity, _strError) || !_pEntity)
        {
            throw std::runtime_error(_strError.empty() ? "CAM create machine definition entity failed" : _strError);
        }
        _pDefinition = _AddComponent<iCAX::CAM::CMachineDefinitionComponent>(_pEntity);
    }
    else if (!_pDefinition)
    {
        _pDefinition = _AddComponent<iCAX::CAM::CMachineDefinitionComponent>(_pEntity);
    }

    const auto _DisplayName = strName_.empty() ? Description_.ModelName : strName_;
    _SetStringProperty(_pDefinition, iCAX::CAM::CMachineDefinitionComponent::PropertyName_Name, _DisplayName.empty() ? std::string("Machine Definition") : _DisplayName);
    _SetStringProperty(_pDefinition, iCAX::CAM::CMachineDefinitionComponent::PropertyName_SourcePath, strSourcePath_);
    _SetStringProperty(_pDefinition, iCAX::CAM::CMachineDefinitionComponent::PropertyName_MachineResourceID, strMachineResourceID_);
    _SetStringProperty(_pDefinition, iCAX::CAM::CMachineDefinitionComponent::PropertyName_DescriptionFormat, Description_.SourceFormat);
    _SetStringProperty(_pDefinition, iCAX::CAM::CMachineDefinitionComponent::PropertyName_DescriptionVersion, Description_.SourceFormatVersion);
    _SetStringProperty(_pDefinition, iCAX::CAM::CMachineDefinitionComponent::PropertyName_ModelName, Description_.ModelName);
    _SetBoolProperty(_pDefinition, iCAX::CAM::CMachineDefinitionComponent::PropertyName_StaticModel, Description_.bStaticModel);
    _SetUInt64Property(_pDefinition, iCAX::CAM::CMachineDefinitionComponent::PropertyName_LinkCount, static_cast<unsigned long long>(iCAX::CAM::CountMachinePartElements(Description_)));
    _SetUInt64Property(_pDefinition, iCAX::CAM::CMachineDefinitionComponent::PropertyName_JointCount, static_cast<unsigned long long>(iCAX::CAM::CountMachineJoints(Description_)));
    _SetUInt64Property(_pDefinition, iCAX::CAM::CMachineDefinitionComponent::PropertyName_VisualCount, static_cast<unsigned long long>(iCAX::CAM::CountMachineVisuals(Description_)));
    _SetUInt64Property(_pDefinition, iCAX::CAM::CMachineDefinitionComponent::PropertyName_CollisionCount, static_cast<unsigned long long>(iCAX::CAM::CountMachineCollisions(Description_)));
    _SetUInt64Property(_pDefinition, iCAX::CAM::CMachineDefinitionComponent::PropertyName_IncludeCount, static_cast<unsigned long long>(Description_.Includes.size()));

    auto _pCatalog = _GetOrCreateMachineDefinitionCatalog(Repository_);
    auto _IDs = _NormalizeExistingDefinitionIDs(Repository_, _pCatalog->GetMachineDefinitionIDs());
    if (!_ContainsDefinitionID(_IDs, DefinitionID_))
    {
        _IDs.emplace_back(_UuidToString(DefinitionID_));
    }
    _SetVariantArrayProperty(_pCatalog, iCAX::CAM::CMachineDefinitionCatalogComponent::PropertyName_MachineDefinitionIDs, _IDs);
    return { _pEntity, _pDefinition };
}

ObjectMap _MakeMachineDefinitionPayload(
    IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
    IN const std::shared_ptr<iCAX::CAM::CMachineDefinitionComponent>& pDefinition_)
{
    ObjectMap _Definition;
    _Definition["entityId"] = pEntity_ ? _UuidToString(pEntity_->GetID()) : std::string();
    _Definition["id"] = _Definition["entityId"];
    _Definition["name"] = pDefinition_ ? pDefinition_->GetName() : std::string();
    _Definition["sourcePath"] = pDefinition_ ? pDefinition_->GetSourcePath() : std::string();
    _Definition["machineResourceId"] = pDefinition_ ? pDefinition_->GetMachineResourceID() : std::string();
    _Definition["resourceId"] = _Definition["machineResourceId"];
    _Definition["format"] = pDefinition_ ? pDefinition_->GetDescriptionFormat() : std::string();
    _Definition["formatVersion"] = pDefinition_ ? pDefinition_->GetDescriptionVersion() : std::string();
    _Definition["modelName"] = pDefinition_ ? pDefinition_->GetModelName() : std::string();
    _Definition["staticModel"] = pDefinition_ ? pDefinition_->GetStaticModel() : false;
    _Definition["linkCount"] = pDefinition_ ? pDefinition_->GetLinkCount() : 0ull;
    _Definition["jointCount"] = pDefinition_ ? pDefinition_->GetJointCount() : 0ull;
    _Definition["visualCount"] = pDefinition_ ? pDefinition_->GetVisualCount() : 0ull;
    _Definition["collisionCount"] = pDefinition_ ? pDefinition_->GetCollisionCount() : 0ull;
    _Definition["includeCount"] = pDefinition_ ? pDefinition_->GetIncludeCount() : 0ull;
    _Definition["enabled"] = pDefinition_ ? pDefinition_->GetEnabled() : false;
    return _Definition;
}

VariantArray _MakeMachineDefinitionArray(IN iCAX::Database::IRepository& Repository_)
{
    VariantArray _Definitions;
    auto _pCatalog = _GetComponent<iCAX::CAM::CMachineDefinitionCatalogComponent>(Repository_);
    if (!_pCatalog)
    {
        return _Definitions;
    }

    auto _IDs = _NormalizeExistingDefinitionIDs(Repository_, _pCatalog->GetMachineDefinitionIDs());
    for (const auto& _Value : _IDs)
    {
        auto [_pEntity, _pDefinition] = _FindMachineDefinition(Repository_, _VariantToUuid(_Value));
        if (_pDefinition)
        {
            _Definitions.emplace_back(_MakeMachineDefinitionPayload(_pEntity, _pDefinition));
        }
    }
    return _Definitions;
}

void _RemoveMachineDefinitionFromCatalog(
    IN iCAX::Database::IRepository& Repository_,
    IN const iCAX::Data::uuid& DefinitionID_)
{
    auto _pCatalog = _GetComponent<iCAX::CAM::CMachineDefinitionCatalogComponent>(Repository_);
    if (!_pCatalog)
    {
        return;
    }

    VariantArray _IDs;
    for (const auto& _Value : _NormalizeExistingDefinitionIDs(Repository_, _pCatalog->GetMachineDefinitionIDs()))
    {
        if (_VariantToUuid(_Value) != DefinitionID_)
        {
            _IDs.emplace_back(_Value);
        }
    }
    _SetVariantArrayProperty(
        _pCatalog,
        iCAX::CAM::CMachineDefinitionCatalogComponent::PropertyName_MachineDefinitionIDs,
        _IDs);
}
} // namespace iCAX::CAM::Commands::Internal
