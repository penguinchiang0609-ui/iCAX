#pragma once

#include "FlatLaserCamCommandExport.h"

#include "CommandHandler/CommandTarget.h"

namespace iCAX::FlatLaserCAM
{
    class _FLATLASERCAMCOMMAND_EXP CFlatLaserCamCommandTarget final : public iCAX::Command::CCommandTarget
    {
    public:
        CFlatLaserCamCommandTarget();
        ~CFlatLaserCamCommandTarget() override = default;

    private:
        iCAX::Command::CCommandResponse HandleAccepted(
            IN const iCAX::Command::CCommandRequest& request,
            IN iCAX::Application::IApplicationContext& applicationContext,
            IN iCAX::Product::IProductContext* productContext,
            IN iCAX::Project::IProjectContext* projectContext);
    };
}
