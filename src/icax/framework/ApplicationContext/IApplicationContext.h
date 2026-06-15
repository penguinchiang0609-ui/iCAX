#pragma once

#include "ApplicationDescriptor.h"
#include "ApplicationPaths.h"
#include "ApplicationSettings.h"

namespace iCAX
{
    namespace Application
    {
        class _APPLICATION_CONTEXT_EXP IApplicationContext
        {
        public:
            IApplicationContext() = default;
            virtual ~IApplicationContext() = default;

            IApplicationContext(IN const IApplicationContext&) = delete;
            IApplicationContext& operator=(IN const IApplicationContext&) = delete;

        public:
            virtual const CApplicationDescriptor& GetDescriptor() const = 0;
            virtual const CApplicationPaths& GetPaths() const = 0;
            virtual const CApplicationSettings& GetSettings() const = 0;
        };
    }
}
