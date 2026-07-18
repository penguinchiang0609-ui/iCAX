#pragma once

#include "../Machine/MachineExport.h"

#include "MachineResources.h"

#include <string>

namespace iCAX
{
    namespace CAM
    {
        _MACHINE_EXP bool IsSupportedMachineDescriptionPath(
            IN const std::string& strSourcePath_);

        _MACHINE_EXP bool LoadMachineDescription(
            IN const std::string& strSourcePath_,
            OUT CMachineDescriptionResource& Description_,
            OUT std::string& strError_);
    }
}
