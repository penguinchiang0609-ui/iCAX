#pragma once

#include "CommandRegistry.h"

namespace iCAX
{
    namespace Command
    {
        /*
        * @brief 命令分发器。
        * @details
        *   Dispatcher 只根据 Request.nTypeCode 查找 handler 并执行。
        *   它负责把常见异常转换为 CCommandResponse，不绑定 mailbox 或任何 UI 技术。
        */
        class _COMMAND_HANDLER_EXP CCommandDispatcher final
        {
        public:
            /*
            * @brief 构造分发器。
            * @param [in] pRegistry_ 命令注册表，不能为空。
            */
            explicit CCommandDispatcher(IN std::shared_ptr<CCommandRegistry> pRegistry_);
            ~CCommandDispatcher() = default;

            CCommandDispatcher(IN const CCommandDispatcher&) = delete;
            CCommandDispatcher& operator=(IN const CCommandDispatcher&) = delete;

        public:
            /*
            * @brief 分发命令。
            * @param [in] Request_ 命令请求。
            * @param [in,out] Context_ 命令上下文。
            * @return 命令响应；找不到 handler 时返回 NoHandler。
            * @details
            *   handler 抛出 std::invalid_argument 会转换为 InvalidRequest；
            *   其他异常会转换为 ExecutionError。
            */
            CCommandResponse Dispatch(IN const CCommandRequest& Request_, IN ICommandContext& Context_) const;

        private:
            std::shared_ptr<CCommandRegistry> m_pRegistry;
        };
    }
}
