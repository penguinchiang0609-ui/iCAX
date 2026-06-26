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
    std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
    for (auto& _Entry : m_mapSingletons)
    {
        if (_Entry.second)
        {
            _Entry.second->OnUnload();
        }
    }
    m_mapSingletons.clear();
}
