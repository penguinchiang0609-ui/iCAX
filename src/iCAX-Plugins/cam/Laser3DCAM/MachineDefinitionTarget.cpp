#include "pch.h"
#include "Targets.h"
#include "CommandTargetSupport.h"
#include "MachineDefinitionTargetImplement.h"
#include "MachineDescriptionLoader.h"
#include "MachineInstanceComponents.h"
#include "MachineResourceKeys.h"
#include "MachineTargetImplement.h"

#include "CommandTargets/CommandRegistrationCatalog.h"
#include "CommandTargets/CommandTarget.h"

namespace
{
    class CMachineDefinitionTarget final : public iCAX::Command::CCommandTarget
    {
    public:
        CMachineDefinitionTarget()
            : CCommandTarget("MachineDefinition")
        {
            Bind("Import", &iCAX::CAM::Commands::HandleImportMachineDefinition);
            Bind("List", &iCAX::CAM::Commands::HandleListMachineDefinitions);
            Bind("SetEnabled", &iCAX::CAM::Commands::HandleSetMachineDefinitionEnabled);
            Bind("Delete", &iCAX::CAM::Commands::HandleDeleteMachineDefinition);
        }
    };

    static_assert(iCAX::Command::IsStatelessCommandTargetType<CMachineDefinitionTarget>);
}

ICAX_REGISTER_COMMAND_TARGET(CMachineDefinitionTarget)

namespace iCAX
{
namespace CAM
{
namespace Commands
{
using namespace Internal;

namespace
{
    bool GetRequiredBool(IN const ObjectMap& Payload_, IN const std::string& strName_)
    {
        auto _Iter = Payload_.find(strName_);
        if (_Iter == Payload_.end() || _Iter->second.Is<std::monostate>())
        {
            throw std::invalid_argument("Cam payload field is required: " + strName_);
        }
        if (!_Iter->second.Is<bool>())
        {
            throw std::invalid_argument("Cam payload field must be a bool: " + strName_);
        }
        return _Iter->second.To<bool>();
    }
}

iCAX::Command::CCommandResponse HandleImportMachineDefinition(
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
        throw std::invalid_argument("Cam MachineDefinition.Import requires sourcePath");
    }
    if (!_IsSupportedMachineDescriptionPath(_SourcePath))
    {
        throw std::invalid_argument("Cam MachineDefinition.Import only supports SDF/XML machine description files");
    }

    iCAX::CAM::CMachineDescriptionResource _Description;
    std::string _LoadError;
    if (!iCAX::CAM::LoadMachineDescription(_SourcePath, _Description, _LoadError))
    {
        throw std::invalid_argument("Cam MachineDefinition.Import description load failed: " + _LoadError);
    }

    const auto _ModelName = _Description.ModelName.empty() ? _GetDisplayNameFromPath(_SourcePath) : _Description.ModelName;
    const auto _Name = _GetOptionalString(_Payload, "name", _ModelName);
    auto _ResourceID = _GetOptionalString(_Payload, "resourceId");
    if (_ResourceID.empty())
    {
        _ResourceID = iCAX::CAM::MakeMachineDefinitionResourceID(iCAX::Data::GenerateNewUUID());
    }

    auto& _Repository = _Scene.Database();
    auto _Undo = _Repository.BeginUndoCommand("Import CAM machine definition");
    _StoreMachineDefinitionResource(_Scene, _ResourceID, _Name, _SourcePath, _Description);

    auto _DefinitionID = iCAX::Data::GenerateNewUUID();
    const auto _DefinitionIDText = _GetOptionalString(_Payload, "machineDefinitionId");
    if (!_DefinitionIDText.empty())
    {
        _DefinitionID = _ParseRequiredUuid(_DefinitionIDText, "machineDefinitionId");
    }
    auto [_pDefinitionEntity, _pDefinition] = _CreateOrUpdateMachineDefinition(
        _Repository,
        _DefinitionID,
        _Name,
        _SourcePath,
        _ResourceID,
        _Description);
    _Undo->End();

