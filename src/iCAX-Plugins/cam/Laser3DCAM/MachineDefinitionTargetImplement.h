#pragma once

#include "CommandTargetSupport.h"
#include "MachineDefinitionComponents.h"
#include "MachineResources.h"

namespace iCAX::CAM::Commands::Internal
{
std::shared_ptr<iCAX::CAM::CMachineDefinitionCatalogComponent> _GetOrCreateMachineDefinitionCatalog(
    IN iCAX::Database::IRepository& Repository_);

std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CMachineDefinitionComponent>> _FindMachineDefinition(
    IN iCAX::Database::IRepository& Repository_,
    IN const iCAX::Data::uuid& DefinitionID_);

std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CMachineDefinitionComponent>> _CreateOrUpdateMachineDefinition(
    IN iCAX::Database::IRepository& Repository_,
    IN const iCAX::Data::uuid& DefinitionID_,
    IN const std::string& strName_,
    IN const std::string& strSourcePath_,
    IN const std::string& strMachineResourceID_,
    IN const iCAX::CAM::CMachineDescriptionResource& Description_);

ObjectMap _MakeMachineDefinitionPayload(
    IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
    IN const std::shared_ptr<iCAX::CAM::CMachineDefinitionComponent>& pDefinition_);

VariantArray _MakeMachineDefinitionArray(IN iCAX::Database::IRepository& Repository_);

void _RemoveMachineDefinitionFromCatalog(
    IN iCAX::Database::IRepository& Repository_,
    IN const iCAX::Data::uuid& DefinitionID_);
} // namespace iCAX::CAM::Commands::Internal
