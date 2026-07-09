#include "pch.h"
#include "CommandHandlers.h"

#include "CommandHandler/CommandRegistrationCatalog.h"
#include "CommandHandler/CommandTarget.h"

namespace
{
    class CSceneCommandTarget final : public iCAX::Command::CCommandTarget
    {
    public:
        CSceneCommandTarget()
            : CCommandTarget("Scene")
        {
            Bind("Get", &iCAX::CAM::Commands::HandleGetScene);
        }
    };

    static_assert(iCAX::Command::IsStatelessCommandTargetType<CSceneCommandTarget>);
}

ICAX_REGISTER_COMMAND_TARGET(CSceneCommandTarget)
