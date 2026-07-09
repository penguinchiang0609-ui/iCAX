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

void iCAX::Services::CServiceProvider::UpdateSceneServices(
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN const iCAX::Product::IProductContext& ProductContext_,
    IN iCAX::Project::IProjectContext& ProjectContext_,
    IN iCAX::Project::ISceneContext& SceneContext_,
    IN const double& nDeltaTime_,
    IN const double& nTotalTime_)
{
    std::vector<std::shared_ptr<IService>> _Services;
    {
        std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
        _Services.reserve(m_mapSingletons.size());
        for (const auto& _Entry : m_mapSingletons)
        {
            if (_Entry.second)
            {
                _Services.push_back(_Entry.second);
            }
        }
    }

    for (const auto& _pService : _Services)
    {
        _pService->OnSceneTick(
            ApplicationContext_,
            ProductContext_,
            ProjectContext_,
            SceneContext_,
            nDeltaTime_,
            nTotalTime_);
    }
}
