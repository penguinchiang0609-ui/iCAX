#include "pch.h"
#include "FlatLaserCamService.h"

#include <stdexcept>

namespace iCAX::FlatLaserCAM
{
    void CFlatLaserCamService::OnLoad()
    {
    }

    void CFlatLaserCamService::OnUnload()
    {
    }

    std::string CFlatLaserCamService::GetDefaultProjectExtension() const
    {
        return ".ilcam";
    }

    std::vector<std::string> CFlatLaserCamService::GetSupportedDrawingExtensions() const
    {
        return { ".dxf", ".dwg", ".svg" };
    }

    double CFlatLaserCamService::EstimateCutTimeSeconds(
        IN double totalLengthMm,
        IN double cutSpeedMmPerMinute,
        IN double pierceTimeSeconds,
        IN int pierceCount) const
    {
        if (cutSpeedMmPerMinute <= 0.0)
        {
            throw std::invalid_argument("cutSpeedMmPerMinute must be greater than zero.");
        }
        if (pierceCount < 0)
        {
            throw std::invalid_argument("pierceCount must not be negative.");
        }

        const double cuttingSeconds = totalLengthMm / cutSpeedMmPerMinute * 60.0;
        const double piercingSeconds = pierceTimeSeconds * static_cast<double>(pierceCount);
        return cuttingSeconds + piercingSeconds;
    }
}

