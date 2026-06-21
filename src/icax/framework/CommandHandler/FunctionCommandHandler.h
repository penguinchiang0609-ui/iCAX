#pragma once

#include "ICommandHandler.h"

#include <functional>

namespace iCAX
{
    namespace Command
    {
        /*
        * @brief 函数式命令处理器。
        * @details 用于把 lambda/std::function 快速挂到 CCommandRegistry，适合内建命令。
        */
        class _COMMAND_HANDLER_EXP CFunctionCommandHandler final : public ICommandHandler
        {
        public:
            /*
            * @brief 命令处理函数类型。
            */
            using HandlerFunc = std::function<CCommandResponse(const CCommandRequest&, ICommandContext&)>;

            /*
            * @brief 构造函数式处理器。
            * @param [in] Func_ 处理函数，不能为空。
            */
            explicit CFunctionCommandHandler(IN HandlerFunc Func_);
            ~CFunctionCommandHandler() override = default;

        public:
            /*
            * @brief 调用构造时传入的处理函数。
            */
            CCommandResponse Handle(IN const CCommandRequest& Request_, IN ICommandContext& Context_) override;

        private:
            HandlerFunc m_Func;
        };
    }
}
