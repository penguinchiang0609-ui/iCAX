#pragma once

#include "FlatLaserCamCommandExport.h"

#include "CommandHandler/ICommandHandler.h"

namespace iCAX::FlatLaserCAM
{
    class _FLATLASERCAMCOMMAND_EXP CImportDrawingCommandHandler final : public iCAX::Command::ICommandHandler
    {
    public:
        iCAX::Command::CCommandResponse Handle(
            IN const iCAX::Command::CCommandRequest& request,
            IN iCAX::Command::ICommandContext& context) override;
    };

    class _FLATLASERCAMCOMMAND_EXP CCreateSheetCommandHandler final : public iCAX::Command::ICommandHandler
    {
    public:
        iCAX::Command::CCommandResponse Handle(
            IN const iCAX::Command::CCommandRequest& request,
            IN iCAX::Command::ICommandContext& context) override;
    };

    class _FLATLASERCAMCOMMAND_EXP CGenerateNestingCommandHandler final : public iCAX::Command::ICommandHandler
    {
    public:
        iCAX::Command::CCommandResponse Handle(
            IN const iCAX::Command::CCommandRequest& request,
            IN iCAX::Command::ICommandContext& context) override;
    };

    class _FLATLASERCAMCOMMAND_EXP CGenerateToolpathCommandHandler final : public iCAX::Command::ICommandHandler
    {
    public:
        iCAX::Command::CCommandResponse Handle(
            IN const iCAX::Command::CCommandRequest& request,
            IN iCAX::Command::ICommandContext& context) override;
    };

    class _FLATLASERCAMCOMMAND_EXP CRunSimulationCommandHandler final : public iCAX::Command::ICommandHandler
    {
    public:
        iCAX::Command::CCommandResponse Handle(
            IN const iCAX::Command::CCommandRequest& request,
            IN iCAX::Command::ICommandContext& context) override;
    };

    class _FLATLASERCAMCOMMAND_EXP CGenerateNcCommandHandler final : public iCAX::Command::ICommandHandler
    {
    public:
        iCAX::Command::CCommandResponse Handle(
            IN const iCAX::Command::CCommandRequest& request,
            IN iCAX::Command::ICommandContext& context) override;
    };
}

