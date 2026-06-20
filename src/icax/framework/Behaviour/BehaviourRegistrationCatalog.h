#pragma once

#include "System.h"

#include <functional>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Behaviour
    {
        class IBehaviourRegistry;

        class _SYSTEM_EXP CBehaviourRegistrationCatalog final
        {
        public:
            using ReplayFunc = std::function<void(IBehaviourRegistry&)>;
            struct RegistrationRecord final
            {
                std::string ModulePath;
                ReplayFunc Replay;
            };

        private:
            CBehaviourRegistrationCatalog() = delete;
            ~CBehaviourRegistrationCatalog() = delete;

        public:
            static void Register(IN ReplayFunc Func_);
            static void Register(IN ReplayFunc Func_, IN const void* pModuleAddress_);
            static void ReplayAll(IN IBehaviourRegistry& Registry_);
            static size_t ReplayFrom(IN size_t nFirstIndex_, IN IBehaviourRegistry& Registry_);
            static void ReplayByModulePaths(IN IBehaviourRegistry& Registry_, IN const std::vector<std::string>& ModulePaths_);
            static size_t Count();

        private:
            static std::vector<RegistrationRecord>& GetRegistrations();
        };
    }
}
