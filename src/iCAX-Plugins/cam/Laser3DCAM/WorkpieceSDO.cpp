#include "pch.h"
#include "SDO.h"
#include "SDOSupport.h"
#include "RenderData/RenderData.h"
#include "RenderInteraction/RenderInteraction.h"
#include "ToolpathSDOImplement.h"
#include "Transform/Transform.h"
#include "GeometryData/TubeNeutralGeometry.h"
#include "WorkpieceSDOImplement.h"
#include "WorkpieceResourceKeys.h"

#include "SDO/SDORegistrationCatalog.h"
#include "SDO/SDO.h"

namespace
{
    class CWorkpieceSDO final : public iCAX::Interaction::CSDO
    {
    public:
        CWorkpieceSDO()
            : CSDO("Workpiece")
        {
            ExposeMethod("Instantiate", &iCAX::CAM::SDO::HandleInstantiateWorkpiece);
            ExposeMethod("List", &iCAX::CAM::SDO::HandleListWorkpieces);
            ExposeMethod("SetActive", &iCAX::CAM::SDO::HandleSetActiveWorkpiece);
            ExposeMethod("Delete", &iCAX::CAM::SDO::HandleDeleteWorkpiece);
        }
    };

    static_assert(iCAX::Interaction::IsStatelessSDOType<CWorkpieceSDO>);
}

ICAX_REGISTER_SDO(CWorkpieceSDO)

