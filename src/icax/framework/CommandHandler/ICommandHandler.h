#pragma once

#include "CommandMessage.h"
#include "ICommandContext.h"

namespace iCAX
{
    namespace Command
    {
        class _COMMAND_HANDLER_EXP ICommandHandler
        {
        public:
            ICommandHandler() = default;
            virtual ~ICommandHandler() = default;

            ICommandHandler(IN const ICommandHandler&) = delete;
            ICommandHandler& operator=(IN const ICommandHandler&) = delete;

        public:
            virtual CCommandResponse Handle(IN const CCommandRequest& Request_, IN ICommandContext& Context_) = 0;
        };
    }
}
