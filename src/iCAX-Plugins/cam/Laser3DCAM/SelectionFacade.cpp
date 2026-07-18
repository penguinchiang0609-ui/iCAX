#include "pch.h"
#include "Facades.h"
#include "FacadeSupport.h"
#include "MachineInstanceComponents.h"
#include "ToolpathFacadeImplement.h"

#include "Facades/FacadeRegistrationCatalog.h"
#include "Facades/Facade.h"

namespace
{
    class CSelectionFacade final : public iCAX::Interaction::CFacade
    {
    public:
        CSelectionFacade()
            : CFacade("Selection")
        {
            ExposeMethod("Get", &iCAX::CAM::Facades::HandleGetSelection);
            ExposeMethod("PickTopology", &iCAX::CAM::Facades::HandlePickTopology);
            ExposeMethod("PickMachineObject", &iCAX::CAM::Facades::HandlePickMachineObject);
        }
    };

    static_assert(iCAX::Interaction::IsStatelessFacadeType<CSelectionFacade>);
}

ICAX_REGISTER_FACADE(CSelectionFacade)

namespace iCAX
{
namespace CAM
{
namespace Facades
{
using namespace Internal;

namespace
{
    ObjectMap MakeSelectionPayload(
        IN iCAX::Database::IRepository& Repository_,
        IN const std::shared_ptr<iCAX::CAM::CSelectionComponent>& pSelection_)
    {
        ObjectMap _Selection;
        _Selection["kind"] = pSelection_ ? pSelection_->GetSelectedKind() : std::string();
        _Selection["id"] = pSelection_ ? pSelection_->GetSelectedID() : 0ull;
        _Selection["entityId"] = pSelection_ ? _UuidToString(pSelection_->GetSelectedEntityID()) : std::string();
        _Selection["label"] = pSelection_ ? pSelection_->GetSelectedLabel() : std::string();
        _Selection["hoverKind"] = pSelection_ ? pSelection_->GetHoverKind() : std::string();
        _Selection["hoverId"] = pSelection_ ? pSelection_->GetHoverID() : 0ull;
        _Selection["hoverEntityId"] = pSelection_ ? _UuidToString(pSelection_->GetHoverEntityID()) : std::string();

        if (pSelection_)
        {
            const auto _EntityID = pSelection_->GetSelectedEntityID();
            auto _pEntity = _EntityID.is_nil() ? nullptr : Repository_.GetEntity(_EntityID);
            if (auto _pElement = _GetComponent<iCAX::CAM::CMachineElementComponent>(_pEntity))
            {
                _Selection["machineId"] = _UuidToString(_pElement->GetMachineID());
            }
            else if (_GetComponent<iCAX::CAM::CMachineInstanceComponent>(_pEntity))
            {
                _Selection["machineId"] = _UuidToString(_EntityID);
            }
        }
        return _Selection;
    }

    iCAX::Interaction::CFacadeResult MakeSelectionResponse(IN iCAX::Database::IRepository& Repository_)
    {
        ObjectMap _Result;
        _Result["selection"] = MakeSelectionPayload(Repository_, _GetComponent<iCAX::CAM::CSelectionComponent>(Repository_));
        return _MakeResponse(Variant(_Result));
    }
}

iCAX::Interaction::CFacadeResult HandleGetSelection(
    IN const iCAX::Interaction::CFacadeCall&,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext*,
    IN iCAX::Project::IProjectContext*,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _Scene = _RequireSceneContext(pSceneContext_);
    return MakeSelectionResponse(_Scene.Database());
}

iCAX::Interaction::CFacadeResult HandlePickTopology(
    IN const iCAX::Interaction::CFacadeCall &Request_,
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
    _SetUuidProperty(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedEntityID, iCAX::Data::uuid());
    _SetStringProperty(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedLabel, _Label);
    return MakeSelectionResponse(_Repository);
}

iCAX::Interaction::CFacadeResult HandlePickMachineObject(
    IN const iCAX::Interaction::CFacadeCall &Request_,
    IN iCAX::Application::IApplicationContext &,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto &_Repository = _Scene.Database();
    auto _ID = _GetOptionalUInt64(_Payload, "id");
    auto _Kind = _GetOptionalString(_Payload, "kind", "machine");
    auto _Label = _GetOptionalString(_Payload, "label");
    auto _EntityIDText = _GetOptionalString(_Payload, "entityId");
    if (_EntityIDText.empty())
    {
        _EntityIDText = _GetOptionalString(_Payload, "objectId");
    }
    if (_EntityIDText.empty())
    {
        throw std::invalid_argument("Cam PickMachineObject requires objectId or entityId");
    }

    const auto _EntityID = _ParseRequiredUuid(_EntityIDText, "entityId");
    auto _pEntity = _Repository.GetEntity(_EntityID);
    if (!_pEntity)
    {
        throw std::invalid_argument("Cam PickMachineObject entity does not exist");
    }

    if (auto _pElement = _GetComponent<iCAX::CAM::CMachineElementComponent>(_pEntity))
    {
        _Kind = "machine." + (_pElement->GetElementKind().empty() ? std::string("element") : _pElement->GetElementKind());
        if (_Label.empty())
        {
            _Label = _pElement->GetName();
        }
    }
    else if (auto _pMachine = _GetComponent<iCAX::CAM::CMachineInstanceComponent>(_pEntity))
    {
        _Kind = "machine";
        if (_Label.empty())
        {
            _Label = _pMachine->GetName();
        }
    }

    if (_ID == 0ull)
    {
        if (auto _pElement = _GetComponent<iCAX::CAM::CMachineElementComponent>(_pEntity))
        {
            _ID = _pElement->GetSourceIndex();
            if (_Label.empty())
            {
                _Label = _pElement->GetName();
            }
        }
    }
    if (_Label.empty())
    {
        _Label = _Kind + " " + iCAX::Data::to_string(_EntityID);
    }
    auto _pSelection = _GetOrCreateComponent<iCAX::CAM::CSelectionComponent>(_Repository);
    _SetStringProperty(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedKind, _Kind);
    _SetUInt64Property(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedID, _ID);
    _SetUuidProperty(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedEntityID, _EntityID);
    _SetStringProperty(_pSelection, iCAX::CAM::CSelectionComponent::PropertyName_SelectedLabel, _Label);
    return MakeSelectionResponse(_Repository);
}
} // namespace Facades
} // namespace CAM
} // namespace iCAX
