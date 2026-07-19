#include "pch.h"
#include "FrontendBridge.h"

#include "ApplicationRuntime/ApplicationRuntime.h"
#include "Data/uuid.h"
#include "Facades/FacadePayload.h"
#include "Facades/FacadeEndpoint.h"

#include <chrono>


namespace
{
    std::string _ToString(IN const iCAX::Data::uuid& ID_)
    {
        return iCAX::Data::to_string(ID_);
    }

    iCAX::Data::uuid _ParseChannelID(IN const std::string& strChannelID_)
    {
        auto _ID = iCAX::Data::uuid::from_string(strChannelID_);
        if (!_ID || _ID->is_nil())
        {
            throw std::invalid_argument("Invalid channel id: " + strChannelID_);
        }
        return *_ID;
    }

    iCAX::Frontend::CFrontendFacadeFrame _ToFrontendFrame(
        IN const iCAX::Data::uuid& ChannelID_,
        IN const iCAX::Interaction::CFacadeFrame& Frame_)
    {
        iCAX::Frontend::CFrontendFacadeFrame _Result;
        _Result.ChannelID = _ToString(ChannelID_);
        _Result.nCallID = Frame_.nCallID;
        _Result.nMethodCode = Frame_.nMethodCode;
        _Result.nKind = static_cast<uint16_t>(Frame_.nKind);
        _Result.nStatus = static_cast<uint16_t>(Frame_.nStatus);
        _Result.PayloadText = iCAX::Interaction::GetFacadePayloadText(Frame_);
        return _Result;
    }

    iCAX::Interaction::CFacadeFrame _ToFacadeFrame(IN const iCAX::Frontend::CFrontendFacadeFrame& Frame_)
    {
        return iCAX::Interaction::CreateTextFacadeFrame(
            Frame_.nCallID,
            Frame_.nMethodCode,
            static_cast<iCAX::Interaction::EFacadeFrameKind>(Frame_.nKind),
            Frame_.PayloadText,
            static_cast<iCAX::Interaction::EInvocationStatus>(Frame_.nStatus));
    }
}

class iCAX::Application::CFrontendBridge::Impl final
{
public:
    using EndpointResolver = std::function<iCAX::Interaction::CFacadeEndpoint()>;

    struct RegisteredChannel final
    {
        EndpointResolver Resolver;
        bool bApplicationChannel = false;
    };

    mutable std::mutex Mutex;
    iCAX::Application::CApplicationRuntime* pRuntime = nullptr;
    std::map<iCAX::Data::uuid, RegisteredChannel> FrontendChannels;
    FrontendFacadeFrameHandler FrameHandler;
    std::shared_ptr<iCAX::Tasks::EventLoopTaskScheduler> pFrontTaskScheduler =
        std::make_shared<iCAX::Tasks::EventLoopTaskScheduler>();
    iCAX::Coroutines::CCoroutineRuntime FrontCoroutineRuntime{ pFrontTaskScheduler };
    std::chrono::steady_clock::time_point CoroutineStartTime;
    std::chrono::steady_clock::time_point LastCoroutineTickTime;
    bool bHasCoroutineClock = false;

public:
    iCAX::Application::CApplicationRuntime& RequireRuntime() const
    {
        std::lock_guard<std::mutex> _Lock(Mutex);
        if (!pRuntime)
        {
            throw std::logic_error("FrontendBridge is not attached to ApplicationRuntime");
        }
        return *pRuntime;
    }

    void RegisterChannel(
        IN const iCAX::Data::uuid& ChannelID_,
        IN EndpointResolver Resolver_,
        IN bool bApplicationChannel_)
    {
        if (ChannelID_.is_nil())
        {
            throw std::invalid_argument("Channel id cannot be nil");
        }
        if (!Resolver_)
        {
            throw std::invalid_argument("Frontend Facade endpoint resolver is empty");
        }

        auto _Endpoint = Resolver_();
        if (!_Endpoint.IsValid())
        {
            throw std::invalid_argument("Frontend Facade endpoint is not valid");
        }

        std::lock_guard<std::mutex> _Lock(Mutex);
        if (!pRuntime)
        {
            throw std::logic_error("FrontendBridge is not attached to ApplicationRuntime");
        }
        FrontendChannels[ChannelID_] = RegisteredChannel{ std::move(Resolver_), bApplicationChannel_ };
    }

    iCAX::Interaction::CFacadeEndpoint ResolveRegisteredEndpoint(IN const iCAX::Data::uuid& ChannelID_) const
    {
        RegisteredChannel _Channel;
        {
            std::lock_guard<std::mutex> _Lock(Mutex);
            auto _Iter = FrontendChannels.find(ChannelID_);
            if (_Iter == FrontendChannels.end())
            {
                throw std::runtime_error("Frontend Facade endpoint is not registered: " + _ToString(ChannelID_));
            }
            _Channel = _Iter->second;
        }
        return _Channel.Resolver();
    }

