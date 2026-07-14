#pragma once

#include "MailHandlerExport.h"

#include <ApplicationContext/IApplicationContext.h>
#include <CommandTargets/CommandDispatcher.h>
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
        * @brief Mailbox 到 CommandTargets 的桥接器。
        * @details
        *   CMailCommandHandler 不拥有 PostOffice、CommandDispatcher 或上下文。
        *   它只负责把收到的 Mail 转成 CommandRequest，调用 CommandDispatcher，
        *   再把 CommandResponse 写回同一个 PostOffice。
        */
        class _MAIL_HANDLER_EXP CMailCommandHandler final
        {
        public:
            using MailIDAllocator = std::function<uint64_t()>;

        public:
            CMailCommandHandler() = default;
            ~CMailCommandHandler() = default;

            CMailCommandHandler(IN const CMailCommandHandler&) = delete;
            CMailCommandHandler& operator=(IN const CMailCommandHandler&) = delete;

        public:
            /*
            * @brief 把一封 Mail 转换成 CommandRequest。
            * @param [in] Mail_ 源邮件。
            * @return 命令请求；请求 ID、来源 ID、路由和 Payload 来自邮件。
            * @throws std::invalid_argument 当邮件声明非空 Payload 但 pData 为空时抛出。
            */
            iCAX::Command::CCommandRequest ToCommandRequest(IN const iCAX::Mail::Mail& Mail_) const;

            /*
            * @brief 把命令状态映射成邮件状态戳。
            * @param [in] Status_ 命令执行状态。
            * @return 对应邮件状态戳。
            */
            iCAX::Mail::StampCode ToMailStamp(IN iCAX::Command::ECommandStatusCode Status_) const noexcept;

            /*
            * @brief 分发当前 PostOffice 中已经到达的全部邮件。
            * @param [in] PostOffice_ 当前运行体后端视角邮局；从它收请求，也通过它发送响应。
            * @param [in] Dispatcher_ 命令分发器。
            * @param [in] ApplicationContext_ 应用上下文。
            * @param [in] pProductContext_ 产品上下文；应用级命令可为空。
            * @param [in] pProjectContext_ 项目上下文；非项目命令可为空。
            * @param [in] pSceneContext_ 场景上下文；非场景命令可为空。
            * @param [in] AllocateResponseMailID_ 响应邮件 ID 分配器，不能为空。
            * @return 已处理邮件数量。
            * @details 单封邮件的命令异常会被转换为失败响应，避免 UI 请求超时或运行体被单个请求打断。
            */
            size_t DispatchAvailableMails(
                IN const iCAX::Mail::CMailPostOffice& PostOffice_,
                IN const iCAX::Command::CCommandDispatcher& Dispatcher_,
                IN iCAX::Application::IApplicationContext& ApplicationContext_,
                IN iCAX::Product::IProductContext* pProductContext_,
                IN iCAX::Project::IProjectContext* pProjectContext_,
                IN iCAX::Project::ISceneContext* pSceneContext_,
                IN const MailIDAllocator& AllocateResponseMailID_) const;

        private:
            void SendCommandResponse(
                IN const iCAX::Mail::CMailPostOffice& PostOffice_,
                IN const iCAX::Mail::Mail& RequestMail_,
                IN const iCAX::Command::CCommandResponse& Response_,
                IN uint64_t nResponseMailID_) const;
        };
    }
}
