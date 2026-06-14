#pragma once
#include "RenderService.h"
#include <GL/glew.h>
#include<GL/GL.h>
#include <vector>
#include <memory>
#include "glEntity.h"
#include "glCamera.h"
#include "glLight.h"
#include "../RenderComponent/CameraComponent.h"
#include "../RenderComponent/LightComponent.h"
#include <Windows.h>
#include "glEnvironment.h"
#include "../RenderComponent/EnvironmentComponent.h"
#include "../RenderComponent/SceneComponent.h"

namespace iCAX
{
    namespace Render
    {
        /*
        * @brief 场景
        */
        class glScene final
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] ID_ 场景ID
            */
            glScene(IN const iCAX::Data::uuid& ID_);

            /*
            * @brief 析构函数
            */
            ~glScene() { Cleanup(); }
        public:
            /*
            * 
            * @brief 获取ID
            */
            const iCAX::Data::uuid& GetID() const;

            /*
            * @brief 设置画布
            * @param [in] Component_
            * @param [in] nVerion_
            * @return bool
            */
            bool SetCanva(IN const SceneComponent& Component_, IN const size_t& nVerion_);

            /*
            * @brief 销毁画布
            */
            void DestroyCanva();

            /*
            * @brief 取消画布
            */
            void RemoveCanva();

            /*
            * @brief 设置主相机
            * @param [in] Camera_
            * @param [in] Tranform
            * @param [in] nVerion_
            */
            void SetMainCamera(IN const CameraComponent& Camera_, IN const TransformComponent& Tranform, IN const size_t& nVerion_);

            /*
            * @brief 移除主相机
            */
            void RemoveMainCamera();

            /*
            * @brief 设置环境信息
            * @param [in] Env_
            * @param [in] nVerion_
            */
            void SetEnviroment(IN const EnvironmentComponent& Env_, IN const size_t& nVerion_);

            /*
            * @brief 更新光源
            * @param [in] ID_
            * @param [in] Light_
            * @param [in] Tranform
            * @param [in] nVerion_
            */
            void SetLight(IN const iCAX::Data::uuid& ID_, IN const LightComponent& Light_, IN const TransformComponent& Tranform, IN const size_t& nVerion_);

            /*
            * @brief 移除光源
            * @param [in] ID_
            */
            void RemoveLight(IN const iCAX::Data::uuid& ID_);

            /*
            * @brief 设置物体
            * @param [in] ID_ 物体ID
            * @param [in] Transform_ 变换组件
            * @param [in] Layer_ 层组件
            * @param [in] Meshes_ 网格组件
            * @param [in] Meterials_ 材质组件
            * @param [in] nVersions_ 版本
            */
            void SetEntity(IN const iCAX::Data::uuid& ID_, IN const TransformComponent& Transform_, IN const LayerComponent& Layer_, IN const MeshFilterComponent& Meshes_, IN const MeshRenderComponent& Meterials_, IN const size_t& nVersions_);

            /*
            * @brief 移除实体
            * @param [in] ID_
            */
            void RemoveEntity(IN const iCAX::Data::uuid& ID_);

            /*
            * @brief 渲染
            */
            void Render();

            /*
            * @brief 设置下一帧保存图片位置
            * @param [in] strPath_
            */
            void SetNextFrameImagePath(IN const std::string& strPath_);

            /*
            * @brief 清理场景资源
            */
            void Cleanup();

        private:
            /**
            * @struct 画布
            * @brief 画布，场景对应的渲染上下文与资源。
            */
            struct CanvaData
            {
                HWND hwnd = nullptr;                              //!< 目标窗口句柄
                int width = 0, height = 0;                        //!< 渲染尺寸
                HDC hdc = nullptr;                                //!< Windows DC
                HGLRC glContext = nullptr;                        //!< OpenGL 渲染上下文
                GLuint fbo = 0;                                   //!< 帧缓冲对象
                GLuint colorTex = 0;                              //!< 颜色贴图附件
                glm::vec4 background;                             //!< 背景色
                size_t nVersion = (size_t)-1;
            };
        private:
            /**
            * @brief 创建 OpenGL 渲染上下文
            * @param data 场景数据引用
            * @return 成功返回 true
            */
            bool CreateGLContext(CanvaData& data);

            /**
            * @brief 销毁场景对应的 OpenGL 渲染上下文
            */
            void DestroyGLContext(CanvaData& data);

            /**
            * @brief 创建 Framebuffer 对象与附件
            */
            bool CreateFBO(CanvaData& data);

            /**
            * @brief 销毁 FBO 及附件
            */
            void DestroyFBO(CanvaData& data);

            void SaveToImage();
        private:
            iCAX::Data::uuid m_ID{};
            std::unordered_map<iCAX::Data::uuid, std::shared_ptr<glEntity>> m_mapEntities{};
            std::unordered_map<iCAX::Data::uuid, std::shared_ptr<glLight>> m_mapLights{};
            std::shared_ptr<glCamera>              m_MainCamera = nullptr;
            std::shared_ptr<glEnvironment>         m_Env = nullptr;
            std::string m_strFrame2ImagePath;
            CanvaData m_Canva{};
        };
    }
}