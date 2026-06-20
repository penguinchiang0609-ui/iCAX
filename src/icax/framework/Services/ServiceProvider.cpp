#include "pch.h"
#include "ServiceProvider.h"
#include "ServiceRegistrationCatalog.h"

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
    for (auto& _Entry : m_mapSingletons)
    {
        _Entry.second->OnUnload();
    }
    m_mapSingletons.clear();
}

//! 获取全局服务供应商
std::shared_ptr<iCAX::Services::CServiceProvider> iCAX::Services::GetGlobalServiceProvider()
{
    static std::shared_ptr<CServiceProvider> s_pServiceProvider = std::make_shared<CServiceProvider>();
    static size_t s_nReplayedRegistrationCount = 0;
    s_nReplayedRegistrationCount = CServiceRegistrationCatalog::ReplayFrom(s_nReplayedRegistrationCount, *s_pServiceProvider);
    return s_pServiceProvider;
}
