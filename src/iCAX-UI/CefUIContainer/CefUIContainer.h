#pragma once

#include "CefUIContainerExport.h"
#include "framework.h"
#include "UIContainer/UIContainer.h"

#include <memory>
#include <string>

namespace iCAX
{
    namespace Frontend
    {
        namespace Cef
        {
            /*
            * @brief CEF runtime 初始化配置。
            * @details
            *   CEF 子进程入口必须在宿主 EXE 早期调用 ExecuteSubProcess。
            *   主进程在 CCefUIContainer::Start 中会按需初始化 runtime。
            */
            struct _CEF_UI_CONTAINER_EXP CCefRuntimeConfig final
            {
                HINSTANCE hInstance = nullptr;              //!< 宿主进程 HINSTANCE。
                std::wstring BrowserSubprocessPath;          //!< 可选 CEF 子进程路径；为空时使用当前 EXE。
                std::wstring CachePath;                      //!< CEF cache 目录。
                std::wstring UserDataPath;                   //!< CEF user data 目录。
                std::wstring LogFile;                        //!< CEF 日志文件路径；为空时使用 CEF 默认路径。
                int nRemoteDebuggingPort = 0;                //!< DevTools 远程调试端口；0 表示不启用。
                bool bAllowFileAccessFromFiles = true;       //!< 允许 AppShell 以 file:// 方式加载 ES module。
                bool bDisableGPU = false;                    //!< 当前机器 GPU 子进程不稳定时可禁用硬件加速。
                bool bMultiThreadedMessageLoop = true;       //!< Windows 桌面宿主默认使用 CEF 多线程消息循环。
                bool bWindowlessRenderingEnabled = false;    //!< 当前阶段默认使用原生子窗口。
            };

            /*
            * @brief CEF UI 容器。
            * @details
            *   本类直接实现 IUIContainer。它负责 CEF runtime、浏览器窗口、JS bridge 注入、
            *   backend -> frontend Facade frame 轮询和 JS 派发，不再依赖额外的中间 adapter。
            */
            class _CEF_UI_CONTAINER_EXP CCefUIContainer final : public IUIContainer
            {
            public:
                CCefUIContainer();
                ~CCefUIContainer() override;

                CCefUIContainer(const CCefUIContainer&) = delete;
                CCefUIContainer& operator=(const CCefUIContainer&) = delete;

            public:
                /*
                * @brief 执行 CEF 子进程入口。
                * @return CEF 子进程返回值；主进程返回 -1。
                */
                static int ExecuteSubProcess(HINSTANCE hInstance_);

                /*
                * @brief 初始化 CEF runtime。
                * @throws std::runtime_error 初始化失败时抛出。
                */
                static void InitializeRuntime(const CCefRuntimeConfig& Config_);

                /*
                * @brief 关闭 CEF runtime。
                */
                static void ShutdownRuntime();

                void SetConfig(const CUIContainerConfig& Config_) override;
                void Start() override;
                void Stop() override;
                void WaitForExit() override;
                bool IsRunning() const override;

                /*
                * @brief 手动轮询一次 Engine 发给 H5 的 Facade frame。
                * @details Start 后会有内部轮询线程；该方法主要用于集成测试或外部消息泵手动驱动。
                */
                void PollFacadeFrames();

            private:
                class Impl;
                std::unique_ptr<Impl> m_pImpl;
            };
        }
    }
}
