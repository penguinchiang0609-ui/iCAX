#include "pch.h"
#include "Facades.h"
#include "FacadeSupport.h"
#include "MachineDefinitionFacadeImplement.h"
#include "MachineDescriptionLoader.h"
#include "MachineResourceKeys.h"
#include "MachineFacadeImplement.h"

#include "Facades/FacadeRegistrationCatalog.h"
#include "Facades/Facade.h"
#include "Transform/Transform.h"

#include <initializer_list>

namespace
{
    class CMachineFacade final : public iCAX::Interaction::CFacade
    {
    public:
        CMachineFacade()
            : CFacade("Machine")
        {
            ExposeMethod("Instantiate", &iCAX::CAM::Facades::HandleInstantiateMachine);
            ExposeMethod("List", &iCAX::CAM::Facades::HandleListMachines);
            ExposeMethod("GetElement", &iCAX::CAM::Facades::HandleGetMachineElement);
            ExposeMethod("SetElementTransform", &iCAX::CAM::Facades::HandleSetMachineElementTransform);
            ExposeMethod("SetElementAppearance", &iCAX::CAM::Facades::HandleSetMachineElementAppearance);
            ExposeMethod("SetJointPosition", &iCAX::CAM::Facades::HandleSetMachineJointPosition);
            ExposeMethod("SetJointLimits", &iCAX::CAM::Facades::HandleSetMachineJointLimits);
            ExposeMethod("SetParameters", &iCAX::CAM::Facades::HandleSetMachineParameters);
            ExposeMethod("SetTCP", &iCAX::CAM::Facades::HandleSetMachineTCP);
            ExposeMethod("SetToolTCP", &iCAX::CAM::Facades::HandleSetMachineToolTCP);
            ExposeMethod("SetEnabled", &iCAX::CAM::Facades::HandleSetMachineEnabled);
            ExposeMethod("SetName", &iCAX::CAM::Facades::HandleSetMachineName);
            ExposeMethod("Jog", &iCAX::CAM::Facades::HandleJogMachine);
            ExposeMethod("Home", &iCAX::CAM::Facades::HandleHomeMachine);
            ExposeMethod("Reset", &iCAX::CAM::Facades::HandleResetMachine);
            ExposeMethod("CheckLimits", &iCAX::CAM::Facades::HandleCheckMachineLimits);
            ExposeMethod("CheckReach", &iCAX::CAM::Facades::HandleCheckMachineReach);
        }
    };

    static_assert(iCAX::Interaction::IsStatelessFacadeType<CMachineFacade>);
}

ICAX_REGISTER_FACADE(CMachineFacade)

namespace iCAX
{
namespace CAM
{
namespace Facades
{
using namespace Internal;

namespace
{
    std::string MakeDefaultMachineInstanceName(
        IN iCAX::Database::IRepository& Repository_,
        IN const std::string& BaseName_,
        IN const std::string& MachineDefinitionID_)
    {
        const auto _BaseName = BaseName_.empty() ? std::string("Machine") : BaseName_;
        size_t _SameDefinitionCount = 0;
        for (const auto& [_pEntity, _pMachine] : _CollectEntitiesWithComponent<iCAX::CAM::CMachineInstanceComponent>(Repository_))
        {
            (void)_pEntity;
            if (_pMachine && _pMachine->GetMachineDefinitionID() == MachineDefinitionID_)
            {
                ++_SameDefinitionCount;
            }
        }
        return _SameDefinitionCount == 0 ? _BaseName : _BaseName + " " + std::to_string(_SameDefinitionCount + 1);
    }

    std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CMachineInstanceComponent>> RequireMachineFromPayload(
        IN iCAX::Database::IRepository& Repository_,
        IN const ObjectMap& Payload_)
    {
        auto _MachineIDText = _GetOptionalString(Payload_, "machineEntityId");
        if (_MachineIDText.empty())
        {
            _MachineIDText = _GetOptionalString(Payload_, "entityId");
        }
        const auto _MachineID = _ParseRequiredUuid(_MachineIDText, "machineEntityId");
        auto _pMachineEntity = Repository_.GetEntity(_MachineID);
        auto _pMachine = _GetComponent<iCAX::CAM::CMachineInstanceComponent>(_pMachineEntity);
        if (!_pMachineEntity || !_pMachine)
        {
            throw std::invalid_argument("Cam machine target does not exist: " + _MachineIDText);
        }
        return { _pMachineEntity, _pMachine };
    }

