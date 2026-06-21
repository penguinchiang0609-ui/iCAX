#pragma once

#include "IFlatLaserCamService.h"
#include "Services/ServicesHelper.h"

namespace iCAX::FlatLaserCAM
{
    class CFlatLaserCamService final : public IFlatLaserCamService
    {
    public:
        CFlatLaserCamService() = default;
        ~CFlatLaserCamService() override = default;

    public:
        void OnLoad() override;
        void OnUnload() override;

        std::string GetDefaultProjectExtension() const override;
        std::vector<std::string> GetSupportedDrawingExtensions() const override;
        double EstimateCutTimeSeconds(
            IN double totalLengthMm,
            IN double cutSpeedMmPerMinute,
            IN double pierceTimeSeconds,
            IN int pierceCount) const override;

        AUTO_REGIST_SERVICE(IFlatLaserCamService, CFlatLaserCamService)
    };
}

