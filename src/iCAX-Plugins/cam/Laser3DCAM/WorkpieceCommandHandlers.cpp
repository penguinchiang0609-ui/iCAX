#include "pch.h"
#include "CommandHandlers.h"
#include "CommandInternal.h"

namespace iCAX
{
namespace CAM
{
namespace Commands
{
using namespace Internal;

iCAX::Command::CCommandResponse HandleImportModel(
    IN const iCAX::Command::CCommandRequest &Request_,
    IN iCAX::Application::IApplicationContext &,
    IN iCAX::Product::IProductContext *pProductContext_,
    IN iCAX::Project::IProjectContext *pProjectContext_,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    (void)_RequireProductContext(pProductContext_);
    (void)_RequireProjectContext(pProjectContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto _SourcePath = _GetOptionalString(_Payload, "sourcePath");
    if (_SourcePath.empty())
    {
        throw std::invalid_argument("Cam ImportModel requires sourcePath");
    }
    if (!_IsSupportedWorkpieceModelPath(_SourcePath))
    {
        throw std::invalid_argument("Cam ImportModel only supports STEP/STP and IGS/IGES workpiece files");
    }
    const auto _Tolerance = _GetOptionalDouble(_Payload, "tolerance", 0.001);
    if (_Tolerance <= 0.0)
    {
        throw std::invalid_argument("Cam ImportModel tolerance must be greater than zero");
    }
    const auto _ImportResult = _ImportCadModel(_Scene, _SourcePath, _Tolerance);
    auto &_Repository = _Scene.Database();
    auto _Undo = _Repository.BeginUndoCommand("Import CAM workpiece");
    auto _pRoot = _GetOrCreateComponent<iCAX::CAM::CRootComponent>(_Repository);
    auto _pSelection = _GetOrCreateComponent<iCAX::CAM::CSelectionComponent>(_Repository);
    auto [_pProgramRootEntity, _pProgramRootBlock] = _EnsureProgramRootBlock(_Repository);
    _EnsureDefaultLayers(_Repository);
    auto [_pWorkpieceEntity, _pWorkpiece] = _CreateEntityWithComponent<iCAX::CAM::CWorkpieceComponent>(_Repository);
    auto _pRender = _GetOrAddEntityComponent<iCAX::RenderInteraction::CRenderInstanceComponent>(_pWorkpieceEntity);
    (void)_GetOrAddEntityComponent<iCAX::RenderInteraction::CRenderTransformComponent>(_pWorkpieceEntity);
    _SetStringProperty(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_Name, _GetDisplayNameFromPath(_SourcePath));
    _SetStringProperty(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_SourcePath, _SourcePath);
    _SetStringProperty(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_ModelResourceID, _ImportResult.ModelResourceID);
    _SetStringProperty(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_BRepResourceID, _ImportResult.BRepResourceID);
    _SetStringProperty(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_TopologyResourceID, _ImportResult.TopologyResourceID);
    _SetUInt64Property(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_TopologyVersion, _ImportResult.nTopologyVersion);
    _SetStringProperty(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_GeometryResourceID, _ImportResult.BRepResourceID);
    _SetUInt64Property(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_GeometryKind, 1ull);
    _SetUInt64Property(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_RenderClass, 1ull);
    _SetUInt64Property(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_StyleID, 1ull);
    _SetUInt64Property(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_ColorRGBA, 0x8FB8C9FFull);
    _SetDoubleProperty(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_LineWidth, 1.0);
    _SetUuidProperty(_pRoot, iCAX::CAM::CRootComponent::PropertyName_ActiveWorkpieceID, _pWorkpieceEntity->GetID());
    _SetUuidProperty(_pRoot, iCAX::CAM::CRootComponent::PropertyName_ActiveOrderPlanID, iCAX::Data::uuid());
    _SetUuidProperty(_pRoot, iCAX::CAM::CRootComponent::PropertyName_LatestSafetyCheckID, iCAX::Data::uuid());
    _SetUuidProperty(_pRoot, iCAX::CAM::CRootComponent::PropertyName_ActiveSimulationID, iCAX::Data::uuid());
    _SetStringProperty(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedKind, std::string());
    _SetUInt64Property(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedID, 0ull);
    _SetStringProperty(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedLabel, std::string());
    _Undo->End();
    return _MakeResponse(_BuildScenePayload(_Scene));
}

iCAX::Command::CCommandResponse HandleSetActiveWorkpiece(
    IN const iCAX::Command::CCommandRequest &Request_,
    IN iCAX::Application::IApplicationContext &,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto _WorkpieceIDText = _GetOptionalString(_Payload, "workpieceEntityId");
    if (_WorkpieceIDText.empty())
    {
        _WorkpieceIDText = _GetOptionalString(_Payload, "entityId");
    }
    const auto _WorkpieceID = _ParseRequiredUuid(_WorkpieceIDText, "workpieceEntityId");
    auto &_Repository = _Scene.Database();
    auto _pWorkpieceEntity = _Repository.GetEntity(_WorkpieceID);
    auto _pWorkpiece = _GetComponent<iCAX::CAM::CWorkpieceComponent>(_pWorkpieceEntity);
    if (!_pWorkpieceEntity || !_pWorkpiece)
    {
        throw std::invalid_argument("Cam SetActiveWorkpiece target does not exist");
    }
    auto _Undo = _Repository.BeginUndoCommand("Set active CAM workpiece");
    auto _pRoot = _GetOrCreateComponent<iCAX::CAM::CRootComponent>(_Repository);
    auto _pSelection = _GetOrCreateComponent<iCAX::CAM::CSelectionComponent>(_Repository);
    _SetUuidProperty(_pRoot, iCAX::CAM::CRootComponent::PropertyName_ActiveWorkpieceID, _WorkpieceID);
    _SetStringProperty(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_HoverKind, std::string());
    _SetUInt64Property(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_HoverID, 0ull);
    _SetStringProperty(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedKind, std::string());
    _SetUInt64Property(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedID, 0ull);
    _SetStringProperty(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedLabel, std::string());
    _Undo->End();
    return _MakeResponse(_BuildScenePayload(_Scene));
}
} // namespace Commands
} // namespace CAM
} // namespace iCAX
