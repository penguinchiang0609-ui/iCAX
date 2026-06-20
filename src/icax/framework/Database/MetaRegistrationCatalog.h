#pragma once

#include "Database.h"

#include <functional>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Database
    {
        class IMetaRegistry;

        class _DATABASE_EXP CMetaRegistrationCatalog final
        {
        public:
            using ReplayFunc = std::function<void(IMetaRegistry&)>;
            struct RegistrationRecord final
            {
                std::string ModulePath;
                ReplayFunc Replay;
            };

        private:
            CMetaRegistrationCatalog() = delete;
            ~CMetaRegistrationCatalog() = delete;

        public:
            static void Register(IN ReplayFunc Func_);
            static void Register(IN ReplayFunc Func_, IN const void* pModuleAddress_);
            static void ReplayAll(IN IMetaRegistry& Registry_);
            static size_t ReplayFrom(IN size_t nFirstIndex_, IN IMetaRegistry& Registry_);
            static void ReplayByModulePaths(IN IMetaRegistry& Registry_, IN const std::vector<std::string>& ModulePaths_);
            static size_t Count();

        private:
            static std::vector<RegistrationRecord>& GetRegistrations();
        };
    }
}
