#include "pch.h"
#include "FlatLaserCamCommandHandlers.h"

#include "CommandHandler/CommandRegistrationCatalog.h"
#include "FlatLaserCamCommands.h"
#include "FlatLaserCamService/IFlatLaserCamService.h"
#include "Services/ServiceProvider.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
    std::vector<uint8_t> ToPayload(IN const std::string& text)
    {
        return std::vector<uint8_t>(text.begin(), text.end());
    }

    void RequireFlatLaserService(IN iCAX::Command::ICommandContext& context)
    {
        auto provider = context.RequireDependency<iCAX::Services::CServiceProvider>();
        auto service = provider->Resolve<iCAX::FlatLaserCAM::IFlatLaserCamService>();
        (void)service;
    }

    iCAX::Command::CCommandResponse MakeAcceptedResponse(IN uint64_t nTypeCode_)
    {
        iCAX::Command::CCommandResponse response;
        response.nStatus = iCAX::Command::ECommandStatusCode::Ok;
        const std::string payload =
            std::string("{\"accepted\":true,\"command\":\"") +
            iCAX::FlatLaserCAM::Commands::GetCommandName(nTypeCode_) +
            "\"}";
        response.Payload = ToPayload(payload);
        return response;
    }
}

namespace iCAX::FlatLaserCAM
{
    iCAX::Command::CCommandResponse CImportDrawingCommandHandler::Handle(
        IN const iCAX::Command::CCommandRequest& request,
        IN iCAX::Command::ICommandContext& context)
    {
        RequireFlatLaserService(context);
        return MakeAcceptedResponse(request.nTypeCode);
    }

    iCAX::Command::CCommandResponse CCreateSheetCommandHandler::Handle(
        IN const iCAX::Command::CCommandRequest& request,
        IN iCAX::Command::ICommandContext& context)
    {
        RequireFlatLaserService(context);
        return MakeAcceptedResponse(request.nTypeCode);
    }

    iCAX::Command::CCommandResponse CGenerateNestingCommandHandler::Handle(
        IN const iCAX::Command::CCommandRequest& request,
        IN iCAX::Command::ICommandContext& context)
    {
        RequireFlatLaserService(context);
        return MakeAcceptedResponse(request.nTypeCode);
    }

    iCAX::Command::CCommandResponse CGenerateToolpathCommandHandler::Handle(
        IN const iCAX::Command::CCommandRequest& request,
        IN iCAX::Command::ICommandContext& context)
    {
        RequireFlatLaserService(context);
        return MakeAcceptedResponse(request.nTypeCode);
    }

    iCAX::Command::CCommandResponse CRunSimulationCommandHandler::Handle(
        IN const iCAX::Command::CCommandRequest& request,
        IN iCAX::Command::ICommandContext& context)
    {
        RequireFlatLaserService(context);
        return MakeAcceptedResponse(request.nTypeCode);
    }

    iCAX::Command::CCommandResponse CGenerateNcCommandHandler::Handle(
        IN const iCAX::Command::CCommandRequest& request,
        IN iCAX::Command::ICommandContext& context)
    {
        RequireFlatLaserService(context);
        return MakeAcceptedResponse(request.nTypeCode);
    }
}

ICAX_REGISTER_COMMAND(iCAX::FlatLaserCAM::Commands::KImportDrawing, iCAX::FlatLaserCAM::CImportDrawingCommandHandler)
ICAX_REGISTER_COMMAND(iCAX::FlatLaserCAM::Commands::KCreateSheet, iCAX::FlatLaserCAM::CCreateSheetCommandHandler)
ICAX_REGISTER_COMMAND(iCAX::FlatLaserCAM::Commands::KGenerateNesting, iCAX::FlatLaserCAM::CGenerateNestingCommandHandler)
ICAX_REGISTER_COMMAND(iCAX::FlatLaserCAM::Commands::KGenerateToolpath, iCAX::FlatLaserCAM::CGenerateToolpathCommandHandler)
ICAX_REGISTER_COMMAND(iCAX::FlatLaserCAM::Commands::KRunSimulation, iCAX::FlatLaserCAM::CRunSimulationCommandHandler)
ICAX_REGISTER_COMMAND(iCAX::FlatLaserCAM::Commands::KGenerateNc, iCAX::FlatLaserCAM::CGenerateNcCommandHandler)

