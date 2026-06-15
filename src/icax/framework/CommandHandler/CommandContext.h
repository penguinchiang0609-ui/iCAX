#pragma once

#include "ICommandContext.h"

#include <map>
#include <mutex>

namespace iCAX
{
    namespace Command
    {
        class _COMMAND_HANDLER_EXP CCommandContext final : public ICommandContext
        {
        public:
            CCommandContext() = default;
            ~CCommandContext() override = default;

            CCommandContext(IN const CCommandContext&) = delete;
            CCommandContext& operator=(IN const CCommandContext&) = delete;

        public:
            void SetDependencyUntyped(IN const std::type_index& Type_, IN std::shared_ptr<void> pValue_) override;
            std::shared_ptr<void> GetDependencyUntyped(IN const std::type_index& Type_) const override;
            bool HasDependency(IN const std::type_index& Type_) const;
            void RemoveDependency(IN const std::type_index& Type_);
            void Clear();

        private:
            mutable std::mutex m_Mutex;
            std::map<std::type_index, std::shared_ptr<void>> m_mapDependencies;
        };
    }
}