namespace iCAX
{
namespace CAM
{
namespace SDO
{
using namespace Internal;

iCAX::Interaction::CInvocationResult Internal::_MakeWorkpieceListResponse(IN iCAX::Project::ISceneContext& Scene_)
{
    auto& _Repository = Scene_.Database();
    auto [_pWorkpieceEntity, _pWorkpiece] = _GetActiveWorkpiece(_Repository);
    auto _pTopology = _GetTopologyResource(Scene_, _pWorkpiece);
    auto _Workpiece = _MakeWorkpiecePayload(_pWorkpieceEntity, _pWorkpiece, &Scene_);

    ObjectMap _Result;
    _Result["workpiece"] = _Workpiece;
    _Result["workpieces"] = _MakeWorkpieceArray(Scene_);
    _Result["model"] = _Workpiece;
    _Result["topology"] = _MakeTopologyStatusPayload(_pTopology);
    _Result["faces"] = _pTopology ? _MakeTopologyPickArray(_pTopology->Faces) : VariantArray{};
    _Result["loops"] = _pTopology ? _MakeTopologyPickArray(_pTopology->Loops) : VariantArray{};
    _Result["edges"] = _pTopology ? _MakeTopologyPickArray(_pTopology->Edges) : VariantArray{};
    const auto _TubeNeutralResourceID = _pWorkpiece
        ? _pWorkpiece->GetTubeNeutralGeometryResourceID()
        : std::string();
    auto _pTubeNeutral = _TubeNeutralResourceID.empty()
        ? std::shared_ptr<iCAX::GeometryData::Tube::CTubeNeutralGeometry>()
        : Scene_.Resources().Get<iCAX::GeometryData::Tube::CTubeNeutralGeometry>(_TubeNeutralResourceID);
    _Result["tubeGeometry"] = _MakeTubeNeutralGeometryPayload(
        _TubeNeutralResourceID,
        _pTubeNeutral);
    return _MakeResponse(Variant(_Result));
}

namespace
{
    std::string _EnsureWorkpieceMaterialResource(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const std::string& strModelResourceID_,
        IN const std::string& strName_)
    {
        const auto _MaterialResourceID =
            iCAX::CAM::MakeWorkpieceMaterialResourceID(
                Scene_.Resources(),
                strModelResourceID_);
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
        return iCAX::RenderInteraction::EnsureFrontendMaterialResource(
            Scene_.Resources(),
            _MaterialResourceID).URL;
    }
}

iCAX::Interaction::CInvocationResult HandleListWorkpieces(
    IN const iCAX::Interaction::CInvocation&,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext*,
    IN iCAX::Project::IProjectContext*,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _Scene = _RequireSceneContext(pSceneContext_);
    return _MakeWorkpieceListResponse(_Scene);
}

iCAX::Interaction::CInvocationResult HandleInstantiateWorkpiece(
    IN const iCAX::Interaction::CInvocation &Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext *pProductContext_,
    IN iCAX::Project::IProjectContext *pProjectContext_,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    (void)_RequireProductContext(pProductContext_);
    (void)_RequireProjectContext(pProjectContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto _GeometryResourceID = _GetOptionalString(_Payload, "geometryResourceUrl");
    auto _BRepResourceID = _GetOptionalString(_Payload, "brepResourceId");
    if (_GeometryResourceID.empty())
    {
        _GeometryResourceID = _BRepResourceID;
    }
    if (_BRepResourceID.empty())
    {
        _BRepResourceID = _GeometryResourceID;
    }
    auto _ModelResourceID = _GetOptionalString(_Payload, "modelResourceId");
    if (_ModelResourceID.empty())
    {
        _ModelResourceID = _GeometryResourceID;
    }
    const auto _TopologyResourceID = _GetOptionalString(_Payload, "topologyResourceId");
    const auto _TubeNeutralResourceID = _GetOptionalString(_Payload, "tubeNeutralGeometryResourceId");
    if (_GeometryResourceID.empty() || _TopologyResourceID.empty())
    {
        throw std::invalid_argument(
            "Cam Workpiece.Instantiate requires geometryResourceUrl and topologyResourceId");
    }
    if (!_Scene.Resources().Get<iCAX::GeometryData::BRepModel>(_GeometryResourceID))
    {
        throw std::runtime_error(
            "Cam Workpiece.Instantiate geometry resource is not loaded: "
            + _GeometryResourceID);
    }
    if (!_Scene.Resources().Get<iCAX::CAM::CTopologyResource>(_TopologyResourceID))
    {
        throw std::runtime_error("Cam Workpiece.Instantiate topology resource is not loaded: " + _TopologyResourceID);
    }
    if (!_TubeNeutralResourceID.empty()
        && !_Scene.Resources().Get<iCAX::GeometryData::Tube::CTubeNeutralGeometry>(_TubeNeutralResourceID))
    {
        throw std::runtime_error(
            "Cam Workpiece.Instantiate tube neutral geometry resource is not loaded: "
            + _TubeNeutralResourceID);
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
    const auto _SectionTypeID = _GetOptionalString(_Payload, "sectionTypeId");
    const auto _Length = _GetOptionalDouble(_Payload, "length", 0.0);
    iCAX::Data::ObjectMap _SectionParameters;
    if (const auto _Parameters = _Payload.find("sectionParameters");
        _Parameters != _Payload.end()
        && _Parameters->second.Is<iCAX::Data::ObjectMap>())
    {
        _SectionParameters = _Parameters->second.To<iCAX::Data::ObjectMap>();
    }
    const auto _TopologyVersion = _GetOptionalUInt64(_Payload, "topologyVersion", _Scene.Resources().GetVersion(_TopologyResourceID));
    const auto _MaterialResourceID = _EnsureWorkpieceMaterialResource(
        _Scene, _GeometryResourceID, _Name);
    const auto _GeometryResource =
        iCAX::RenderInteraction::EnsureFrontendGeometryResource(
            _Scene.Resources(),
            _GeometryResourceID,
            iCAX::Render::ERenderGeometryKind::Mesh);
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
    _SetUInt64Property(
        _pWorkpiece,
        iCAX::CAM::CWorkpieceComponent::PropertyName_Quantity,
        std::max(1ull, _GetOptionalUInt64(_Payload, "quantity", 1ull)));
    _SetStringProperty(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_SourcePath, _SourcePath);
    _SetStringProperty(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_GeometryResourceID, _GeometryResourceID);
    _SetStringProperty(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_SectionTypeID, _SectionTypeID);
    _SetObjectMapProperty(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_SectionParameters, _SectionParameters);
    _SetDoubleProperty(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_Length, _Length);
    _SetStringProperty(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_ModelResourceID, _ModelResourceID);
    _SetStringProperty(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_BRepResourceID, _BRepResourceID);
    _SetStringProperty(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_TopologyResourceID, _TopologyResourceID);
    _SetStringProperty(
        _pWorkpiece,
        iCAX::CAM::CWorkpieceComponent::PropertyName_TubeNeutralGeometryResourceID,
        _TubeNeutralResourceID);
    _SetUInt64Property(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_TopologyVersion, _TopologyVersion);
    _SetUInt64Property(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_GeometryRevision, 1ull);
    _SetStringProperty(_pWorkpiece, iCAX::CAM::CWorkpieceComponent::PropertyName_EditState, "Current");
    _SetStringProperty(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_GeometryResourceID, _GeometryResource.URL);
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
    return _MakeWorkpieceListResponse(_Scene);
}

iCAX::Interaction::CInvocationResult HandleSetActiveWorkpiece(
    IN const iCAX::Interaction::CInvocation &Request_,
    IN const iCAX::Application::IApplicationContext&,
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
    return _MakeWorkpieceListResponse(_Scene);
}

iCAX::Interaction::CInvocationResult HandleDeleteWorkpiece(
    IN const iCAX::Interaction::CInvocation& Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext*,
    IN iCAX::Project::IProjectContext*,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto _WorkpieceIDText = _GetOptionalString(_Payload, "workpieceEntityId");
    if (_WorkpieceIDText.empty())
    {
        _WorkpieceIDText = _GetOptionalString(_Payload, "entityId");
    }
    const auto _WorkpieceID = _ParseRequiredUuid(_WorkpieceIDText, "workpieceEntityId");
    auto& _Repository = _Scene.Database();
    const auto _pEntity = _Repository.GetEntity(_WorkpieceID);
    if (!_pEntity || !_GetComponent<iCAX::CAM::CWorkpieceComponent>(_pEntity))
    {
        throw std::invalid_argument("Cam Workpiece.Delete target does not exist");
    }

    auto _Undo = _Repository.BeginUndoCommand("Delete CAM workpiece");
    auto _pRoot = _GetOrCreateComponent<iCAX::CAM::CRootComponent>(_Repository);
    const auto _WasActive = _pRoot->GetActiveWorkpieceID() == _WorkpieceID;
    std::string _strError;
    if (!_Repository.DeleteEntity(_WorkpieceID, _strError))
    {
        throw std::runtime_error(
            _strError.empty() ? "Cam Workpiece.Delete failed" : _strError);
    }
    if (_WasActive)
    {
        const auto _Remaining = _CollectEntitiesWithComponent<
            iCAX::CAM::CWorkpieceComponent>(_Repository);
        _SetUuidProperty(
            _pRoot,
            iCAX::CAM::CRootComponent::PropertyName_ActiveWorkpieceID,
            _Remaining.empty()
                ? iCAX::Data::uuid()
                : _Remaining.front().first->GetID());
    }
    auto _pSelection = _GetOrCreateComponent<iCAX::CAM::CSelectionComponent>(_Repository);
    _SetStringProperty(
        _pSelection,
        iCAX::CAM::CSelectionComponent::PropertyName_SelectedKind,
        std::string());
    _SetUInt64Property(
        _pSelection,
        iCAX::CAM::CSelectionComponent::PropertyName_SelectedID,
        0ull);
    _SetUuidProperty(
        _pSelection,
        iCAX::CAM::CSelectionComponent::PropertyName_SelectedEntityID,
        iCAX::Data::uuid());
    _Undo->End();
    return _MakeWorkpieceListResponse(_Scene);
}

} // namespace SDO
} // namespace CAM
} // namespace iCAX