    bool GetRequiredBool(
        IN const ObjectMap& Payload_,
        IN const std::string& strName_)
    {
        const auto _Iter = Payload_.find(strName_);
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

    bool TryReadVector3(
        IN const ObjectMap& Payload_,
        IN const std::string& strName_,
        OUT std::vector<double>& Values_)
    {
        const auto _Iter = Payload_.find(strName_);
        if (_Iter == Payload_.end() || _Iter->second.Is<std::monostate>())
        {
            return false;
        }
        if (!_Iter->second.Is<VariantArray>())
        {
            throw std::invalid_argument("Cam payload field must be a numeric array: " + strName_);
        }

        Values_ = _VariantArrayToDoubles(_NormalizeDoubleArray(_Iter->second.To<VariantArray>(), strName_, 3), strName_);
        for (const auto _Value : Values_)
        {
            if (!std::isfinite(_Value))
            {
                throw std::invalid_argument("Cam payload field must be finite: " + strName_);
            }
        }
        return true;
    }

    bool HasPayloadValue(
        IN const ObjectMap& Payload_,
        IN const std::string& strName_)
    {
        const auto _Iter = Payload_.find(strName_);
        return _Iter != Payload_.end() && !_Iter->second.Is<std::monostate>();
    }

    bool TryGetOptionalBool(
        IN const ObjectMap& Payload_,
        IN const std::string& strName_,
        OUT bool& bValue_)
    {
        if (!HasPayloadValue(Payload_, strName_))
        {
            return false;
        }
        const auto& _Value = Payload_.at(strName_);
        if (!_Value.Is<bool>())
        {
            throw std::invalid_argument("Cam payload field must be a bool: " + strName_);
        }
        bValue_ = _Value.To<bool>();
        return true;
    }

    uint32_t ParseColorHexRGBA(IN std::string strColor_)
    {
        if (!strColor_.empty() && strColor_[0] == '#')
        {
            strColor_.erase(strColor_.begin());
        }
        if (strColor_.size() != 6 && strColor_.size() != 8)
        {
            throw std::invalid_argument("Cam color must be #RRGGBB or #RRGGBBAA");
        }
        if (!std::all_of(strColor_.begin(), strColor_.end(), [](IN unsigned char ch_) { return std::isxdigit(ch_) != 0; }))
        {
            throw std::invalid_argument("Cam color contains non-hex characters");
        }
        if (strColor_.size() == 6)
        {
            strColor_ += "FF";
        }
        return static_cast<uint32_t>(std::stoul(strColor_, nullptr, 16));
    }

    bool TryGetOptionalColorRGBA(
        IN const ObjectMap& Payload_,
        OUT uint32_t& nColorRGBA_)
    {
        if (HasPayloadValue(Payload_, "colorHex"))
        {
            const auto& _Value = Payload_.at("colorHex");
            if (!_Value.Is<std::string>())
            {
                throw std::invalid_argument("Cam Machine.SetElementAppearance colorHex must be a string");
            }
            nColorRGBA_ = ParseColorHexRGBA(_Value.To<std::string>());
            return true;
        }
        if (HasPayloadValue(Payload_, "color"))
        {
            const auto& _Value = Payload_.at("color");
            if (!_Value.Is<std::string>())
            {
                throw std::invalid_argument("Cam Machine.SetElementAppearance color must be a string");
            }
            nColorRGBA_ = ParseColorHexRGBA(_Value.To<std::string>());
            return true;
        }
        if (HasPayloadValue(Payload_, "colorRGBA"))
        {
            const auto _Color = _GetOptionalUInt64(Payload_, "colorRGBA", 0ull);
            if (_Color > 0xFFFFFFFFull)
            {
                throw std::invalid_argument("Cam Machine.SetElementAppearance colorRGBA must fit uint32");
            }
            nColorRGBA_ = static_cast<uint32_t>(_Color);
            return true;
        }
        return false;
    }

    std::string TrimCopy(IN std::string Value_)
    {
        auto _IsSpace = [](IN unsigned char ch_) { return std::isspace(ch_) != 0; };
        Value_.erase(Value_.begin(), std::find_if(Value_.begin(), Value_.end(), [_IsSpace](IN unsigned char ch_) { return !_IsSpace(ch_); }));
        Value_.erase(std::find_if(Value_.rbegin(), Value_.rend(), [_IsSpace](IN unsigned char ch_) { return !_IsSpace(ch_); }).base(), Value_.end());
        return Value_;
    }

    std::string GetObjectString(
        IN const ObjectMap& Object_,
        IN const std::string& strName_,
        IN const std::string& strDefault_ = std::string())
    {
        const auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || _Iter->second.Is<std::monostate>())
        {
            return strDefault_;
        }
        if (!_Iter->second.Is<std::string>())
        {
            throw std::invalid_argument("Cam object field must be a string: " + strName_);
        }
        return _Iter->second.To<std::string>();
    }

    bool GetObjectBool(
        IN const ObjectMap& Object_,
        IN const std::string& strName_,
        IN bool bDefault_)
    {
        const auto _Iter = Object_.find(strName_);
        if (_Iter == Object_.end() || _Iter->second.Is<std::monostate>())
        {
            return bDefault_;
        }
        if (!_Iter->second.Is<bool>())
        {
            throw std::invalid_argument("Cam object field must be a bool: " + strName_);
        }
        return _Iter->second.To<bool>();
    }

    double GetOptionalFiniteDoubleByNames(
        IN const ObjectMap& Payload_,
        IN const std::initializer_list<const char*> Names_,
        IN double dDefault_,
        OUT bool& bFound_)
    {
        for (const auto* _pszName : Names_)
        {
            const std::string _Name(_pszName ? _pszName : "");
            if (_Name.empty() || !HasPayloadValue(Payload_, _Name))
            {
                continue;
            }

            const auto _Value = _ToDouble(Payload_.at(_Name), _Name);
            if (!std::isfinite(_Value))
            {
                throw std::invalid_argument("Cam payload field must be finite: " + _Name);
            }
            bFound_ = true;
            return _Value;
        }

        bFound_ = false;
        return dDefault_;
    }

    double GetDescriptionPluginDouble(
        IN const iCAX::CAM::CMachineDescriptionResource& Description_,
        IN const std::string& strName_,
        IN double dDefault_)
    {
        for (const auto& _PluginVariant : Description_.Plugins)
        {
            if (!_PluginVariant.Is<ObjectMap>())
            {
                continue;
            }

            const auto _Plugin = _PluginVariant.To<ObjectMap>();
            const auto _Iter = _Plugin.find(strName_);
            if (_Iter == _Plugin.end() || _Iter->second.Is<std::monostate>())
            {
                continue;
            }
            return _ToDouble(_Iter->second, strName_);
        }
        return dDefault_;
    }

    iCAX::Interaction::CInvocationResult MakeMachineListResponse(IN iCAX::Project::ISceneContext& Scene_)
    {
        ObjectMap _Result;
        _Result["machines"] = _MakeMachineArray(Scene_.Resources(), Scene_.Database());
        return _MakeResponse(Variant(_Result));
    }

