#pragma once

#include "ResourcesExport.h"

#include <functional>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Resource
    {
        class CResourceLoaderRegistry;

        class _RESOURCES_EXP CResourceLoaderRegistrationCatalog final
        {
        public:
            using ReplayFunc = std::function<void(CResourceLoaderRegistry&)>;
            struct RegistrationRecord final
            {
                std::string ModulePath;
                ReplayFunc Replay;
            };

        private:
            CResourceLoaderRegistrationCatalog() = delete;
            ~CResourceLoaderRegistrationCatalog() = delete;

        public:
            static void Register(IN ReplayFunc Func_);
            static void Register(IN ReplayFunc Func_, IN const void* pModuleAddress_);
            static void ReplayAll(IN CResourceLoaderRegistry& Registry_);
            static size_t ReplayFrom(IN size_t nFirstIndex_, IN CResourceLoaderRegistry& Registry_);
            static void ReplayByModulePaths(IN CResourceLoaderRegistry& Registry_, IN const std::vector<std::string>& ModulePaths_);
            static size_t Count();

        private:
            static std::vector<RegistrationRecord>& GetRegistrations();
        };
    }
}
