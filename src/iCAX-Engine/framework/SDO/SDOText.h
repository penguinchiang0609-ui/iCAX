#pragma once

#include "SDOFrame.h"

#include <string>

namespace iCAX::Interaction
{
    _SDO_EXP void SetSDOPayloadText(
        IN OUT CSDOFrame& Frame_,
        IN const std::string& strText_);

    _SDO_EXP std::string GetSDOPayloadText(IN const CSDOFrame& Frame_);

    _SDO_EXP CSDOFrame CreateTextSDOFrame(
        IN uint64_t nCallID_,
        IN uint64_t nMethodCode_,
        IN ESDOFrameKind Kind_,
        IN const std::string& strText_,
        IN EInvocationStatus Status_ = EInvocationStatus::Ok);
}
