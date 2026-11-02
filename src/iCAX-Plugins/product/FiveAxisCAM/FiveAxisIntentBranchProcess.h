#pragma once
#include "FiveAxisIntentOperations.h"

namespace iCAX::FiveAxisCAM
{
FIVE_AXIS_CAM_API void RegisterFiveAxisIntentBranchProcess(
    iCAX::CAM::Intent::CToolpathOperationRegistry&,FiveAxisIntentContextResolver);
}
