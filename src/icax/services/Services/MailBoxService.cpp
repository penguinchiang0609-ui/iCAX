#include "pch.h"
#include "MailBoxService.h"

//! 构造函数
iCAX::Services::CMailBoxService::CMailBoxService()
{
}

//! 析构函数
iCAX::Services::CMailBoxService::~CMailBoxService()
{
}

//! 加载
void iCAX::Services::CMailBoxService::OnLoad()
{
}

//! 卸载
void iCAX::Services::CMailBoxService::OnUnload()
{
}

//! 获取前端邮件柜
iCAX::Mailbox::CMailBox& iCAX::Services::CMailBoxService::GetOutBox(IN const iCAX::Data::uuid& ID_)
{
    if (!m_FrontendMailBoxes.contains(ID_))
    {
        m_FrontendMailBoxes[ID_] = iCAX::Mailbox::CMailBox();
    }
    return  m_FrontendMailBoxes[ID_];
}

//! 获取后端邮件柜
iCAX::Mailbox::CMailBox& iCAX::Services::CMailBoxService::GetInBox(IN const iCAX::Data::uuid& ID_)
{
    if (!m_BackendMailBoxes.contains(ID_))
    {
        m_BackendMailBoxes[ID_] = iCAX::Mailbox::CMailBox();
    }
    return  m_BackendMailBoxes[ID_];
}


