#include "pch.h"
#include "glEnvironment.h"

//!< 刷新
void iCAX::Render::glEnvironment::Update(IN const EnvironmentComponent& Env_, IN const size_t& nVerion_)
{
    if (m_nVerision == nVerion_)
    {
        return;
    }

    // 同步数据
    auto& color = Env_.GetAmbient();
    m_AmbientColor = glm::vec3
    (
        static_cast<float>(color.R()) / 255.0f,
        static_cast<float>(color.G()) / 255.0f,
        static_cast<float>(color.B()) / 255.0f
    );

    m_nIntensity = static_cast<float>(Env_.GetIntensity());
    m_nExposure = static_cast<float>(Env_.GetExposure());

    // 更新版本号
    m_nVerision = nVerion_;
}

//!< 将环境光数据绑定到着色器中
void iCAX::Render::glEnvironment::BindToShader(IN const glShader& Shader_) const
{
    Shader_.SetUniform("u_AmbientColor", m_AmbientColor * m_nIntensity);
    Shader_.SetUniform("u_Exposure", m_nExposure);
}
