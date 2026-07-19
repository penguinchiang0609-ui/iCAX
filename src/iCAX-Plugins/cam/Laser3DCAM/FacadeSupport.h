#pragma once

#include "Facades/Invocation.h"
#include "Data/VariantSerializer.h"
#include "Data/uuid.h"
#include "Database/IEntity.h"
#include "Database/IRepository.h"
#include "ProductContext/IProductContext.h"
#include "ProjectContext/IProjectContext.h"
#include "ProjectContext/ISceneContext.h"
#include "Resources/ResourceInfo.h"
#include "Resources/ResourceLibrary.h"

#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace iCAX::CAM::Facades::Internal
{
using iCAX::Data::ObjectMap;
using iCAX::Data::Variant;
using iCAX::Data::VariantArray;

ObjectMap _DecodeObjectPayload(IN const iCAX::Interaction::CInvocation& Request_);
iCAX::Interaction::CInvocationResult _MakeResponse(IN const Variant& Payload_);

iCAX::Project::ISceneContext& _RequireSceneContext(IN iCAX::Project::ISceneContext* pSceneContext_);
iCAX::Product::IProductContext& _RequireProductContext(IN iCAX::Product::IProductContext* pProductContext_);
iCAX::Project::IProjectContext& _RequireProjectContext(IN iCAX::Project::IProjectContext* pProjectContext_);

double _ToDouble(IN const Variant& Value_, IN const std::string& strFieldName_);
std::string _UuidToString(IN const iCAX::Data::uuid& ID_);
VariantArray _MakeStringArray(IN std::initializer_list<const char*> Values_);
VariantArray _MakeDoubleArray(IN std::initializer_list<double> Values_);

std::string _GetOptionalString(
    IN const ObjectMap& Payload_,
    IN const std::string& strName_,
    IN const std::string& strDefault_ = std::string());

unsigned long long _GetOptionalUInt64(
    IN const ObjectMap& Payload_,
    IN const std::string& strName_,
    IN unsigned long long nDefault_ = 0ull);

double _GetOptionalDouble(
    IN const ObjectMap& Payload_,
    IN const std::string& strName_,
    IN double dDefault_ = 0.0);

VariantArray _GetOptionalVariantArray(
    IN const ObjectMap& Payload_,
    IN const std::string& strName_,
    IN const VariantArray& Default_ = VariantArray());

std::shared_ptr<iCAX::Database::CComponentBase> _GetOrCreateComponent(
    IN iCAX::Database::IRepository& Repository_,
    IN const std::string& strComponentClass_);

template <typename TComponent>
std::shared_ptr<TComponent> _GetComponent(IN iCAX::Database::IRepository& Repository_)
{
    auto _pMetaEntity = Repository_.GetMetaEntity();
    if (!_pMetaEntity)
    {
        return nullptr;
    }
    return std::dynamic_pointer_cast<TComponent>(_pMetaEntity->GetComponent(TComponent::S_ClassName));
}

template <typename TComponent>
std::shared_ptr<TComponent> _GetOrCreateComponent(IN iCAX::Database::IRepository& Repository_)
{
    return std::dynamic_pointer_cast<TComponent>(_GetOrCreateComponent(Repository_, TComponent::S_ClassName));
}

template <typename TComponent>
std::shared_ptr<TComponent> _GetComponent(IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_)
{
    if (!pEntity_)
    {
        return nullptr;
    }
    return std::dynamic_pointer_cast<TComponent>(pEntity_->GetComponent(TComponent::S_ClassName));
}

template <typename TComponent>
std::shared_ptr<TComponent> _AddComponent(IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_)
{
    if (!pEntity_)
    {
        throw std::invalid_argument("Cam add component requires entity");
    }

    std::shared_ptr<iCAX::Database::CComponentBase> _pComponent;
    std::string _strError;
    if (!pEntity_->AddComponent(TComponent::S_ClassName, _pComponent, _strError) || !_pComponent)
    {
        throw std::runtime_error(_strError.empty() ? "Cam add component failed: " + std::string(TComponent::S_ClassName) : _strError);
    }
    return std::dynamic_pointer_cast<TComponent>(_pComponent);
}

template <typename TComponent>
std::shared_ptr<TComponent> _GetOrAddEntityComponent(IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_)
{
    auto _pComponent = _GetComponent<TComponent>(pEntity_);
    if (_pComponent)
    {
        return _pComponent;
    }
    return _AddComponent<TComponent>(pEntity_);
}

template <typename TComponent>
std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<TComponent>> _CreateEntityWithComponent(
    IN iCAX::Database::IRepository& Repository_)
{
    std::shared_ptr<iCAX::Database::IEntity> _pEntity;
    std::string _strError;
    const auto _EntityID = iCAX::Data::GenerateNewUUID();
    if (!Repository_.CreateEntity(_EntityID, _pEntity, _strError) || !_pEntity)
    {
        throw std::runtime_error(_strError.empty() ? "Cam create entity failed" : _strError);
    }
    return { _pEntity, _AddComponent<TComponent>(_pEntity) };
}

template <typename TComponent>
std::vector<std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<TComponent>>> _CollectEntitiesWithComponent(
    IN iCAX::Database::IRepository& Repository_)
{
    std::vector<std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<TComponent>>> _Result;
    auto _Entities = Repository_.FilterEntities([](std::shared_ptr<iCAX::Database::IEntity> pEntity_) {
        return pEntity_ && pEntity_->HasComponent(TComponent::S_ClassName);
    });

    _Result.reserve(_Entities.size());
    for (auto& _pEntity : _Entities)
    {
        auto _pComponent = _GetComponent<TComponent>(_pEntity);
        if (_pComponent)
        {
            _Result.emplace_back(_pEntity, _pComponent);
        }
    }
    return _Result;
}

template <typename TComponent>
std::vector<iCAX::Data::uuid> _CollectEntityIDsWithComponent(IN iCAX::Database::IRepository& Repository_)
{
    std::vector<iCAX::Data::uuid> _IDs;
    for (const auto& [_pEntity, _pComponent] : _CollectEntitiesWithComponent<TComponent>(Repository_))
    {
        if (_pEntity)
        {
            _IDs.push_back(_pEntity->GetID());
        }
    }
    return _IDs;
}

template <typename TComponent>
void _DeleteEntitiesWithComponent(IN iCAX::Database::IRepository& Repository_)
{
    auto _IDs = _CollectEntityIDsWithComponent<TComponent>(Repository_);
    for (const auto& _ID : _IDs)
    {
        std::string _strError;
        if (!Repository_.DeleteEntity(_ID, _strError))
        {
            throw std::runtime_error(_strError.empty() ? "Cam delete entity failed" : _strError);
        }
    }
}

bool _SetStringProperty(
    IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_,
    IN const std::string& strPropertyName_,
    IN const std::string& strValue_);

bool _SetUInt64Property(
    IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_,
    IN const std::string& strPropertyName_,
    IN unsigned long long nValue_);

bool _SetBoolProperty(
    IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_,
    IN const std::string& strPropertyName_,
    IN bool bValue_);

bool _SetUuidProperty(
    IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_,
    IN const std::string& strPropertyName_,
    IN const iCAX::Data::uuid& Value_);

bool _SetDoubleProperty(
    IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_,
    IN const std::string& strPropertyName_,
    IN double dValue_);

bool _SetVariantArrayProperty(
    IN const std::shared_ptr<iCAX::Database::CComponentBase>& pComponent_,
    IN const std::string& strPropertyName_,
    IN const VariantArray& Value_);

std::string _GetObjectString(
    IN const ObjectMap& Object_,
    IN const std::string& strName_,
    IN const std::string& strDefault_ = std::string());

unsigned long long _GetObjectUInt64(
    IN const ObjectMap& Object_,
    IN const std::string& strName_,
    IN unsigned long long nDefault_ = 0ull);

double _GetObjectDouble(
    IN const ObjectMap& Object_,
    IN const std::string& strName_,
    IN double nDefault_ = 0.0);

iCAX::Data::uuid _ParseRequiredUuid(IN const std::string& strValue_, IN const std::string& strFieldName_);

std::vector<double> _VariantArrayToDoubles(IN const VariantArray& Values_, IN const std::string& strFieldName_);
VariantArray _DoublesToVariantArray(IN const std::vector<double>& Values_);
VariantArray _NormalizeDoubleArray(IN const VariantArray& Values_, IN const std::string& strFieldName_, IN size_t nExpectedSize_);
VariantArray _NormalizeDirectionArray(IN const VariantArray& Values_, IN const std::string& strFieldName_);

iCAX::Resource::CResourceInfo _MakeResourceInfo(
    IN const std::string& strSource_,
    IN const std::string& strName_,
    IN const std::string& strKind_,
    IN iCAX::Resource::EResourcePersistenceMode Persistence_,
    IN uint64_t nVersion_);

uint64_t _NextResourceVersion(IN iCAX::Resource::CResourceLibrary& Resources_, IN const std::string& strResourceID_);
std::string _GetDisplayNameFromPath(IN const std::string& strSourcePath_);
} // namespace iCAX::CAM::Facades::Internal
