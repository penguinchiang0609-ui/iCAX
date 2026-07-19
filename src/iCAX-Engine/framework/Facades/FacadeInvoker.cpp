#include "pch.h"
#include "FacadeInvoker.h"

#include "FacadeEndpoint.h"
#include "FacadeFrame.h"

iCAX::Interaction::CFacadeInvoker::CFacadeInvoker(IN std::shared_ptr<CFacadeRegistry> pRegistry_)
    : m_pRegistry(std::move(pRegistry_))
{
    if (!m_pRegistry)
    {
        throw std::invalid_argument("Facade registry cannot be null");
    }
}

iCAX::Interaction::CInvocationResult iCAX::Interaction::CFacadeInvoker::Invoke(
    IN const CInvocation& Call_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_) const
{
    CInvocationResult _Result;
    _Result.nCallID = Call_.nCallID;
    _Result.Method = Call_.Method;

    if (!Call_.Method.IsValid())
    {
        _Result.nStatus = EInvocationStatus::InvalidInvocation;
        _Result.strError = "Facade invocation has no valid method";
        return _Result;
    }

    const auto _Facade = m_pRegistry->Find(Call_.Method.nFacadeCode);
    if (!_Facade)
    {
        _Result.nStatus = EInvocationStatus::FacadeNotFound;
        _Result.strError = "Facade not found: " + FormatFacadeMethod(Call_.Method);
        return _Result;
    }

    _Result = _Facade->Invoke(
        Call_,
        ApplicationContext_,
        pProductContext_,
        pProjectContext_,
        pSceneContext_);

    _Result.nCallID = Call_.nCallID;
    _Result.Method = Call_.Method;
    return _Result;
}

iCAX::Interaction::CFacadeInvoker::RemoteTask iCAX::Interaction::CFacadeInvoker::CallRemote(
    IN const CFacadeEndpoint& Endpoint_,
    IN const CFacadeMethod& Method_,
    IN const std::vector<uint8_t>& Payload_,
    IN RemoteReportHandler ReportHandler_,
    IN iCAX::Tasks::TaskSchedulerPtr Scheduler_) const
{
    if (!Endpoint_.IsValid())
    {
        throw std::invalid_argument("Facade endpoint is not valid");
    }
    if (!Method_.IsValid())
    {
        throw std::invalid_argument("Remote Facade method is not valid");
    }
    const auto _CallID = AllocateCallID();
    iCAX::Tasks::TaskCompletionSource<CInvocationResult> _CompletionSource(std::move(Scheduler_));
    auto _Task = _CompletionSource.GetTask();
    {
        std::lock_guard<std::mutex> _Lock(m_RemoteCallMutex);
        m_PendingRemoteCalls.emplace(
            _CallID,
            CPendingRemoteCall{ Method_, _CompletionSource, std::move(ReportHandler_) });
    }

    CFacadeFrame _Frame;
    _Frame.nCallID = _CallID;
    _Frame.nMethodCode = Method_.GetCode();
    _Frame.nKind = EFacadeFrameKind::Request;
    _Frame.Payload = Payload_;
    try
    {
        Endpoint_.Send(std::move(_Frame));
    }
    catch (...)
    {
        {
            std::lock_guard<std::mutex> _Lock(m_RemoteCallMutex);
            m_PendingRemoteCalls.erase(_CallID);
        }
        _CompletionSource.TrySetException(std::current_exception());
    }
    return _Task;
}

size_t iCAX::Interaction::CFacadeInvoker::DispatchAvailableFrames(
    IN const CFacadeEndpoint& Endpoint_,
    IN const iCAX::Application::IApplicationContext& ApplicationContext_,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_) const
{
    if (!Endpoint_.IsValid())
    {
        return 0;
    }

    auto _Frames = Endpoint_.Receive();
    for (const auto& _Frame : _Frames)
    {
        switch (_Frame.nKind)
        {
        case EFacadeFrameKind::Request:
        {
            CInvocationResult _Result;
            try
            {
                auto _Call = ToInvocation(_Frame);
                const auto _ReportEndpoint = Endpoint_;
                const auto _RequestFrame = _Frame;
                _Call.SetReportHandler([_ReportEndpoint, _RequestFrame](const std::vector<uint8_t>& Payload_) {
                    SendFacadeReport(_ReportEndpoint, _RequestFrame, Payload_);
                });
                _Result = Invoke(
                    _Call,
                    ApplicationContext_,
                    pProductContext_,
                    pProjectContext_,
                    pSceneContext_);
            }
            catch (const std::exception& Error_)
            {
                _Result.nCallID = _Frame.nCallID;
                _Result.Method = MakeFacadeMethod(_Frame.nMethodCode);
                _Result.nStatus = EInvocationStatus::InvalidInvocation;
                _Result.strError = Error_.what();
            }
            catch (...)
            {
                _Result.nCallID = _Frame.nCallID;
                _Result.Method = MakeFacadeMethod(_Frame.nMethodCode);
                _Result.nStatus = EInvocationStatus::InvalidInvocation;
                _Result.strError = "Facade method caught a non-standard exception";
            }
            SendInvocationResult(Endpoint_, _Frame, _Result);
            break;
        }
        case EFacadeFrameKind::Report:
            DispatchRemoteReport(_Frame);
            break;
        case EFacadeFrameKind::Response:
            DispatchRemoteResult(_Frame);
            break;
        case EFacadeFrameKind::Event:
            DispatchEvent(_Frame);
            break;
        }
    }
    return _Frames.size();
}

