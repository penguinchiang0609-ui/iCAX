#include "pch.h"
#include "Facades.h"
#include "FacadeSupport.h"
#include "RenderInteraction/RenderInteraction.h"
#include "ToolpathFacadeImplement.h"
#include "WorkpieceFacadeImplement.h"

#include "Facades/FacadeRegistrationCatalog.h"
#include "Facades/Facade.h"

namespace
{
class CWorkpieceEditFacade final : public iCAX::Interaction::CFacade
{
public:
    CWorkpieceEditFacade() : CFacade("WorkpieceEdit")
    {
        ExposeMethod("Begin", &iCAX::CAM::Facades::HandleBeginWorkpieceEdit);
        ExposeMethod("Inspect", &iCAX::CAM::Facades::HandleInspectWorkpieceEdit);
        ExposeMethod("Commit", &iCAX::CAM::Facades::HandleCommitWorkpieceEdit);
        ExposeMethod("Discard", &iCAX::CAM::Facades::HandleDiscardWorkpieceEdit);
    }
};
static_assert(iCAX::Interaction::IsStatelessFacadeType<CWorkpieceEditFacade>);
}

ICAX_REGISTER_FACADE(CWorkpieceEditFacade)

namespace iCAX::CAM::Facades
{
using namespace Internal;

namespace
{
auto _RequireEditableWorkpiece(iCAX::Project::ISceneContext& Scene_)
{
    auto [_pEntity, _pWorkpiece] = _GetActiveWorkpiece(Scene_.Database());
    if (!_pEntity || !_pWorkpiece)
    {
        throw std::invalid_argument("Cam WorkpieceEdit requires an active workpiece");
    }
    return std::make_pair(_pEntity, _pWorkpiece);
}

ObjectMap _MakeInspection(
    const std::shared_ptr<iCAX::GeometryData::BRepModel>& pBRep_,
    const std::shared_ptr<iCAX::CAM::CTopologyResource>& pTopology_,
    const std::string& strScope_)
{
    auto _Report = _MakeTopologyStatusPayload(pTopology_);
    _Report["scope"] = strScope_;
    const auto _Count = [](const auto& Items_, auto Predicate_) {
        return static_cast<unsigned long long>(std::count_if(Items_.begin(), Items_.end(), Predicate_));
    };
    const auto _DegeneratedEdges = pBRep_ ? _Count(pBRep_->Edges, [](const auto& Edge_) { return Edge_.Degenerated; }) : 0ull;
    const auto _OpenWires = pBRep_ ? _Count(pBRep_->Wires, [](const auto& Wire_) { return !Wire_.Closed; }) : 0ull;
    const auto _OpenShells = pBRep_ ? _Count(pBRep_->Shells, [](const auto& Shell_) { return !Shell_.Closed; }) : 0ull;
    _Report["vertexCount"] = pBRep_ ? static_cast<unsigned long long>(pBRep_->Vertices.size()) : 0ull;
    _Report["wireCount"] = pBRep_ ? static_cast<unsigned long long>(pBRep_->Wires.size()) : 0ull;
    _Report["shellCount"] = pBRep_ ? static_cast<unsigned long long>(pBRep_->Shells.size()) : 0ull;
    _Report["solidCount"] = pBRep_ ? static_cast<unsigned long long>(pBRep_->Solids.size()) : 0ull;
    _Report["degeneratedEdgeCount"] = _DegeneratedEdges;
    _Report["openWireCount"] = _OpenWires;
    _Report["openShellCount"] = _OpenShells;
    const bool _Ready = pBRep_ && pTopology_;
    const bool _HasWarnings = _DegeneratedEdges > 0 || _OpenWires > 0 || _OpenShells > 0;
    _Report["status"] = !_Ready ? std::string("Failed") : (_HasWarnings ? std::string("Warning") : std::string("Ready"));
    _Report["message"] = !_Ready
        ? std::string("BRep or topology resource is missing")
        : (_HasWarnings ? std::string("Geometry contains open or degenerated topology") : std::string("Geometry passed the current structural checks"));
    return _Report;
}
}

Interaction::CInvocationResult HandleBeginWorkpieceEdit(
    const Interaction::CInvocation&, const Application::IApplicationContext&, Product::IProductContext*, Project::IProjectContext*, Project::ISceneContext* pScene_)
{
    auto& _Scene = _RequireSceneContext(pScene_);
    auto [_pEntity, _pWorkpiece] = _RequireEditableWorkpiece(_Scene);
    if (!_pWorkpiece->GetDraftBRepResourceID().empty())
    {
        return _MakeWorkpieceListResponse(_Scene);
    }

    auto _pBRep = _Scene.Resources().Get<iCAX::GeometryData::BRepModel>(_pWorkpiece->GetBRepResourceID());
    auto _pTopology = _Scene.Resources().Get<iCAX::CAM::CTopologyResource>(_pWorkpiece->GetTopologyResourceID());
    if (!_pBRep || !_pTopology)
    {
        throw std::runtime_error("Cam WorkpieceEdit.Begin cannot load current geometry resources");
    }

    const auto _Revision = _pWorkpiece->GetGeometryRevision() + 1ull;
    const auto _Prefix = std::string("cam.workpiece.") + _UuidToString(_pEntity->GetID()) + ".draft." + std::to_string(_Revision);
    const auto _DraftBRepID = _Prefix + ".brep";
    const auto _DraftTopologyID = _Prefix + ".topology";
    const auto _DraftTopologyVersion = _NextResourceVersion(_Scene.Resources(), _DraftTopologyID);

    auto _pDraftBRep = std::make_shared<iCAX::GeometryData::BRepModel>(*_pBRep);
    auto _pDraftTopology = std::make_shared<iCAX::CAM::CTopologyResource>(*_pTopology);
    _pDraftTopology->nVersion = _DraftTopologyVersion;
    _pDraftTopology->Metadata["editState"] = std::string("Draft");
    _pDraftTopology->Metadata["parentBRepResourceId"] = _pWorkpiece->GetBRepResourceID();

    auto _BRepInfo = _MakeResourceInfo(_DraftBRepID, _pWorkpiece->GetName() + " draft BRep", iCAX::GeometryData::BRepModel::kResourceTypeName,
        iCAX::Resource::EResourcePersistenceMode::Embedded, _NextResourceVersion(_Scene.Resources(), _DraftBRepID));
    auto _TopologyInfo = _MakeResourceInfo(_DraftTopologyID, _pWorkpiece->GetName() + " draft topology", "topology",
        iCAX::Resource::EResourcePersistenceMode::Embedded, _DraftTopologyVersion);
    _Scene.Resources().Set<iCAX::GeometryData::BRepModel>(_DraftBRepID, _pDraftBRep, _BRepInfo);
    _Scene.Resources().Set<iCAX::CAM::CTopologyResource>(_DraftTopologyID, _pDraftTopology, _TopologyInfo);

    auto _Undo = _Scene.Database().BeginUndoCommand("Begin CAM workpiece CAD edit");
    _SetStringProperty(_pWorkpiece, CWorkpieceComponent::PropertyName_DraftBRepResourceID, _DraftBRepID);
    _SetStringProperty(_pWorkpiece, CWorkpieceComponent::PropertyName_DraftTopologyResourceID, _DraftTopologyID);
    _SetUInt64Property(_pWorkpiece, CWorkpieceComponent::PropertyName_DraftTopologyVersion, _DraftTopologyVersion);
    _SetStringProperty(_pWorkpiece, CWorkpieceComponent::PropertyName_EditState, "Draft");
    _Undo->End();
    return _MakeWorkpieceListResponse(_Scene);
}

Interaction::CInvocationResult HandleInspectWorkpieceEdit(
    const Interaction::CInvocation&, const Application::IApplicationContext&, Product::IProductContext*, Project::IProjectContext*, Project::ISceneContext* pScene_)
{
    auto& _Scene = _RequireSceneContext(pScene_);
    auto [_pEntity, _pWorkpiece] = _RequireEditableWorkpiece(_Scene);
    (void)_pEntity;
    const bool _HasDraft = !_pWorkpiece->GetDraftTopologyResourceID().empty();
    const auto _BRepID = _HasDraft ? _pWorkpiece->GetDraftBRepResourceID() : _pWorkpiece->GetBRepResourceID();
    const auto _TopologyID = _HasDraft ? _pWorkpiece->GetDraftTopologyResourceID() : _pWorkpiece->GetTopologyResourceID();
    ObjectMap _Result;
    _Result["cadInspection"] = _MakeInspection(
        _Scene.Resources().Get<iCAX::GeometryData::BRepModel>(_BRepID),
        _Scene.Resources().Get<CTopologyResource>(_TopologyID),
        _HasDraft ? "Draft" : "Current");
    return _MakeResponse(Variant(_Result));
}

Interaction::CInvocationResult HandleCommitWorkpieceEdit(
    const Interaction::CInvocation&, const Application::IApplicationContext&, Product::IProductContext*, Project::IProjectContext*, Project::ISceneContext* pScene_)
{
    auto& _Scene = _RequireSceneContext(pScene_);
    auto [_pEntity, _pWorkpiece] = _RequireEditableWorkpiece(_Scene);
    if (_pWorkpiece->GetDraftBRepResourceID().empty() || _pWorkpiece->GetDraftTopologyResourceID().empty())
    {
        throw std::invalid_argument("Cam WorkpieceEdit.Commit requires an active draft");
    }
    auto _pRender = _GetOrAddEntityComponent<iCAX::RenderInteraction::CRenderInstanceComponent>(_pEntity);
    auto _Undo = _Scene.Database().BeginUndoCommand("Commit CAM workpiece CAD edit");
    _SetStringProperty(_pWorkpiece, CWorkpieceComponent::PropertyName_BRepResourceID, _pWorkpiece->GetDraftBRepResourceID());
    _SetStringProperty(_pWorkpiece, CWorkpieceComponent::PropertyName_TopologyResourceID, _pWorkpiece->GetDraftTopologyResourceID());
    _SetUInt64Property(_pWorkpiece, CWorkpieceComponent::PropertyName_TopologyVersion, _pWorkpiece->GetDraftTopologyVersion());
    _SetUInt64Property(_pWorkpiece, CWorkpieceComponent::PropertyName_GeometryRevision, _pWorkpiece->GetGeometryRevision() + 1ull);
    _SetStringProperty(_pRender, iCAX::RenderInteraction::CRenderInstanceComponent::PropertyName_GeometryResourceID, _pWorkpiece->GetDraftBRepResourceID());
    _SetStringProperty(_pWorkpiece, CWorkpieceComponent::PropertyName_DraftBRepResourceID, std::string());
    _SetStringProperty(_pWorkpiece, CWorkpieceComponent::PropertyName_DraftTopologyResourceID, std::string());
    _SetUInt64Property(_pWorkpiece, CWorkpieceComponent::PropertyName_DraftTopologyVersion, 0ull);
    _SetStringProperty(_pWorkpiece, CWorkpieceComponent::PropertyName_EditState, "Current");
    _Undo->End();
    return _MakeWorkpieceListResponse(_Scene);
}

Interaction::CInvocationResult HandleDiscardWorkpieceEdit(
    const Interaction::CInvocation&, const Application::IApplicationContext&, Product::IProductContext*, Project::IProjectContext*, Project::ISceneContext* pScene_)
{
    auto& _Scene = _RequireSceneContext(pScene_);
    auto [_pEntity, _pWorkpiece] = _RequireEditableWorkpiece(_Scene);
    (void)_pEntity;
    auto _Undo = _Scene.Database().BeginUndoCommand("Discard CAM workpiece CAD edit");
    _SetStringProperty(_pWorkpiece, CWorkpieceComponent::PropertyName_DraftBRepResourceID, std::string());
    _SetStringProperty(_pWorkpiece, CWorkpieceComponent::PropertyName_DraftTopologyResourceID, std::string());
    _SetUInt64Property(_pWorkpiece, CWorkpieceComponent::PropertyName_DraftTopologyVersion, 0ull);
    _SetStringProperty(_pWorkpiece, CWorkpieceComponent::PropertyName_EditState, "Current");
    _Undo->End();
    return _MakeWorkpieceListResponse(_Scene);
}
}
