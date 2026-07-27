#include "pch.h"
#include "SDO.h"
#include "SDOSupport.h"
#include "MachineDefinitionSDOImplement.h"
#include "MachineSDOImplement.h"

#include "SDO/SDORegistrationCatalog.h"
#include "SDO/SDO.h"

namespace
{
    class CMachineDefinitionSDO final : public iCAX::Interaction::CSDO
    {
    public:
        CMachineDefinitionSDO()
            : CSDO("MachineDefinition")
        {
            ExposeMethod("Import", &iCAX::CAM::SDO::HandleImportMachineDefinition);
            ExposeMethod("List", &iCAX::CAM::SDO::HandleListMachineDefinitions);
            ExposeMethod("GetSupportedFormats", &iCAX::CAM::SDO::HandleGetSupportedMachineDefinitionFormats);
            ExposeMethod("SetEnabled", &iCAX::CAM::SDO::HandleSetMachineDefinitionEnabled);
            ExposeMethod("SetDefault", &iCAX::CAM::SDO::HandleSetDefaultMachineDefinition);
            ExposeMethod("Delete", &iCAX::CAM::SDO::HandleDeleteMachineDefinition);
        }
    };

    static_assert(iCAX::Interaction::IsStatelessSDOType<CMachineDefinitionSDO>);
}

ICAX_REGISTER_SDO(CMachineDefinitionSDO)

namespace iCAX
{
namespace CAM
{
namespace SDO
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

