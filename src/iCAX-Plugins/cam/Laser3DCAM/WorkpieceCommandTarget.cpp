#include "pch.h"
#include "CommandHandlers.h"

#include "CommandHandler/CommandRegistrationCatalog.h"
#include "CommandHandler/CommandTarget.h"

namespace
{
    class CWorkpieceCommandTarget final : public iCAX::Command::CCommandTarget
    {
    public:
        CWorkpieceCommandTarget()
            : CCommandTarget("Workpiece")
        {
            Bind("Import", &iCAX::CAM::Commands::HandleImportModel);
            Bind("SetActive", &iCAX::CAM::Commands::HandleSetActiveWorkpiece);
        }
    };

    static_assert(iCAX::Command::IsStatelessCommandTargetType<CWorkpieceCommandTarget>);
}

ICAX_REGISTER_COMMAND_TARGET(CWorkpieceCommandTarget)