    iCAX::Interaction::CInvocationResult MakeMachineResponse(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const std::shared_ptr<iCAX::Database::IEntity>& pMachineEntity_,
        IN const std::shared_ptr<iCAX::CAM::CMachineInstanceComponent>& pMachine_)
    {
        ObjectMap _Result;
        _Result["machine"] = _MakeMachinePayload(Scene_.Resources(), Scene_.Database(), pMachineEntity_, pMachine_);
        _Result["machines"] = _MakeMachineArray(Scene_.Resources(), Scene_.Database());
        return _MakeResponse(Variant(_Result));
    }
}

iCAX::Interaction::CInvocationResult HandleListMachines(
    IN const iCAX::Interaction::CInvocation&,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext*,
    IN iCAX::Project::IProjectContext*,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _Scene = _RequireSceneContext(pSceneContext_);
    return MakeMachineListResponse(_Scene);
}

iCAX::Interaction::CInvocationResult HandleGetMachineElement(
    IN const iCAX::Interaction::CInvocation& Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext*,
    IN iCAX::Project::IProjectContext*,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto _EntityIDText = _GetOptionalString(_Payload, "entityId");
    if (_EntityIDText.empty())
    {
        _EntityIDText = _GetOptionalString(_Payload, "objectId");
    }
    if (_EntityIDText.empty())
    {
        throw std::invalid_argument("Cam Machine.GetElement requires entityId");
    }

    ObjectMap _Result;
    _Result["machineElement"] = _MakeMachineElementDetailPayload(
        _Scene.Database(),
        _ParseRequiredUuid(_EntityIDText, "entityId"));
    return _MakeResponse(Variant(_Result));
}

iCAX::Interaction::CInvocationResult HandleSetMachineElementTransform(
    IN const iCAX::Interaction::CInvocation& Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext*,
    IN iCAX::Project::IProjectContext*,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto _EntityIDText = _GetOptionalString(_Payload, "entityId");
    if (_EntityIDText.empty())
    {
        _EntityIDText = _GetOptionalString(_Payload, "objectId");
    }
    if (_EntityIDText.empty())
    {
        throw std::invalid_argument("Cam Machine.SetElementTransform requires entityId");
    }

    auto& _Repository = _Scene.Database();
    const auto _EntityID = _ParseRequiredUuid(_EntityIDText, "entityId");
    auto _pEntity = _Repository.GetEntity(_EntityID);
    if (!_pEntity)
    {
        throw std::invalid_argument("Cam Machine.SetElementTransform entity does not exist: " + _EntityIDText);
    }
    auto _pTransform = _GetComponent<iCAX::Transform::CTransformComponent>(_pEntity);
    if (!_pTransform)
    {
        throw std::invalid_argument("Cam Machine.SetElementTransform entity has no Transform component: " + _EntityIDText);
    }

    std::vector<double> _Values;
    bool _Changed = false;
    iCAX::Data::PropertySet _Properties;
    if (TryReadVector3(_Payload, "position", _Values) || TryReadVector3(_Payload, "localPosition", _Values))
    {
        _Properties[iCAX::Transform::CTransformComponent::PropertyName_PositionX] = _Values[0];
        _Properties[iCAX::Transform::CTransformComponent::PropertyName_PositionY] = _Values[1];
        _Properties[iCAX::Transform::CTransformComponent::PropertyName_PositionZ] = _Values[2];
        _Changed = true;
    }
    if (TryReadVector3(_Payload, "rotationRadians", _Values) || TryReadVector3(_Payload, "localRotationRadians", _Values))
    {
        _Properties[iCAX::Transform::CTransformComponent::PropertyName_YawRadians] = _Values[0];
        _Properties[iCAX::Transform::CTransformComponent::PropertyName_PitchRadians] = _Values[1];
        _Properties[iCAX::Transform::CTransformComponent::PropertyName_RollRadians] = _Values[2];
        _Changed = true;
    }
    if (TryReadVector3(_Payload, "scale", _Values) || TryReadVector3(_Payload, "localScale", _Values))
    {
        if (_Values[0] == 0.0 || _Values[1] == 0.0 || _Values[2] == 0.0)
        {
            throw std::invalid_argument("Cam Machine.SetElementTransform scale cannot contain zero");
        }
        _Properties[iCAX::Transform::CTransformComponent::PropertyName_ScaleX] = _Values[0];
        _Properties[iCAX::Transform::CTransformComponent::PropertyName_ScaleY] = _Values[1];
        _Properties[iCAX::Transform::CTransformComponent::PropertyName_ScaleZ] = _Values[2];
        _Changed = true;
    }
    if (!_Changed)
    {
        throw std::invalid_argument("Cam Machine.SetElementTransform requires position, rotationRadians or scale");
    }
    std::string _strError;
    if (!_pTransform->SetProperties(_Properties, _strError))
    {
        throw std::invalid_argument(_strError.empty() ? "Cam Machine.SetElementTransform is rejected by modify filter" : _strError);
    }

    ObjectMap _Result;
    _Result["machineElement"] = _MakeMachineElementDetailPayload(_Repository, _EntityID);
    return _MakeResponse(Variant(_Result));
}

iCAX::Interaction::CInvocationResult HandleSetMachineElementAppearance(
    IN const iCAX::Interaction::CInvocation& Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext*,
    IN iCAX::Project::IProjectContext*,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto _EntityIDText = _GetOptionalString(_Payload, "entityId");
    if (_EntityIDText.empty())
    {
        _EntityIDText = _GetOptionalString(_Payload, "objectId");
    }
    if (_EntityIDText.empty())
    {
        throw std::invalid_argument("Cam Machine.SetElementAppearance requires entityId");
    }

    auto& _Repository = _Scene.Database();
    const auto _EntityID = _ParseRequiredUuid(_EntityIDText, "entityId");
    auto _pEntity = _Repository.GetEntity(_EntityID);
    auto _pElement = _GetComponent<iCAX::CAM::CMachineElementComponent>(_pEntity);
    if (!_pEntity || !_pElement)
    {
        throw std::invalid_argument("Cam Machine.SetElementAppearance entity is not a machine element: " + _EntityIDText);
    }

    uint32_t _ColorRGBA = 0;
    const bool _HasColor = TryGetOptionalColorRGBA(_Payload, _ColorRGBA);
    bool _bShowCollision = true;
    const bool _HasShowCollision = TryGetOptionalBool(_Payload, "showCollision", _bShowCollision);
    if (!_HasColor && !_HasShowCollision)
    {
        throw std::invalid_argument("Cam Machine.SetElementAppearance requires colorHex, colorRGBA or showCollision");
    }

    auto _pAppearance = _GetOrAddEntityComponent<iCAX::CAM::CMachineAppearanceComponent>(_pEntity);
    iCAX::Data::PropertySet _Properties;
    if (_HasColor)
    {
        _Properties[iCAX::CAM::CMachineAppearanceComponent::PropertyName_UseColorOverride] = true;
        _Properties[iCAX::CAM::CMachineAppearanceComponent::PropertyName_ColorRGBA] = static_cast<unsigned long long>(_ColorRGBA);
    }
    if (_HasShowCollision)
    {
        _Properties[iCAX::CAM::CMachineAppearanceComponent::PropertyName_ShowCollision] = _bShowCollision;
    }

    auto _Undo = _Repository.BeginUndoCommand("Set CAM machine element appearance");
    std::string _strError;
    if (!_pAppearance->SetProperties(_Properties, _strError))
    {
        throw std::invalid_argument(_strError.empty() ? "Cam Machine.SetElementAppearance is rejected by modify filter" : _strError);
    }
    _RebuildMachineElementRenderMesh(_Scene, _pEntity, _pElement);
    _Undo->End();

    ObjectMap _Result;
    _Result["machineElement"] = _MakeMachineElementDetailPayload(_Repository, _EntityID);
    auto _pMachineEntity = _Repository.GetEntity(_pElement->GetMachineID());
    auto _pMachine = _GetComponent<iCAX::CAM::CMachineInstanceComponent>(_pMachineEntity);
    if (_pMachineEntity && _pMachine)
    {
        _Result["machine"] = _MakeMachinePayload(_Scene.Resources(), _Repository, _pMachineEntity, _pMachine);
        _Result["machines"] = _MakeMachineArray(_Scene.Resources(), _Repository);
    }
    return _MakeResponse(Variant(_Result));
}

iCAX::Interaction::CInvocationResult HandleSetMachineJointPosition(
    IN const iCAX::Interaction::CInvocation& Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext*,
    IN iCAX::Project::IProjectContext*,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto _EntityIDText = _GetOptionalString(_Payload, "entityId");
    if (_EntityIDText.empty())
    {
        _EntityIDText = _GetOptionalString(_Payload, "jointEntityId");
    }
    if (_EntityIDText.empty())
    {
        throw std::invalid_argument("Cam Machine.SetJointPosition requires entityId");
    }

    auto& _Repository = _Scene.Database();
    const auto _EntityID = _ParseRequiredUuid(_EntityIDText, "entityId");
    auto _pEntity = _Repository.GetEntity(_EntityID);
    auto _pElement = _GetComponent<iCAX::CAM::CMachineElementComponent>(_pEntity);
    auto _pJoint = _GetComponent<iCAX::CAM::CMachineJointComponent>(_pEntity);
    if (!_pEntity || !_pElement || !_pJoint)
    {
        throw std::invalid_argument("Cam Machine.SetJointPosition entity is not a machine joint: " + _EntityIDText);
    }
    if (_pJoint->GetJointType() == "fixed")
    {
        throw std::invalid_argument("Cam Machine.SetJointPosition cannot move a fixed joint: " + _pJoint->GetJointName());
    }

    bool _HasPosition = false;
    const auto _Position = GetOptionalFiniteDoubleByNames(_Payload, { "position" }, _pJoint->GetPosition(), _HasPosition);
    if (!_HasPosition)
    {
        throw std::invalid_argument("Cam Machine.SetJointPosition requires position");
    }

    iCAX::Data::PropertySet _Properties;
    _Properties[iCAX::CAM::CMachineJointComponent::PropertyName_Position] = _Position;
    std::string _strError;
    if (!_pJoint->SetProperties(_Properties, _strError))
    {
        throw std::invalid_argument(_strError.empty() ? "Cam Machine.SetJointPosition is rejected by modify filter" : _strError);
    }

    auto _pMachineEntity = _Repository.GetEntity(_pElement->GetMachineID());
    auto _pMachine = _GetComponent<iCAX::CAM::CMachineInstanceComponent>(_pMachineEntity);
    if (_pMachineEntity)
    {
        auto _pStatus = _GetOrAddMachineStatus(_pMachineEntity);
        _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_Status, "Positioned");
        _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_LastCheckResult, _pJoint->GetJointName() + " 轴位置已更新");
    }

