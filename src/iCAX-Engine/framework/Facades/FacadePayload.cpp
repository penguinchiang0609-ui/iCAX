#include "pch.h"
#include "FacadePayload.h"

void iCAX::Interaction::SetFacadePayloadText(
    IN OUT CFacadeFrame& Frame_,
    IN const std::string& strText_)
{
    Frame_.Payload.assign(strText_.begin(), strText_.end());
}

std::string iCAX::Interaction::GetFacadePayloadText(IN const CFacadeFrame& Frame_)
{
    return std::string(Frame_.Payload.begin(), Frame_.Payload.end());
}

iCAX::Interaction::CFacadeFrame iCAX::Interaction::CreateTextFacadeFrame(
    IN uint64_t nCallID_,
    IN uint64_t nMethodCode_,
    IN EFacadeFrameKind Kind_,
    IN const std::string& strText_,
    IN EInvocationStatus Status_)
{
    CFacadeFrame _Frame;
    _Frame.nCallID = nCallID_;
    _Frame.nMethodCode = nMethodCode_;
    _Frame.nKind = Kind_;
    _Frame.nStatus = Status_;
    SetFacadePayloadText(_Frame, strText_);
    return _Frame;
}
