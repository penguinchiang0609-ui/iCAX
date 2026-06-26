#include "pch.h"
#include "CommandDispatcher.h"

#include <stdexcept>
#include <utility>

iCAX::Command::CCommandDispatcher::CCommandDispatcher(IN std::shared_ptr<CCommandRegistry> pRegistry_)
    : m_pRegistry(std::move(pRegistry_))
{
    if (!m_pRegistry)
    {
        throw std::invalid_argument("Command registry cannot be null");
    }
}

iCAX::Command::CCommandResponse iCAX::Command::CCommandDispatcher::Dispatch(
    IN const CCommandRequest& Request_,
    IN iCAX::Application::IApplicationContext& ApplicationContext_,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_) const
{
    CCommandResponse _Response;
    _Response.nCommandID = Request_.nCommandID;
    _Response.nOriginID = Request_.nOriginID;
    _Response.Route = Request_.Route;

    if (!Request_.Route.IsValid())
    {
        _Response.nStatus = ECommandStatusCode::InvalidRequest;
        _Response.strError = "Command route is invalid";
        return _Response;
    }

    auto _pCommandTarget = m_pRegistry->Find(Request_.Route.nMainCode);
    if (!_pCommandTarget)
    {
        _Response.nStatus = ECommandStatusCode::NoHandler;
        _Response.strError = "Command target not found: " + FormatCommandRoute(Request_.Route);
        return _Response;
    }

    _Response = _pCommandTarget->Handle(Request_, ApplicationContext_, pProductContext_, pProjectContext_);
    // handler 可以只填业务状态和 Payload；请求身份字段由 Dispatcher 统一回填，保证响应可被调用方关联。
    _Response.nCommandID = Request_.nCommandID;
    _Response.nOriginID = Request_.nOriginID;
    _Response.Route = Request_.Route;
    return _Response;
}