    ObjectMap _Result;
    _Result["machineElement"] = _MakeMachineElementDetailPayload(_Repository, _EntityID);
    if (_pMachineEntity && _pMachine)
    {
        _Result["machine"] = _MakeMachinePayload(_Scene.Resources(), _Repository, _pMachineEntity, _pMachine);
        _Result["machines"] = _MakeMachineArray(_Scene.Resources(), _Repository);
    }
    return _MakeResponse(Variant(_Result));
}

iCAX::Interaction::CInvocationResult HandleSetMachineJointLimits(
    IN const iCAX::Interaction::CInvocation& Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext*,
    IN iCAX::Project::IProjectContext*,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto _EntityIDText = _GetOptionalString(_Payload, "entityId");
    if (_EntityIDText.empty())
    {
        _EntityIDText = _GetOptionalString(_Payload, "objectId");
    }
    if (_EntityIDText.empty())
    {
        _EntityIDText = _GetOptionalString(_Payload, "jointEntityId");
    }
    if (_EntityIDText.empty())
    {
        throw std::invalid_argument("Cam Machine.SetJointLimits requires entityId");
    }

    auto& _Repository = _Scene.Database();
    const auto _EntityID = _ParseRequiredUuid(_EntityIDText, "entityId");
    auto _pEntity = _Repository.GetEntity(_EntityID);
    auto _pElement = _GetComponent<iCAX::CAM::CMachineElementComponent>(_pEntity);
    auto _pJoint = _GetComponent<iCAX::CAM::CMachineJointComponent>(_pEntity);
    if (!_pEntity || !_pElement || !_pJoint)
    {
        throw std::invalid_argument("Cam Machine.SetJointLimits entity is not a machine joint: " + _EntityIDText);
    }
    if (_pJoint->GetJointType() == "fixed")
    {
        throw std::invalid_argument("Cam Machine.SetJointLimits cannot edit a fixed joint: " + _pJoint->GetJointName());
    }

    bool _HasLower = false;
    bool _HasUpper = false;
    const auto _Lower = GetOptionalFiniteDoubleByNames(_Payload, { "lower", "lowerLimit" }, _pJoint->GetLowerLimit(), _HasLower);
    const auto _Upper = GetOptionalFiniteDoubleByNames(_Payload, { "upper", "upperLimit" }, _pJoint->GetUpperLimit(), _HasUpper);
    if (!_HasLower && !_HasUpper)
    {
        throw std::invalid_argument("Cam Machine.SetJointLimits requires lower or upper");
    }

    iCAX::Data::PropertySet _Properties;
    if (_HasLower)
    {
        _Properties[iCAX::CAM::CMachineJointComponent::PropertyName_LowerLimit] = _Lower;
    }
    if (_HasUpper)
    {
        _Properties[iCAX::CAM::CMachineJointComponent::PropertyName_UpperLimit] = _Upper;
    }

    auto _Undo = _Repository.BeginUndoCommand("Set CAM machine joint limits");
    std::string _strError;
    if (!_pJoint->SetProperties(_Properties, _strError))
    {
        throw std::invalid_argument(_strError.empty() ? "Cam Machine.SetJointLimits is rejected by modify filter" : _strError);
    }

    auto _pMachineEntity = _Repository.GetEntity(_pElement->GetMachineID());
    auto _pMachine = _GetComponent<iCAX::CAM::CMachineInstanceComponent>(_pMachineEntity);
    if (_pMachineEntity)
    {
        auto _pStatus = _GetOrAddMachineStatus(_pMachineEntity);
        _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_Status, "Ready");
        _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_LastCheckResult, _pJoint->GetJointName() + " 软限位已更新");
    }
    _Undo->End();

    ObjectMap _Result;
    _Result["machineElement"] = _MakeMachineElementDetailPayload(_Repository, _EntityID);
    if (_pMachineEntity && _pMachine)
    {
        _Result["machine"] = _MakeMachinePayload(_Scene.Resources(), _Repository, _pMachineEntity, _pMachine);
        _Result["machines"] = _MakeMachineArray(_Scene.Resources(), _Repository);
    }
    return _MakeResponse(Variant(_Result));
}