    bool GetOptionalBool(IN const ObjectMap& Payload_, IN const std::string& strName_, IN bool bDefault_)
    {
        auto _Iter = Payload_.find(strName_);
        if (_Iter == Payload_.end() || _Iter->second.Is<std::monostate>())
        {
            return bDefault_;
        }
        if (!_Iter->second.Is<bool>())
        {
            throw std::invalid_argument("Cam payload field must be a bool: " + strName_);
        }
        return _Iter->second.To<bool>();
    }
}

iCAX::Interaction::CInvocationResult HandleImportMachineDefinition(
    IN const iCAX::Interaction::CInvocation& Request_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _ProductContext = _RequireProductContext(pProductContext_);
    (void)pProjectContext_;
    (void)pSceneContext_;

    auto _Payload = _DecodeObjectPayload(Request_);
    const auto _SourcePath = _GetOptionalString(_Payload, "sourcePath");
    if (_SourcePath.empty())
    {
        throw std::invalid_argument("Cam MachineDefinition.Import requires sourcePath");
    }
    if (!_IsSupportedProductMachineDefinitionPath(_ProductContext, _SourcePath))
    {
        throw std::invalid_argument("Cam MachineDefinition.Import source format is not enabled by current product definition");
    }
    if (!_IsSupportedMachineDescriptionPath(_SourcePath))
    {
        throw std::invalid_argument("Cam MachineDefinition.Import source format has no loader implementation");
    }

    auto _DefinitionIDText = _GetOptionalString(_Payload, "machineDefinitionId");
    if (_DefinitionIDText.empty())
    {
        _DefinitionIDText = _UuidToString(iCAX::Data::GenerateNewUUID());
    }
    else
    {
        (void)_ParseRequiredUuid(_DefinitionIDText, "machineDefinitionId");
    }

    const auto _ManagedPath = _EnsureProductMachineDefinitionFiles(
        ApplicationContext_,
        _ProductContext,
        _DefinitionIDText,
        _SourcePath);
    const auto _Name = _GetOptionalString(_Payload, "name", _GetDisplayNameFromPath(_SourcePath));
    const auto _bDefault = GetOptionalBool(_Payload, "default", false);
    const auto _Definition = _MakeProductMachineDefinition(
        _DefinitionIDText,
        _Name,
        _SourcePath,
        _ManagedPath,
        true,
        _bDefault);
    const auto _Definitions = _UpsertProductMachineDefinition(_ProductContext, _Definition);

    ObjectMap _Result;
    _Result["machineDefinitionId"] = _DefinitionIDText;
    _Result["definition"] = _Definition;
    _Result["definitions"] = _Definitions;
    _Result["sourcePath"] = _SourcePath;
    _Result["managedPath"] = _ManagedPath;
    _Result["name"] = _Name;
    return _MakeResponse(Variant(_Result));
}

iCAX::Interaction::CInvocationResult HandleListMachineDefinitions(
    IN const iCAX::Interaction::CInvocation&,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _ProductContext = _RequireProductContext(pProductContext_);
    (void)pProjectContext_;
    (void)pSceneContext_;
    _EnsureBuiltInProductMachineDefinitions(ApplicationContext_, _ProductContext);

    ObjectMap _Result;
    _Result["definitions"] = _GetProductMachineDefinitionArray(_ProductContext);
    return _MakeResponse(Variant(_Result));
}

iCAX::Interaction::CInvocationResult HandleGetSupportedMachineDefinitionFormats(
    IN const iCAX::Interaction::CInvocation&,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _ProductContext = _RequireProductContext(pProductContext_);
    (void)pProjectContext_;
    (void)pSceneContext_;

    ObjectMap _Result;
    _Result["supportedFormats"] = _GetProductMachineDefinitionSupportedFormats(_ProductContext);
    return _MakeResponse(Variant(_Result));
}

iCAX::Interaction::CInvocationResult HandleSetMachineDefinitionEnabled(
    IN const iCAX::Interaction::CInvocation& Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _ProductContext = _RequireProductContext(pProductContext_);
    (void)pProjectContext_;
    (void)pSceneContext_;

    auto _Payload = _DecodeObjectPayload(Request_);
    const auto _DefinitionIDText = _GetOptionalString(_Payload, "machineDefinitionId");
    (void)_ParseRequiredUuid(_DefinitionIDText, "machineDefinitionId");
    const auto _bEnabled = GetRequiredBool(_Payload, "enabled");

    const auto _Definitions = _SetProductMachineDefinitionEnabled(_ProductContext, _DefinitionIDText, _bEnabled);
    ObjectMap _Result;
    _Result["definition"] = _FindProductMachineDefinition(_ProductContext, _DefinitionIDText);
    _Result["definitions"] = _Definitions;
    return _MakeResponse(Variant(_Result));
}

iCAX::Interaction::CInvocationResult HandleSetDefaultMachineDefinition(
    IN const iCAX::Interaction::CInvocation& Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _ProductContext = _RequireProductContext(pProductContext_);
    (void)pProjectContext_;
    (void)pSceneContext_;

    auto _Payload = _DecodeObjectPayload(Request_);
    const auto _DefinitionIDText = _GetOptionalString(_Payload, "machineDefinitionId");
    (void)_ParseRequiredUuid(_DefinitionIDText, "machineDefinitionId");

    const auto _Definitions = _SetDefaultProductMachineDefinition(_ProductContext, _DefinitionIDText);
    ObjectMap _Result;
    _Result["definition"] = _FindProductMachineDefinition(_ProductContext, _DefinitionIDText);
    _Result["definitions"] = _Definitions;
    _Result["defaultMachineDefinitionId"] = _DefinitionIDText;
    return _MakeResponse(Variant(_Result));
}

iCAX::Interaction::CInvocationResult HandleDeleteMachineDefinition(
    IN const iCAX::Interaction::CInvocation& Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _ProductContext = _RequireProductContext(pProductContext_);
    (void)pProjectContext_;
    (void)pSceneContext_;

    auto _Payload = _DecodeObjectPayload(Request_);
    const auto _DefinitionIDText = _GetOptionalString(_Payload, "machineDefinitionId");
    (void)_ParseRequiredUuid(_DefinitionIDText, "machineDefinitionId");

    const auto _Definitions = _DeleteProductMachineDefinition(_ProductContext, _DefinitionIDText);
    ObjectMap _Result;
    _Result["deletedMachineDefinitionId"] = _DefinitionIDText;
    _Result["definitions"] = _Definitions;
    return _MakeResponse(Variant(_Result));
}

} // namespace SDO
} // namespace CAM
} // namespace iCAX
