#include "pch.h"
#include "glCamera.h"
#include "../Core/CoreComponent/TransformComponent.h"

using namespace glm;

//!< 刷新
void iCAX::Render::glCamera::Update(IN const CameraComponent& Camera_, IN const TransformComponent& Tranform, IN const size_t& nVerion_)
{
    if (nVerion_ == m_nVersion)
    {
        return;
    }

    auto& _vTranslation = Tranform.GetTranslation();
    auto& _qRotation = Tranform.GetRotation();
    auto _mat = _qRotation.ToTrsf3();
    auto _dirForward = _mat.Applied(Dir3::Forward());
    auto _dirUp = _mat.Applied(Dir3::Up());

    iCAX::Math::Point3 _ptTarget = _vTranslation.XYZ() + _dirForward.Vector().XYZ();

    // View 矩阵
    m_view = glm::lookAt
    (
        glm::vec3(_vTranslation.X(), _vTranslation.Y(), _vTranslation.Z()),
        glm::vec3(_ptTarget.X(), _ptTarget.Y(), _ptTarget.Z()),
        glm::vec3(_dirUp.X(), _dirUp.Y(), _dirUp.Z())
    );

    // Projection 矩阵
    if (Camera_.GetCameraType() == rCameraType::kCameraPerspective)
    {
        float fovRad = glm::radians(static_cast<float>(Camera_.GetFOV()));
        m_proj = glm::perspective
        (
            fovRad,
            static_cast<float>(Camera_.GetAspect()),
            static_cast<float>(Camera_.GetNearPlane()),
            static_cast<float>(Camera_.GetFarPlane())
        );
    }
    else // Orthographic
    {
        float halfW = static_cast<float>(Camera_.GetOrthoWidth() * 0.5);
        float halfH = static_cast<float>(Camera_.GetOrthoHeight() * 0.5);
        m_proj = glm::ortho(
            -halfW, halfW,
            -halfH, halfH,
            static_cast<float>(Camera_.GetNearPlane()),
            static_cast<float>(Camera_.GetFarPlane())
        );
    }
    m_nVisiableLayers = Camera_.GetVisiableLayers();

    //!< 更新本地版本
    m_nVersion = nVerion_;
}

//!< 将相机数据绑定到着色器中
void iCAX::Render::glCamera::BindToShader(IN const glShader& Shader_) const
{
    Shader_.SetUniform("view_matrix", m_view);
    Shader_.SetUniform("projection_matrix", m_proj);
}

//!< 指定图层是否显示
bool iCAX::Render::glCamera::IsVisiable(IN const unsigned int& nLayerNo_) const
{
    if (nLayerNo_ >= 32)
    {
        throw std::runtime_error("iCAX::Component::CameraComponent::IsVisiable, nLayerNo_ bigger than 32");
    }
    return ((1 << nLayerNo_) & m_nVisiableLayers) != 0;
}