    std::vector<std::pair<iCAX::Data::uuid, RegisteredChannel>> SnapshotChannels() const
    {
        std::lock_guard<std::mutex> _Lock(Mutex);
        std::vector<std::pair<iCAX::Data::uuid, RegisteredChannel>> _Channels;
        _Channels.reserve(FrontendChannels.size());
        for (const auto& _Item : FrontendChannels)
        {
            _Channels.emplace_back(_Item.first, _Item.second);
        }
        return _Channels;
    }

    void RemoveChannelIfCurrent(IN const iCAX::Data::uuid& ChannelID_, IN bool bAllowApplicationChannel_)
    {
        std::lock_guard<std::mutex> _Lock(Mutex);
        auto _Iter = FrontendChannels.find(ChannelID_);
        if (_Iter == FrontendChannels.end())
        {
            return;
        }
        if (_Iter->second.bApplicationChannel && !bAllowApplicationChannel_)
        {
            return;
        }
        FrontendChannels.erase(_Iter);
    }
};

iCAX::Application::CFrontendBridge::CFrontendBridge()
    : m_pImpl(std::make_unique<Impl>())
{
}

iCAX::Application::CFrontendBridge::~CFrontendBridge() = default;

void iCAX::Application::CFrontendBridge::Attach(iCAX::Application::CApplicationRuntime& Runtime_)
{
    if (!Runtime_.IsRunning())
    {
        throw std::logic_error("FrontendBridge can only attach to a running ApplicationRuntime");
    }

    m_pImpl->FrontCoroutineRuntime.CancelAll();
    m_pImpl->pFrontTaskScheduler->Clear();
    m_pImpl->bHasCoroutineClock = false;

    std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
    m_pImpl->pRuntime = &Runtime_;
    m_pImpl->FrontendChannels.clear();

    const auto _ApplicationChannelID = Runtime_.GetApplicationChannelID();
    if (_ApplicationChannelID.is_nil())
    {
        m_pImpl->pRuntime = nullptr;
        throw std::logic_error("Application channel id is nil");
    }
    auto _ApplicationEndpoint = Runtime_.GetApplicationFrontendFacadeEndpoint();
    if (!_ApplicationEndpoint.IsValid())
    {
        m_pImpl->pRuntime = nullptr;
        throw std::logic_error("Application frontend Facade endpoint is not valid");
    }
    m_pImpl->FrontendChannels[_ApplicationChannelID] = Impl::RegisteredChannel{
        [&Runtime_]() {
            return Runtime_.GetApplicationFrontendFacadeEndpoint();
        },
        true
    };
}

void iCAX::Application::CFrontendBridge::Detach()
{
    {
        std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
        m_pImpl->FrameHandler = nullptr;
        m_pImpl->FrontendChannels.clear();
        m_pImpl->pRuntime = nullptr;
    }
    m_pImpl->FrontCoroutineRuntime.CancelAll();
    m_pImpl->pFrontTaskScheduler->Clear();
    m_pImpl->bHasCoroutineClock = false;
}

bool iCAX::Application::CFrontendBridge::IsAttached() const
{
    std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
    return m_pImpl->pRuntime != nullptr;
}

std::string iCAX::Application::CFrontendBridge::GetApplicationChannelIDText() const
{
    auto& _Runtime = m_pImpl->RequireRuntime();
    return _ToString(_Runtime.GetApplicationChannelID());
}

std::string iCAX::Application::CFrontendBridge::RegisterProductChannel(const std::string& strProductID_)
{
    auto& _Runtime = m_pImpl->RequireRuntime();
    auto _pRuntime = _Runtime.FindProductRuntime(strProductID_);
    if (!_pRuntime)
    {
        throw std::runtime_error("Product runtime is not started: " + strProductID_);
    }

    const auto _ChannelID = _pRuntime->GetProductChannelID();
    m_pImpl->RegisterChannel(
        _ChannelID,
        [&_Runtime, strProductID_]() {
            return _Runtime.GetProductFrontendFacadeEndpoint(strProductID_);
        },
        false);
    return _ToString(_ChannelID);
}

std::string iCAX::Application::CFrontendBridge::RegisterSceneChannel(
    const std::string& strProjectID_,
    const std::string& strSceneID_)
{
    auto& _Runtime = m_pImpl->RequireRuntime();
    auto _ProjectID = _ParseChannelID(strProjectID_);
    auto _SceneID = _ParseChannelID(strSceneID_);
    for (const auto& _pRuntime : _Runtime.GetProductRuntimes())
    {
        if (!_pRuntime)
        {
            continue;
        }

        auto _pCatalog = _pRuntime->FindProjectCatalogByProjectID(_ProjectID);
        if (!_pCatalog)
        {
            continue;
        }

        auto _pProject = _pCatalog->FindProject(_ProjectID);
        if (!_pProject)
        {
            continue;
        }

        auto _pScene = _pProject->GetScene(_SceneID);
        if (!_pScene)
        {
            continue;
        }

        const auto _SceneChannelID = _pScene->GetSceneChannelID();
        m_pImpl->RegisterChannel(
            _SceneChannelID,
            [&_Runtime, _ProjectID, _SceneID]() {
                return _Runtime.GetSceneFrontendFacadeEndpoint(_ProjectID, _SceneID);
            },
            false);
        return _ToString(_SceneChannelID);
    }

    throw std::runtime_error("Scene runtime is not found: " + strProjectID_ + "/" + strSceneID_);
}