    ObjectMap _Result;
    _Result["machineDefinitionId"] = _UuidToString(_pDefinitionEntity->GetID());
    _Result["definition"] = _MakeMachineDefinitionPayload(_pDefinitionEntity, _pDefinition);
    _Result["resourceId"] = _ResourceID;
    _Result["machineResourceId"] = _ResourceID;
    _Result["sourcePath"] = _SourcePath;
    _Result["name"] = _Name;
    _Result["format"] = _Description.SourceFormat;
    _Result["formatVersion"] = _Description.SourceFormatVersion;
    _Result["modelName"] = _Description.ModelName;
    _Result["staticModel"] = _Description.bStaticModel;
    _Result["linkCount"] = static_cast<unsigned long long>(CountMachinePartElements(_Description));
    _Result["jointCount"] = static_cast<unsigned long long>(CountMachineJoints(_Description));
    _Result["visualCount"] = static_cast<unsigned long long>(CountMachineVisuals(_Description));
    _Result["collisionCount"] = static_cast<unsigned long long>(CountMachineCollisions(_Description));
    _Result["includeCount"] = static_cast<unsigned long long>(_Description.Includes.size());
    return _MakeResponse(Variant(_Result));
}

iCAX::Command::CCommandResponse HandleListMachineDefinitions(
    IN const iCAX::Command::CCommandRequest&,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _Scene = _RequireSceneContext(pSceneContext_);
    (void)_RequireProductContext(pProductContext_);
    (void)_RequireProjectContext(pProjectContext_);

    ObjectMap _Result;
    _Result["definitions"] = _MakeMachineDefinitionArray(_Scene.Database());
    return _MakeResponse(Variant(_Result));
}

iCAX::Command::CCommandResponse HandleSetMachineDefinitionEnabled(
    IN const iCAX::Command::CCommandRequest& Request_,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _Scene = _RequireSceneContext(pSceneContext_);
    (void)_RequireProductContext(pProductContext_);
    (void)_RequireProjectContext(pProjectContext_);

    auto _Payload = _DecodeObjectPayload(Request_);
    const auto _DefinitionIDText = _GetOptionalString(_Payload, "machineDefinitionId");
    const auto _DefinitionID = _ParseRequiredUuid(_DefinitionIDText, "machineDefinitionId");
    const auto _bEnabled = GetRequiredBool(_Payload, "enabled");

    auto& _Repository = _Scene.Database();
    auto [_pDefinitionEntity, _pDefinition] = _FindMachineDefinition(_Repository, _DefinitionID);
    (void)_pDefinitionEntity;
    if (!_pDefinition)
    {
        throw std::runtime_error("Cam MachineDefinition.SetEnabled machine definition is not found: " + _DefinitionIDText);
    }

    auto _Undo = _Repository.BeginUndoCommand(_bEnabled ? "Enable CAM machine definition" : "Disable CAM machine definition");
    _SetBoolProperty(_pDefinition, iCAX::CAM::CMachineDefinitionComponent::PropertyName_Enabled, _bEnabled);
    _Undo->End();
    ObjectMap _Result;
    _Result["definition"] = _MakeMachineDefinitionPayload(_pDefinitionEntity, _pDefinition);
    _Result["definitions"] = _MakeMachineDefinitionArray(_Repository);
    return _MakeResponse(Variant(_Result));
}

iCAX::Command::CCommandResponse HandleDeleteMachineDefinition(
    IN const iCAX::Command::CCommandRequest& Request_,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _Scene = _RequireSceneContext(pSceneContext_);
    (void)_RequireProductContext(pProductContext_);
    (void)_RequireProjectContext(pProjectContext_);

    auto _Payload = _DecodeObjectPayload(Request_);
    const auto _DefinitionIDText = _GetOptionalString(_Payload, "machineDefinitionId");
    const auto _DefinitionID = _ParseRequiredUuid(_DefinitionIDText, "machineDefinitionId");

    auto& _Repository = _Scene.Database();
    auto [_pDefinitionEntity, _pDefinition] = _FindMachineDefinition(_Repository, _DefinitionID);
    if (!_pDefinitionEntity || !_pDefinition)
    {
        throw std::runtime_error("Cam MachineDefinition.Delete machine definition is not found: " + _DefinitionIDText);
    }

    for (const auto& [_pInstanceEntity, _pInstance] : _CollectEntitiesWithComponent<iCAX::CAM::CMachineInstanceComponent>(_Repository))
    {
        if (_pInstance && _pInstance->GetMachineDefinitionID() == _DefinitionIDText)
        {
            const auto _InstanceName = _pInstance->GetName().empty() ? _UuidToString(_pInstanceEntity->GetID()) : _pInstance->GetName();
            throw std::runtime_error("Cam MachineDefinition.Delete definition is used by machine instance: " + _InstanceName);
        }
    }

    const auto _ResourceID = _pDefinition->GetMachineResourceID();
    auto _Undo = _Repository.BeginUndoCommand("Delete CAM machine definition");
    _RemoveMachineDefinitionFromCatalog(_Repository, _DefinitionID);
    std::string _strError;
    if (!_Repository.DeleteEntity(_DefinitionID, _strError))
    {
        throw std::runtime_error(_strError.empty() ? "Cam MachineDefinition.Delete entity delete failed" : _strError);
    }
    if (!_ResourceID.empty())
    {
        (void)_Scene.Resources().Remove(_ResourceID);
    }
    _Undo->End();

    ObjectMap _Result;
    _Result["deletedMachineDefinitionId"] = _DefinitionIDText;
    _Result["definitions"] = _MakeMachineDefinitionArray(_Repository);
    return _MakeResponse(Variant(_Result));
}

} // namespace Commands
} // namespace CAM
} // namespace iCAX
