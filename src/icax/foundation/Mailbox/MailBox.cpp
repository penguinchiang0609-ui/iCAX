#include "pch.h"
#include "MailBox.h"
#include <utility>

//! 构造函数
iCAX::Mailbox::CMailBox::CMailBox()
    : m_Mutex()
    , m_vecMails()
{
}

//! 析构函数
iCAX::Mailbox::CMailBox::~CMailBox()
{
    ClearMails();
}

//! 移动构造函数
iCAX::Mailbox::CMailBox::CMailBox(CMailBox&& Other_) noexcept
    : m_Mutex()
    , m_vecMails()
{
    std::lock_guard<std::mutex> _Lock(Other_.m_Mutex);
    m_vecMails.swap(Other_.m_vecMails);
}

//! 移动赋值
iCAX::Mailbox::CMailBox& iCAX::Mailbox::CMailBox::operator=(CMailBox&& Other_) noexcept
{
    if (this != &Other_)
    {
        std::scoped_lock _Lock(m_Mutex, Other_.m_Mutex);
        ReleasePayloads(m_vecMails);
        m_vecMails.swap(Other_.m_vecMails);
    }
    return *this;
}

//! 投递邮件
void iCAX::Mailbox::CMailBox::DeliverMail(const Mail& Mail_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_vecMails.push_back(Mail_);
}

//! 获取邮件
std::vector<iCAX::Mailbox::Mail> iCAX::Mailbox::CMailBox::RetrieveMails()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    std::vector<Mail> RetrievedMails;
    RetrievedMails.swap(m_vecMails);
    return RetrievedMails;
}

//! 清空邮件
void iCAX::Mailbox::CMailBox::ClearMails()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    ReleasePayloads(m_vecMails);
}

void iCAX::Mailbox::CMailBox::ReleasePayloads(std::vector<Mail>& Mails_) noexcept
{
    for (auto& _Mail : Mails_)
    {
        delete[] _Mail.Payload.pData;
        _Mail.Payload.pData = nullptr;
        _Mail.Payload.nSize = 0;
    }
    Mails_.clear();
}
