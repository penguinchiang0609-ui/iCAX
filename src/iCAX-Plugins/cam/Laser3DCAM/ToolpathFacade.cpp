#include "pch.h"
#include "Facades.h"
#include "FacadeSupport.h"
#include "ToolpathFacadeImplement.h"
#include "ToolpathResourceKeys.h"

#include "Facades/FacadeRegistrationCatalog.h"
#include "Facades/Facade.h"

namespace
{
    class CToolpathFacade final : public iCAX::Interaction::CFacade
    {
    public:
        CToolpathFacade()
            : CFacade("Toolpath")
        {
            ExposeMethod("List", &iCAX::CAM::Facades::HandleListToolpaths);
            ExposeMethod("RecognizeLoops", &iCAX::CAM::Facades::HandleRecognizeLoops);
            ExposeMethod("AddSelectionPath", &iCAX::CAM::Facades::HandleAddSelectionPath);
            ExposeMethod("SetPoseField", &iCAX::CAM::Facades::HandleSetPoseField);
            ExposeMethod("ClearProgram", &iCAX::CAM::Facades::HandleClearProgram);
        }
    };

    static_assert(iCAX::Interaction::IsStatelessFacadeType<CToolpathFacade>);
}

ICAX_REGISTER_FACADE(CToolpathFacade)

