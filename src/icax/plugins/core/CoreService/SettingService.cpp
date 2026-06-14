#include "pch.h"
#include "SettingService.h"

//! 构造函数
iCAX::Core::CSettingService::CSettingService()
{
}

//! 析构函数
iCAX::Core::CSettingService::~CSettingService()
{
}

//! 加载
void iCAX::Core::CSettingService::OnLoad()
{
}

//! 卸载
void iCAX::Core::CSettingService::OnUnload()
{
}

//! 设置
void iCAX::Core::CSettingService::SetSetting(IN const std::shared_ptr<iCAX::Data::PropertyBag>& pApplication_, IN const std::shared_ptr<iCAX::Data::PropertyBag>& pUsr_)
{
    m_pApplication = pApplication_;
    m_pUsr = pUsr_;
}

//! 获取应用程序配置
const iCAX::Data::PropertyBag& iCAX::Core::CSettingService::GetApplicationSetting() const
{
    if (m_pApplication)
    {
        return *m_pApplication;
    }
    else
    {
        static iCAX::Data::PropertyBag s_Setting;
        return s_Setting;
    }
}

//! 获取用户配置
const iCAX::Data::PropertyBag& iCAX::Core::CSettingService::GetUsrSetting() const
{
    if (m_pUsr)
    {
        return *m_pUsr;
    }
    else
    {
        static iCAX::Data::PropertyBag s_Setting;
        return s_Setting;
    }
}
