#pragma once

#include "CefWebPageHostExport.h"
#include "framework.h"
#include "WebPageHost/WebPageHost.h"

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
            *   CEF 的子进程入口必须在宿主 EXE 早期调用 ExecuteSubProcess。
            *   InitializeRuntime 只负责初始化当前主进程的 CEF runtime。
            */
            struct _CEF_WEB_PAGE_HOST_EXP CCefRuntimeConfig final
            {
                HINSTANCE hInstance = nullptr;              //!< 宿主进程 HINSTANCE。
                std::wstring BrowserSubprocessPath;          //!< 可选 CEF 子进程路径；为空时使用当前 EXE。
                std::wstring CachePath;                      //!< CEF cache 目录。
                std::wstring UserDataPath;                   //!< CEF user data 目录。
                bool bMultiThreadedMessageLoop = true;       //!< Windows 桌面宿主默认使用 CEF 多线程消息循环。
                bool bWindowlessRenderingEnabled = false;    //!< 当前阶段默认使用原生子窗口。
            };

            /*
            * @brief CEF WebPageHost 启动配置。
            */
            struct _CEF_WEB_PAGE_HOST_EXP CCefWebPageHostConfig final
            {
                CWebPageHostConfig CoreConfig;               //!< H5 到 Application FrontendBridge 的适配配置。
                HINSTANCE hInstance = nullptr;                //!< 宿主进程 HINSTANCE。
                HWND hParentWnd = nullptr;                    //!< CEF 浏览器父窗口；为空时创建 popup 窗口。
                int nX = 0;                                   //!< 子窗口左上角 X。
                int nY = 0;                                   //!< 子窗口左上角 Y。
                int nWidth = 1280;                            //!< 子窗口宽度。
                int nHeight = 720;                            //!< 子窗口高度。
                int nMailPollIntervalMS = 16;                 //!< backend -> JS 邮件轮询间隔。
                std::wstring StartURL;                        //!< 可选启动 URL；为空时加载 CoreConfig.WebPageRoot/index.html。
            };

            /*
            * @brief CEF 版 WebPageHost。
            * @details
            *   本类是 CEF adapter，不替代 CWebPageHost。它持有 CWebPageHost，
            *   并把 CEF/JS bridge 调用转发到核心宿主。
            */
            class _CEF_WEB_PAGE_HOST_EXP CCefWebPageHost final
            {
            public:
                CCefWebPageHost();
                ~CCefWebPageHost();

                CCefWebPageHost(const CCefWebPageHost&) = delete;
                CCefWebPageHost& operator=(const CCefWebPageHost&) = delete;

            public:
                /*
                * @brief 执行 CEF 子进程入口。
                * @return CEF 子进程返回值；主进程返回 -1。
                * @details 宿主 EXE 必须在进入主逻辑前调用它。
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

                /*
                * @brief 启动 CEF 浏览器和 H5 bridge adapter。
                * @details Engine 由 Application 拥有，本方法不启动 Engine。
                */
                void Start(const CCefWebPageHostConfig& Config_);

                /*
                * @brief 关闭浏览器并停止 H5 bridge adapter。
                * @details 本方法不停止 Engine。
                */
                void Stop();

                /*
                * @brief 当前宿主是否运行。
                */
                bool IsRunning() const;

                /*
                * @brief 手动轮询一次 Engine 发给 H5 的邮件。
                * @details Start 后会有内部轮询线程；该方法主要用于集成测试或外部消息泵手动驱动。
                */
                void PollMails();

                /*
                * @brief 获取内部 CWebPageHost。
                */
                CWebPageHost& CoreHost();

            private:
                class Impl;
                std::unique_ptr<Impl> m_pImpl;
            };
        }
    }
}
