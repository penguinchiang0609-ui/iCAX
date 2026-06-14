#pragma once
#include "RenderService.h"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "../RenderComponent/EnvironmentComponent.h"
#include "glShader.h"

namespace iCAX
{
    namespace Render
    {
        /**
        * @brief GL层相机
        * @details
        * 负责把 CameraComponent 转换成 OpenGL 的 View/Projection 矩阵
        */
        class glEnvironment final
        {
        public:
            /*
            * @brief 更新
            * @param [in] Env_
            * @param [in] nVerion_
            */
            void Update(IN const EnvironmentComponent& Env_, IN const size_t& nVerion_);

            /**
            * @brief 将环境光数据绑定到着色器中
            * @param [in] Shader_ 着色器对象
            */
            void BindToShader(IN const glShader& Shader_) const;

        private:
            glm::vec3 m_AmbientColor = glm::vec3(1.0f, 1.0f, 1.0f); // 环境光颜色
            float m_nIntensity = 0.2f;                        // 环境光强度 (0~1)
            float m_nExposure = 1.0f;                                // 可选：全局曝光调节
            size_t m_nVerision = (size_t)-1;
        };
    }
}
