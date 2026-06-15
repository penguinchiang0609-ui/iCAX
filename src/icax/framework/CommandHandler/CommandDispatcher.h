#pragma once

#include "CommandRegistry.h"

namespace iCAX
{
    namespace Command
    {
        class _COMMAND_HANDLER_EXP CCommandDispatcher final
        {
        public:
            explicit CCommandDispatcher(IN std::shared_ptr<CCommandRegistry> pRegistry_);
            ~CCommandDispatcher() = default;

            CCommandDispatcher(IN const CCommandDispatcher&) = delete;
            CCommandDispatcher& operator=(IN const CCommandDispatcher&) = delete;

        public:
            CCommandResponse Dispatch(IN const CCommandRequest& Request_, IN ICommandContext& Context_) const;

        private:
            std::shared_ptr<CCommandRegistry> m_pRegistry;
        };
    }
}
