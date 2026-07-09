#include "pch.h"
#include "CommandHandlers.h"

#include "CommandHandler/CommandRegistrationCatalog.h"
#include "CommandHandler/CommandTarget.h"

namespace
{
    class CToolpathCommandTarget final : public iCAX::Command::CCommandTarget
    {
    public:
        CToolpathCommandTarget()
            : CCommandTarget("Toolpath")
        {
            Bind("RecognizeLoops", &iCAX::CAM::Commands::HandleRecognizeLoops);
            Bind("AddSelectionPath", &iCAX::CAM::Commands::HandleAddSelectionPath);
            Bind("SetPoseField", &iCAX::CAM::Commands::HandleSetPoseField);
            Bind("ClearProgram", &iCAX::CAM::Commands::HandleClearProgram);
        }
    };

    static_assert(iCAX::Command::IsStatelessCommandTargetType<CToolpathCommandTarget>);
}

ICAX_REGISTER_COMMAND_TARGET(CToolpathCommandTarget)