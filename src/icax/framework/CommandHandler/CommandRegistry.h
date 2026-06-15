#pragma once

#include "ICommandHandler.h"

#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace iCAX
{
    namespace Command
    {
        class _COMMAND_HANDLER_EXP CCommandRegistry final
        {
        public:
            CCommandRegistry() = default;
            ~CCommandRegistry() = default;

            CCommandRegistry(IN const CCommandRegistry&) = delete;
            CCommandRegistry& operator=(IN const CCommandRegistry&) = delete;

        public:
            bool Register(IN uint64_t nTypeCode_, IN std::shared_ptr<ICommandHandler> pHandler_);
            void Set(IN uint64_t nTypeCode_, IN std::shared_ptr<ICommandHandler> pHandler_);
            bool Unregister(IN uint64_t nTypeCode_);
            bool Has(IN uint64_t nTypeCode_) const;
            std::shared_ptr<ICommandHandler> Find(IN uint64_t nTypeCode_) const;
            std::vector<uint64_t> GetTypeCodes() const;
            void Clear();

        private:
            static void ValidateHandler(IN const std::shared_ptr<ICommandHandler>& pHandler_);

        private:
            mutable std::mutex m_Mutex;
            std::map<uint64_t, std::shared_ptr<ICommandHandler>> m_mapHandlers;
        };
    }
}
