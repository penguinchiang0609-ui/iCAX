#pragma once

#include "CommandMessage.h"
#include "ICommandContext.h"

namespace iCAX
{
    namespace Command
    {
        /*
        * @brief 命令处理器接口。
        * @details
        *   一个 handler 处理一个或一组 typeCode。它只依赖 ICommandContext，
        *   不应直接假设命令来自 ApplicationHost、Product 或 Project 邮箱。
        */
        class _COMMAND_HANDLER_EXP ICommandHandler
        {
        public:
            ICommandHandler() = default;
            virtual ~ICommandHandler() = default;

            ICommandHandler(IN const ICommandHandler&) = delete;
            ICommandHandler& operator=(IN const ICommandHandler&) = delete;

        public:
            /*
            * @brief 执行命令。
            * @param [in] Request_ 命令请求。
            * @param [in,out] Context_ 命令上下文，提供运行时依赖。
            * @return 命令响应。
            * @throws std::invalid_argument 请求非法时可抛出，Dispatcher 会转换为 InvalidRequest。
            * @throws std::exception 其他执行失败会转换为 ExecutionError。
            */
            virtual CCommandResponse Handle(IN const CCommandRequest& Request_, IN ICommandContext& Context_) = 0;
        };
    }
}
