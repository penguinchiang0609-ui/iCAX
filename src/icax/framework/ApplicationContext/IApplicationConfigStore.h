#pragma once

#include "ApplicationContextExport.h"
#include "ApplicationSettings.h"

#include <string>

namespace iCAX
{
    namespace Application
    {
        class _APPLICATION_CONTEXT_EXP IApplicationConfigStore
        {
        public:
            IApplicationConfigStore() = default;
            virtual ~IApplicationConfigStore() = default;

            IApplicationConfigStore(IN const IApplicationConfigStore&) = delete;
            IApplicationConfigStore& operator=(IN const IApplicationConfigStore&) = delete;

        public:
            virtual CApplicationSettings Load(IN const std::string& strPath_) const = 0;
            virtual void Save(IN const std::string& strPath_, IN const CApplicationSettings& Settings_) const = 0;
        };
    }
}
