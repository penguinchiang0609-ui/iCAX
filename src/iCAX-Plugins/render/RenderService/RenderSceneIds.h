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

        /*
        * @brief 为 Scene 中的独立 ViewInstance 生成稳定 RenderSceneID。
        * @details 同一 Entity 可以同时出现在多个 View RenderScene 中而拥有不同的表现数据。
        */
        _RENDER_SERVICE_EXP RenderSceneID MakeViewRenderSceneID(
            IN const iCAX::Data::uuid& SceneID_,
            IN const iCAX::Data::uuid& ViewInstanceID_) noexcept;
    }
}
