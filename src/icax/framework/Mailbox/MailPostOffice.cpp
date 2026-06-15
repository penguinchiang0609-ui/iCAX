#include "pch.h"
#include "MailPostOffice.h"
#include <stdexcept>

//! 构造函数
iCAX::Mail::CMailPostOffice::CMailPostOffice(CMailQueue& IncomingQueue_, CMailQueue& OutgoingQueue_) noexcept
    : m_pIncomingQueue(&IncomingQueue_)
    , m_pOutgoingQueue(&OutgoingQueue_)
{
}

//! 当前邮局是否已经绑定队列
bool iCAX::Mail::CMailPostOffice::IsValid() const noexcept
{
    return m_pIncomingQueue != nullptr && m_pOutgoingQueue != nullptr;
}

//! 发送邮件
void iCAX::Mail::CMailPostOffice::Send(const Mail& Mail_) const
{
    RequireOutgoingQueue().Enqueue(Mail_);
}

//! 接收邮件
std::vector<iCAX::Mail::Mail> iCAX::Mail::CMailPostOffice::Receive() const
{
    return RequireIncomingQueue().Drain();
}

//! 清空收件队列
void iCAX::Mail::CMailPostOffice::ClearIncoming() const
{
    RequireIncomingQueue().Clear();
}

//! 清空发件队列
void iCAX::Mail::CMailPostOffice::ClearOutgoing() const
{
    RequireOutgoingQueue().Clear();
}

//! 获取收件队列
iCAX::Mail::CMailQueue& iCAX::Mail::CMailPostOffice::RequireIncomingQueue() const
{
    if (m_pIncomingQueue == nullptr)
    {
        throw std::logic_error("mail post office incoming queue is not bound");
    }
    return *m_pIncomingQueue;
}

//! 获取发件队列
iCAX::Mail::CMailQueue& iCAX::Mail::CMailPostOffice::RequireOutgoingQueue() const
{
    if (m_pOutgoingQueue == nullptr)
    {
        throw std::logic_error("mail post office outgoing queue is not bound");
    }
    return *m_pOutgoingQueue;
}
