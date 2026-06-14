#include "pch.h"
#include "ServiceProvider.h"

//!< 构造函数
iCAX::Services::CServiceProvider::CServiceProvider()
{

}

//!< 析构函数
iCAX::Services::CServiceProvider::~CServiceProvider()
{
    UnloadAll();
}

//! 卸载所有
void iCAX::Services::CServiceProvider::UnloadAll()
{
    // 清理所有注册的单例服务
    for (auto& _Entry : m_mapSingletons)
    {
        _Entry.second->OnUnload();
    }
    m_mapSingletonMask.clear();
    m_mapSingletons.clear();
}

//! 获取全局服务供应商
std::shared_ptr<iCAX::Services::CServiceProvider> iCAX::Services::GetGlobalServiceProvider()
{
    static std::shared_ptr<CServiceProvider> s_pServiceProvider = std::make_shared<CServiceProvider>();
    return s_pServiceProvider;
}
