#include "pch.h"
#include "CommandHandlers.h"

#include "CommandHandler/CommandRegistrationCatalog.h"
#include "CommandHandler/CommandTarget.h"

namespace
{
    class CSelectionCommandTarget final : public iCAX::Command::CCommandTarget
    {
    public:
        CSelectionCommandTarget()
            : CCommandTarget("Selection")
        {
            Bind("PickTopology", &iCAX::CAM::Commands::HandlePickTopology);
            Bind("PickMachineObject", &iCAX::CAM::Commands::HandlePickMachineObject);
        }
    };

    static_assert(iCAX::Command::IsStatelessCommandTargetType<CSelectionCommandTarget>);
}

ICAX_REGISTER_COMMAND_TARGET(CSelectionCommandTarget)