#pragma once

#include "FacadeSupport.h"
#include "MachineInstanceComponents.h"
#include "MachineResources.h"

namespace iCAX::CAM::Facades::Internal
{
bool _IsSupportedMachineDescriptionPath(IN const std::string& strSourcePath_);

std::shared_ptr<iCAX::CAM::CMachineKinematicsComponent> _GetOrAddMachineKinematics(
    IN const std::shared_ptr<iCAX::Database::IEntity>& pMachineEntity_);
std::shared_ptr<iCAX::CAM::CMachineStatusComponent> _GetOrAddMachineStatus(
    IN const std::shared_ptr<iCAX::Database::IEntity>& pMachineEntity_);
void _SetMachineDefaultKinematics(IN const std::shared_ptr<iCAX::CAM::CMachineKinematicsComponent>& pKinematics_);
std::vector<std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CMachineJointComponent>>> _CollectMachineJointComponents(
    IN iCAX::Database::IRepository& Repository_,
    IN const iCAX::Data::uuid& MachineID_);
void _CreateMachineStructureEntitiesFromDescription(
    IN iCAX::Project::ISceneContext& Scene_,
    IN const iCAX::Data::uuid& MachineID_,
    IN const std::string& strResourceScopeID_,
    IN const std::string& strMachineName_,
    IN const iCAX::CAM::CMachineDescriptionResource& Description_);
std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CMachineInstanceComponent>> _CreateMachineEntity(
    IN iCAX::Database::IRepository& Repository_);
std::string _CheckMachineLimitResult(
    IN iCAX::Database::IRepository& Repository_,
    IN const iCAX::Data::uuid& MachineID_);
ObjectMap _MakeMachinePayload(
    IN iCAX::Resource::CResourceLibrary& Resources_,
    IN iCAX::Database::IRepository& Repository_,
    IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
    IN const std::shared_ptr<iCAX::CAM::CMachineInstanceComponent>& pMachine_);
ObjectMap _MakeMachineElementDetailPayload(
    IN iCAX::Database::IRepository& Repository_,
    IN const iCAX::Data::uuid& EntityID_);
void _RebuildMachineElementRenderMesh(
    IN iCAX::Project::ISceneContext& Scene_,
    IN const std::shared_ptr<iCAX::Database::IEntity>& pElementEntity_,
    IN const std::shared_ptr<iCAX::CAM::CMachineElementComponent>& pElement_);
VariantArray _MakeMachineArray(
    IN iCAX::Resource::CResourceLibrary& Resources_,
    IN iCAX::Database::IRepository& Repository_);
} // namespace iCAX::CAM::Facades::Internal
