#include "pch.h"
#include "Targets.h"
#include "CommandTargetSupport.h"
#include "RenderData/RenderData.h"
#include "RenderInteraction/RenderInteraction.h"
#include "ToolpathTargetImplement.h"
#include "Transform/Transform.h"
#include "WorkpieceTargetImplement.h"
#include "WorkpieceResourceKeys.h"

#include "CommandTargets/CommandRegistrationCatalog.h"
#include "CommandTargets/CommandTarget.h"

namespace
{
    class CWorkpieceTarget final : public iCAX::Command::CCommandTarget
    {
    public:
        CWorkpieceTarget()
            : CCommandTarget("Workpiece")
        {
            Bind("Instantiate", &iCAX::CAM::Commands::HandleInstantiateWorkpiece);
            Bind("List", &iCAX::CAM::Commands::HandleListWorkpieces);
            Bind("SetActive", &iCAX::CAM::Commands::HandleSetActiveWorkpiece);
        }
    };

    static_assert(iCAX::Command::IsStatelessCommandTargetType<CWorkpieceTarget>);
}

ICAX_REGISTER_COMMAND_TARGET(CWorkpieceTarget)

namespace iCAX
{
namespace CAM
{
namespace Commands
{
using namespace Internal;

namespace
{
    iCAX::Command::CCommandResponse MakeWorkpieceListResponse(IN iCAX::Project::ISceneContext& Scene_)
    {
        auto& _Repository = Scene_.Database();
        auto [_pWorkpieceEntity, _pWorkpiece] = _GetActiveWorkpiece(_Repository);
        auto _pTopology = _GetTopologyResource(Scene_, _pWorkpiece);
        auto _Workpiece = _MakeWorkpiecePayload(_pWorkpieceEntity, _pWorkpiece);

        ObjectMap _Result;
        _Result["workpiece"] = _Workpiece;
        _Result["workpieces"] = _MakeWorkpieceArray(_Repository);
        _Result["model"] = _Workpiece;
        _Result["topology"] = _MakeTopologyStatusPayload(_pTopology);
        _Result["faces"] = _pTopology ? _MakeTopologyPickArray(_pTopology->Faces) : VariantArray{};
        _Result["loops"] = _pTopology ? _MakeTopologyPickArray(_pTopology->Loops) : VariantArray{};
        _Result["edges"] = _pTopology ? _MakeTopologyPickArray(_pTopology->Edges) : VariantArray{};
        return _MakeResponse(Variant(_Result));
    }

