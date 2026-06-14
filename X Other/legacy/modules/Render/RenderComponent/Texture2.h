#pragma once
#include "Render.h"
#include <vector>
#include <array>

namespace iCAX
{
    namespace Render
    {
        /*
        * @brief 二维纹理类
        * @details
        * rTexture2 表示一个二维纹理，用于贴图、渲染采样或 GPU 上传。
        * 数据按行优先存储，即行优先（row-major），每个像素包含 m_nChannels 个通道（例如 RGBA=4）。
        * 提供对宽度、高度、通道数和像素数据的引用访问，支持清空和直接填充原始数据。
        */
        struct _RENDERCOMPONENT_EXP rTexture2 final
        {
            int nWidth;                               //!< 纹理宽度（像素）
            int nHeight;                              //!< 纹理高度（像素）
            int nChannels;                            //!< 每个像素的通道数（例如 3=RGB, 4=RGBA）
            std::vector<unsigned char> vecData;       //!< 像素数据，行优先存储
            //!< 暂不支持动态修改，后续如果需要再打开（CADCAM应该不需要此功能，除非集成图片纹理编辑器）
            //int nVersion = 0;//!< 版本号，初始为零。每次修改都需要加一，调用者需要注意
        };
    }
}