#pragma once

#include "ICommandHandler.h"

#include <functional>

namespace iCAX
{
    namespace Command
    {
        class _COMMAND_HANDLER_EXP CFunctionCommandHandler final : public ICommandHandler
        {
        public:
            using HandlerFunc = std::function<CCommandResponse(const CCommandRequest&, ICommandContext&)>;

            explicit CFunctionCommandHandler(IN HandlerFunc Func_);
            ~CFunctionCommandHandler() override = default;

        public:
            CCommandResponse Handle(IN const CCommandRequest& Request_, IN ICommandContext& Context_) override;

        private:
            HandlerFunc m_Func;
        };
    }
}
