#include "pch.h"
#include "FacadeInvoker.h"

#include <stdexcept>
#include <utility>

iCAX::Interaction::CFacadeInvoker::CFacadeInvoker(IN std::shared_ptr<CFacadeRegistry> pRegistry_)
    : m_pRegistry(std::move(pRegistry_))
{
    if (!m_pRegistry)
    {
        throw std::invalid_argument("Facade registry cannot be null");
    }
}

iCAX::Interaction::CFacadeResult iCAX::Interaction::CFacadeInvoker::Invoke(
    IN const CFacadeCall& Call_,
    IN iCAX::Application::IApplicationContext& ApplicationContext_,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_) const
{
    CFacadeResult _Result;
    _Result.nCallID = Call_.nCallID;
    _Result.nOriginID = Call_.nOriginID;
    _Result.Method = Call_.Method;

    if (!Call_.Method.IsValid())
    {
        _Result.nStatus = EFacadeCallStatus::InvalidCall;
        _Result.strError = "Facade call has no valid method";
        return _Result;
    }

    const auto _Facade = m_pRegistry->Find(Call_.Method.nFacadeCode);
    if (!_Facade)
    {
        _Result.nStatus = EFacadeCallStatus::FacadeNotFound;
        _Result.strError = "Facade not found: " + FormatFacadeMethod(Call_.Method);
        return _Result;
    }

    _Result = _Facade->Invoke(
        Call_,
        ApplicationContext_,
        pProductContext_,
        pProjectContext_,
        pSceneContext_);

    // Facade 方法只需填写业务状态和 Payload；调用身份由 Invoker 统一回填。
    _Result.nCallID = Call_.nCallID;
    _Result.nOriginID = Call_.nOriginID;
    _Result.Method = Call_.Method;
    return _Result;
}
