#pragma once

#include "UIContainerExport.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Frontend
    {
        /*
        * @brief UI 与 Engine 之间传递的通用邮件信封。
        * @details
        *   该结构只表达跨 UI 边界的 mail envelope，不绑定 H5/CEF/WPF/QT 等具体前端技术。
        *   PayloadText 是 UTF-8 文本，通常是 Data::VariantSerializer 产生的文本。
        */
        struct _UI_CONTAINER_EXP CFrontendMailEnvelope final
        {
            std::string ChannelID;    //!< 目标 channel id。
            uint64_t nID = 0;         //!< 本封邮件 ID，由 UI 侧分配。
            uint64_t nOriginID = 0;   //!< 响应来源 ID；请求和主动事件为 0。
            uint64_t nTypeCode = 0;   //!< FacadeName.MethodName 的 64 位紧凑编码。
            uint16_t nStamp = 0;      //!< Mail::StampCode 数值。
            std::string PayloadText;  //!< UTF-8 文本 payload。
        };

        using FrontendMailHandler = std::function<void(const CFrontendMailEnvelope&)>;

        /*
        * @brief UI 容器依赖的前端桥接口。
        * @details
        *   UI 容器不能反向链接 Application.exe。Application.exe 持有具体实现，
        *   并通过该接口把 UI 邮件投递到 Engine。
        */
        class _UI_CONTAINER_EXP IFrontendBridge
        {
        public:
            virtual ~IFrontendBridge() = default;

        public:
            virtual bool IsAttached() const = 0;
            virtual std::string GetApplicationChannelIDText() const = 0;
            virtual std::string RegisterProductChannel(const std::string& strProductID_) = 0;
            virtual std::string RegisterSceneChannel(
                const std::string& strProjectID_,
                const std::string& strSceneID_) = 0;
            virtual void PostMail(const CFrontendMailEnvelope& Envelope_) = 0;
            virtual std::vector<CFrontendMailEnvelope> PollMails() = 0;
            virtual void SetMailHandler(FrontendMailHandler Handler_) = 0;
        };
    }
}
