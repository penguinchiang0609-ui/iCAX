#pragma once

#include "ProjectExport.h"

#include <string>
#include <vector>

namespace iCAX
{
    namespace Project
    {
        struct _PROJECT_EXP CProductModule final
        {
            std::string Name;
            std::string ComponentPath;
            std::string BehaviourPath;
            std::string ServicePath;
            std::string CommandPath;
        };

        struct _PROJECT_EXP CProductDefinition final
        {
            std::string ProductID;
            std::string DisplayName;
            std::string ManifestPath;
            std::string ProductRootPath;
            std::string FrontendEntry;
            std::string ProtocolPath;
            std::string StartupComponent;
            std::vector<std::string> ComponentModules;
            std::vector<std::string> BehaviourModules;
            std::vector<std::string> ServiceModules;
            std::vector<std::string> CommandModules;
            std::vector<std::string> FileExtensions;
            std::vector<CProductModule> PluginModules;

            bool IsValid() const;
            std::string GetDisplayNameOrID() const;
        };
    }
}
