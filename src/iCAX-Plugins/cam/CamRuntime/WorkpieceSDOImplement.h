#pragma once

#include "SDOSupport.h"
#include "ToolpathResources.h"
#include "GeometryData/TubeNeutralGeometry.h"
#include "WorkpieceComponents.h"

namespace iCAX::CAM::SDO::Internal
{
struct SImportedCadResources final
{
    std::string ModelResourceID;
    std::string BRepResourceID;
    std::string TopologyResourceID;
    std::string TubeNeutralGeometryResourceID;
    std::string SectionTypeID;
    iCAX::Data::ObjectMap SectionParameters;
    double dLength = 0.0;
    uint64_t nTopologyVersion = 0;
};

bool _IsSupportedWorkpieceModelPath(IN const std::string& strSourcePath_);
SImportedCadResources _ImportCadModel(
    IN iCAX::Project::ISceneContext& Scene_,
    IN const iCAX::Product::IProductContext& Product_,
    IN const std::string& strSourcePath_,
    IN double dTolerance_);
ObjectMap _MakeWorkpiecePayload(
    IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
    IN const std::shared_ptr<iCAX::CAM::CWorkpieceComponent>& pWorkpiece_,
    IN iCAX::Project::ISceneContext* pScene_ = nullptr);
VariantArray _MakeWorkpieceArray(IN iCAX::Project::ISceneContext& Scene_);
ObjectMap _MakeTopologyStatusPayload(IN const std::shared_ptr<iCAX::CAM::CTopologyResource>& pTopology_);
std::shared_ptr<iCAX::CAM::CTopologyResource> _MakeTopologyResourceFromBRep(
    IN const iCAX::GeometryData::BRepModel& Model_,
    IN const std::string& strDisplayName_,
    IN uint64_t nTopologyVersion_);
ObjectMap _MakeTubeNeutralGeometryPayload(
    IN const std::string& strResourceID_,
    IN const std::shared_ptr<iCAX::GeometryData::Tube::CTubeNeutralGeometry>& pGeometry_);
VariantArray _MakeTopologyPickArray(IN const VariantArray& Items_);
Interaction::CInvocationResult _MakeWorkpieceListResponse(IN iCAX::Project::ISceneContext& Scene_);
} // namespace iCAX::CAM::SDO::Internal
