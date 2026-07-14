#pragma once

#include "CommandTargetSupport.h"

namespace iCAX::CAM::Commands::Internal
{
ObjectMap _FitActiveCameraToRenderableBounds(
    IN iCAX::Project::IProjectContext& ProjectContext_,
    IN iCAX::Project::ISceneContext& SceneContext_);
ObjectMap _SetActiveCameraToStandardView(
    IN iCAX::Project::IProjectContext& ProjectContext_,
    IN iCAX::Project::ISceneContext& SceneContext_,
    IN const std::string& strViewName_);
} // namespace iCAX::CAM::Commands::Internal
