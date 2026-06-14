#include "pch.h"
#include "glLight.h"


//!< 刷新
void iCAX::Render::glLight::Update(IN const LightComponent& Light_, IN const TransformComponent& Tranform, IN const size_t& nVerion_)
{
    if (m_nVersion == nVerion_)
    {
        return;
    }

    auto _dirForward = Tranform.GetRotation().Applied(Dir3::Forward());

    m_lightType = Light_.GetLightType();
    m_color = glm::vec4((float)Light_.GetColor().R() / 255.f, (float)Light_.GetColor().G() / 255.f, (float)Light_.GetColor().B() / 255.f, (float)Light_.GetColor().A() / 255.f);
    m_intensity = (float)Light_.GetIntensity();
    m_position = glm::vec3((float)Tranform.GetTranslation().X(), (float)Tranform.GetTranslation().Y(), (float)Tranform.GetTranslation().Z());
    m_direction = glm::vec3((float)_dirForward.X(), (float)_dirForward.Y(), (float)_dirForward.Z());
    m_range = (float)Light_.GetRange();
    m_constantAttenuation = (float)Light_.GetConstantAttenuation();
    m_linearAttenuation = (float)Light_.GetLinearAttenuation();
    m_quadraticAttenuation = (float)Light_.GetQuadraticAttenuation();
    m_innerConeAngle = (float)Light_.GetInnerConeAngle();
    m_outerConeAngle = (float)Light_.GetOuterConeAngle();

    m_nVersion = nVerion_;
}


//!< 将光源数据绑定到着色器中
void iCAX::Render::glLight::BindToShader(IN const int& _nLightIndex, IN const glShader& Shader_) const
{
    std::string prefix = "u_lights[" + std::to_string(_nLightIndex) + "]";

    // 设置光源的基本数据
    Shader_.SetUniform(prefix + ".type", m_lightType);
    Shader_.SetUniform(prefix + ".color", m_color);
    Shader_.SetUniform(prefix + ".intensity", m_intensity);

    // 设置点光源和聚光灯的参数
    if (m_lightType == iCAX::Render::kLightPoint || m_lightType == iCAX::Render::kLightSpot)
    {
        Shader_.SetUniform(prefix + ".position", m_position);
        Shader_.SetUniform(prefix + ".range", m_range);
        Shader_.SetUniform(prefix + ".constantAttenuation", m_constantAttenuation);
        Shader_.SetUniform(prefix + ".linearAttenuation", m_linearAttenuation);
        Shader_.SetUniform(prefix + ".quadraticAttenuation", m_quadraticAttenuation);
    }

    // 设置平行光和聚光灯的方向
    if (m_lightType == iCAX::Render::kLightDirectional || m_lightType == iCAX::Render::kLightSpot)
    {
        Shader_.SetUniform(prefix + ".direction", m_direction);
    }

    // 设置聚光灯的角度
    if (m_lightType == iCAX::Render::kLightSpot)
    {
        Shader_.SetUniform(prefix + ".innerConeAngle", m_innerConeAngle);
        Shader_.SetUniform(prefix + ".outerConeAngle", m_outerConeAngle);
    }
}
