#pragma once

#include "CommandTargetSupport.h"
#include "ToolpathResources.h"
#include "WorkpieceComponents.h"

namespace iCAX::CAM::Commands::Internal
{
struct SImportedCadResources final
{
    std::string ModelResourceID;
    std::string BRepResourceID;
    std::string TopologyResourceID;
    uint64_t nTopologyVersion = 0;
};

bool _IsSupportedWorkpieceModelPath(IN const std::string& strSourcePath_);
SImportedCadResources _ImportCadModel(
    IN iCAX::Project::ISceneContext& Scene_,
    IN const std::string& strSourcePath_,
    IN double dTolerance_);
ObjectMap _MakeWorkpiecePayload(
    IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
    IN const std::shared_ptr<iCAX::CAM::CWorkpieceComponent>& pWorkpiece_);
VariantArray _MakeWorkpieceArray(IN iCAX::Database::IRepository& Repository_);
ObjectMap _MakeTopologyStatusPayload(IN const std::shared_ptr<iCAX::CAM::CTopologyResource>& pTopology_);
VariantArray _MakeTopologyPickArray(IN const VariantArray& Items_);
} // namespace iCAX::CAM::Commands::Internal
