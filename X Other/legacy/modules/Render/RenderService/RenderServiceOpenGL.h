#pragma once
#include "RenderService.h"
#include "IRenderService.h"
#include <unordered_map>
#include <GL/glew.h>
#include <Windows.h>
#include "Services/ServicesHelper.h"
namespace iCAX
{
    namespace Render
    {
        /**
        * @class OpenGLRenderService
        * @brief OpenGL 渲染服务实现
        * @details
        * 该类是 IRenderService 的具体实现，基于 OpenGL 管理渲染上下文、FBO 以及场景渲染流程。
        * 支持多场景绑定，每个场景维护独立的上下文与 Framebuffer。
        *
        * 典型生命周期：
        * 1. 调用 Upload() 初始化 GLEW、OpenGL 环境。
        * 2. 调用 BindScene() 绑定一个渲染场景与目标窗口。
        * 3. 每帧调用 RenderAll() 完成场景渲染。
        * 4. 调用 UnbindScene() 或 Shutdown() 清理资源。
        *
        * 设计要点：
        * - 与上层 ECS/逻辑解耦。
        * - 每个 RenderScene 独立的渲染上下文（HGLRC），便于多窗口渲染。
        * - 通过 RenderSceneInternal() 控制整体渲染流程。
        * - 可扩展：未来可添加后处理、阴影、反射探针、PBR 等。
        */
        struct OpenGLRenderService : public IRenderService
        {
        public:
            virtual ~OpenGLRenderService() { OnUnload(); }

        public:
            /*
            * @brief 加载
            * @details
            *   服务需要的相关资源初始化，保证就绪
            */
            virtual void OnLoad() override;

            /*
            * @brief 卸载
            * @details
            *   释放服务占用的资源
            */
            virtual void OnUnload() override;

        public:
            /**
            * @brief 判断渲染服务中是否包含指定场景
            * @param [in] SceneID_ 场景唯一标识
            * @return true 如果场景已存在，false 否则
            * @note
            * - 可用于避免重复绑定场景。
            */
            virtual bool HasScene(IN const iCAX::Data::uuid& SceneID_) override;

            /*
            * @brief 创建场景
            * @param [in] SceneID_
            */
            virtual void CreateScene(IN const iCAX::Data::uuid& SceneID_) override;

            /*
            * @brief 销毁场景
            * @param [in] SceneID_
            */
            virtual void DestoryScene(IN const iCAX::Data::uuid& SceneID_) override;

            /*
            * @brief 设置画布
            * @param [in] SceneID_
            * @param [in] Component_
            * @param [in] nVerion_
            * @return bool
            */
            virtual bool SetCanva(IN const iCAX::Data::uuid& SceneID_, IN const SceneComponent& Component_, IN const size_t& nVerion_) override;

            /*
            * @brief 销毁画布
            * @param [in] SceneID_
            */
            virtual void DestroyCanva(IN const iCAX::Data::uuid& SceneID_) override;

            /*
            * @brief 设置主相机
            * @param [in] SceneID_
            * @param [in] Camera_
            * @param [in] Transform_
            * @param [in] nVerion_
            */
            virtual void SetMainCamera(IN const iCAX::Data::uuid& SceneID_, IN const CameraComponent& Camera_, IN const TransformComponent& Transform_, IN const size_t& nVerion_) override;

            /*
            * @brief 移除主相机
            * @param [in] SceneID_ 场景ID
            */
            virtual void RemoveMainCamera(IN const iCAX::Data::uuid& SceneID_) override;

            /*
            * @brief 设置环境信息
            * @param [in] SceneID_
            * @param [in] Component_
            * @param [in] nVerion_
            */
            virtual void SetEnviroment(IN const iCAX::Data::uuid& SceneID_, IN const EnvironmentComponent& Env_, IN const size_t& nVerion_) override;

            /*
            * @brief 设置光源
            * @param [in] SceneID_
            * @param [in] Component_
            * @param [in] nVerion_
            */
            virtual void SetLight(IN const iCAX::Data::uuid& SceneID_, IN const iCAX::Data::uuid& ID_, IN const LightComponent& Light_, IN const TransformComponent& Tranform_, IN const size_t& nVerion_) override;

            /*
            * @brief 移除光源
            * @param [in] ID_
            */
            virtual void RemoveLight(IN const iCAX::Data::uuid& SceneID_, IN const iCAX::Data::uuid& ID_) override;

            /*
            * @brief 增加实体
            * @param [in] SceneID_
            * @param [in] pLight_
            */
            virtual void SetEntity(IN const iCAX::Data::uuid& SceneID_, IN const iCAX::Data::uuid& ID_, IN const TransformComponent& Transform_, IN const LayerComponent& Layer_, IN const MeshFilterComponent& Meshes_, IN const MeshRenderComponent& Meterials_, IN const size_t& nVersions_) override;

            /*
            * @brief 移除实体
            * @param [in] SceneID_
            * @param [in] pLight_
            */
            virtual void RemoveEntity(IN const iCAX::Data::uuid& SceneID_, IN const iCAX::Data::uuid& ID_) override;

            /*
            * @brief 渲染特定场景
            * @param [in] SceneID_
            */
            virtual void RenderScene(IN const iCAX::Data::uuid& SceneID_) override;

            /*
            * @brief 设置下一帧保存图片位置
            * @param [in] SceneID_
            * @param [in] strPath_
            */
            virtual void SetNextFrameImagePath(IN const iCAX::Data::uuid& SceneID_, IN const std::string& strPath_) override;

            /**
            * @brief 渲染所有绑定的场景
            * @note
            * - ECS 系统或上层循环中每帧调用一次。
            * - RenderService 遍历所有绑定场景：
            *   1. 绑定对应 FBO / 渲染目标
            *   2. 渲染 RenderScene 中的所有 RenderObject
            *   3. 如果是 WPF D3DImage，则刷新共享纹理
            * - 上层无需管理各个窗口或纹理的刷新。
            */
            virtual void RenderAll() override;

        private:
            std::unordered_map<iCAX::Data::uuid, std::shared_ptr<class glScene>> m_scenes; //!< 场景表（多窗口）

            AUTO_REGIST_SERVICE(IRenderService, OpenGLRenderService);
        };
    }
}
