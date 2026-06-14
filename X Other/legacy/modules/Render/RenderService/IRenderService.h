#pragma once
#include "RenderService.h"
#include <memory>
#include <string>
#include <vector>
#include "Data/uuid.h"
#include "../RenderComponent/CameraComponent.h"
#include "../RenderComponent/EnvironmentComponent.h"
#include "../RenderComponent/LightComponent.h"
#include "CoreComponent/TransformComponent.h"
#include "../RenderComponent/MeshFilterComponent.h"
#include "../RenderComponent/MeshRenderComponent.h"
#include "../RenderComponent/LayerComponent.h"
#include "Services/IService.h"
#include "../RenderComponent/SceneComponent.h"

using namespace iCAX::Core;

namespace iCAX
{
    namespace Render
    {
        /**
        * @brief 渲染服务接口
        * @details
        * IRenderService 定义了渲染模块的统一接口。
        * 任何具体渲染实现（OpenGL、Vulkan、DirectX）都需继承此接口并实现其方法。
        *
        * 渲染服务与 ECS 系统解耦：
        * - 所有渲染数据由 RenderScene 提供。
        * - 支持多窗口 / 多场景渲染。
        * - 上层只需提供窗口句柄或共享纹理，渲染服务负责内部资源管理和刷新。
        */
        struct _RENDERSERVICE_EXP IRenderService : public iCAX::Services::IService
        {
        public:
            /**
            * @brief 析构函数
            * @details 确保派生类资源正确释放。
            */
            virtual ~IRenderService() = default;

        public:
            /**
            * @brief 判断渲染服务中是否包含指定场景
            * @param [in] SceneID_ 场景唯一标识
            * @return true 如果场景已存在，false 否则
            * @note
            * - 可用于避免重复绑定场景。
            */
            virtual bool HasScene(IN const iCAX::Data::uuid& SceneID_) = 0;

            /*
            * @brief 创建场景
            * @param [in] SceneID_
            */
            virtual void CreateScene(IN const iCAX::Data::uuid& SceneID_) = 0;

            /*
            * @brief 销毁场景
            * @param [in] SceneID_
            */
            virtual void DestoryScene(IN const iCAX::Data::uuid& SceneID_) = 0;

            /*
            * @brief 设置画布
            * @param [in] SceneID_
            * @param [in] Component_
            * @param [in] nVerion_
            * @return bool
            */
            virtual bool SetCanva(IN const iCAX::Data::uuid& SceneID_, IN const SceneComponent& Component_, IN const size_t& nVerion_) = 0;

            /*
            * @brief 销毁画布
            * @param [in] SceneID_
            */
            virtual void DestroyCanva(IN const iCAX::Data::uuid& SceneID_) = 0;

            /*
            * @brief 设置主相机
            * @param [in] SceneID_
            * @param [in] Camera_
            * @param [in] Transform_
            * @param [in] nVerion_
            */
            virtual void SetMainCamera(IN const iCAX::Data::uuid& SceneID_, IN const CameraComponent& Camera_, IN const TransformComponent& Transform_, IN const size_t& nVerion_) = 0;

            /*
            * @brief 移除主相机
            * @param [in] SceneID_ 场景ID
            */
            virtual void RemoveMainCamera(IN const iCAX::Data::uuid& SceneID_) = 0;

            /*
            * @brief 设置环境信息
            * @param [in] SceneID_
            * @param [in] Component_
            * @param [in] nVerion_
            */
            virtual void SetEnviroment(IN const iCAX::Data::uuid& SceneID_, IN const EnvironmentComponent& Env_, IN const size_t& nVerion_) = 0;

            /*
            * @brief 增加光源
            * @param [in] SceneID_
            * @param [in] Component_
            * @param [in] nVerion_
            */
            virtual void SetLight(IN const iCAX::Data::uuid& SceneID_, IN const iCAX::Data::uuid& ID_, IN const LightComponent& Light_, IN const TransformComponent& Tranform, IN const size_t& nVerion_) = 0;

            /*
            * @brief 移除光源
            * @param [in] ID_
            */
            virtual void RemoveLight(IN const iCAX::Data::uuid& SceneID_, IN const iCAX::Data::uuid& ID_) = 0;

            /*
            * @brief 增加实体
            * @param [in] SceneID_
            * @param [in] pLight_
            */
            virtual void SetEntity(IN const iCAX::Data::uuid& SceneID_, IN const iCAX::Data::uuid& ID_, IN const TransformComponent& Transform_, IN const LayerComponent& Layer_, IN const MeshFilterComponent& Meshes_, IN const MeshRenderComponent& Meterials_, IN const size_t& nVersions_) = 0;

            /*
            * @brief 移除实体
            * @param [in] SceneID_
            * @param [in] pLight_
            */
            virtual void RemoveEntity(IN const iCAX::Data::uuid& SceneID_, IN const iCAX::Data::uuid& ID_) = 0;

            /*
            * @brief 渲染特定场景
            * @param [in] SceneID_
            */
            virtual void RenderScene(IN const iCAX::Data::uuid& SceneID_) = 0;

            /*
            * @brief 设置下一帧保存图片位置
            * @param [in] SceneID_
            * @param [in] strPath_
            */
            virtual void SetNextFrameImagePath(IN const iCAX::Data::uuid& SceneID_, IN const std::string& strPath_) = 0;

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
            virtual void RenderAll() = 0;
        };
    }
}
