#pragma once

#include "FacadeRegistry.h"

namespace iCAX::Interaction
{
    /*
    * @brief 在一个运行范围内调用 Facade 方法。
    */
    class _FACADES_EXP CFacadeInvoker final
    {
    public:
        explicit CFacadeInvoker(IN std::shared_ptr<CFacadeRegistry> pRegistry_);
        ~CFacadeInvoker() = default;

        CFacadeInvoker(IN const CFacadeInvoker&) = delete;
        CFacadeInvoker& operator=(IN const CFacadeInvoker&) = delete;

        CFacadeResult Invoke(
            IN const CFacadeCall& Call_,
            IN iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_,
            IN iCAX::Project::ISceneContext* pSceneContext_) const;

    private:
        std::shared_ptr<CFacadeRegistry> m_pRegistry;
    };
}
