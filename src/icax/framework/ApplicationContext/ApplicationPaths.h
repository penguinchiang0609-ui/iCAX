#pragma once

#include "ApplicationContextExport.h"

#include <string>

namespace iCAX
{
    namespace Application
    {
        struct _APPLICATION_CONTEXT_EXP CApplicationPaths final
        {
            std::string InstallDirectory;
            std::string UserConfigDirectory;
            std::string CacheDirectory;
            std::string TempDirectory;
            std::string LogDirectory;
        };
    }
}
