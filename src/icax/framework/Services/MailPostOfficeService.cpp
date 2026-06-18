#include "pch.h"
#include "MailPostOfficeService.h"

//! 构造函数
iCAX::Services::CMailPostOfficeService::CMailPostOfficeService()
{
}

//! 析构函数
iCAX::Services::CMailPostOfficeService::~CMailPostOfficeService()
{
}

//! 加载
void iCAX::Services::CMailPostOfficeService::OnLoad()
{
}

//! 卸载
void iCAX::Services::CMailPostOfficeService::OnUnload()
{
    ClearPostOffices();
}

//! 获取 backend 视角的邮局
iCAX::Mail::CMailPostOffice iCAX::Services::CMailPostOfficeService::GetBackendPostOffice(IN const iCAX::Data::uuid& ID_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto _Iterator = m_Channels.try_emplace(ID_).first;
    return _Iterator->second.GetPostOffice(iCAX::Mail::kMailEndB);
}

//! 获取 frontend 视角的邮局
iCAX::Mail::CMailPostOffice iCAX::Services::CMailPostOfficeService::GetFrontendPostOffice(IN const iCAX::Data::uuid& ID_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto _Iterator = m_Channels.try_emplace(ID_).first;
    return _Iterator->second.GetPostOffice(iCAX::Mail::kMailEndA);
}

bool iCAX::Services::CMailPostOfficeService::RemovePostOffice(IN const iCAX::Data::uuid& ID_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_Channels.erase(ID_) > 0;
}

void iCAX::Services::CMailPostOfficeService::ClearPostOffices()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_Channels.clear();
}
