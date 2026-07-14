#include "pch.h"
#include "Targets.h"
#include "CameraViewTargetImplement.h"
#include "CommandTargetSupport.h"

#include "CommandTargets/CommandRegistrationCatalog.h"
#include "CommandTargets/CommandTarget.h"

namespace
{
    class CCameraViewTarget final : public iCAX::Command::CCommandTarget
    {
    public:
        CCameraViewTarget()
            : CCommandTarget("CameraView")
        {
            Bind("Fit", &iCAX::CAM::Commands::HandleFitCameraView);
            Bind("SetStandard", &iCAX::CAM::Commands::HandleSetStandardCameraView);
        }
    };

    static_assert(iCAX::Command::IsStatelessCommandTargetType<CCameraViewTarget>);
}

ICAX_REGISTER_COMMAND_TARGET(CCameraViewTarget)

namespace iCAX
{
namespace CAM
{
namespace Commands
{
using namespace Internal;

iCAX::Command::CCommandResponse HandleFitCameraView(
    IN const iCAX::Command::CCommandRequest &,
    IN iCAX::Application::IApplicationContext &,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *pProjectContext_,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Project = _RequireProjectContext(pProjectContext_);
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    auto _FitInfo = _FitActiveCameraToRenderableBounds(_Project, _Scene);
    ObjectMap _Payload;
    _Payload["fitView"] = _FitInfo;
    return _MakeResponse(Variant(_Payload));
}

iCAX::Command::CCommandResponse HandleSetStandardCameraView(
    IN const iCAX::Command::CCommandRequest& Request_,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext*,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    auto& _Project = _RequireProjectContext(pProjectContext_);
    auto& _Scene = _RequireSceneContext(pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    const auto _ViewName = _GetOptionalString(_Payload, "view", "iso");
    auto _ViewInfo = _SetActiveCameraToStandardView(_Project, _Scene, _ViewName);
    ObjectMap _Result;
    _Result["cameraView"] = _ViewInfo;
    return _MakeResponse(Variant(_Result));
}
} // namespace Commands
} // namespace CAM
} // namespace iCAX
