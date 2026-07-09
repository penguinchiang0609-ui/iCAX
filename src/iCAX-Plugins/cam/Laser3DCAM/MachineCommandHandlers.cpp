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

iCAX::Command::CCommandResponse HandleImportMachine(
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
    const auto _SourcePath = _GetOptionalString(_Payload, "sourcePath");
    if (_SourcePath.empty())
    {
        throw std::invalid_argument("Cam ImportMachine requires sourcePath");
    }
    if (!_IsSupportedMachineDescriptionPath(_SourcePath))
    {
        throw std::invalid_argument("Cam ImportMachine only supports SDF/XML machine description files");
    }
    iCAX::CAM::SSDFMachineDocument _SDF;
    std::string _ParseError;
    if (!iCAX::CAM::ParseSDFMachineDocument(_SourcePath, _SDF, _ParseError))
    {
        throw std::invalid_argument("Cam ImportMachine SDF parse failed: " + _ParseError);
    }
    auto &_Repository = _Scene.Database();
    auto _Undo = _Repository.BeginUndoCommand("Import CAM machine");
    auto [_pMachineEntity, _pMachine] = _EnsureMachineEntity(_Repository);
    auto _pRoot = _GetOrCreateComponent<iCAX::CAM::CRootComponent>(_Repository);
    const auto _ModelName = _SDF.ModelName.empty() ? _GetDisplayNameFromPath(_SourcePath) : _SDF.ModelName;
    const auto _Name = _GetOptionalString(_Payload, "name", _ModelName);
    _SetUuidProperty(_pRoot, iCAX::CAM::CRootComponent::PropertyName_ActiveMachineID, _pMachineEntity->GetID());
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_Name, _Name);
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_SourcePath, _SourcePath);
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_DescriptionFormat, "SDF");
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_SDFVersion, _SDF.SDFVersion);
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_ModelName, _SDF.ModelName);
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_WorkstationName, "默认工位");
    _SetBoolProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_StaticModel, _SDF.bStaticModel);
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_Status, "Ready");
    _SetMachineJointStateFromSDF(_pMachine, _SDF.Joints);
    _SetMachineDefaultPose(_pMachine);
    _SetDoubleProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_MaxVelocity, _GetOptionalDouble(_Payload, "maxVelocity", 1000.0));
    _SetDoubleProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_MaxAcceleration, _GetOptionalDouble(_Payload, "maxAcceleration", 2000.0));
    _SetVariantArrayProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_Links, _SDF.Links);
    _SetVariantArrayProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_Joints, _SDF.Joints);
    _SetVariantArrayProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_Visuals, _SDF.Visuals);
    _SetVariantArrayProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_Collisions, _SDF.Collisions);
    _SetVariantArrayProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_Materials, _SDF.Materials);
    _SetVariantArrayProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_Includes, _SDF.Includes);
    _SetVariantArrayProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_Frames, _SDF.Frames);
    _SetVariantArrayProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_Plugins, _SDF.Plugins);
    _CreateMachinePartEntitiesFromSDF(_Scene, _pMachineEntity->GetID(), _Name, _SDF);
    _SetStringProperty(
        _pMachine,
        iCAX::CAM::CMachineComponent::PropertyName_LastCheckResult,
        "SDF 已解析："
            + std::to_string(_SDF.Links.size()) + " links / "
            + std::to_string(_SDF.Joints.size()) + " joints / "
            + std::to_string(_SDF.Visuals.size()) + " visuals");
    _Undo->End();
    return _MakeResponse(_BuildScenePayload(_Scene));
}

