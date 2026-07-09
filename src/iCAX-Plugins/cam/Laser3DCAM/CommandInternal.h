#pragma once

#include "SDFMachineParser.h"
#include "ToolpathComponents.h"
#include "ToolpathResourceKeys.h"
#include "ToolpathResources.h"

#include "ApplicationContext/IApplicationContext.h"
#include "CommandHandler/CommandMessage.h"
#include "Data/VariantSerializer.h"
#include "Data/uuid.h"
#include "Database/IEntity.h"
#include "Database/IRepository.h"
#include "GeometryData/GeometryData.h"
#include "ProductContext/IProductContext.h"
#include "ProjectContext/IProjectContext.h"
#include "ProjectContext/ISceneContext.h"
#include "ProjectContext/SceneObjectRegistry.h"
#include "CameraNavigation/CameraNavigation.h"
#include "RenderInteraction/RenderInteraction.h"
#include "RenderService/RenderService.h"
#include "Resources/ResourceImportExport.h"
#include "Resources/ResourceInfo.h"
#include "Resources/ResourceLibrary.h"
#include "Services/ServiceProvider.h"

#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace iCAX::CAM::Commands::Internal
{
using iCAX::Data::ObjectMap;
using iCAX::Data::PropertySet;
using iCAX::Data::Variant;
using iCAX::Data::VariantArray;

inline constexpr const char *kTopologyKindFace = "face";
inline constexpr const char *kTopologyKindLoop = "loop";
inline constexpr const char *kTopologyKindEdge = "edge";

struct SImportedCadResources final
{
    std::string ModelResourceID;
    std::string BRepResourceID;
    std::string TopologyResourceID;
    uint64_t nTopologyVersion = 0;
};

ObjectMap _DecodeObjectPayload(IN const iCAX::Command::CCommandRequest &Request_);
iCAX::Command::CCommandResponse _MakeResponse(IN const Variant &Payload_);

iCAX::Project::ISceneContext &_RequireSceneContext(IN iCAX::Project::ISceneContext *pSceneContext_);
iCAX::Product::IProductContext &_RequireProductContext(IN iCAX::Product::IProductContext *pProductContext_);
iCAX::Project::IProjectContext &_RequireProjectContext(IN iCAX::Project::IProjectContext *pProjectContext_);

double _ToDouble(IN const Variant &Value_, IN const std::string &strFieldName_);
std::string _UuidToString(IN const iCAX::Data::uuid &ID_);
VariantArray _MakeStringArray(IN std::initializer_list<const char *> Values_);
VariantArray _MakeDoubleArray(IN std::initializer_list<double> Values_);

std::string _GetOptionalString(
    IN const ObjectMap &Payload_,
    IN const std::string &strName_,
    IN const std::string &strDefault_ = std::string());

unsigned long long _GetOptionalUInt64(
    IN const ObjectMap &Payload_,
    IN const std::string &strName_,
    IN unsigned long long nDefault_ = 0ull);

double _GetOptionalDouble(
    IN const ObjectMap &Payload_,
    IN const std::string &strName_,
    IN double dDefault_ = 0.0);

VariantArray _GetOptionalVariantArray(
    IN const ObjectMap &Payload_,
    IN const std::string &strName_,
    IN const VariantArray &Default_ = VariantArray());

std::shared_ptr<iCAX::Database::CComponentBase> _GetOrCreateComponent(
    IN iCAX::Database::IRepository &Repository_,
    IN const std::string &strComponentClass_);

template <typename TComponent>
std::shared_ptr<TComponent> _GetComponent(IN iCAX::Database::IRepository &Repository_)
{
    auto _pMetaEntity = Repository_.GetMetaEntity();
    if (!_pMetaEntity)
    {
        return nullptr;
    }
    return std::dynamic_pointer_cast<TComponent>(_pMetaEntity->GetComponent(TComponent::S_ClassName));
}

template <typename TComponent>
std::shared_ptr<TComponent> _GetOrCreateComponent(IN iCAX::Database::IRepository &Repository_)
{
    return std::dynamic_pointer_cast<TComponent>(_GetOrCreateComponent(Repository_, TComponent::S_ClassName));
}

template <typename TComponent>
std::shared_ptr<TComponent> _GetComponent(IN const std::shared_ptr<iCAX::Database::IEntity> &pEntity_)
{
    if (!pEntity_)
    {
        return nullptr;
    }
    return std::dynamic_pointer_cast<TComponent>(pEntity_->GetComponent(TComponent::S_ClassName));
}

template <typename TComponent>
std::shared_ptr<TComponent> _AddComponent(IN const std::shared_ptr<iCAX::Database::IEntity> &pEntity_)
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
std::shared_ptr<TComponent> _GetOrAddEntityComponent(IN const std::shared_ptr<iCAX::Database::IEntity> &pEntity_)
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
    IN iCAX::Database::IRepository &Repository_)
{
    std::shared_ptr<iCAX::Database::IEntity> _pEntity;
    std::string _strError;
    const auto _EntityID = iCAX::Data::GenerateNewUUID();
    if (!Repository_.CreateEntity(_EntityID, _pEntity, _strError) || !_pEntity)
    {
        throw std::runtime_error(_strError.empty() ? "Cam create entity failed" : _strError);
    }
    return {_pEntity, _AddComponent<TComponent>(_pEntity)};
}

