#pragma once
#include "RenderService.h"
#include <GL/glew.h>
#include <string>
#include <vector>
#include <array>
#include "../RenderComponent/Texture2.h"

namespace iCAX
{
    namespace Render
    {
        /**
        * @class glTexture2
        * @brief 二维纹理的 OpenGL 封装
        * @details
        * 该类用于将 CPU 层的 rTexture2 上传到 GPU 并管理 OpenGL 纹理对象。
        * 支持绑定纹理单元、设置过滤和重复模式。
        */
        class glTexture2
        {
        public:
            /**
            * @brief 默认构造函数
            * @details 初始化 textureID 为 0
            */
            glTexture2();

            /**
            * @brief 析构函数
            * @details 删除 GPU 纹理对象
            */
            ~glTexture2();

        public:
            /**
            * @brief 从 rTexture2 上传纹理数据
            * @param [in] Tex_ rTexture2 对象
            * @return 是否上传成功
            */
            bool Upload(IN const rTexture2& Tex_);

            /**
            * @brief 绑定到指定纹理单元
            * @param nIndex_ 纹理单元索引
            */
            void Bind(IN const GLuint& nIndex_) const;

            /**
            * @brief 解绑纹理
            * @param nIndex_ 纹理单元索引
            */
            void Unbind(IN const GLuint& nIndex_) const;

            /**
            * @brief 获取 OpenGL 纹理 ID
            * @return GLuint
            */
            GLuint TextureID() const;

            void BindToShader(IN const std::string& paramName, IN class glShader& shader, IN const GLuint& nIndex_) const;
        
            //!< 暂不支持Shader的版本号，CADCAM程序不需要支持动态修改纹理图片代码（游戏引擎也不需要，只有专业图片软件才会有这个需求）
            ///*
            //* @brief 获取版本号
            //*/
            //int Version();

        private:
            GLuint m_nTextureID;
        };
    }
}