void iCAX::Interaction::CFacadeInvoker::SetEventHandler(IN EventHandler Handler_)
{
    std::lock_guard<std::mutex> _Lock(m_EventHandlerMutex);
    m_EventHandler = std::move(Handler_);
}

iCAX::Interaction::CInvocation iCAX::Interaction::CFacadeInvoker::ToInvocation(
    IN const CFacadeFrame& Frame_)
{
    if (Frame_.nKind != EFacadeFrameKind::Request)
    {
        throw std::invalid_argument("Facade invocation requires a request frame");
    }

    CInvocation _Call;
    _Call.nCallID = Frame_.nCallID;
    _Call.Method = MakeFacadeMethod(Frame_.nMethodCode);
    _Call.Payload = Frame_.Payload;
    return _Call;
}

iCAX::Interaction::CInvocationResult iCAX::Interaction::CFacadeInvoker::ToInvocationResult(
    IN const CFacadeFrame& Frame_,
    IN const CFacadeMethod& Method_)
{
    CInvocationResult _Result;
    _Result.nCallID = Frame_.nCallID;
    _Result.Method = Method_;
    _Result.nStatus = Frame_.nStatus;
    _Result.Payload = Frame_.Payload;
    if (_Result.nStatus != EInvocationStatus::Ok)
    {
        _Result.strError.assign(_Result.Payload.begin(), _Result.Payload.end());
    }
    return _Result;
}

void iCAX::Interaction::CFacadeInvoker::SendFacadeReport(
    IN const CFacadeEndpoint& Endpoint_,
    IN const CFacadeFrame& RequestFrame_,
    IN const std::vector<uint8_t>& Payload_)
{
    CFacadeFrame _Frame;
    _Frame.nCallID = RequestFrame_.nCallID;
    _Frame.nMethodCode = RequestFrame_.nMethodCode;
    _Frame.nKind = EFacadeFrameKind::Report;
    _Frame.Payload = Payload_;
    Endpoint_.Send(std::move(_Frame));
}

void iCAX::Interaction::CFacadeInvoker::SendInvocationResult(
    IN const CFacadeEndpoint& Endpoint_,
    IN const CFacadeFrame& RequestFrame_,
    IN const CInvocationResult& Result_)
{
    CFacadeFrame _Frame;
    _Frame.nCallID = RequestFrame_.nCallID;
    _Frame.nMethodCode = RequestFrame_.nMethodCode;
    _Frame.nKind = EFacadeFrameKind::Response;
    _Frame.nStatus = Result_.nStatus;
    _Frame.Payload = Result_.Payload;
    if (_Frame.Payload.empty() && !Result_.strError.empty())
    {
        _Frame.Payload.assign(Result_.strError.begin(), Result_.strError.end());
    }
    Endpoint_.Send(std::move(_Frame));
}

void iCAX::Interaction::CFacadeInvoker::DispatchRemoteReport(IN const CFacadeFrame& Frame_) const
{
    RemoteReportHandler _Handler;
    CFacadeMethod _Method;
    {
        std::lock_guard<std::mutex> _Lock(m_RemoteCallMutex);
        auto _Iter = m_PendingRemoteCalls.find(Frame_.nCallID);
        if (_Iter == m_PendingRemoteCalls.end())
        {
            return;
        }
        _Method = _Iter->second.Method;
        _Handler = _Iter->second.ReportHandler;
    }
    if (_Handler)
    {
        auto _Result = ToInvocationResult(Frame_, _Method);
        _Handler(_Result);
    }
}

void iCAX::Interaction::CFacadeInvoker::DispatchRemoteResult(IN const CFacadeFrame& Frame_) const
{
    CPendingRemoteCall _Pending;
    {
        std::lock_guard<std::mutex> _Lock(m_RemoteCallMutex);
        auto _Iter = m_PendingRemoteCalls.find(Frame_.nCallID);
        if (_Iter == m_PendingRemoteCalls.end())
        {
            return;
        }
        _Pending = std::move(_Iter->second);
        m_PendingRemoteCalls.erase(_Iter);
    }
    _Pending.CompletionSource.TrySetResult(ToInvocationResult(Frame_, _Pending.Method));
}

void iCAX::Interaction::CFacadeInvoker::DispatchEvent(IN const CFacadeFrame& Frame_) const
{
    EventHandler _Handler;
    {
        std::lock_guard<std::mutex> _Lock(m_EventHandlerMutex);
        _Handler = m_EventHandler;
    }
    if (_Handler)
    {
        _Handler(Frame_);
    }
}

uint64_t iCAX::Interaction::CFacadeInvoker::AllocateCallID() const noexcept
{
    auto _CallID = m_nNextCallID.fetch_add(1, std::memory_order_relaxed);
    if (_CallID == 0)
    {
        _CallID = m_nNextCallID.fetch_add(1, std::memory_order_relaxed);
    }
    return _CallID;
}