iCAX::Command::CCommandResponse HandleSetMachineParameters(
    IN const iCAX::Command::CCommandRequest &Request_,
    IN iCAX::Application::IApplicationContext &,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto &_Repository = _Scene.Database();
    auto [_pMachineEntity, _pMachine] = _RequireActiveMachine(_Repository);
    const auto _MaxVelocity = _GetOptionalDouble(_Payload, "maxVelocity", _pMachine->GetMaxVelocity());
    const auto _MaxAcceleration = _GetOptionalDouble(_Payload, "maxAcceleration", _pMachine->GetMaxAcceleration());
    if (_MaxVelocity <= 0.0 || _MaxAcceleration <= 0.0)
    {
        throw std::invalid_argument("Cam machine maxVelocity and maxAcceleration must be greater than zero");
    }
    const auto _JointCount = _pMachine->GetJointNames().size();
    const auto _LowerLimits = _GetOptionalVariantArray(_Payload, "jointLowerLimits", _pMachine->GetJointLowerLimits());
    const auto _UpperLimits = _GetOptionalVariantArray(_Payload, "jointUpperLimits", _pMachine->GetJointUpperLimits());
    auto _Undo = _Repository.BeginUndoCommand("Set CAM machine parameters");
    _SetDoubleProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_MaxVelocity, _MaxVelocity);
    _SetDoubleProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_MaxAcceleration, _MaxAcceleration);
    if (!_LowerLimits.empty())
    {
        _SetVariantArrayProperty(
            _pMachine,
            iCAX::CAM::CMachineComponent::PropertyName_JointLowerLimits,
            _NormalizeDoubleArray(_LowerLimits, "jointLowerLimits", _JointCount));
    }
    if (!_UpperLimits.empty())
    {
        _SetVariantArrayProperty(
            _pMachine,
            iCAX::CAM::CMachineComponent::PropertyName_JointUpperLimits,
            _NormalizeDoubleArray(_UpperLimits, "jointUpperLimits", _JointCount));
    }
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_Status, "Ready");
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_LastCheckResult, "机器参数已更新");
    _Undo->End();
    return _MakeResponse(_BuildScenePayload(_Scene));
}

iCAX::Command::CCommandResponse HandleSetMachineTCP(
    IN const iCAX::Command::CCommandRequest &Request_,
    IN iCAX::Application::IApplicationContext &,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto &_Repository = _Scene.Database();
    auto [_pMachineEntity, _pMachine] = _RequireActiveMachine(_Repository);
    const auto _TCP = _GetOptionalVariantArray(_Payload, "tcp", _pMachine->GetTCP());
    const auto _BeamDirection = _GetOptionalVariantArray(_Payload, "beamDirection", _pMachine->GetBeamDirection());
    auto _Undo = _Repository.BeginUndoCommand("Set CAM machine TCP");
    _SetVariantArrayProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_TCP, _NormalizeDoubleArray(_TCP, "tcp", 3));
    _SetVariantArrayProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_BeamDirection, _NormalizeDirectionArray(_BeamDirection, "beamDirection"));
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_Status, "Ready");
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_LastCheckResult, "TCP 已更新");
    _Undo->End();
    return _MakeResponse(_BuildScenePayload(_Scene));
}

