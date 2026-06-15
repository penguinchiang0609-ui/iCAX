#pragma once

#include "ApplicationContextExport.h"

#include <cstdint>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Application
    {
        struct _APPLICATION_CONTEXT_EXP CApplicationDescriptor final
        {
            std::string AppID;
            std::string AppName;
            std::string AppVersion;
            std::vector<std::string> SupportedProjectMagics;
            std::vector<uint32_t> SupportedProjectVersions;
            std::string DefaultProjectExtension;

            bool SupportsProjectMagic(IN const std::string& strMagic_) const;
            bool SupportsProjectVersion(IN uint32_t nVersion_) const;
        };
    }
}
