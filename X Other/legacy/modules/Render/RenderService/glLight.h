#pragma once
#include "RenderService.h"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include "Math/Point3.h"
#include "Math/Dir3.h"
#include "Math/RGBA32.h"
#include "../RenderComponent/Shader.h"
#include "../RenderComponent/LightComponent.h"
#include "glShader.h"
#include "../Core/CoreComponent/TransformComponent.h"
#include "Data/uuid.h"

using namespace iCAX::Core;
using namespace iCAX::Data;

namespace iCAX
{
    namespace Render
    {
        /**
        * @class glLight
        * @brief OpenGL 光源封装类
        * @details
        * 该类用于将 LightComponent 的数据传递给 OpenGL，提供接口在渲染时绑定光源信息。
        * 支持平行光、点光源和聚光灯。
        */
        class glLight final
        {
        public:
            /*
            * @brief 更新
            * @param [in] Light_
            * @param [in] Tranform
            * @param [in] nVerion_
            */
            void Update(IN const LightComponent& Light_, IN const TransformComponent& Tranform, IN const size_t& nVerion_);

            /**
            * @brief 将光源数据绑定到着色器中
            * @param _nLightIndex 光源在着色器中的索引（通常是数组的索引）
            * @param Shader_ 着色器对象
            */
            void BindToShader(IN const int& _nLightIndex, IN const glShader& Shader_) const;

        private:
            int m_lightType = 0;// 光源类型（平行光、点光源、聚光灯）
            glm::vec4 m_color = glm::vec4(1.0f);// 光源颜色
            float m_intensity = 1.0f;// 光源强度
            glm::vec3 m_position = glm::vec3(0.0f);// 光源位置
            glm::vec3 m_direction = glm::vec3(0.0f, 0.0f, -1.0f);// 光源方向
            float m_range = 100.0f;// 光源范围（适用于点光源）
            // 光源衰减项
            float m_constantAttenuation = 1.0f;
            float m_linearAttenuation = 0.0f;
            float m_quadraticAttenuation = 0.0f;
            // 聚光灯角度（适用于聚光灯）
            float m_innerConeAngle = 0.0f;
            float m_outerConeAngle = 0.0f;
            size_t m_nVersion = (size_t)-1;
        };
    }
}
