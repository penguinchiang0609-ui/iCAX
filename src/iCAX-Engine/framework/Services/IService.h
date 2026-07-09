#pragma once
#include "Services.h"

namespace iCAX
{
    namespace Application
    {
        class IApplicationContext;
    }

    namespace Product
    {
        class IProductContext;
    }

    namespace Project
    {
        class IProjectContext;
        class ISceneContext;
    }

    namespace Services
    {
        /*
        * @brief 服务接口
        */
        class _SERVICE_EXP IService
        {
        public:
            /*
            * @brief 构造函数
            */
            IService() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IService() = default;

        public:
            /*
            * @brief 加载
            * @details
            *   服务需要的相关资源初始化，保证就绪
            */
            virtual void OnLoad() = 0;

            /*
            * @brief 卸载
            * @details
            *   释放服务占用的资源
            */
            virtual void OnUnload() = 0;

            /*
            * @brief Scene 帧刷新钩子。
            * @param [in] ApplicationContext_ 应用上下文。
            * @param [in] ProductContext_ 产品上下文。
            * @param [in,out] ProjectContext_ 当前项目上下文。
            * @param [in,out] SceneContext_ 当前场景上下文。
            * @param [in] nDeltaTime_ 当前帧间隔，单位秒。
            * @param [in] nTotalTime_ 当前 Scene 累计运行时间，单位秒。
            * @details
            *   Service 实例可以是应用级共享对象，但该钩子的语义永远是“只处理传入 Scene 的一帧”。
            *   服务内部如需保存运行态数据，必须按 ProjectID + SceneID 分区，不能在这里扫描或修改其他 Scene。
            *   默认实现为空；只有真正需要帧刷新的服务才重写该函数。
            */
            virtual void OnSceneTick(
                IN const iCAX::Application::IApplicationContext& ApplicationContext_,
                IN const iCAX::Product::IProductContext& ProductContext_,
                IN iCAX::Project::IProjectContext& ProjectContext_,
                IN iCAX::Project::ISceneContext& SceneContext_,
                IN const double& nDeltaTime_,
                IN const double& nTotalTime_)
            {
                (void)ApplicationContext_;
                (void)ProductContext_;
                (void)ProjectContext_;
                (void)SceneContext_;
                (void)nDeltaTime_;
                (void)nTotalTime_;
            }
        };
    }
}
