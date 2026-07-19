#pragma once

#include "FacadeFrame.h"

#include <string>

namespace iCAX::Interaction
{
    _FACADES_EXP void SetFacadePayloadText(
        IN OUT CFacadeFrame& Frame_,
        IN const std::string& strText_);

    _FACADES_EXP std::string GetFacadePayloadText(IN const CFacadeFrame& Frame_);

    _FACADES_EXP CFacadeFrame CreateTextFacadeFrame(
        IN uint64_t nCallID_,
        IN uint64_t nMethodCode_,
        IN EFacadeFrameKind Kind_,
        IN const std::string& strText_,
        IN EInvocationStatus Status_ = EInvocationStatus::Ok);
}