iCAX::Interaction::CInvocationResult HandleInstantiateMachine(
    IN const iCAX::Interaction::CInvocation &Request_,
    IN const iCAX::Application::IApplicationContext&ApplicationContext_,
    IN iCAX::Product::IProductContext *pProductContext_,
    IN iCAX::Project::IProjectContext *pProjectContext_,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto& _ProductContext = _RequireProductContext(pProductContext_);
    (void)_RequireProjectContext(pProjectContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto& _Repository = _Scene.Database();
    const auto _MachineDefinitionIDText = _GetOptionalString(_Payload, "machineDefinitionId");
    if (_MachineDefinitionIDText.empty())
    {
        throw std::invalid_argument("Cam Machine.Instantiate requires machineDefinitionId");
    }
    (void)_ParseRequiredUuid(_MachineDefinitionIDText, "machineDefinitionId");

    _EnsureBuiltInProductMachineDefinitions(ApplicationContext_, _ProductContext);
    const auto _MachineDefinition = _FindProductMachineDefinition(_ProductContext, _MachineDefinitionIDText);
    if (_MachineDefinition.empty())
    {
        throw std::runtime_error("Cam Machine.Instantiate machine definition is not found: " + _MachineDefinitionIDText);
    }
    if (!GetObjectBool(_MachineDefinition, "enabled", true))
    {
        throw std::runtime_error("Cam Machine.Instantiate machine definition is disabled: " + _MachineDefinitionIDText);
    }

    auto _SourcePath = GetObjectString(_MachineDefinition, "managedPath");
    if (_SourcePath.empty())
    {
        _SourcePath = GetObjectString(_MachineDefinition, "sourcePath");
    }
    if (_SourcePath.empty())
    {
        throw std::runtime_error("Cam Machine.Instantiate machine definition has no managed path: " + _MachineDefinitionIDText);
    }

    iCAX::CAM::CMachineDescriptionResource _Description;
    std::string _LoadError;
    if (!iCAX::CAM::LoadMachineDescription(_SourcePath, _Description, _LoadError))
    {
        throw std::invalid_argument("Cam Machine.Instantiate description load failed: " + _LoadError);
    }

    const auto _ModelName = _Description.ModelName.empty() ? _GetDisplayNameFromPath(_SourcePath) : _Description.ModelName;
    const auto _DefinitionName = GetObjectString(_MachineDefinition, "name");
    const auto _DefaultName = MakeDefaultMachineInstanceName(_Repository, _DefinitionName.empty() ? _ModelName : _DefinitionName, _MachineDefinitionIDText);
    const auto _Name = _GetOptionalString(_Payload, "name", _DefaultName);
    auto _Undo = _Repository.BeginUndoCommand("Instantiate CAM machine");
    auto [_pMachineEntity, _pMachine] = _CreateMachineEntity(_Repository);
    const auto _ResourceScopeID = iCAX::CAM::MakeMachineInstanceResourceScopeID(_pMachineEntity->GetID());
    auto _pKinematics = _GetOrAddMachineKinematics(_pMachineEntity);
    auto _pStatus = _GetOrAddMachineStatus(_pMachineEntity);
    _SetMachineDefaultKinematics(_pKinematics);
    const auto _MaxVelocity = _GetOptionalDouble(_Payload, "maxVelocity", _pKinematics->GetMaxVelocity() > 0.0 ? _pKinematics->GetMaxVelocity() : 1000.0);
    const auto _MaxAcceleration = _GetOptionalDouble(_Payload, "maxAcceleration", _pKinematics->GetMaxAcceleration() > 0.0 ? _pKinematics->GetMaxAcceleration() : 2000.0);
    const auto _DescriptionLinearJogStep = GetDescriptionPluginDouble(
        _Description,
        "linear_jog_step_mm",
        GetDescriptionPluginDouble(_Description, "linearJogStep", _pKinematics->GetLinearJogStep()));
    const auto _DescriptionAngularJogStep = GetDescriptionPluginDouble(
        _Description,
        "angular_jog_step_deg",
        GetDescriptionPluginDouble(_Description, "angularJogStep", _pKinematics->GetAngularJogStep()));
    const auto _LinearJogStep = _GetOptionalDouble(_Payload, "linearJogStep", _DescriptionLinearJogStep);
    const auto _AngularJogStep = _GetOptionalDouble(_Payload, "angularJogStep", _DescriptionAngularJogStep);
    if (_MaxVelocity <= 0.0 || _MaxAcceleration <= 0.0 || _LinearJogStep <= 0.0 || _AngularJogStep <= 0.0)
    {
        throw std::invalid_argument("Cam Machine.Instantiate motion parameters must be greater than zero");
    }

    _SetStringProperty(_pMachine, iCAX::CAM::CMachineInstanceComponent::PropertyName_Name, _Name);
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineInstanceComponent::PropertyName_SourcePath, _SourcePath);
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineInstanceComponent::PropertyName_MachineDefinitionID, _MachineDefinitionIDText);
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineInstanceComponent::PropertyName_ResourceScopeID, _ResourceScopeID);
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineInstanceComponent::PropertyName_DescriptionFormat, _Description.SourceFormat);
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineInstanceComponent::PropertyName_DescriptionVersion, _Description.SourceFormatVersion);
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineInstanceComponent::PropertyName_ModelName, _Description.ModelName);
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineInstanceComponent::PropertyName_WorkstationName, "默认工位");
    _SetBoolProperty(_pMachine, iCAX::CAM::CMachineInstanceComponent::PropertyName_StaticModel, _Description.bStaticModel);
    _SetBoolProperty(_pMachine, iCAX::CAM::CMachineInstanceComponent::PropertyName_Enabled, true);
    _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_Status, "Ready");
    _SetDoubleProperty(_pKinematics, iCAX::CAM::CMachineKinematicsComponent::PropertyName_MaxVelocity, _MaxVelocity);
    _SetDoubleProperty(_pKinematics, iCAX::CAM::CMachineKinematicsComponent::PropertyName_MaxAcceleration, _MaxAcceleration);
    _SetDoubleProperty(_pKinematics, iCAX::CAM::CMachineKinematicsComponent::PropertyName_LinearJogStep, _LinearJogStep);
    _SetDoubleProperty(_pKinematics, iCAX::CAM::CMachineKinematicsComponent::PropertyName_AngularJogStep, _AngularJogStep);
    _CreateMachineStructureEntitiesFromDescription(_Scene, _pMachineEntity->GetID(), _ResourceScopeID, _pMachine->GetName(), _Description);
    _SetStringProperty(
        _pStatus,
        iCAX::CAM::CMachineStatusComponent::PropertyName_LastCheckResult,
        "机器定义已实例化："
            + std::to_string(iCAX::CAM::CountMachinePartElements(_Description)) + " parts / "
            + std::to_string(iCAX::CAM::CountMachineJoints(_Description)) + " joints / "
            + std::to_string(iCAX::CAM::CountMachineVisuals(_Description)) + " visuals / "
            + std::to_string(iCAX::CAM::CountMachineCollisions(_Description)) + " collisions");
    _Undo->End();
    return MakeMachineResponse(_Scene, _pMachineEntity, _pMachine);
}

