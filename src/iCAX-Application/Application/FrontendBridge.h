#pragma once

#include "ApplicationExport.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace iCAX
{
    namespace ApplicationHost
    {
        class CApplicationHost;
    }

    namespace Application
    {
        /*
        * @brief UI 与 Engine 之间传递的邮件信封。
        * @details
        *   该结构只表达跨 UI 边界的通用 mail envelope，不绑定 H5/CEF/Qt/WPF。
        *   PayloadText 是 UTF-8 文本，通常是 Data::VariantSerializer 产生的文本。
        */
        struct _APPLICATION_EXP CFrontendMailEnvelope final
        {
            std::string ChannelID;    //!< 目标 channel id。
            uint64_t nID = 0;         //!< 本封邮件 ID，由 UI 侧分配。
            uint64_t nOriginID = 0;   //!< 响应来源 ID；请求和主动事件为 0。
            uint64_t nTypeCode = 0;   //!< CommandHandler 64 位主/子命令路由码。
            uint16_t nStamp = 0;      //!< Mail::StampCode 数值。
            std::string PayloadText;  //!< UTF-8 文本 payload。
        };

        using FrontendMailHandler = std::function<void(const CFrontendMailEnvelope&)>;

        /*
        * @brief 前端桥接器。
        * @details
        *   CFrontendBridge 是 Application 级别的 UI 桥，不属于任何具体 UI 技术。
        *   它只负责把 UI mail envelope 投递到 Engine 的 mailbox，并从 Engine frontend post office
        *   取出 response/event。CEF、Qt、WPF 等 UI 宿主都应复用该桥。
        */
        class _APPLICATION_EXP CFrontendBridge final
        {
        public:
            CFrontendBridge();
            ~CFrontendBridge();

            CFrontendBridge(IN const CFrontendBridge&) = delete;
            CFrontendBridge& operator=(IN const CFrontendBridge&) = delete;

        public:
            /*
            * @brief 绑定已经启动的 ApplicationHost。
            * @param [in] Engine_ 已启动的 Engine 后台宿主。
            * @throws std::logic_error Engine 未运行时抛出。
            * @details Attach 后会缓存 application channel 的 frontend post office。
            */
            void Attach(IN iCAX::ApplicationHost::CApplicationHost& Engine_);

            /*
            * @brief 解除 Engine 绑定，并清空所有已缓存 post office。
            */
            void Detach();

            /*
            * @brief 当前是否已经绑定 Engine。
            */
            bool IsAttached() const;

            /*
            * @brief 获取应用级 channel id 文本。
            */
            std::string GetApplicationChannelIDText() const;

            /*
            * @brief 登记产品通信通道。
            * @param [in] strProductID_ 产品 ID。
            * @return 产品 channel id 文本。
            * @details 该方法只缓存 post office，不负责启动产品；产品启动应走 App.StartProduct 命令。
            */
            std::string RegisterProductChannel(IN const std::string& strProductID_);

            /*
            * @brief 登记项目通信通道。
            * @param [in] strProjectID_ 项目 ID 文本。
            * @return 项目 channel id 文本。
            */
            std::string RegisterProjectChannel(IN const std::string& strProjectID_);

            /*
            * @brief 从 UI 向指定 Engine mail channel 投递邮件。
            * @param [in] Envelope_ 邮件信封。
            */
            void PostMail(IN const CFrontendMailEnvelope& Envelope_);

            /*
            * @brief 轮询所有已登记 frontend post office，取出 Engine 发给 UI 的邮件。
            * @return 本轮收到的邮件。
            */
            std::vector<CFrontendMailEnvelope> PollMails();

            /*
            * @brief 设置收到 Engine 邮件时的回调。
            * @details 如果设置了回调，PollMails 仍返回邮件，同时逐封调用回调。
            */
            void SetMailHandler(IN FrontendMailHandler Handler_);

        private:
            class Impl;
            std::unique_ptr<Impl> m_pImpl;
        };
    }
}
