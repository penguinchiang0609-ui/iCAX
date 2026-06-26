#pragma once

#include "WebPageHostExport.h"
#include "Application/FrontendBridge.h"

#include <mutex>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Frontend
    {
        /*
        * @brief H5 与 WebPageHost 之间传递的邮件信封。
        * @details
        *   CWebPageHost 只作为 H5 侧 adapter，真正的通用 envelope 定义在 iCAX-Application。
        */
        using CWebPageMailEnvelope = iCAX::Application::CFrontendMailEnvelope;

        using WebPageMailHandler = iCAX::Application::FrontendMailHandler;

        /*
        * @brief WebPageHost 配置。
        */
        struct _WEB_PAGE_HOST_EXP CWebPageHostConfig final
        {
            iCAX::Application::CFrontendBridge* pFrontendBridge = nullptr; //!< Application 提供的通用前端桥，非拥有。
            std::string WebPageRoot; //!< AppShell 根目录，供 CEF/宿主适配器加载 index.html。
        };

        /*
        * @brief WebPage 原生宿主核心。
        * @details
        *   该类只处理 H5 宿主和通用 FrontendBridge 的适配，不拥有 Engine。
        *   CEF/WebView 等宿主适配器负责窗口和 JS 绑定，然后调用这里的 Start/Post/Poll 方法。
        */
        class _WEB_PAGE_HOST_EXP CWebPageHost final
        {
        public:
            CWebPageHost();
            ~CWebPageHost();

            CWebPageHost(const CWebPageHost&) = delete;
            CWebPageHost& operator=(const CWebPageHost&) = delete;

        public:
            /*
            * @brief 设置宿主配置。
            * @param [in] Config_ 宿主配置。
            * @throws std::logic_error 宿主已启动时抛出。
            */
            void SetConfig(const CWebPageHostConfig& Config_);

            /*
            * @brief 启动 H5 bridge adapter。
            * @details 只校验 FrontendBridge 是否已由 Application 绑定到 Engine，不启动 Engine。
            */
            void Start();

            /*
            * @brief 停止 H5 bridge adapter。
            * @details 不停止 Engine。
            */
            void Stop();

            /*
            * @brief 当前 adapter 是否运行。
            */
            bool IsRunning() const;

            /*
            * @brief 获取应用级 channel id 文本。
            */
            std::string GetApplicationChannelIDText() const;

            /*
            * @brief 登记产品通信通道。
            * @param [in] strProductID_ 产品 ID。
            * @return 产品 channel id 文本。
            * @details H5 调用 App.StartProduct 后可用响应中的 productChannelId；CEF/宿主适配器也可以调用该方法显式缓存 post office。
            */
            std::string RegisterProductChannel(const std::string& strProductID_);

            /*
            * @brief 登记项目通信通道。
            * @param [in] strProjectID_ 项目 ID 文本。
            * @return 项目 channel id 文本。
            */
            std::string RegisterProjectChannel(const std::string& strProjectID_);

            /*
            * @brief 从 H5 向指定 mail channel 投递邮件。
            * @param [in] Envelope_ 邮件信封。
            */
            void PostMail(const CWebPageMailEnvelope& Envelope_);

            /*
            * @brief 轮询所有已登记 frontend post office，取出 backend 发给 H5 的邮件。
            * @return 本轮收到的邮件。
            * @details CEF/宿主适配器可在 UI 线程定时调用，也可在消息泵空闲时调用。
            */
            std::vector<CWebPageMailEnvelope> PollMails();

            /*
            * @brief 设置收到 backend 邮件时的回调。
            * @details 如果设置了回调，PollMails 仍返回邮件，同时逐封调用回调，方便 CEF/宿主适配器直接推送 JS。
            */
            void SetMailHandler(WebPageMailHandler Handler_);

        private:
            iCAX::Application::CFrontendBridge& RequireBridge() const;

        private:
            mutable std::mutex m_Mutex;
            CWebPageHostConfig m_Config;
            bool m_bStarted = false;
        };
    }
}

