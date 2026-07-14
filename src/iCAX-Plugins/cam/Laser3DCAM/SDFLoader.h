#pragma once

#include "MachineResources.h"

#include <string>

namespace iCAX
{
    namespace CAM
    {
        bool CanLoadSDFMachineDescription(
            IN const std::string& strSourcePath_);

        bool LoadSDFMachineDescription(
            IN const std::string& strSourcePath_,
            OUT CMachineDescriptionResource& Description_,
            OUT std::string& strError_);
    }
}
