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

iCAX::Command::CCommandResponse HandleGetScene(
    IN const iCAX::Command::CCommandRequest &,
    IN iCAX::Application::IApplicationContext &,
    IN iCAX::Product::IProductContext *,
    IN iCAX::Project::IProjectContext *,
    IN iCAX::Project::ISceneContext *pSceneContext_)
{
    auto &_Scene = _RequireSceneContext(pSceneContext_);
    return _MakeResponse(_BuildScenePayload(_Scene));
}
} // namespace Commands
} // namespace CAM
} // namespace iCAX
