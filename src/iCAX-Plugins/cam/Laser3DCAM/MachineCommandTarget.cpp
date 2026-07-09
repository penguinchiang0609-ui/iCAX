#include "pch.h"
#include "CommandHandlers.h"

#include "CommandHandler/CommandRegistrationCatalog.h"
#include "CommandHandler/CommandTarget.h"

namespace
{
    class CMachineCommandTarget final : public iCAX::Command::CCommandTarget
    {
    public:
        CMachineCommandTarget()
            : CCommandTarget("Machine")
        {
            Bind("Import", &iCAX::CAM::Commands::HandleImportMachine);
            Bind("SetParameters", &iCAX::CAM::Commands::HandleSetMachineParameters);
            Bind("SetTCP", &iCAX::CAM::Commands::HandleSetMachineTCP);
            Bind("Jog", &iCAX::CAM::Commands::HandleJogMachine);
            Bind("Home", &iCAX::CAM::Commands::HandleHomeMachine);
            Bind("Reset", &iCAX::CAM::Commands::HandleResetMachine);
            Bind("CheckLimits", &iCAX::CAM::Commands::HandleCheckMachineLimits);
            Bind("CheckReach", &iCAX::CAM::Commands::HandleCheckMachineReach);
        }
    };

    static_assert(iCAX::Command::IsStatelessCommandTargetType<CMachineCommandTarget>);
}

ICAX_REGISTER_COMMAND_TARGET(CMachineCommandTarget)