#pragma once
#include "RenderService.h"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "../RenderComponent/CameraComponent.h"
#include "glShader.h"
#include "../Core/CoreComponent/TransformComponent.h"

using namespace iCAX::Core;

namespace iCAX
{
    namespace Render
    {
        /**
        * @brief GL层相机
        * @details
        * 负责把 CameraComponent 转换成 OpenGL 的 View/Projection 矩阵
        */
        class glCamera final
        {
        public:
            /*
            * @brief 更新
            * @param [in] Camera_
            * @param [in] Tranform
            * @param [in] nVerion_
            */
            void Update(IN const CameraComponent& Camera_, IN const TransformComponent& Tranform, IN const size_t& nVerion_);

            /**
            * @brief 将环境光数据绑定到着色器中
            * @param [in] Shader_ 着色器对象
            */
            void BindToShader(IN const glShader& Shader_) const;

            /*
            * @brief 是否可见
            * @param [in] nLayerNo_
            */
            bool IsVisiable(IN const unsigned int& nLayerNo_) const;

        private:
            glm::mat4 m_view = glm::mat4(1.0f);
            glm::mat4 m_proj = glm::mat4(1.0f);
            unsigned int m_nVisiableLayers = 0xFFFFFFFF;
            size_t m_nVersion = (size_t)-1;
        };
    }
}
