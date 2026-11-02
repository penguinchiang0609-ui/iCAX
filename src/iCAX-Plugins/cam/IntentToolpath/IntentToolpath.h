#pragma once

#include "IntentToolpathComponents.h"
#include "IntentGeometryResources.h"
#include "IntentToolpathRelations.h"
#include "EditableToolpath.h"
#include "IntentToolpathExport.h"

#include <cstdint>

namespace iCAX::CAM::Intent
{
_INTENT_TOOLPATH_EXP std::uint32_t GetIntentToolpathContractVersion() noexcept;
}