iCAX::Interaction::CInvocationResult HandleSetMachineEnabled(
    IN const iCAX::Interaction::CInvocation& Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext*,
    IN iCAX::Project::IProjectContext*,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto& _Repository = _Scene.Database();
    auto [_pMachineEntity, _pMachine] = RequireMachineFromPayload(_Repository, _Payload);
    auto _pStatus = _GetOrAddMachineStatus(_pMachineEntity);
    const auto _bEnabled = GetRequiredBool(_Payload, "enabled");

    auto _Undo = _Repository.BeginUndoCommand(_bEnabled ? "Enable CAM machine" : "Disable CAM machine");
    _SetBoolProperty(_pMachine, iCAX::CAM::CMachineInstanceComponent::PropertyName_Enabled, _bEnabled);
    _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_Status, _bEnabled ? "Ready" : "Disabled");
    _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_LastCheckResult, _bEnabled ? "机床实例已启用" : "机床实例已禁用");
    _Undo->End();
    return MakeMachineResponse(_Scene, _pMachineEntity, _pMachine);
}

iCAX::Interaction::CInvocationResult HandleSetMachineName(
    IN const iCAX::Interaction::CInvocation& Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext*,
    IN iCAX::Project::IProjectContext*,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto& _Repository = _Scene.Database();
    auto [_pMachineEntity, _pMachine] = RequireMachineFromPayload(_Repository, _Payload);
    auto _pStatus = _GetOrAddMachineStatus(_pMachineEntity);

    const auto _Name = TrimCopy(_GetOptionalString(_Payload, "name"));
    if (_Name.empty())
    {
        throw std::invalid_argument("Cam Machine.SetName requires non-empty name");
    }

    auto _Undo = _Repository.BeginUndoCommand("Rename CAM machine");
    _SetStringProperty(_pMachine, iCAX::CAM::CMachineInstanceComponent::PropertyName_Name, _Name);
    _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_LastCheckResult, "机床实例已重命名");
    _Undo->End();
    return MakeMachineResponse(_Scene, _pMachineEntity, _pMachine);
}