    std::string _EnsureWorkpieceMaterialResource(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const std::string& strModelResourceID_,
        IN const std::string& strName_)
    {
        const auto _MaterialResourceID = iCAX::CAM::MakeWorkpieceMaterialResourceID(strModelResourceID_);
        if (_MaterialResourceID.empty())
        {
            throw std::invalid_argument("Cam workpiece material requires model resource id");
        }

        auto _pMaterial = std::make_shared<iCAX::Render::SRenderMaterialData>();
        _pMaterial->nDataVersion = _NextResourceVersion(Scene_.Resources(), _MaterialResourceID);
        _pMaterial->nColorRGBA = 0x8FB8C9FFu;
        _pMaterial->nAmbientRGBA = 0x8FB8C9FFu;
        _pMaterial->nSpecularRGBA = 0x000000FFu;
        _pMaterial->nEmissiveRGBA = 0x000000FFu;
        _pMaterial->nLineWidth = 1.0f;

        auto _Info = _MakeResourceInfo(
            _MaterialResourceID,
            (strName_.empty() ? std::string("Workpiece") : strName_) + " Material",
            iCAX::Render::SRenderMaterialData::kResourceTypeName,
            iCAX::Resource::EResourcePersistenceMode::Embedded,
            _pMaterial->nDataVersion);
        _Info.Metadata["source"] = "workpiece.default";
        _Info.Metadata["modelResourceId"] = strModelResourceID_;
        Scene_.Resources().Set<iCAX::Render::SRenderMaterialData>(_MaterialResourceID, _pMaterial, _Info);
        return _MaterialResourceID;
    }
}

iCAX::Command::CCommandResponse HandleListWorkpieces(
    IN const iCAX::Command::CCommandRequest&,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext*,
    IN iCAX::Project::IProjectContext*,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _Scene = _RequireSceneContext(pSceneContext_);
    return MakeWorkpieceListResponse(_Scene);
}

iCAX::Command::CCommandResponse HandleInstantiateWorkpiece(
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
    const auto _ModelResourceID = _GetOptionalString(_Payload, "modelResourceId");
    const auto _BRepResourceID = _GetOptionalString(_Payload, "brepResourceId");
    const auto _TopologyResourceID = _GetOptionalString(_Payload, "topologyResourceId");
    if (_ModelResourceID.empty() || _BRepResourceID.empty() || _TopologyResourceID.empty())
    {
        throw std::invalid_argument("Cam Workpiece.Instantiate requires modelResourceId, brepResourceId and topologyResourceId");
    }
    if (_Scene.Resources().GetVersion(_ModelResourceID) == 0)
    {
        throw std::runtime_error("Cam Workpiece.Instantiate model resource is not loaded: " + _ModelResourceID);
    }
    if (_Scene.Resources().GetVersion(_BRepResourceID) == 0)
    {
        throw std::runtime_error("Cam Workpiece.Instantiate brep resource is not loaded: " + _BRepResourceID);
    }
    if (!_Scene.Resources().Get<iCAX::CAM::CTopologyResource>(_TopologyResourceID))
    {
        throw std::runtime_error("Cam Workpiece.Instantiate topology resource is not loaded: " + _TopologyResourceID);
    }

    auto _SourcePath = _GetOptionalString(_Payload, "sourcePath");
    if (_SourcePath.empty())
    {
        const auto _Info = _Scene.Resources().GetInfo(_ModelResourceID);
        if (_Info)
        {
            _SourcePath = _Info->Source;
        }
    }
    const auto _Name = _GetOptionalString(_Payload, "name", _GetDisplayNameFromPath(_SourcePath));
    const auto _TopologyVersion = _GetOptionalUInt64(_Payload, "topologyVersion", _Scene.Resources().GetVersion(_TopologyResourceID));
    const auto _MaterialResourceID = _EnsureWorkpieceMaterialResource(_Scene, _ModelResourceID, _Name);
    auto &_Repository = _Scene.Database();
    auto _Undo = _Repository.BeginUndoCommand("Instantiate CAM workpiece");
    auto _pRoot = _GetOrCreateComponent<iCAX::CAM::CRootComponent>(_Repository);
    auto _pSelection = _GetOrCreateComponent<iCAX::CAM::CSelectionComponent>(_Repository);
    auto [_pProgramRootEntity, _pProgramRootBlock] = _EnsureProgramRootBlock(_Repository);
    (void)_pProgramRootEntity;
    (void)_pProgramRootBlock;
    _EnsureDefaultLayers(_Repository);
    auto [_pWorkpieceEntity, _pWorkpiece] = _CreateEntityWithComponent<iCAX::CAM::CWorkpieceComponent>(_Repository);
    auto _pRender = _GetOrAddEntityComponent<iCAX::RenderInteraction::CRenderInstanceComponent>(_pWorkpieceEntity);
    (void)_GetOrAddEntityComponent<iCAX::Transform::CTransformComponent>(_pWorkpieceEntity);
    _SetStringProperty(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_Name, _Name);
    _SetStringProperty(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_SourcePath, _SourcePath);
    _SetStringProperty(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_ModelResourceID, _ModelResourceID);
    _SetStringProperty(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_BRepResourceID, _BRepResourceID);
    _SetStringProperty(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_TopologyResourceID, _TopologyResourceID);
    _SetUInt64Property(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_TopologyVersion, _TopologyVersion);
    _SetStringProperty(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_GeometryResourceID, _BRepResourceID);
    _SetStringProperty(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_MaterialResourceID, _MaterialResourceID);
    _SetUInt64Property(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_GeometryKind, 1ull);
    _SetUInt64Property(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_RenderClass, 1ull);
    _SetUInt64Property(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_StyleID, 1ull);
    _SetUuidProperty(_pRoot, iCAX::CAM::CRootComponent::PropertyName_ActiveWorkpieceID, _pWorkpieceEntity->GetID());
    _SetUuidProperty(_pRoot, iCAX::CAM::CRootComponent::PropertyName_ActiveOrderPlanID, iCAX::Data::uuid());
    _SetUuidProperty(_pRoot, iCAX::CAM::CRootComponent::PropertyName_LatestSafetyCheckID, iCAX::Data::uuid());
    _SetUuidProperty(_pRoot, iCAX::CAM::CRootComponent::PropertyName_ActiveSimulationID, iCAX::Data::uuid());
    _SetStringProperty(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedKind, std::string());
    _SetUInt64Property(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedID, 0ull);
    _SetUuidProperty(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedEntityID, iCAX::Data::uuid());
    _SetStringProperty(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedLabel, std::string());
    _Undo->End();
    return MakeWorkpieceListResponse(_Scene);
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
    _SetUuidProperty(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_HoverEntityID, iCAX::Data::uuid());
    _SetStringProperty(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedKind, std::string());
    _SetUInt64Property(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedID, 0ull);
    _SetUuidProperty(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedEntityID, iCAX::Data::uuid());
    _SetStringProperty(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedLabel, std::string());
    _Undo->End();
    return MakeWorkpieceListResponse(_Scene);
}
} // namespace Commands
} // namespace CAM
} // namespace iCAX

