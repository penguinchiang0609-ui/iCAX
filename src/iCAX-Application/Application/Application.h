#pragma once

#include "ApplicationExport.h"
#include "FrontendBridge.h"
#include "ApplicationHost/ApplicationHost.h"

#include <mutex>
#include <string>

namespace iCAX
{
    namespace Application
    {
        /*
        * @brief 进程级 Application 配置。
        * @details EngineConfig 由启动层准备，产品 manifest 应在进入 CApplication 前完成解析。
        */
        struct _APPLICATION_EXP CApplicationConfig final
        {
            iCAX::ApplicationHost::ApplicationHostConfig EngineConfig;
            std::string UIName; //!< 可选 UI 名称，例如 H5/Qt/WPF，仅用于上层标识。
        };

        /*
        * @brief iCAX 进程级应用容器。
        * @details
        *   CApplication 是 Engine 和 UI 之间更大的拥有者。
        *   它拥有 ApplicationHost 和 FrontendBridge；具体 UI 宿主只接入 FrontendBridge，
        *   不再直接创建或停止 Engine。
        */
        class _APPLICATION_EXP CApplication final
        {
        public:
            CApplication();
            ~CApplication();

            CApplication(IN const CApplication&) = delete;
            CApplication& operator=(IN const CApplication&) = delete;

        public:
            /*
            * @brief 设置 Application 配置。
            * @param [in] Config_ 新配置。
            * @throws std::logic_error Application 已启动时抛出。
            */
            void SetConfig(IN const CApplicationConfig& Config_);

            /*
            * @brief 启动 Engine 并把 FrontendBridge 绑定到 Engine。
            */
            void Start();

            /*
            * @brief 停止 Engine 并解除 FrontendBridge 绑定。
            */
            void Stop();

            /*
            * @brief 当前 Application 是否运行。
            */
            bool IsRunning() const;

            /*
            * @brief 获取 Engine 后台宿主。
            */
            iCAX::ApplicationHost::CApplicationHost& Engine();
            const iCAX::ApplicationHost::CApplicationHost& Engine() const;

            /*
            * @brief 获取 UI 通用桥接器。
            */
            CFrontendBridge& Frontend();
            const CFrontendBridge& Frontend() const;

        private:
            mutable std::mutex m_Mutex;
            CApplicationConfig m_Config;
            iCAX::ApplicationHost::CApplicationHost m_Engine;
            CFrontendBridge m_FrontendBridge;
            bool m_bStarted = false;
        };
    }
}
