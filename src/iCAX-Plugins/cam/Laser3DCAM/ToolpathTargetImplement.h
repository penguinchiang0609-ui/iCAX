#pragma once

#include "CommandTargetSupport.h"
#include "LayerComponents.h"
#include "MachineInstanceComponents.h"
#include "SceneComponents.h"
#include "SelectionComponents.h"
#include "TopologyPayloadNames.h"
#include "ToolpathComponents.h"
#include "ToolpathResources.h"
#include "WorkpieceComponents.h"

namespace iCAX::CAM::Commands::Internal
{
std::vector<iCAX::CAM::SPoseSample> _NormalizePoseSamples(IN const VariantArray& Samples_);

VariantArray _MakeCuttingLayerArray(IN iCAX::Database::IRepository& Repository_);
VariantArray _MakeVisibleLayerArray(IN iCAX::Database::IRepository& Repository_);
ObjectMap _MakeProgramPayload(IN iCAX::Database::IRepository& Repository_);
VariantArray _MakePathArray(IN iCAX::Database::IRepository& Repository_);

std::pair<iCAX::Data::uuid, iCAX::Data::uuid> _EnsureDefaultLayers(IN iCAX::Database::IRepository& Repository_);
std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CBlockComponent>> _EnsureProgramRootBlock(
    IN iCAX::Database::IRepository& Repository_);
void _AppendPathToProgramRoot(IN iCAX::Database::IRepository& Repository_, IN const iCAX::Data::uuid& PathEntityID_);
void _ClearProgramRootBlockChildren(IN iCAX::Database::IRepository& Repository_);
void _DeleteNonRootProgramBlocks(IN iCAX::Database::IRepository& Repository_);

std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CWorkpieceComponent>> _GetActiveWorkpiece(
    IN iCAX::Database::IRepository& Repository_);
std::shared_ptr<iCAX::CAM::CTopologyResource> _GetTopologyResource(
    IN iCAX::Project::ISceneContext& Scene_,
    IN const std::shared_ptr<iCAX::CAM::CWorkpieceComponent>& pWorkpiece_);
std::optional<ObjectMap> _FindTopologyObject(
    IN const iCAX::CAM::CTopologyResource& Topology_,
    IN const std::string& strKind_,
    IN unsigned long long nID_);
std::string _ResolveTopologyLabel(
    IN const iCAX::CAM::CTopologyResource& Topology_,
    IN const std::string& strKind_,
    IN unsigned long long nID_);
bool _HasPathForTopology(
    IN iCAX::Database::IRepository& Repository_,
    IN const iCAX::Data::uuid& WorkpieceEntityID_,
    IN const std::string& strKind_,
    IN unsigned long long nTopologyID_);
std::shared_ptr<iCAX::CAM::CPathComponent> _CreatePathEntity(
    IN iCAX::Project::ISceneContext& Scene_,
    IN const iCAX::Data::uuid& WorkpieceEntityID_,
    IN const std::string& strKind_,
    IN unsigned long long nTopologyID_,
    IN const ObjectMap& TopologyObject_,
    IN const std::string& strSource_);
} // namespace iCAX::CAM::Commands::Internal
