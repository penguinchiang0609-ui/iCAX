#include "pch.h"
#include "FlatLaserCamCommandHandlers.h"

#include "CommandHandler/CommandRegistrationCatalog.h"
#include "FlatLaserCamCommands.h"
#include "FlatLaserCamService/IFlatLaserCamService.h"
#include "ProductContext/IProductContext.h"
#include "ProjectContext/IProjectContext.h"
#include "Services/ServiceProvider.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    std::vector<uint8_t> ToPayload(IN const std::string& text)
    {
        return std::vector<uint8_t>(text.begin(), text.end());
    }

    void RequireFlatLaserService(
        IN iCAX::Product::IProductContext* productContext,
        IN iCAX::Project::IProjectContext* projectContext)
    {
        iCAX::Services::CServiceProvider* provider = nullptr;
        if (projectContext)
        {
            provider = &projectContext->Services();
        }
        else if (productContext)
        {
            provider = &productContext->GetServiceProvider();
        }
        else
        {
            throw std::invalid_argument("FlatLaserCam command requires ProductContext or ProjectContext");
        }

        auto service = provider->Resolve<iCAX::FlatLaserCAM::IFlatLaserCamService>();
        (void)service;
    }

    iCAX::Command::CCommandResponse MakeAcceptedResponse(IN const iCAX::Command::CCommandRequest& request)
    {
        iCAX::Command::CCommandResponse response;
        response.nStatus = iCAX::Command::ECommandStatusCode::Ok;
        const std::string payload =
            std::string("{\"accepted\":true,\"command\":\"") +
            iCAX::FlatLaserCAM::Commands::GetCommandName(request.Route.nSubCode) +
            "\"}";
        response.Payload = ToPayload(payload);
        return response;
    }
}

namespace iCAX::FlatLaserCAM
{
    CFlatLaserCamCommandTarget::CFlatLaserCamCommandTarget()
        : iCAX::Command::CCommandTarget(Commands::KMainName)
    {
        Bind(Commands::KGetStateName, [this](
            IN const iCAX::Command::CCommandRequest& request,
            IN iCAX::Application::IApplicationContext& applicationContext,
            IN iCAX::Product::IProductContext* productContext,
            IN iCAX::Project::IProjectContext* projectContext) {
            return HandleAccepted(request, applicationContext, productContext, projectContext);
        });
        Bind(Commands::KImportDrawingName, [this](
            IN const iCAX::Command::CCommandRequest& request,
            IN iCAX::Application::IApplicationContext& applicationContext,
            IN iCAX::Product::IProductContext* productContext,
            IN iCAX::Project::IProjectContext* projectContext) {
            return HandleAccepted(request, applicationContext, productContext, projectContext);
        });
        Bind(Commands::KCreateSheetName, [this](
            IN const iCAX::Command::CCommandRequest& request,
            IN iCAX::Application::IApplicationContext& applicationContext,
            IN iCAX::Product::IProductContext* productContext,
            IN iCAX::Project::IProjectContext* projectContext) {
            return HandleAccepted(request, applicationContext, productContext, projectContext);
        });
        Bind(Commands::KGenerateNestingName, [this](
            IN const iCAX::Command::CCommandRequest& request,
            IN iCAX::Application::IApplicationContext& applicationContext,
            IN iCAX::Product::IProductContext* productContext,
            IN iCAX::Project::IProjectContext* projectContext) {
            return HandleAccepted(request, applicationContext, productContext, projectContext);
        });
        Bind(Commands::KGenerateToolpathName, [this](
            IN const iCAX::Command::CCommandRequest& request,
            IN iCAX::Application::IApplicationContext& applicationContext,
            IN iCAX::Product::IProductContext* productContext,
            IN iCAX::Project::IProjectContext* projectContext) {
            return HandleAccepted(request, applicationContext, productContext, projectContext);
        });
        Bind(Commands::KRunSimulationName, [this](
            IN const iCAX::Command::CCommandRequest& request,
            IN iCAX::Application::IApplicationContext& applicationContext,
            IN iCAX::Product::IProductContext* productContext,
            IN iCAX::Project::IProjectContext* projectContext) {
            return HandleAccepted(request, applicationContext, productContext, projectContext);
        });
        Bind(Commands::KGenerateNcName, [this](
            IN const iCAX::Command::CCommandRequest& request,
            IN iCAX::Application::IApplicationContext& applicationContext,
            IN iCAX::Product::IProductContext* productContext,
            IN iCAX::Project::IProjectContext* projectContext) {
            return HandleAccepted(request, applicationContext, productContext, projectContext);
        });
    }

    iCAX::Command::CCommandResponse CFlatLaserCamCommandTarget::HandleAccepted(
        IN const iCAX::Command::CCommandRequest& request,
        IN iCAX::Application::IApplicationContext& applicationContext,
        IN iCAX::Product::IProductContext* productContext,
        IN iCAX::Project::IProjectContext* projectContext)
    {
        (void)applicationContext;
        RequireFlatLaserService(productContext, projectContext);
        return MakeAcceptedResponse(request);
    }
}

ICAX_REGISTER_COMMAND_TARGET(iCAX::FlatLaserCAM::CFlatLaserCamCommandTarget)
