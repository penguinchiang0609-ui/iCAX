#include "pch.h"
#include "Facades.h"

#include "FacadeSupport.h"
#include "JobComponents.h"
#include "MachineInstanceComponents.h"
#include "SceneComponents.h"

#include "Facades/FacadeRegistrationCatalog.h"
#include "Facades/Facade.h"

namespace
{
    class CJobFacade final : public iCAX::Interaction::CFacade
    {
    public:
        CJobFacade()
            : CFacade("Job")
        {
            ExposeMethod("Get", &iCAX::CAM::Facades::HandleGetJob);
            ExposeMethod("SetMachine", &iCAX::CAM::Facades::HandleSetJobMachine);
        }
    };

    static_assert(iCAX::Interaction::IsStatelessFacadeType<CJobFacade>);
}

ICAX_REGISTER_FACADE(CJobFacade)

namespace iCAX
{
namespace CAM
{
namespace Facades
{
using namespace Internal;

namespace
{
    std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CJobComponent>> GetOrCreateDefaultJob(
        IN iCAX::Database::IRepository& Repository_)
    {
        auto _Jobs = _CollectEntitiesWithComponent<iCAX::CAM::CJobComponent>(Repository_);
        if (!_Jobs.empty())
        {
            return _Jobs.front();
        }

        auto [_pJobEntity, _pJob] = _CreateEntityWithComponent<iCAX::CAM::CJobComponent>(Repository_);
        _SetStringProperty(_pJob, iCAX::CAM::CJobComponent::PropertyName_Name, "默认作业");
        _SetStringProperty(_pJob, iCAX::CAM::CJobComponent::PropertyName_Status, "Draft");

        auto _pRoot = _GetComponent<iCAX::CAM::CRootComponent>(Repository_);
        if (_pRoot && !_pRoot->GetProgramRootBlockID().is_nil())
        {
            _SetUuidProperty(_pJob, iCAX::CAM::CJobComponent::PropertyName_ProgramRootBlockID, _pRoot->GetProgramRootBlockID());
        }

        return { _pJobEntity, _pJob };
    }

    std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CMachineInstanceComponent>> FindMachineByID(
        IN iCAX::Database::IRepository& Repository_,
        IN const iCAX::Data::uuid& MachineID_)
    {
        if (MachineID_.is_nil())
        {
            return {};
        }

        auto _pEntity = Repository_.GetEntity(MachineID_);
        auto _pMachine = _GetComponent<iCAX::CAM::CMachineInstanceComponent>(_pEntity);
        if (!_pEntity || !_pMachine)
        {
            return {};
        }
        return { _pEntity, _pMachine };
    }

    ObjectMap MakeJobPayload(
        IN iCAX::Database::IRepository& Repository_,
        IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_,
        IN const std::shared_ptr<iCAX::CAM::CJobComponent>& pJob_)
    {
        ObjectMap _Job;
        _Job["entityId"] = pEntity_ ? _UuidToString(pEntity_->GetID()) : std::string();
        _Job["name"] = pJob_ ? pJob_->GetName() : std::string();
        _Job["machineEntityId"] = pJob_ ? _UuidToString(pJob_->GetMachineEntityID()) : std::string();
        _Job["workpieceEntityId"] = pJob_ ? _UuidToString(pJob_->GetWorkpieceEntityID()) : std::string();
        _Job["programRootBlockId"] = pJob_ ? _UuidToString(pJob_->GetProgramRootBlockID()) : std::string();
        _Job["status"] = pJob_ ? pJob_->GetStatus() : std::string();

        if (pJob_)
        {
            auto [_pMachineEntity, _pMachine] = FindMachineByID(Repository_, pJob_->GetMachineEntityID());
            (void)_pMachineEntity;
            _Job["machineName"] = _pMachine ? _pMachine->GetName() : std::string();
            _Job["machineEnabled"] = _pMachine ? _pMachine->GetEnabled() : false;
            _Job["machineLoaded"] = _pMachine && _pMachine->GetEnabled() && !_pMachine->GetSourcePath().empty();
        }
        else
        {
            _Job["machineName"] = std::string();
            _Job["machineEnabled"] = false;
            _Job["machineLoaded"] = false;
        }
        return _Job;
    }

    VariantArray MakeJobArray(IN iCAX::Database::IRepository& Repository_)
    {
        VariantArray _Jobs;
        for (const auto& [_pEntity, _pJob] : _CollectEntitiesWithComponent<iCAX::CAM::CJobComponent>(Repository_))
        {
            _Jobs.emplace_back(MakeJobPayload(Repository_, _pEntity, _pJob));
        }
        return _Jobs;
    }

    iCAX::Interaction::CFacadeResult MakeJobResponse(IN iCAX::Database::IRepository& Repository_)
    {
        auto _Jobs = _CollectEntitiesWithComponent<iCAX::CAM::CJobComponent>(Repository_);
        std::shared_ptr<iCAX::Database::IEntity> _pJobEntity;
        std::shared_ptr<iCAX::CAM::CJobComponent> _pJob;
        if (!_Jobs.empty())
        {
            _pJobEntity = _Jobs.front().first;
            _pJob = _Jobs.front().second;
        }

        ObjectMap _Result;
        _Result["job"] = MakeJobPayload(Repository_, _pJobEntity, _pJob);
        _Result["jobs"] = MakeJobArray(Repository_);
        return _MakeResponse(Variant(_Result));
    }
}

iCAX::Interaction::CFacadeResult HandleGetJob(
    IN const iCAX::Interaction::CFacadeCall&,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext*,
    IN iCAX::Project::IProjectContext*,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _Scene = _RequireSceneContext(pSceneContext_);
    return MakeJobResponse(_Scene.Database());
}

iCAX::Interaction::CFacadeResult HandleSetJobMachine(
    IN const iCAX::Interaction::CFacadeCall& Request_,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext*,
    IN iCAX::Project::IProjectContext*,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _Scene = _RequireSceneContext(pSceneContext_);
    auto& _Repository = _Scene.Database();
    auto _Payload = _DecodeObjectPayload(Request_);
    auto _MachineIDText = _GetOptionalString(_Payload, "machineEntityId");
    if (_MachineIDText.empty())
    {
        _MachineIDText = _GetOptionalString(_Payload, "entityId");
    }

    const auto _MachineID = _ParseRequiredUuid(_MachineIDText, "machineEntityId");
    auto _pMachineEntity = _Repository.GetEntity(_MachineID);
    auto _pMachine = _GetComponent<iCAX::CAM::CMachineInstanceComponent>(_pMachineEntity);
    if (!_pMachineEntity || !_pMachine)
    {
        throw std::invalid_argument("Cam Job.SetMachine target machine does not exist: " + _MachineIDText);
    }
    if (!_pMachine->GetEnabled())
    {
        throw std::invalid_argument("Cam Job.SetMachine target machine is disabled: " + _MachineIDText);
    }

    auto _Undo = _Repository.BeginUndoCommand("Set CAM job machine");
    auto [_pJobEntity, _pJob] = GetOrCreateDefaultJob(_Repository);
    (void)_pJobEntity;
    _SetUuidProperty(_pJob, iCAX::CAM::CJobComponent::PropertyName_MachineEntityID, _MachineID);
    _SetStringProperty(_pJob, iCAX::CAM::CJobComponent::PropertyName_Status, "Draft");
    _Undo->End();

    return MakeJobResponse(_Repository);
}

} // namespace Facades
} // namespace CAM
} // namespace iCAX
