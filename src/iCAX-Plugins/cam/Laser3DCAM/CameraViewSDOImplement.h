#pragma once

#include "SDOSupport.h"

namespace iCAX::CAM::SDO::Internal
{
ObjectMap _FitActiveCameraToRenderableBounds(
    IN iCAX::Project::IProjectContext& ProjectContext_,
    IN iCAX::Project::ISceneContext& SceneContext_);
ObjectMap _SetActiveCameraToStandardView(
    IN iCAX::Project::IProjectContext& ProjectContext_,
    IN iCAX::Project::ISceneContext& SceneContext_,
    IN const std::string& strViewName_);
} // namespace iCAX::CAM::SDO::Internal
