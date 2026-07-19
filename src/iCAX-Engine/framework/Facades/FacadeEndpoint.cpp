#include "pch.h"
#include "FacadeEndpoint.h"

#include "FacadePayload.h"

iCAX::Interaction::CFacadeEndpoint::CFacadeEndpoint(
    IN std::shared_ptr<CFacadeQueue> pIncomingQueue_,
    IN std::shared_ptr<CFacadeQueue> pOutgoingQueue_) noexcept
    : m_pIncomingQueue(std::move(pIncomingQueue_))
    , m_pOutgoingQueue(std::move(pOutgoingQueue_))
{
}

bool iCAX::Interaction::CFacadeEndpoint::IsValid() const noexcept
{
    return !m_pIncomingQueue.expired() && !m_pOutgoingQueue.expired();
}

void iCAX::Interaction::CFacadeEndpoint::Send(IN const CFacadeFrame& Frame_) const
{
    RequireOutgoingQueue()->Enqueue(Frame_);
}

void iCAX::Interaction::CFacadeEndpoint::Send(IN CFacadeFrame&& Frame_) const
{
    RequireOutgoingQueue()->Enqueue(std::move(Frame_));
}

void iCAX::Interaction::CFacadeEndpoint::SendText(
    IN uint64_t nCallID_,
    IN uint64_t nMethodCode_,
    IN EFacadeFrameKind Kind_,
    IN const std::string& strPayloadText_,
    IN EInvocationStatus Status_) const
{
    Send(CreateTextFacadeFrame(
        nCallID_,
        nMethodCode_,
        Kind_,
        strPayloadText_,
        Status_));
}

std::vector<iCAX::Interaction::CFacadeFrame> iCAX::Interaction::CFacadeEndpoint::Receive() const
{
    return RequireIncomingQueue()->Drain();
}

void iCAX::Interaction::CFacadeEndpoint::ClearIncoming() const
{
    RequireIncomingQueue()->Clear();
}

void iCAX::Interaction::CFacadeEndpoint::ClearOutgoing() const
{
    RequireOutgoingQueue()->Clear();
}

std::shared_ptr<iCAX::Interaction::CFacadeQueue> iCAX::Interaction::CFacadeEndpoint::RequireIncomingQueue() const
{
    auto _pQueue = m_pIncomingQueue.lock();
    if (!_pQueue)
    {
        throw std::logic_error("Facade endpoint incoming queue is not bound");
    }
    return _pQueue;
}

std::shared_ptr<iCAX::Interaction::CFacadeQueue> iCAX::Interaction::CFacadeEndpoint::RequireOutgoingQueue() const
{
    auto _pQueue = m_pOutgoingQueue.lock();
    if (!_pQueue)
    {
        throw std::logic_error("Facade endpoint outgoing queue is not bound");
    }
    return _pQueue;
}
