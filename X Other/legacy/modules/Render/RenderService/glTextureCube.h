#pragma once
#include <GL/glew.h>
#include <string>
#include <vector>
#include <array>
#include "../RenderComponent/TextureCube.h"

namespace iCAX
{
    namespace Render
    {
        /**
        * @class glTextureCube
        * @brief 立方体纹理的 OpenGL 封装
        * @details
        * 用于将 rTextureCube 上传到 GPU 并管理 OpenGL Cube Map。
        */
        class glTextureCube
        {
        public:
        public:
            /**
            * @brief 默认构造函数
            * @details 初始化 textureID 为 0
            */
            glTextureCube();
            /**
            * @brief 析构函数
            * @details 删除 GPU 纹理对象
            */
            ~glTextureCube();

        public:
            /**
            * @brief 上传立方体纹理到 GPU
            * @param [in] Tex_ rTextureCube 对象
            * @return 是否上传成功
            */
            bool Upload(IN const rTextureCube& Tex_);

            /**
            * @brief 绑定到指定纹理单元
            * @param [in] _nIndex 纹理单元
            */
            void Bind(IN const GLuint& _nIndex) const;

            /**
            * @brief 解绑纹理
            * @param [in] _nIndex 纹理单元
            */
            void Unbind(IN const GLuint& _nIndex) const;

            /*
            * @brief 获取纹理ID
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
