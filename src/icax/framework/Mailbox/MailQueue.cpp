#include "pch.h"
#include "MailQueue.h"
#include <utility>

//! 构造函数
iCAX::Mail::CMailQueue::CMailQueue()
    : m_Mutex()
    , m_vecMails()
{
}

//! 析构函数
iCAX::Mail::CMailQueue::~CMailQueue()
{
    Clear();
}

//! 移动构造函数
iCAX::Mail::CMailQueue::CMailQueue(CMailQueue&& Other_) noexcept
    : m_Mutex()
    , m_vecMails()
{
    std::lock_guard<std::mutex> _Lock(Other_.m_Mutex);
    m_vecMails.swap(Other_.m_vecMails);
}

//! 移动赋值
iCAX::Mail::CMailQueue& iCAX::Mail::CMailQueue::operator=(CMailQueue&& Other_) noexcept
{
    if (this != &Other_)
    {
        std::scoped_lock _Lock(m_Mutex, Other_.m_Mutex);
        ReleasePayloads(m_vecMails);
        m_vecMails.swap(Other_.m_vecMails);
    }
    return *this;
}

//! 入队邮件
void iCAX::Mail::CMailQueue::Enqueue(const Mail& Mail_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_vecMails.push_back(Mail_);
}

//! 取出邮件
std::vector<iCAX::Mail::Mail> iCAX::Mail::CMailQueue::Drain()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    std::vector<Mail> _Mails;
    _Mails.swap(m_vecMails);
    return _Mails;
}

//! 清空队列
void iCAX::Mail::CMailQueue::Clear()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    ReleasePayloads(m_vecMails);
}

void iCAX::Mail::CMailQueue::ReleasePayloads(std::vector<Mail>& Mails_) noexcept
{
    for (auto& _Mail : Mails_)
    {
        delete[] _Mail.Payload.pData;
        _Mail.Payload.pData = nullptr;
        _Mail.Payload.nSize = 0;
    }
    Mails_.clear();
}
