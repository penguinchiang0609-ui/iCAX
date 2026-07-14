#pragma once

#include "MachineResources.h"

#include <string>

namespace iCAX
{
    namespace CAM
    {
        bool IsSupportedMachineDescriptionPath(
            IN const std::string& strSourcePath_);

        bool LoadMachineDescription(
            IN const std::string& strSourcePath_,
            OUT CMachineDescriptionResource& Description_,
            OUT std::string& strError_);
    }
}