iCAX::Interaction::CInvocationResult HandleSetMachineParameters(
    IN const iCAX::Interaction::CInvocation &Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto &_Repository = _Scene.Database();
    auto [_pMachineEntity, _pMachine] = RequireMachineFromPayload(_Repository, _Payload);
    auto _pKinematics = _GetOrAddMachineKinematics(_pMachineEntity);
    auto _pStatus = _GetOrAddMachineStatus(_pMachineEntity);
    const auto _MaxVelocity = _GetOptionalDouble(_Payload, "maxVelocity", _pKinematics->GetMaxVelocity());
    const auto _MaxAcceleration = _GetOptionalDouble(_Payload, "maxAcceleration", _pKinematics->GetMaxAcceleration());
    const auto _LinearJogStep = _GetOptionalDouble(_Payload, "linearJogStep", _pKinematics->GetLinearJogStep());
    const auto _AngularJogStep = _GetOptionalDouble(_Payload, "angularJogStep", _pKinematics->GetAngularJogStep());
    if (_MaxVelocity <= 0.0 || _MaxAcceleration <= 0.0 || _LinearJogStep <= 0.0 || _AngularJogStep <= 0.0)
    {
        throw std::invalid_argument("Cam machine motion parameters must be greater than zero");
    }
    auto _Joints = _CollectMachineJointComponents(_Repository, _pMachineEntity->GetID());
    const auto _LowerLimits = _GetOptionalVariantArray(_Payload, "jointLowerLimits");
    const auto _UpperLimits = _GetOptionalVariantArray(_Payload, "jointUpperLimits");
    auto _Undo = _Repository.BeginUndoCommand("Set CAM machine parameters");
    _SetDoubleProperty(_pKinematics, iCAX::CAM::CMachineKinematicsComponent::PropertyName_MaxVelocity, _MaxVelocity);
    _SetDoubleProperty(_pKinematics, iCAX::CAM::CMachineKinematicsComponent::PropertyName_MaxAcceleration, _MaxAcceleration);
    _SetDoubleProperty(_pKinematics, iCAX::CAM::CMachineKinematicsComponent::PropertyName_LinearJogStep, _LinearJogStep);
    _SetDoubleProperty(_pKinematics, iCAX::CAM::CMachineKinematicsComponent::PropertyName_AngularJogStep, _AngularJogStep);
    if (!_LowerLimits.empty() && !_Joints.empty())
    {
        const auto _Values = _VariantArrayToDoubles(_NormalizeDoubleArray(_LowerLimits, "jointLowerLimits", _Joints.size()), "jointLowerLimits");
        for (size_t _Index = 0; _Index < _Joints.size(); ++_Index)
        {
            _SetDoubleProperty(_Joints[_Index].second, iCAX::CAM::CMachineJointComponent::PropertyName_LowerLimit, _Values[_Index]);
        }
    }
    if (!_UpperLimits.empty() && !_Joints.empty())
    {
        const auto _Values = _VariantArrayToDoubles(_NormalizeDoubleArray(_UpperLimits, "jointUpperLimits", _Joints.size()), "jointUpperLimits");
        for (size_t _Index = 0; _Index < _Joints.size(); ++_Index)
        {
            _SetDoubleProperty(_Joints[_Index].second, iCAX::CAM::CMachineJointComponent::PropertyName_UpperLimit, _Values[_Index]);
        }
    }
    _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_Status, "Ready");
    _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_LastCheckResult, "机器参数已更新");
    _Undo->End();
    return MakeMachineResponse(_Scene, _pMachineEntity, _pMachine);
}

iCAX::Interaction::CInvocationResult HandleSetMachineTCP(
    IN const iCAX::Interaction::CInvocation &Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto &_Repository = _Scene.Database();
    auto [_pMachineEntity, _pMachine] = RequireMachineFromPayload(_Repository, _Payload);
    auto _pKinematics = _GetOrAddMachineKinematics(_pMachineEntity);
    auto _pStatus = _GetOrAddMachineStatus(_pMachineEntity);
    const auto _TCP = _GetOptionalVariantArray(_Payload, "tcp", _pKinematics->GetTCP());
    const auto _BeamDirection = _GetOptionalVariantArray(_Payload, "beamDirection", _pKinematics->GetBeamDirection());
    auto _Undo = _Repository.BeginUndoCommand("Set CAM machine TCP");
    _SetVariantArrayProperty(_pKinematics, iCAX::CAM::CMachineKinematicsComponent::PropertyName_TCP, _NormalizeDoubleArray(_TCP, "tcp", 3));
    _SetVariantArrayProperty(_pKinematics, iCAX::CAM::CMachineKinematicsComponent::PropertyName_BeamDirection, _NormalizeDirectionArray(_BeamDirection, "beamDirection"));
    _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_Status, "Ready");
    _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_LastCheckResult, "TCP 已更新");
    _Undo->End();
    return MakeMachineResponse(_Scene, _pMachineEntity, _pMachine);
}

iCAX::Interaction::CInvocationResult HandleSetMachineToolTCP(
    IN const iCAX::Interaction::CInvocation& Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext*,
    IN iCAX::Project::IProjectContext*,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto _EntityIDText = _GetOptionalString(_Payload, "entityId");
    if (_EntityIDText.empty())
    {
        _EntityIDText = _GetOptionalString(_Payload, "toolEntityId");
    }
    if (_EntityIDText.empty())
    {
        _EntityIDText = _GetOptionalString(_Payload, "objectId");
    }
    if (_EntityIDText.empty())
    {
        throw std::invalid_argument("Cam Machine.SetToolTCP requires entityId");
    }

    auto& _Repository = _Scene.Database();
    const auto _EntityID = _ParseRequiredUuid(_EntityIDText, "entityId");
    auto _pEntity = _Repository.GetEntity(_EntityID);
    auto _pElement = _GetComponent<iCAX::CAM::CMachineElementComponent>(_pEntity);
    auto _pTool = _GetComponent<iCAX::CAM::CMachineToolComponent>(_pEntity);
    if (!_pEntity || !_pElement || !_pTool)
    {
        throw std::invalid_argument("Cam Machine.SetToolTCP entity is not a machine tool element: " + _EntityIDText);
    }

    auto _TryReadVectorByNames = [&_Payload](
        IN std::initializer_list<const char*> Names_,
        OUT std::vector<double>& Values_) {
        for (const auto* _pszName : Names_)
        {
            const std::string _Name(_pszName ? _pszName : "");
            if (_Name.empty() || !HasPayloadValue(_Payload, _Name))
            {
                continue;
            }
            return TryReadVector3(_Payload, _Name, Values_);
        }
        return false;
    };

    std::vector<double> _TcpLocalPosition;
    std::vector<double> _BeamLocalDirection;
    iCAX::Data::PropertySet _Properties;
    if (_TryReadVectorByNames({ "tcpLocalPosition", "tcp", "tcpPosition" }, _TcpLocalPosition))
    {
        _Properties[iCAX::CAM::CMachineToolComponent::PropertyName_TcpLocalPosition] = _DoublesToVariantArray(_TcpLocalPosition);
    }
    if (_TryReadVectorByNames({ "beamLocalDirection", "beamDirection", "beam" }, _BeamLocalDirection))
    {
        _Properties[iCAX::CAM::CMachineToolComponent::PropertyName_BeamLocalDirection] =
            _NormalizeDirectionArray(_DoublesToVariantArray(_BeamLocalDirection), "beamLocalDirection");
    }
    if (_Properties.empty())
    {
        throw std::invalid_argument("Cam Machine.SetToolTCP requires tcpLocalPosition or beamLocalDirection");
    }

    auto _Undo = _Repository.BeginUndoCommand("Set CAM machine tool TCP");
    std::string _strError;
    if (!_pTool->SetProperties(_Properties, _strError))
    {
        throw std::invalid_argument(_strError.empty() ? "Cam Machine.SetToolTCP is rejected by modify filter" : _strError);
    }

    auto _pMachineEntity = _Repository.GetEntity(_pElement->GetMachineID());
    auto _pMachine = _GetComponent<iCAX::CAM::CMachineInstanceComponent>(_pMachineEntity);
    if (_pMachineEntity)
    {
        auto _pStatus = _GetOrAddMachineStatus(_pMachineEntity);
        _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_Status, "Ready");
        _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_LastCheckResult, "工具 TCP 已更新");
    }
    _Undo->End();

    ObjectMap _Result;
    _Result["machineElement"] = _MakeMachineElementDetailPayload(_Repository, _EntityID);
    if (_pMachineEntity && _pMachine)
    {
        _Result["machine"] = _MakeMachinePayload(_Scene.Resources(), _Repository, _pMachineEntity, _pMachine);
        _Result["machines"] = _MakeMachineArray(_Scene.Resources(), _Repository);
    }
    return _MakeResponse(Variant(_Result));
}

