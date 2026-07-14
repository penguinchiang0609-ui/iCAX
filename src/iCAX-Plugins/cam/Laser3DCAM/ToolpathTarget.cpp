#include "pch.h"
#include "Targets.h"
#include "CommandTargetSupport.h"
#include "ToolpathTargetImplement.h"
#include "ToolpathResourceKeys.h"

#include "CommandTargets/CommandRegistrationCatalog.h"
#include "CommandTargets/CommandTarget.h"

namespace
{
    class CToolpathTarget final : public iCAX::Command::CCommandTarget
    {
    public:
        CToolpathTarget()
            : CCommandTarget("Toolpath")
        {
            Bind("List", &iCAX::CAM::Commands::HandleListToolpaths);
            Bind("RecognizeLoops", &iCAX::CAM::Commands::HandleRecognizeLoops);
            Bind("AddSelectionPath", &iCAX::CAM::Commands::HandleAddSelectionPath);
            Bind("SetPoseField", &iCAX::CAM::Commands::HandleSetPoseField);
            Bind("ClearProgram", &iCAX::CAM::Commands::HandleClearProgram);
        }
    };

    static_assert(iCAX::Command::IsStatelessCommandTargetType<CToolpathTarget>);
}

ICAX_REGISTER_COMMAND_TARGET(CToolpathTarget)

namespace iCAX
{
namespace CAM
{
namespace Commands
{
using namespace Internal;

namespace
{
    iCAX::Command::CCommandResponse MakeToolpathListResponse(IN iCAX::Project::ISceneContext& Scene_)
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

iCAX::Command::CCommandResponse HandleListToolpaths(
    IN const iCAX::Command::CCommandRequest&,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext*,
    IN iCAX::Project::IProjectContext*,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _Scene = _RequireSceneContext(pSceneContext_);
    return MakeToolpathListResponse(_Scene);
}

iCAX::Command::CCommandResponse HandleRecognizeLoops(
    IN const iCAX::Command::CCommandRequest &,
    IN iCAX::Application::IApplicationContext &,
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

iCAX::Command::CCommandResponse HandleAddSelectionPath(
    IN const iCAX::Command::CCommandRequest &,
    IN iCAX::Application::IApplicationContext &,
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

iCAX::Command::CCommandResponse HandleSetPoseField(
    IN const iCAX::Command::CCommandRequest &Request_,
    IN iCAX::Application::IApplicationContext &,
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

iCAX::Command::CCommandResponse HandleClearProgram(
    IN const iCAX::Command::CCommandRequest &,
    IN iCAX::Application::IApplicationContext &,
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
} // namespace Commands
} // namespace CAM
} // namespace iCAX
