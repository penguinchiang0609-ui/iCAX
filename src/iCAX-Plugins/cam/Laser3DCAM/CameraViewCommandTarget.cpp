#include "pch.h"
#include "CommandHandlers.h"

#include "CommandHandler/CommandRegistrationCatalog.h"
#include "CommandHandler/CommandTarget.h"

namespace
{
    class CCameraViewCommandTarget final : public iCAX::Command::CCommandTarget
    {
    public:
        CCameraViewCommandTarget()
            : CCommandTarget("CameraView")
        {
            Bind("Fit", &iCAX::CAM::Commands::HandleFitCameraView);
        }
    };

    static_assert(iCAX::Command::IsStatelessCommandTargetType<CCameraViewCommandTarget>);
}

ICAX_REGISTER_COMMAND_TARGET(CCameraViewCommandTarget)