template <typename TComponent>
std::vector<std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<TComponent>>> _CollectEntitiesWithComponent(
    IN iCAX::Database::IRepository &Repository_)
{
    std::vector<std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<TComponent>>> _Result;
    auto _Entities = Repository_.FilterEntities([](std::shared_ptr<iCAX::Database::IEntity> pEntity_) {
        return pEntity_ && pEntity_->HasComponent(TComponent::S_ClassName);
    });

    _Result.reserve(_Entities.size());
    for (auto &_pEntity : _Entities)
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
std::vector<iCAX::Data::uuid> _CollectEntityIDsWithComponent(IN iCAX::Database::IRepository &Repository_)
{
    std::vector<iCAX::Data::uuid> _IDs;
    for (const auto &[_pEntity, _pComponent] : _CollectEntitiesWithComponent<TComponent>(Repository_))
    {
        if (_pEntity)
        {
            _IDs.push_back(_pEntity->GetID());
        }
    }
    return _IDs;
}

template <typename TComponent>
void _DeleteEntitiesWithComponent(IN iCAX::Database::IRepository &Repository_)
{
    auto _IDs = _CollectEntityIDsWithComponent<TComponent>(Repository_);
    for (const auto &_ID : _IDs)
    {
        std::string _strError;
        if (!Repository_.DeleteEntity(_ID, _strError))
        {
            throw std::runtime_error(_strError.empty() ? "Cam delete entity failed" : _strError);
        }
    }
}

bool _SetStringProperty(
    IN const std::shared_ptr<iCAX::Database::CComponentBase> &pComponent_,
    IN const std::string &strPropertyName_,
    IN const std::string &strValue_);

bool _SetUInt64Property(
    IN const std::shared_ptr<iCAX::Database::CComponentBase> &pComponent_,
    IN const std::string &strPropertyName_,
    IN unsigned long long nValue_);

bool _SetBoolProperty(
    IN const std::shared_ptr<iCAX::Database::CComponentBase> &pComponent_,
    IN const std::string &strPropertyName_,
    IN bool bValue_);

bool _SetUuidProperty(
    IN const std::shared_ptr<iCAX::Database::CComponentBase> &pComponent_,
    IN const std::string &strPropertyName_,
    IN const iCAX::Data::uuid &Value_);

bool _SetDoubleProperty(
    IN const std::shared_ptr<iCAX::Database::CComponentBase> &pComponent_,
    IN const std::string &strPropertyName_,
    IN double dValue_);

bool _SetVariantArrayProperty(
    IN const std::shared_ptr<iCAX::Database::CComponentBase> &pComponent_,
    IN const std::string &strPropertyName_,
    IN const VariantArray &Value_);

std::string _GetObjectString(
    IN const ObjectMap &Object_,
    IN const std::string &strName_,
    IN const std::string &strDefault_ = std::string());

unsigned long long _GetObjectUInt64(
    IN const ObjectMap &Object_,
    IN const std::string &strName_,
    IN unsigned long long nDefault_ = 0ull);

double _GetObjectDouble(
    IN const ObjectMap &Object_,
    IN const std::string &strName_,
    IN double nDefault_ = 0.0);

iCAX::Data::uuid _ParseRequiredUuid(IN const std::string &strValue_, IN const std::string &strFieldName_);

std::vector<double> _VariantArrayToDoubles(IN const VariantArray &Values_, IN const std::string &strFieldName_);
VariantArray _DoublesToVariantArray(IN const std::vector<double> &Values_);
VariantArray _NormalizeDoubleArray(IN const VariantArray &Values_, IN const std::string &strFieldName_, IN size_t nExpectedSize_);
VariantArray _NormalizeDirectionArray(IN const VariantArray &Values_, IN const std::string &strFieldName_);
std::vector<iCAX::CAM::SPoseSample> _NormalizePoseSamples(IN const VariantArray &Samples_);

iCAX::Resource::CResourceInfo _MakeResourceInfo(
    IN const std::string &strSource_,
    IN const std::string &strName_,
    IN const std::string &strKind_,
    IN iCAX::Resource::EResourcePersistenceMode Persistence_,
    IN uint64_t nVersion_);

uint64_t _NextResourceVersion(IN iCAX::Resource::CResourceLibrary &Resources_, IN const std::string &strResourceID_);

Variant _BuildScenePayload(IN iCAX::Project::ISceneContext &Scene_);
ObjectMap _FitActiveCameraToRenderableBounds(
    IN iCAX::Project::IProjectContext &ProjectContext_,
    IN iCAX::Project::ISceneContext &SceneContext_);

VariantArray _MakeCuttingLayerArray(IN iCAX::Database::IRepository &Repository_);
VariantArray _MakeVisibleLayerArray(IN iCAX::Database::IRepository &Repository_);
ObjectMap _MakeProgramPayload(IN iCAX::Database::IRepository &Repository_);
VariantArray _MakePathArray(IN iCAX::Database::IRepository &Repository_);

std::pair<iCAX::Data::uuid, iCAX::Data::uuid> _EnsureDefaultLayers(IN iCAX::Database::IRepository &Repository_);
std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CBlockComponent>> _EnsureProgramRootBlock(
    IN iCAX::Database::IRepository &Repository_);
void _AppendPathToProgramRoot(IN iCAX::Database::IRepository &Repository_, IN const iCAX::Data::uuid &PathEntityID_);
void _ClearProgramRootBlockChildren(IN iCAX::Database::IRepository &Repository_);
void _DeleteNonRootProgramBlocks(IN iCAX::Database::IRepository &Repository_);

std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CWorkpieceComponent>> _GetActiveWorkpiece(
    IN iCAX::Database::IRepository &Repository_);
std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CMachineComponent>> _GetActiveMachine(
    IN iCAX::Database::IRepository &Repository_);
std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CMachineComponent>> _GetActiveOrFirstMachine(
    IN iCAX::Database::IRepository &Repository_);
std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CMachineComponent>> _RequireActiveMachine(
    IN iCAX::Database::IRepository &Repository_);
std::shared_ptr<iCAX::CAM::CTopologyResource> _GetTopologyResource(
    IN iCAX::Project::ISceneContext &Scene_,
    IN const std::shared_ptr<iCAX::CAM::CWorkpieceComponent> &pWorkpiece_);
std::optional<ObjectMap> _FindTopologyObject(
    IN const iCAX::CAM::CTopologyResource &Topology_,
    IN const std::string &strKind_,
    IN unsigned long long nID_);
std::string _ResolveTopologyLabel(
    IN const iCAX::CAM::CTopologyResource &Topology_,
    IN const std::string &strKind_,
    IN unsigned long long nID_);
bool _HasPathForTopology(
    IN iCAX::Database::IRepository &Repository_,
    IN const iCAX::Data::uuid &WorkpieceEntityID_,
    IN const std::string &strKind_,
    IN unsigned long long nTopologyID_);
std::shared_ptr<iCAX::CAM::CPathComponent> _CreatePathEntity(
    IN iCAX::Project::ISceneContext &Scene_,
    IN const iCAX::Data::uuid &WorkpieceEntityID_,
    IN const std::string &strKind_,
    IN unsigned long long nTopologyID_,
    IN const ObjectMap &TopologyObject_,
    IN const std::string &strSource_);

std::string _GetDisplayNameFromPath(IN const std::string &strSourcePath_);
bool _IsSupportedWorkpieceModelPath(IN const std::string &strSourcePath_);
bool _IsSupportedMachineDescriptionPath(IN const std::string &strSourcePath_);

void _SetMachineJointStateFromSDF(
    IN const std::shared_ptr<iCAX::CAM::CMachineComponent> &pMachine_,
    IN const VariantArray &Joints_);
void _SetMachineDefaultPose(IN const std::shared_ptr<iCAX::CAM::CMachineComponent> &pMachine_);
void _CreateMachinePartEntitiesFromSDF(
    IN iCAX::Project::ISceneContext &Scene_,
    IN const iCAX::Data::uuid &MachineID_,
    IN const std::string &strMachineName_,
    IN const iCAX::CAM::SSDFMachineDocument &SDF_);
std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CMachineComponent>> _EnsureMachineEntity(
    IN iCAX::Database::IRepository &Repository_);
std::string _CheckMachineLimitResult(IN const std::shared_ptr<iCAX::CAM::CMachineComponent> &pMachine_);

SImportedCadResources _ImportCadModel(
    IN iCAX::Project::ISceneContext &Scene_,
    IN const std::string &strSourcePath_,
    IN double dTolerance_);
} // namespace iCAX::CAM::Commands::Internal
