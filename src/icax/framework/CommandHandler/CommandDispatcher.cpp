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

iCAX::Command::CCommandResponse iCAX::Command::CCommandDispatcher::Dispatch(IN const CCommandRequest& Request_, IN ICommandContext& Context_) const
{
    CCommandResponse _Response;
    _Response.nCommandID = Request_.nCommandID;
    _Response.nOriginID = Request_.nOriginID;
    _Response.nTypeCode = Request_.nTypeCode;

    auto _pHandler = m_pRegistry->Find(Request_.nTypeCode);
    if (!_pHandler)
    {
        _Response.nStatus = ECommandStatusCode::NoHandler;
        _Response.strError = "Command handler not found";
        return _Response;
    }

    try
    {
        _Response = _pHandler->Handle(Request_, Context_);
        // handler 可以只填业务状态和 Payload；请求身份字段由 Dispatcher 统一回填，保证响应可被调用方关联。
        _Response.nCommandID = Request_.nCommandID;
        _Response.nOriginID = Request_.nOriginID;
        _Response.nTypeCode = Request_.nTypeCode;
        return _Response;
    }
    catch (const std::invalid_argument& _Error)
    {
        _Response.nStatus = ECommandStatusCode::InvalidRequest;
        _Response.strError = _Error.what();
        return _Response;
    }
    catch (const std::exception& _Error)
    {
        _Response.nStatus = ECommandStatusCode::ExecutionError;
        _Response.strError = _Error.what();
        return _Response;
    }
    catch (...)
    {
        _Response.nStatus = ECommandStatusCode::ExecutionError;
        _Response.strError = "Unknown command execution error";
        return _Response;
    }
}