void iCAX::Application::CFrontendBridge::PostFacadeFrame(const CFrontendFacadeFrame& Frame_)
{
    const auto _ChannelID = _ParseChannelID(Frame_.ChannelID);
    auto _Endpoint = m_pImpl->ResolveRegisteredEndpoint(_ChannelID);
    _Endpoint.Send(_ToFacadeFrame(Frame_));
}

std::vector<iCAX::Application::CFrontendFacadeFrame> iCAX::Application::CFrontendBridge::PollFacadeFrames()
{
    auto _Channels = m_pImpl->SnapshotChannels();

    std::vector<CFrontendFacadeFrame> _Result;
    for (const auto& _Item : _Channels)
    {
        iCAX::Interaction::CFacadeEndpoint _Endpoint;
        try
        {
            _Endpoint = _Item.second.Resolver();
            if (!_Endpoint.IsValid())
            {
                m_pImpl->RemoveChannelIfCurrent(_Item.first, false);
                continue;
            }
        }
        catch (...)
        {
            m_pImpl->RemoveChannelIfCurrent(_Item.first, false);
            continue;
        }

        auto _Frames = _Endpoint.Receive();
        for (auto& _Frame : _Frames)
        {
            auto _Envelope = _ToFrontendFrame(_Item.first, _Frame);
            _Result.push_back(_Envelope);

            FrontendFacadeFrameHandler _Handler;
            {
                std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
                _Handler = m_pImpl->FrameHandler;
            }
            if (_Handler)
            {
                _Handler(_Envelope);
            }

        }
    }
    return _Result;
}

void iCAX::Application::CFrontendBridge::SetFacadeFrameHandler(FrontendFacadeFrameHandler Handler_)
{
    std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
    m_pImpl->FrameHandler = std::move(Handler_);
}

iCAX::Tasks::TaskSchedulerPtr iCAX::Application::CFrontendBridge::GetFrontTaskScheduler() const
{
    return m_pImpl->pFrontTaskScheduler;
}

std::size_t iCAX::Application::CFrontendBridge::RunFrontTasks()
{
    iCAX::Tasks::CurrentTaskSchedulerScope _SchedulerScope(m_pImpl->pFrontTaskScheduler);
    return m_pImpl->pFrontTaskScheduler->RunAll();
}

void iCAX::Application::CFrontendBridge::PauseFrontCoroutine(
    const iCAX::Coroutines::CCoroutineHandleBase& handle_)
{
    m_pImpl->FrontCoroutineRuntime.Pause(handle_);
}

void iCAX::Application::CFrontendBridge::ResumeFrontCoroutine(
    const iCAX::Coroutines::CCoroutineHandleBase& handle_)
{
    m_pImpl->FrontCoroutineRuntime.Resume(handle_);
}

void iCAX::Application::CFrontendBridge::CancelFrontCoroutine(
    const iCAX::Coroutines::CCoroutineHandleBase& handle_)
{
    m_pImpl->FrontCoroutineRuntime.Cancel(handle_);
}

bool iCAX::Application::CFrontendBridge::IsFrontCoroutineRunning(
    const iCAX::Coroutines::CCoroutineHandleBase& handle_) const
{
    return m_pImpl->FrontCoroutineRuntime.IsRunning(handle_);
}

std::size_t iCAX::Application::CFrontendBridge::RunFrontCoroutines()
{
    iCAX::Tasks::CurrentTaskSchedulerScope _SchedulerScope(m_pImpl->pFrontTaskScheduler);
    const auto _Now = std::chrono::steady_clock::now();
    if (!m_pImpl->bHasCoroutineClock)
    {
        m_pImpl->CoroutineStartTime = _Now;
        m_pImpl->LastCoroutineTickTime = _Now;
        m_pImpl->bHasCoroutineClock = true;
    }

    const auto _DeltaTime = std::chrono::duration<double>(_Now - m_pImpl->LastCoroutineTickTime).count();
    const auto _TotalTime = std::chrono::duration<double>(_Now - m_pImpl->CoroutineStartTime).count();
    m_pImpl->LastCoroutineTickTime = _Now;
    return m_pImpl->FrontCoroutineRuntime.Tick(_DeltaTime, _TotalTime);
}

iCAX::Coroutines::CCoroutineRuntime&
iCAX::Application::CFrontendBridge::FrontCoroutineRuntime()
{
    return m_pImpl->FrontCoroutineRuntime;
}
