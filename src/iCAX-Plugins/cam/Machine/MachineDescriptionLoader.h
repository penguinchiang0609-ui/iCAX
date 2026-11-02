#pragma once

#include "MachineExport.h"

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

        // Resolve a local visual/collision resource declared by URDF or SDF.
        _MACHINE_EXP std::string ResolveMachineDescriptionResourcePath(
            const CMachineDescriptionResource& Description_, const std::string& Uri_);
    }
}