namespace iCAX
{
namespace CAM
{
namespace Facades
{
using namespace Internal;

namespace
{
    iCAX::Interaction::CInvocationResult MakeToolpathListResponse(IN iCAX::Project::ISceneContext& Scene_)
    {
        auto& _Repository = Scene_.Database();
        ObjectMap _Result;
        _Result["cuttingLayers"] = _MakeCuttingLayerArray(_Repository);
        _Result["visibleLayers"] = _MakeVisibleLayerArray(_Repository);
        _Result["program"] = _MakeProgramPayload(_Repository);
        _Result["toolpaths"] = _MakePathArray(_Repository);
        return _MakeResponse(Variant(_Result));
    }
}

iCAX::Interaction::CInvocationResult HandleListToolpaths(
    IN const iCAX::Interaction::CInvocation&,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext*,
    IN iCAX::Project::IProjectContext*,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _Scene = _RequireSceneContext(pSceneContext_);
    return MakeToolpathListResponse(_Scene);
}

iCAX::Interaction::CInvocationResult HandleRecognizeLoops(
    IN const iCAX::Interaction::CInvocation &,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto &_Repository = _Scene.Database();
    auto [_pWorkpieceEntity, _pWorkpiece] = _GetActiveWorkpiece(_Repository);
    if (!_pWorkpieceEntity || !_pWorkpiece)
    {
        throw std::invalid_argument("Cam active workpiece is not available");
    }
    auto _pTopology = _GetTopologyResource(_Scene, _pWorkpiece);
    if (!_pTopology)
    {
        throw std::invalid_argument("Cam topology resource is not available");
    }
    auto _Undo = _Repository.BeginUndoCommand("Recognize CAM loops");
    for (const auto &_Loop : _pTopology->Loops)
    {
        if (!_Loop.Is<ObjectMap>())
        {
            continue;
        }
        const auto _LoopObject = _Loop.To<ObjectMap>();
        const auto _Role = _GetObjectString(_LoopObject, "role");
        const auto _TopologyID = _GetObjectUInt64(_LoopObject, "id");
        if (_TopologyID == 0ull || (_Role != "hole" && _Role != "cut"))
        {
            continue;
        }
        if (_HasPathForTopology(_Repository, _pWorkpieceEntity->GetID(), kTopologyKindLoop, _TopologyID))
        {
            continue;
        }
        _CreatePathEntity(_Scene, _pWorkpieceEntity->GetID(), kTopologyKindLoop, _TopologyID, _LoopObject, "auto");
    }
    _Undo->End();
    return MakeToolpathListResponse(_Scene);
}

iCAX::Interaction::CInvocationResult HandleAddSelectionPath(
    IN const iCAX::Interaction::CInvocation &,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto &_Repository = _Scene.Database();
    auto _pSelection = _GetComponent<iCAX::CAM::CSelectionComponent>(_Repository);
    if (!_pSelection || _pSelection->GetSelectedKind().empty() || _pSelection->GetSelectedID() == 0ull)
    {
        throw std::invalid_argument("Cam selection is empty");
    }
    auto [_pWorkpieceEntity, _pWorkpiece] = _GetActiveWorkpiece(_Repository);
    if (!_pWorkpieceEntity || !_pWorkpiece)
    {
        throw std::invalid_argument("Cam active workpiece is not available");
    }
    auto _pTopology = _GetTopologyResource(_Scene, _pWorkpiece);
    if (!_pTopology)
    {
        throw std::invalid_argument("Cam topology resource is not available");
    }
    auto _TopologyObject = _FindTopologyObject(*_pTopology, _pSelection->GetSelectedKind(), _pSelection->GetSelectedID());
    if (!_TopologyObject.has_value())
    {
        throw std::invalid_argument("Cam selected topology target does not exist");
    }
    auto _Undo = _Repository.BeginUndoCommand("Add CAM path target");
    if (!_HasPathForTopology(_Repository, _pWorkpieceEntity->GetID(), _pSelection->GetSelectedKind(), _pSelection->GetSelectedID()))
    {
        _CreatePathEntity(
            _Scene,
            _pWorkpieceEntity->GetID(),
            _pSelection->GetSelectedKind(),
            _pSelection->GetSelectedID(),
            *_TopologyObject,
            "manual");
    }
    _Undo->End();
    return MakeToolpathListResponse(_Scene);
}

iCAX::Interaction::CInvocationResult HandleSetPoseField(
    IN const iCAX::Interaction::CInvocation &Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    const auto _PathIDText = _GetOptionalString(_Payload, "pathEntityId");
    const auto _PathID = _ParseRequiredUuid(_PathIDText, "pathEntityId");
    auto _SamplesIter = _Payload.find("samples");
    if (_SamplesIter == _Payload.end() || !_SamplesIter->second.Is<VariantArray>())
    {
        throw std::invalid_argument("Cam SetPoseField requires samples array");
    }
    auto _Samples = _NormalizePoseSamples(_SamplesIter->second.To<VariantArray>());
    auto &_Repository = _Scene.Database();
    auto _pPathEntity = _Repository.GetEntity(_PathID);
    auto _pPath = _GetComponent<iCAX::CAM::CPathComponent>(_pPathEntity);
    if (!_pPathEntity || !_pPath)
    {
        throw std::invalid_argument("Cam SetPoseField path does not exist");
    }
    auto &_Resources = _Scene.Resources();
    auto _PoseFieldResourceID = _pPath->GetPoseFieldResourceID();
    if (_PoseFieldResourceID.empty())
    {
        _PoseFieldResourceID = iCAX::CAM::MakePoseFieldResourceID(_PathID);
    }
    auto _pPoseField = std::make_shared<iCAX::CAM::CPoseFieldResource>();
    _pPoseField->nVersion = _NextResourceVersion(_Resources, _PoseFieldResourceID);
    _pPoseField->Samples = std::move(_Samples);
    auto _PoseFieldInfo = _MakeResourceInfo(
        _PoseFieldResourceID,
        _pPath->GetName() + " pose field",
        "pose-field",
        iCAX::Resource::EResourcePersistenceMode::Embedded,
        _pPoseField->nVersion);
    _Resources.Set<iCAX::CAM::CPoseFieldResource>(_PoseFieldResourceID, _pPoseField, _PoseFieldInfo);
    auto _Undo = _Repository.BeginUndoCommand("Set CAM pose field");
    if (_pPath->GetPoseFieldResourceID() != _PoseFieldResourceID)
    {
        _SetStringProperty(_pPath, iCAX::CAM::CPathComponent::PropertyName_PoseFieldResourceID, _PoseFieldResourceID);
    }
    _Undo->End();
    return MakeToolpathListResponse(_Scene);
}

iCAX::Interaction::CInvocationResult HandleClearProgram(
    IN const iCAX::Interaction::CInvocation &,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto &_Repository = _Scene.Database();
    auto _Undo = _Repository.BeginUndoCommand("Clear program");
    _DeleteEntitiesWithComponent<iCAX::CAM::CPathComponent>(_Repository);
    _DeleteNonRootProgramBlocks(_Repository);
    _ClearProgramRootBlockChildren(_Repository);
    _Undo->End();
    return MakeToolpathListResponse(_Scene);
}
} // namespace Facades
} // namespace CAM
} // namespace iCAX
