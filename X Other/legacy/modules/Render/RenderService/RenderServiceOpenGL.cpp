#include "pch.h"
#include "glScene.h"
#include "RenderServiceOpenGL.h"
#include <iostream>

//! 加载
void iCAX::Render::OpenGLRenderService::OnLoad()
{
    // 初始化 GLEW 或 OpenGL
    static bool glewInitialized = false;
    if (!glewInitialized)
    {
        GLenum err = glewInit();
        if (err != GLEW_OK) throw std::runtime_error("glew initialzie falied");
        glewInitialized = true;
    }

}

//! 卸载
void iCAX::Render::OpenGLRenderService::OnUnload()
{
    for (auto& kv : m_scenes)
    {
        // glScene 内部会清理 FBO / GLContext
        kv.second.reset();
    }
    m_scenes.clear();
}

bool iCAX::Render::OpenGLRenderService::HasScene(IN const iCAX::Data::uuid& SceneID_)
{
    return m_scenes.find(SceneID_) != m_scenes.end();
}

//! 设置画布
void iCAX::Render::OpenGLRenderService::CreateScene(IN const iCAX::Data::uuid& SceneID_)
{
    if (!HasScene(SceneID_))
    {
        m_scenes[SceneID_] = std::make_shared<glScene>(SceneID_);
    }
}

void iCAX::Render::OpenGLRenderService::DestoryScene(IN const iCAX::Data::uuid& SceneID_)
{
    auto it = m_scenes.find(SceneID_);
    if (it != m_scenes.end())
    {
        m_scenes.erase(it);
    }
}

//! 设置画布
bool iCAX::Render::OpenGLRenderService::SetCanva(IN const iCAX::Data::uuid& SceneID_, IN const SceneComponent& Component_, IN const size_t& nVerion_)
{
    auto it = m_scenes.find(SceneID_);
    if (it == m_scenes.end()) return false;
    return it->second->SetCanva(Component_, nVerion_);
}

//! 销毁画布
void iCAX::Render::OpenGLRenderService::DestroyCanva(IN const iCAX::Data::uuid& SceneID_)
{
    auto it = m_scenes.find(SceneID_);
    if (it == m_scenes.end()) return;
    return it->second->DestroyCanva();
}

//! 设置主相机
void iCAX::Render::OpenGLRenderService::SetMainCamera(IN const iCAX::Data::uuid& SceneID_, IN const CameraComponent& Camera_, IN const TransformComponent& Transform_, IN const size_t& nVerion_)
{
    auto it = m_scenes.find(SceneID_);
    if (it != m_scenes.end())
    {
        it->second->SetMainCamera(Camera_, Transform_, nVerion_);
    }
}

//! 移除主相机
void iCAX::Render::OpenGLRenderService::RemoveMainCamera(IN const iCAX::Data::uuid& SceneID_)
{
    auto it = m_scenes.find(SceneID_);
    if (it != m_scenes.end())
    {
        it->second->RemoveMainCamera();
    }
}

//! 设置环境信息
void iCAX::Render::OpenGLRenderService::SetEnviroment(IN const iCAX::Data::uuid& SceneID_, IN const EnvironmentComponent& Env_, IN const size_t& nVerion_)
{
    auto it = m_scenes.find(SceneID_);
    if (it != m_scenes.end())
    {
        it->second->SetEnviroment(Env_, nVerion_);
    }
}

//! 设置光源
void iCAX::Render::OpenGLRenderService::SetLight(IN const iCAX::Data::uuid& SceneID_, IN const iCAX::Data::uuid& ID_, IN const LightComponent& Light_, IN const TransformComponent& Tranform_, IN const size_t& nVerion_)
{
    auto it = m_scenes.find(SceneID_);
    if (it != m_scenes.end())
    {
        it->second->SetLight(ID_, Light_, Tranform_, nVerion_);
    }
}

//! 移除光源
void iCAX::Render::OpenGLRenderService::RemoveLight(IN const iCAX::Data::uuid& SceneID_, IN const iCAX::Data::uuid& ID_)
{
    auto it = m_scenes.find(SceneID_);
    if (it != m_scenes.end())
    {
        it->second->RemoveLight(ID_);
    }
}

//! 设置物体
void iCAX::Render::OpenGLRenderService::SetEntity(IN const iCAX::Data::uuid& SceneID_, IN const iCAX::Data::uuid& ID_, IN const TransformComponent& Transform_, IN const LayerComponent& Layer_, IN const MeshFilterComponent& Meshes_, IN const MeshRenderComponent& Meterials_, IN const size_t& nVersions_)
{
    auto it = m_scenes.find(SceneID_);
    if (it != m_scenes.end())
    {
        it->second->SetEntity(ID_, Transform_, Layer_, Meshes_, Meterials_, nVersions_);
    }
}

//! 移除实体
void iCAX::Render::OpenGLRenderService::RemoveEntity(IN const iCAX::Data::uuid& SceneID_, IN const iCAX::Data::uuid& ID_)
{
    auto it = m_scenes.find(SceneID_);
    if (it != m_scenes.end())
    {
        it->second->RemoveEntity(ID_);
    }
}

//! 渲染特定场景
void iCAX::Render::OpenGLRenderService::RenderScene(IN const iCAX::Data::uuid& SceneID_)
{
    auto it = m_scenes.find(SceneID_);
    if (it != m_scenes.end())
    {
        it->second->Render();
    }
}

void iCAX::Render::OpenGLRenderService::SetNextFrameImagePath(IN const iCAX::Data::uuid& SceneID_, IN const std::string& strPath_)
{
    auto it = m_scenes.find(SceneID_);
    if (it != m_scenes.end())
    {
        it->second->SetNextFrameImagePath(strPath_);
    }
}

void iCAX::Render::OpenGLRenderService::RenderAll()
{
    for (auto& kv : m_scenes)
    {
        kv.second->Render();
    }
}
