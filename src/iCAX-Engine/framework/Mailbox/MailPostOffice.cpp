#include "pch.h"
#include "MailPostOffice.h"
#include <stdexcept>
#include <utility>

//! 构造函数
iCAX::Mail::CMailPostOffice::CMailPostOffice(
    std::shared_ptr<CMailQueue> pIncomingQueue_,
    std::shared_ptr<CMailQueue> pOutgoingQueue_) noexcept
    : m_pIncomingQueue(std::move(pIncomingQueue_))
    , m_pOutgoingQueue(std::move(pOutgoingQueue_))
{
}

//! 当前邮局是否已经绑定队列
bool iCAX::Mail::CMailPostOffice::IsValid() const noexcept
{
    return !m_pIncomingQueue.expired() && !m_pOutgoingQueue.expired();
}

//! 发送邮件
void iCAX::Mail::CMailPostOffice::Send(const Mail& Mail_) const
{
    RequireOutgoingQueue()->Enqueue(Mail_);
}

//! 发送 header + bytes
void iCAX::Mail::CMailPostOffice::SendPayload(
    IN const MailHeader& Header_,
    IN const void* pPayload_,
    IN size_t nPayloadSize_) const
{
    RequireOutgoingQueue()->EnqueuePayload(Header_, pPayload_, nPayloadSize_);
}

//! 发送 UTF-8 文本
void iCAX::Mail::CMailPostOffice::SendText(
    IN const MailHeader& Header_,
    IN const std::string& strPayloadText_) const
{
    SendPayload(Header_, strPayloadText_.data(), strPayloadText_.size());
}

//! 接收邮件
std::vector<iCAX::Mail::Mail> iCAX::Mail::CMailPostOffice::Receive() const
{
    return RequireIncomingQueue()->Drain();
}

//! 清空收件队列
void iCAX::Mail::CMailPostOffice::ClearIncoming() const
{
    RequireIncomingQueue()->Clear();
}

//! 清空发件队列
void iCAX::Mail::CMailPostOffice::ClearOutgoing() const
{
    RequireOutgoingQueue()->Clear();
}

//! 获取收件队列
std::shared_ptr<iCAX::Mail::CMailQueue> iCAX::Mail::CMailPostOffice::RequireIncomingQueue() const
{
    auto _pQueue = m_pIncomingQueue.lock();
    if (!_pQueue)
    {
        throw std::logic_error("mail post office incoming queue is not bound");
    }
    return _pQueue;
}

//! 获取发件队列
std::shared_ptr<iCAX::Mail::CMailQueue> iCAX::Mail::CMailPostOffice::RequireOutgoingQueue() const
{
    auto _pQueue = m_pOutgoingQueue.lock();
    if (!_pQueue)
    {
        throw std::logic_error("mail post office outgoing queue is not bound");
    }
    return _pQueue;
}
