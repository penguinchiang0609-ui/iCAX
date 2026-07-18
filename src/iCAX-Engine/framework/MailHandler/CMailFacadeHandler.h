#pragma once

#include "MailHandlerExport.h"

#include <ApplicationContext/IApplicationContext.h>
#include <Facades/FacadeInvoker.h>
#include <Mailbox/Mail.h>
#include <Mailbox/MailPostOffice.h>
#include <ProductContext/IProductContext.h>
#include <ProjectContext/IProjectContext.h>
#include <ProjectContext/ISceneContext.h>

#include <cstddef>
#include <cstdint>
#include <functional>

namespace iCAX
{
    namespace MailHandler
    {
        /*
        * @brief Mailbox 到 Facade 调用模型的适配器。
        * @details
        *   CMailFacadeHandler 不拥有 PostOffice、FacadeInvoker 或上下文。
        *   它只负责把收到的 Mail 转成 FacadeCall，调用 FacadeInvoker，
        *   再把 FacadeResult 写回同一个 PostOffice。
        */
        class _MAIL_HANDLER_EXP CMailFacadeHandler final
        {
        public:
            using MailIDAllocator = std::function<uint64_t()>;

        public:
            CMailFacadeHandler() = default;
            ~CMailFacadeHandler() = default;

            CMailFacadeHandler(IN const CMailFacadeHandler&) = delete;
            CMailFacadeHandler& operator=(IN const CMailFacadeHandler&) = delete;

        public:
            /*
            * @brief 把一封 Mail 转换成 FacadeCall。
            * @param [in] Mail_ 源邮件。
            * @return Facade 调用；调用 ID、来源 ID、方法和 Payload 来自邮件。
            * @throws std::invalid_argument 当邮件声明非空 Payload 但 pData 为空时抛出。
            */
            iCAX::Interaction::CFacadeCall ToFacadeCall(IN const iCAX::Mail::Mail& Mail_) const;

            /*
            * @brief 把 Facade 调用状态映射成邮件状态戳。
            * @param [in] Status_ Facade 调用状态。
            * @return 对应邮件状态戳。
            */
            iCAX::Mail::StampCode ToMailStamp(IN iCAX::Interaction::EFacadeCallStatus Status_) const noexcept;

            /*
            * @brief 分发当前 PostOffice 中已经到达的全部邮件。
            * @param [in] PostOffice_ 当前运行体后端视角邮局；从它收请求，也通过它发送响应。
            * @param [in] Invoker_ Facade 调用器。
            * @param [in] ApplicationContext_ 应用上下文。
            * @param [in] pProductContext_ 产品上下文；应用级调用可为空。
            * @param [in] pProjectContext_ 项目上下文；非项目调用可为空。
            * @param [in] pSceneContext_ 场景上下文；非场景调用可为空。
            * @param [in] AllocateResponseMailID_ 响应邮件 ID 分配器，不能为空。
            * @return 已处理邮件数量。
            * @details 单封邮件的调用异常会被转换为失败结果，避免 UI 请求超时或运行体被单次调用打断。
            */
            size_t HandleAvailableMails(
                IN const iCAX::Mail::CMailPostOffice& PostOffice_,
                IN const iCAX::Interaction::CFacadeInvoker& Invoker_,
                IN iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_,
                IN iCAX::Project::ISceneContext* pSceneContext_,
                IN const MailIDAllocator& AllocateResponseMailID_) const;

        private:
            void SendFacadeResult(
                IN const iCAX::Mail::CMailPostOffice& PostOffice_,
                IN const iCAX::Mail::Mail& RequestMail_,
                IN const iCAX::Interaction::CFacadeResult& Result_,
                IN uint64_t nResponseMailID_) const;
        };
    }
}
