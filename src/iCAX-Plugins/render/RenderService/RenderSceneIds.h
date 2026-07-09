#pragma once

#include "RenderServiceExport.h"
#include "RenderData/RenderDataTypes.h"
#include "Data/uuid.h"

namespace iCAX
{
    namespace Render
    {
        /*
        * @brief 从 Scene UUID 生成 RenderSceneID。
        * @details
        *   RenderService 的调用方必须使用同一算法，才能让场景发布行为、相机控制行为、
        *   碰撞/拾取等插件落到同一个渲染场景。
        */
        _RENDER_SERVICE_EXP RenderSceneID MakeRenderSceneID(IN const iCAX::Data::uuid& SceneID_) noexcept;
    }
}