iCAX::Interaction::CInvocationResult HandleJogMachine(
    IN const iCAX::Interaction::CInvocation &Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto &_Repository = _Scene.Database();
    auto [_pMachineEntity, _pMachine] = RequireMachineFromPayload(_Repository, _Payload);
    auto _pStatus = _GetOrAddMachineStatus(_pMachineEntity);
    const auto _Axis = _GetOptionalString(_Payload, "axis", "X");
    const auto _Delta = _GetOptionalDouble(_Payload, "delta", 0.0);
    if (_Delta == 0.0)
    {
        throw std::invalid_argument("Cam JogMachine requires non-zero delta");
    }
    auto _Joints = _CollectMachineJointComponents(_Repository, _pMachineEntity->GetID());
    auto _Iter = std::find_if(_Joints.begin(), _Joints.end(), [&_Axis](IN const auto& Item_) {
        return Item_.second && Item_.second->GetJointName() == _Axis;
    });
    if (_Iter == _Joints.end())
    {
        throw std::invalid_argument("Cam JogMachine axis does not exist: " + _Axis);
    }
    auto _pJoint = _Iter->second;
    const auto _Target = _pJoint->GetPosition() + _Delta;
    if (_Target < _pJoint->GetLowerLimit() || _Target > _pJoint->GetUpperLimit())
    {
        throw std::invalid_argument("Cam JogMachine target exceeds joint limit: " + _Axis);
    }
    _SetDoubleProperty(_pJoint, iCAX::CAM::CMachineJointComponent::PropertyName_Position, _Target);
    _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_Status, "Jogged");
    _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_LastCheckResult, _Axis + " 点动完成");
    return MakeMachineResponse(_Scene, _pMachineEntity, _pMachine);
}

iCAX::Interaction::CInvocationResult HandleHomeMachine(
    IN const iCAX::Interaction::CInvocation &Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto &_Repository = _Scene.Database();
    auto [_pMachineEntity, _pMachine] = RequireMachineFromPayload(_Repository, _Payload);
    auto _pStatus = _GetOrAddMachineStatus(_pMachineEntity);
    for (const auto& [_pEntity, _pJoint] : _CollectMachineJointComponents(_Repository, _pMachineEntity->GetID()))
    {
        (void)_pEntity;
        _SetDoubleProperty(_pJoint, iCAX::CAM::CMachineJointComponent::PropertyName_Position, 0.0);
    }
    _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_Status, "Homed");
    _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_LastCheckResult, "机器已回零");
    return MakeMachineResponse(_Scene, _pMachineEntity, _pMachine);
}

iCAX::Interaction::CInvocationResult HandleResetMachine(
    IN const iCAX::Interaction::CInvocation &Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto &_Repository = _Scene.Database();
    auto [_pMachineEntity, _pMachine] = RequireMachineFromPayload(_Repository, _Payload);
    auto _pStatus = _GetOrAddMachineStatus(_pMachineEntity);
    _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_Status, "Ready");
    _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_LastCheckResult, "机器已复位");
    return MakeMachineResponse(_Scene, _pMachineEntity, _pMachine);
}

iCAX::Interaction::CInvocationResult HandleCheckMachineLimits(
    IN const iCAX::Interaction::CInvocation &Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto &_Repository = _Scene.Database();
    auto [_pMachineEntity, _pMachine] = RequireMachineFromPayload(_Repository, _Payload);
    auto _pStatus = _GetOrAddMachineStatus(_pMachineEntity);
    const auto _Result = _CheckMachineLimitResult(_Repository, _pMachineEntity->GetID());
    _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_Status, _Result == "轴限位检查通过" ? "Ready" : "LimitViolation");
    _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_LastCheckResult, _Result);
    return MakeMachineResponse(_Scene, _pMachineEntity, _pMachine);
}

iCAX::Interaction::CInvocationResult HandleCheckMachineReach(
    IN const iCAX::Interaction::CInvocation &Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto &_Repository = _Scene.Database();
    auto [_pMachineEntity, _pMachine] = RequireMachineFromPayload(_Repository, _Payload);
    auto _pStatus = _GetOrAddMachineStatus(_pMachineEntity);
    const auto _Result = std::string("机床运动学检查入口已就绪：具体ProcessPath的可达性由作业规划服务检查");
    _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_Status, "Ready");
    _SetStringProperty(_pStatus, iCAX::CAM::CMachineStatusComponent::PropertyName_LastCheckResult, _Result);
    return MakeMachineResponse(_Scene, _pMachineEntity, _pMachine);
}
} // namespace Facades
} // namespace CAM
} // namespace iCAX
