#include "pch.h"
#include "Facades.h"
#include "FacadeSupport.h"
#include "WorkpieceFacadeImplement.h"

#include "Facades/FacadeRegistrationCatalog.h"
#include "Facades/Facade.h"

namespace
{
    class CWorkpieceModelFacade final : public iCAX::Interaction::CFacade
    {
    public:
        CWorkpieceModelFacade()
            : CFacade("WorkpieceModel")
        {
            ExposeMethod("Import", &iCAX::CAM::Facades::HandleImportWorkpieceModel);
        }
    };

    static_assert(iCAX::Interaction::IsStatelessFacadeType<CWorkpieceModelFacade>);
}

ICAX_REGISTER_FACADE(CWorkpieceModelFacade)

namespace iCAX
{
namespace CAM
{
namespace Facades
{
using namespace Internal;

iCAX::Interaction::CInvocationResult HandleImportWorkpieceModel(
    IN const iCAX::Interaction::CInvocation &Request_,
    IN const iCAX::Application::IApplicationContext&,
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
        throw std::invalid_argument("Cam WorkpieceModel.Import requires sourcePath");
    }
    if (!_IsSupportedWorkpieceModelPath(_SourcePath))
    {
        throw std::invalid_argument("Cam WorkpieceModel.Import only supports STEP/STP and IGS/IGES workpiece files");
    }

    const auto _Tolerance = _GetOptionalDouble(_Payload, "tolerance", 0.001);
    if (_Tolerance <= 0.0)
    {
        throw std::invalid_argument("Cam WorkpieceModel.Import tolerance must be greater than zero");
    }

    const auto _ImportResult = _ImportCadModel(_Scene, _SourcePath, _Tolerance);
    ObjectMap _Result;
    _Result["sourcePath"] = _SourcePath;
    _Result["name"] = _GetOptionalString(_Payload, "name", _GetDisplayNameFromPath(_SourcePath));
    _Result["modelResourceId"] = _ImportResult.ModelResourceID;
    _Result["brepResourceId"] = _ImportResult.BRepResourceID;
    _Result["topologyResourceId"] = _ImportResult.TopologyResourceID;
    _Result["topologyVersion"] = static_cast<unsigned long long>(_ImportResult.nTopologyVersion);
    return _MakeResponse(Variant(_Result));
}

} // namespace Facades
} // namespace CAM
} // namespace iCAX
