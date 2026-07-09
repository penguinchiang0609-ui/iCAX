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
    auto _PayloadVariant = _BuildScenePayload(_Scene);
    auto _Payload = _PayloadVariant.To<ObjectMap>();
    _Payload["fitView"] = _FitInfo;
    return _MakeResponse(Variant(_Payload));
}
} // namespace Commands
} // namespace CAM
} // namespace iCAX
