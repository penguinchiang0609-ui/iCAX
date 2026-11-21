#pragma once
#include "Data/Variant.h"
#include "ProjectContext/ISceneContext.h"
#include <memory>
#ifdef IPLANE_CAM_BUILD
#define IPLANE_CAM_API __declspec(dllexport)
#else
#define IPLANE_CAM_API __declspec(dllimport)
#endif
namespace iCAX::PlaneCAM {
struct SceneRuntime;
IPLANE_CAM_API void InitializeScene(iCAX::Project::ISceneContext &);
IPLANE_CAM_API void PumpTasks(iCAX::Project::ISceneContext &);
IPLANE_CAM_API void StopTasks(iCAX::Project::ISceneContext &);
IPLANE_CAM_API void StopTasks(iCAX::Project::ISceneContext &,
                              const std::shared_ptr<SceneRuntime> &);
} // namespace iCAX::PlaneCAM
