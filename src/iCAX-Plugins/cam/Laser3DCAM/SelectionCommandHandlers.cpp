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

iCAX::Command::CCommandResponse HandlePickTopology(
    IN const iCAX::Command::CCommandRequest &Request_,
    IN iCAX::Application::IApplicationContext &,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto _Kind = _GetOptionalString(_Payload, "kind");
    const auto _ID = _GetOptionalUInt64(_Payload, "id");
    if (_Kind.empty() || _ID == 0ull)
    {
        throw std::invalid_argument("Cam PickTopology requires kind and id");
    }
    auto &_Repository = _Scene.Database();
    auto [_pWorkpieceEntity, _pWorkpiece] = _GetActiveWorkpiece(_Repository);
    auto _pTopology = _GetTopologyResource(_Scene, _pWorkpiece);
    if (!_pTopology)
    {
        throw std::invalid_argument("Cam topology resource is not available");
    }
    auto _TopologyObject = _FindTopologyObject(*_pTopology, _Kind, _ID);
    if (!_TopologyObject.has_value())
    {
        throw std::invalid_argument("Cam topology target does not exist");
    }
    auto _Label = _GetOptionalString(_Payload, "label", _ResolveTopologyLabel(*_pTopology, _Kind, _ID));
    if (_Label.empty())
    {
        _Label = _Kind + " " + std::to_string(_ID);
    }
    auto _pSelection = _GetOrCreateComponent<iCAX::CAM::CSelectionComponent>(_Repository);
    _SetStringProperty(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedKind, _Kind);
    _SetUInt64Property(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedID, _ID);
    _SetStringProperty(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedLabel, _Label);
    return _MakeResponse(_BuildScenePayload(_Scene));
}

iCAX::Command::CCommandResponse HandlePickMachineObject(
    IN const iCAX::Command::CCommandRequest &Request_,
    IN iCAX::Application::IApplicationContext &,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    const auto _ObjectID = static_cast<iCAX::Project::SceneObjectID>(_GetOptionalUInt64(_Payload, "objectId"));
    auto _ID = _GetOptionalUInt64(_Payload, "id", _ObjectID);
    auto _Kind = _GetOptionalString(_Payload, "kind", "machine");
    auto _Label = _GetOptionalString(_Payload, "label");
    if (_ObjectID != iCAX::Project::kInvalidSceneObjectID)
    {
        const auto *_pBinding = _Scene.Objects().FindObject(_ObjectID);
        if (!_pBinding)
        {
            throw std::invalid_argument("Cam PickMachineObject scene object does not exist");
        }
        if (_Label.empty())
        {
            _Label = _pBinding->Name;
        }
        for (const auto &_Alias : _pBinding->Aliases)
        {
            if (_Alias.Namespace == "machine.visual")
            {
                _Kind = "machine.visual";
                if (_Label.empty())
                {
                    _Label = _Alias.Key;
                }
                break;
            }
            if (_Alias.Namespace == "machine.link")
            {
                _Kind = "machine.link";
                if (_Label.empty())
                {
                    _Label = _Alias.Key;
                }
            }
        }
    }
    if (_ID == 0ull)
    {
        throw std::invalid_argument("Cam PickMachineObject requires objectId or id");
    }
    if (_Label.empty())
    {
        _Label = _Kind + " " + std::to_string(_ID);
    }
    auto &_Repository = _Scene.Database();
    auto _pSelection = _GetOrCreateComponent<iCAX::CAM::CSelectionComponent>(_Repository);
    _SetStringProperty(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedKind, _Kind);
    _SetUInt64Property(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedID, _ID);
    _SetStringProperty(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedLabel, _Label);
    return _MakeResponse(_BuildScenePayload(_Scene));
}
} // namespace Commands
} // namespace CAM
} // namespace iCAX
