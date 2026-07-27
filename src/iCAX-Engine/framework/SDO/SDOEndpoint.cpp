#include "pch.h"
#include "SDOEndpoint.h"

#include "SDOText.h"

iCAX::Interaction::CSDOEndpoint::CSDOEndpoint(
    IN std::shared_ptr<CSDOQueue> pIncomingQueue_,
    IN std::shared_ptr<CSDOQueue> pOutgoingQueue_) noexcept
    : m_pIncomingQueue(std::move(pIncomingQueue_))
    , m_pOutgoingQueue(std::move(pOutgoingQueue_))
{
}

bool iCAX::Interaction::CSDOEndpoint::IsValid() const noexcept
{
    return !m_pIncomingQueue.expired() && !m_pOutgoingQueue.expired();
}

void iCAX::Interaction::CSDOEndpoint::Send(IN const CSDOFrame& Frame_) const
{
    RequireOutgoingQueue()->Enqueue(Frame_);
}

void iCAX::Interaction::CSDOEndpoint::Send(IN CSDOFrame&& Frame_) const
{
    RequireOutgoingQueue()->Enqueue(std::move(Frame_));
}

void iCAX::Interaction::CSDOEndpoint::SendText(
    IN uint64_t nCallID_,
    IN uint64_t nMethodCode_,
    IN ESDOFrameKind Kind_,
    IN const std::string& strPayloadText_,
    IN EInvocationStatus Status_) const
{
    Send(CreateTextSDOFrame(
        nCallID_,
        nMethodCode_,
        Kind_,
        strPayloadText_,
        Status_));
}

std::vector<iCAX::Interaction::CSDOFrame> iCAX::Interaction::CSDOEndpoint::Receive() const
{
    return RequireIncomingQueue()->Drain();
}

void iCAX::Interaction::CSDOEndpoint::ClearIncoming() const
{
    RequireIncomingQueue()->Clear();
}

void iCAX::Interaction::CSDOEndpoint::ClearOutgoing() const
{
    RequireOutgoingQueue()->Clear();
}

std::shared_ptr<iCAX::Interaction::CSDOQueue> iCAX::Interaction::CSDOEndpoint::RequireIncomingQueue() const
{
    auto _pQueue = m_pIncomingQueue.lock();
    if (!_pQueue)
    {
        throw std::logic_error("SDO endpoint incoming queue is not bound");
    }
    return _pQueue;
}

std::shared_ptr<iCAX::Interaction::CSDOQueue> iCAX::Interaction::CSDOEndpoint::RequireOutgoingQueue() const
{
    auto _pQueue = m_pOutgoingQueue.lock();
    if (!_pQueue)
    {
        throw std::logic_error("SDO endpoint outgoing queue is not bound");
    }
    return _pQueue;
}
