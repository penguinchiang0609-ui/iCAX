#include "pch.h"
#include "Facades.h"
#include "FacadeSupport.h"
#include "MachineDefinitionFacadeImplement.h"
#include "MachineFacadeImplement.h"

#include "Facades/FacadeRegistrationCatalog.h"
#include "Facades/Facade.h"

namespace
{
    class CMachineDefinitionFacade final : public iCAX::Interaction::CFacade
    {
    public:
        CMachineDefinitionFacade()
            : CFacade("MachineDefinition")
        {
            ExposeMethod("Import", &iCAX::CAM::Facades::HandleImportMachineDefinition);
            ExposeMethod("List", &iCAX::CAM::Facades::HandleListMachineDefinitions);
            ExposeMethod("GetSupportedFormats", &iCAX::CAM::Facades::HandleGetSupportedMachineDefinitionFormats);
            ExposeMethod("SetEnabled", &iCAX::CAM::Facades::HandleSetMachineDefinitionEnabled);
            ExposeMethod("SetDefault", &iCAX::CAM::Facades::HandleSetDefaultMachineDefinition);
            ExposeMethod("Delete", &iCAX::CAM::Facades::HandleDeleteMachineDefinition);
        }
    };

    static_assert(iCAX::Interaction::IsStatelessFacadeType<CMachineDefinitionFacade>);
}

ICAX_REGISTER_FACADE(CMachineDefinitionFacade)

namespace iCAX
{
namespace CAM
{
namespace Facades
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

iCAX::Interaction::CFacadeResult HandleImportMachineDefinition(
    IN const iCAX::Interaction::CFacadeCall& Request_,
    IN iCAX::Application::IApplicationContext& ApplicationContext_,
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

iCAX::Interaction::CFacadeResult HandleListMachineDefinitions(
    IN const iCAX::Interaction::CFacadeCall&,
    IN iCAX::Application::IApplicationContext& ApplicationContext_,
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

iCAX::Interaction::CFacadeResult HandleGetSupportedMachineDefinitionFormats(
    IN const iCAX::Interaction::CFacadeCall&,
    IN iCAX::Application::IApplicationContext&,
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

iCAX::Interaction::CFacadeResult HandleSetMachineDefinitionEnabled(
    IN const iCAX::Interaction::CFacadeCall& Request_,
    IN iCAX::Application::IApplicationContext&,
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

iCAX::Interaction::CFacadeResult HandleSetDefaultMachineDefinition(
    IN const iCAX::Interaction::CFacadeCall& Request_,
    IN iCAX::Application::IApplicationContext&,
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

iCAX::Interaction::CFacadeResult HandleDeleteMachineDefinition(
    IN const iCAX::Interaction::CFacadeCall& Request_,
    IN iCAX::Application::IApplicationContext&,
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

} // namespace Facades
} // namespace CAM
} // namespace iCAX
