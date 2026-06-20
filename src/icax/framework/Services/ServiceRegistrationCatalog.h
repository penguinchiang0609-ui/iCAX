#pragma once

#include "Services.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Services
    {
        class CServiceProvider;

        class _SERVICE_EXP CServiceRegistrationCatalog final
        {
        public:
            using ReplayFunc = std::function<void(CServiceProvider&)>;
            struct RegistrationRecord final
            {
                std::string ModulePath;
                ReplayFunc Replay;
            };

        private:
            CServiceRegistrationCatalog() = delete;
            ~CServiceRegistrationCatalog() = delete;

        public:
            static void Register(IN ReplayFunc Func_);
            static void Register(IN ReplayFunc Func_, IN const void* pModuleAddress_);
            static void ReplayAll(IN CServiceProvider& Provider_);
            static size_t ReplayFrom(IN size_t nFirstIndex_, IN CServiceProvider& Provider_);
            static void ReplayByModulePaths(IN CServiceProvider& Provider_, IN const std::vector<std::string>& ModulePaths_);
            static size_t Count();

        private:
            static std::vector<RegistrationRecord>& GetRegistrations();
        };
    }
}
