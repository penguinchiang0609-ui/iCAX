#pragma once

#ifdef _SCENE_BOOTSTRAP
#define _SCENE_BOOTSTRAP_EXP __declspec(dllexport)
#else
#define _SCENE_BOOTSTRAP_EXP __declspec(dllimport)
#endif

namespace iCAX::Common
{
    _SCENE_BOOTSTRAP_EXP unsigned int GetSceneBootstrapContractVersion() noexcept;
}
