#pragma once
#include "Render.h"
#include <vector>
#include <array>

namespace iCAX
{
    namespace Render
    {
        /*!
        * @brief 三维纹理类
        * @details
        * rTexture3 表示一个三维纹理，用于体绘制、体积采样或 GPU 上传。
        * 数据按 z-major 行优先（row-major）存储，每个像素包含 m_nChannels 个通道。
        * 提供对宽度、高度、深度、通道数和像素数据的引用访问，支持清空和直接填充原始数据。
        */
        struct _RENDERCOMPONENT_EXP rTexture3 final
        {
            int nWidth;
            int nHeight;
            int nDepth;                               //!< 深度（像素）
            int nChannels;                            //!< 通道数
            std::vector<unsigned char> vecData;       //!< 像素数据 (z-major, 行优先)
            //!< 暂不支持动态修改，后续如果需要再打开（CADCAM应该不需要此功能，除非集成图片纹理编辑器）
            //int nVersion = 0;//!< 版本号，初始为零。每次修改都需要加一，调用者需要注意
        };
    }
}