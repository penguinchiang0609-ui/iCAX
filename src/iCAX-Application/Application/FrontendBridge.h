#pragma once

#include "UIContainer/FrontendBridgeContract.h"

#include <memory>
#include <string>

namespace iCAX
{
    namespace ApplicationHost
    {
        class CApplicationHost;
    }

    namespace Application
    {
        using CFrontendMailEnvelope = iCAX::Frontend::CFrontendMailEnvelope;
        using FrontendMailHandler = iCAX::Frontend::FrontendMailHandler;

        /*
        * @brief 前端桥接器。
        * @details
        *   CFrontendBridge 是 Application 级别的 UI 桥，不属于任何具体 UI 技术。
        *   它只负责把 UI mail envelope 投递到 Engine 的 mailbox，并从 Engine frontend post office
        *   取出 response/event。CEF、Qt、WPF 等 UI 宿主都应复用该桥。
        */
        class CFrontendBridge final : public iCAX::Frontend::IFrontendBridge
        {
        public:
            CFrontendBridge();
            ~CFrontendBridge();

            CFrontendBridge(const CFrontendBridge&) = delete;
            CFrontendBridge& operator=(const CFrontendBridge&) = delete;

        public:
            /*
            * @brief 绑定已经启动的 ApplicationHost。
            * @param [in] Engine_ 已启动的 Engine 后台宿主。
            * @throws std::logic_error Engine 未运行时抛出。
            * @details Attach 后会缓存 application channel 的 frontend post office。
            */
            void Attach(iCAX::ApplicationHost::CApplicationHost& Engine_);

            /*
            * @brief 解除 Engine 绑定，并清空所有已缓存 post office。
            */
            void Detach();

            /*
            * @brief 当前是否已经绑定 Engine。
            */
            bool IsAttached() const override;

            /*
            * @brief 获取应用级 channel id 文本。
            */
            std::string GetApplicationChannelIDText() const override;

            /*
            * @brief 登记产品通信通道。
            * @param [in] strProductID_ 产品 ID。
            * @return 产品 channel id 文本。
            * @details 该方法只缓存 post office，不负责启动产品；产品启动应走 App.StartProduct 命令。
            */
            std::string RegisterProductChannel(const std::string& strProductID_) override;

            /*
            * @brief 登记项目通信通道。
            * @param [in] strProjectID_ 项目 ID 文本。
            * @return 项目 channel id 文本。
            */
            std::string RegisterProjectChannel(const std::string& strProjectID_) override;

            /*
            * @brief 从 UI 向指定 Engine mail channel 投递邮件。
            * @param [in] Envelope_ 邮件信封。
            */
            void PostMail(const CFrontendMailEnvelope& Envelope_) override;

            /*
            * @brief 轮询所有已登记 frontend post office，取出 Engine 发给 UI 的邮件。
            * @return 本轮收到的邮件。
            */
            std::vector<CFrontendMailEnvelope> PollMails() override;

            /*
            * @brief 设置收到 Engine 邮件时的回调。
            * @details 如果设置了回调，PollMails 仍返回邮件，同时逐封调用回调。
            */
            void SetMailHandler(FrontendMailHandler Handler_) override;

        private:
            class Impl;
            std::unique_ptr<Impl> m_pImpl;
        };
    }
}
