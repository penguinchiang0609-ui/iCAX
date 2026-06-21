#pragma once

#include "FlatLaserCamServiceExport.h"
#include "Services/IService.h"

#include <string>
#include <vector>

namespace iCAX::FlatLaserCAM
{
    class _FLATLASERCAMSERVICE_EXP IFlatLaserCamService : public iCAX::Services::IService
    {
    public:
        ~IFlatLaserCamService() override = default;

    public:
        virtual std::string GetDefaultProjectExtension() const = 0;
        virtual std::vector<std::string> GetSupportedDrawingExtensions() const = 0;
        virtual double EstimateCutTimeSeconds(
            IN double totalLengthMm,
            IN double cutSpeedMmPerMinute,
            IN double pierceTimeSeconds,
            IN int pierceCount) const = 0;
    };
}

