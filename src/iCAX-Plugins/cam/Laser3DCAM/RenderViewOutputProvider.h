#pragma once

#include "View/ViewContent.h"

namespace iCAX::CAM
{
    /*
    * @brief 把通用 View 内容投影成独立 RenderScene 的输出 Provider。
    */
    class CLaser3DCAMRenderViewOutputProvider final
        : public iCAX::View::IViewOutputProvider
    {
    public:
        std::unique_ptr<iCAX::View::IViewOutputSession> Open(
            IN iCAX::Project::IProjectContext& Project_,
            IN iCAX::Project::ISceneContext& Scene_,
            IN const iCAX::View::SViewInstanceInfo& Instance_) const override;
    };
}
