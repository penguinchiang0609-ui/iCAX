#include "pch.h"
#include "SDOText.h"

void iCAX::Interaction::SetSDOPayloadText(
    IN OUT CSDOFrame& Frame_,
    IN const std::string& strText_)
{
    Frame_.Payload.assign(strText_.begin(), strText_.end());
}

std::string iCAX::Interaction::GetSDOPayloadText(IN const CSDOFrame& Frame_)
{
    return std::string(Frame_.Payload.begin(), Frame_.Payload.end());
}

iCAX::Interaction::CSDOFrame iCAX::Interaction::CreateTextSDOFrame(
    IN uint64_t nCallID_,
    IN uint64_t nMethodCode_,
    IN ESDOFrameKind Kind_,
    IN const std::string& strText_,
    IN EInvocationStatus Status_)
{
    CSDOFrame _Frame;
    _Frame.nCallID = nCallID_;
    _Frame.nMethodCode = nMethodCode_;
    _Frame.nKind = Kind_;
    _Frame.nStatus = Status_;
    SetSDOPayloadText(_Frame, strText_);
    return _Frame;
}
