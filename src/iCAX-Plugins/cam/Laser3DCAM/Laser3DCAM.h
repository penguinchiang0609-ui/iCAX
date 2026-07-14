#pragma once

#include "Laser3DCAMExport.h"

#include <cstdint>

namespace iCAX
{
    namespace CAM
    {
        inline constexpr double kDefaultCameraPositionX = 260.0;
        inline constexpr double kDefaultCameraPositionY = -320.0;
        inline constexpr double kDefaultCameraPositionZ = 220.0;
        inline constexpr double kDefaultCameraYawRadians = 2.25311279;
        inline constexpr double kDefaultCameraPitchRadians = 0.459504;
        inline constexpr double kDefaultCameraRollRadians = 0.0;

        _LASER_3D_CAM_EXP uint32_t GetLaser3DCAMContractVersion() noexcept;
    }
}