iCAX::Command::CCommandResponse HandleJogMachine(
    IN const iCAX::Command::CCommandRequest &Request_,
    IN iCAX::Application::IApplicationContext &,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto &_Repository = _Scene.Database();
    auto [_pMachineEntity, _pMachine] = _RequireActiveMachine(_Repository);
    const auto _Axis = _GetOptionalString(_Payload, "axis", "X");
    const auto _Delta = _GetOptionalDouble(_Payload, "delta", 0.0);
    if (_Delta == 0.0)
    {
        throw std::invalid_argument("Cam JogMachine requires non-zero delta");
    }
    const auto &_JointNames = _pMachine->GetJointNames();
    auto _Positions = _VariantArrayToDoubles(_pMachine->GetJointPositions(), "jointPositions");
    const auto _LowerLimits = _VariantArrayToDoubles(_pMachine->GetJointLowerLimits(), "jointLowerLimits");
    const auto _UpperLimits = _VariantArrayToDoubles(_pMachine->GetJointUpperLimits(), "jointUpperLimits");
    if (_Positions.size() != _JointNames.size() || _Positions.size() != _LowerLimits.size() || _Positions.size() != _UpperLimits.size())
    {
        throw std::invalid_argument("Cam machine joint state is incomplete");
    }
    auto _AxisIndex = _Positions.size();
    for (size_t _Index = 0; _Index < _JointNames.size(); ++_Index)
    {
        if (_JointNames[_Index].Is<std::string>() && _JointNames[_Index].To<std::string>() == _Axis)
        {
            _AxisIndex = _Index;
            break;
        }
    }
    if (_AxisIndex >= _Positions.size())
    {
        throw std::invalid_argument("Cam JogMachine axis does not exist: " + _Axis);
    }
    const auto _Target = _Positions[_AxisIndex] + _Delta;
    if (_Target < _LowerLimits[_AxisIndex] || _Target > _UpperLimits[_AxisIndex])
    {
        throw std::invalid_argument("Cam JogMachine target exceeds joint limit: " + _Axis);
    }
    _Positions[_AxisIndex] = _Target;
    _SetVariantArrayProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_JointPositions, _DoublesToVariantArray(_Positions));
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_Status, "Jogged");
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_LastCheckResult, _Axis + " 点动完成");
    return _MakeResponse(_BuildScenePayload(_Scene));
}

iCAX::Command::CCommandResponse HandleHomeMachine(
    IN const iCAX::Command::CCommandRequest &,
    IN iCAX::Application::IApplicationContext &,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto &_Repository = _Scene.Database();
    auto [_pMachineEntity, _pMachine] = _RequireActiveMachine(_Repository);
    auto _Positions = _VariantArrayToDoubles(_pMachine->GetJointPositions(), "jointPositions");
    std::fill(_Positions.begin(), _Positions.end(), 0.0);
    _SetVariantArrayProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_JointPositions, _DoublesToVariantArray(_Positions));
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_Status, "Homed");
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_LastCheckResult, "机器已回零");
    return _MakeResponse(_BuildScenePayload(_Scene));
}

iCAX::Command::CCommandResponse HandleResetMachine(
    IN const iCAX::Command::CCommandRequest &,
    IN iCAX::Application::IApplicationContext &,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto &_Repository = _Scene.Database();
    auto [_pMachineEntity, _pMachine] = _RequireActiveMachine(_Repository);
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_Status, "Ready");
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_LastCheckResult, "机器已复位");
    return _MakeResponse(_BuildScenePayload(_Scene));
}

iCAX::Command::CCommandResponse HandleCheckMachineLimits(
    IN const iCAX::Command::CCommandRequest &,
    IN iCAX::Application::IApplicationContext &,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto &_Repository = _Scene.Database();
    auto [_pMachineEntity, _pMachine] = _RequireActiveMachine(_Repository);
    const auto _Result = _CheckMachineLimitResult(_pMachine);
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_Status, _Result == "轴限位检查通过" ? "Ready" : "LimitViolation");
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_LastCheckResult, _Result);
    return _MakeResponse(_BuildScenePayload(_Scene));
}

iCAX::Command::CCommandResponse HandleCheckMachineReach(
    IN const iCAX::Command::CCommandRequest &,
    IN iCAX::Application::IApplicationContext &,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto &_Repository = _Scene.Database();
    auto [_pMachineEntity, _pMachine] = _RequireActiveMachine(_Repository);
    auto [_pWorkpieceEntity, _pWorkpiece] = _GetActiveWorkpiece(_Repository);
    const auto _Result = (_pWorkpieceEntity && _pWorkpiece)
                             ? std::string("行程检查入口已就绪：后续接入 IK、奇异点和碰撞检查")
                             : std::string("尚未导入工件，无法做行程检查");
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_Status, (_pWorkpieceEntity && _pWorkpiece) ? "Ready" : "NeedWorkpiece");
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineComponent::PropertyName_LastCheckResult, _Result);
    return _MakeResponse(_BuildScenePayload(_Scene));
}
} // namespace Commands
} // namespace CAM
} // namespace iCAX
