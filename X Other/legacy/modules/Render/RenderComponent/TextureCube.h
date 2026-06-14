#pragma once
#include "Render.h"
#include <vector>
#include <array>

namespace iCAX
{
    namespace Render
    {
        /**
        * @brief 立方体纹理类
        * @details
        * rTextureCube 表示一个立方体贴图 (Cubemap)，
        * 包含 6 个方向 (+X, -X, +Y, -Y, +Z, -Z) 的纹理面数据。
        * 每个面为 nSize × nSize × nChannels 的像素矩阵。
        *
        * 像素数据按行优先顺序存储，适用于渲染引擎中的反射、天空盒、环境贴图等用途。
        */
        struct _RENDERCOMPONENT_EXP rTextureCube final
        {
            int nSize;                                 //!< 每个面是 nSize × nSize
            int nChannels;
            std::array<std::vector<unsigned char>, 6> vecFaces; //!< 6 个面 (+X, -X, +Y, -Y, +Z, -Z)
            //!< 暂不支持动态修改，后续如果需要再打开（CADCAM应该不需要此功能，除非集成图片纹理编辑器）
            //int nVersion = 0;//!< 版本号，初始为零。每次修改都需要加一，调用者需要注意
        };
    }
}