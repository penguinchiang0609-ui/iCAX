#include "pch.h"
#include "Targets.h"
#include "CommandTargetSupport.h"
#include "WorkpieceTargetImplement.h"

#include "CommandTargets/CommandRegistrationCatalog.h"
#include "CommandTargets/CommandTarget.h"

namespace
{
    class CWorkpieceModelTarget final : public iCAX::Command::CCommandTarget
    {
    public:
        CWorkpieceModelTarget()
            : CCommandTarget("WorkpieceModel")
        {
            Bind("Import", &iCAX::CAM::Commands::HandleImportWorkpieceModel);
        }
    };

    static_assert(iCAX::Command::IsStatelessCommandTargetType<CWorkpieceModelTarget>);
}

ICAX_REGISTER_COMMAND_TARGET(CWorkpieceModelTarget)

namespace iCAX
{
namespace CAM
{
namespace Commands
{
using namespace Internal;

iCAX::Command::CCommandResponse HandleImportWorkpieceModel(
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

} // namespace Commands
} // namespace CAM
} // namespace iCAX
