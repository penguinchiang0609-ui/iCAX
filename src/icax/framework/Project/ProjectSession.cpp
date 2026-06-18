#include "pch.h"
#include "ProjectSession.h"

#include "Behaviour/IBehaviourRegistry.h"
#include "Behaviour/UniverseContext.h"

#include <chrono>
#include <stdexcept>
#include <utility>

class iCAX::Project::CProjectSession::CRepositoryEventForwarder final
    : public iCAX::Database::IRepositoryEventListener
{
public:
    explicit CRepositoryEventForwarder(IN iCAX::Project::CProjectSession& Session_)
        : m_Session(Session_)
    {
    }

    virtual void OnRepositoryChanging(
        IN void*,
        IN const iCAX::Database::RepositoryEventArgs& Args_) override
    {
        m_Session.OnRepositoryChanging(Args_);
    }

    virtual void OnRepositoryChanged(
        IN void*,
        IN const iCAX::Database::RepositoryEventArgs& Args_) override
    {
        m_Session.OnRepositoryChanged(Args_);
    }

private:
    iCAX::Project::CProjectSession& m_Session;
};

iCAX::Project::CProjectSession::CProjectSession(IN const CProjectSessionCreateInfo& CreateInfo_)
    : m_ProjectID(CreateInfo_.ProjectID.is_nil() ? iCAX::Data::GenerateNewUUID() : CreateInfo_.ProjectID)
    , m_Product(CreateInfo_.Product)
    , m_ProjectName(CreateInfo_.ProjectName)
    , m_ProjectPath(CreateInfo_.ProjectPath)
    , m_pApplicationSettings(CreateInfo_.pApplicationSettings ? CreateInfo_.pApplicationSettings : std::make_shared<iCAX::Data::PropertyBag>())
    , m_pRepository(iCAX::Database::GenerateRepository(m_ProjectID))
    , m_pUniverseContext(std::make_shared<iCAX::Behaviour::CUniverseContext>(m_pRepository, m_pApplicationSettings))
    , m_pUniverse(iCAX::Behaviour::GenerateUniverse())
    , m_pRepositoryEventForwarder(std::make_shared<CRepositoryEventForwarder>(*this))
    , m_Resources()
    , m_MailChannel()
    , m_nFrameIntervalMilliseconds(CreateInfo_.nFrameIntervalMilliseconds == 0 ? 1 : CreateInfo_.nFrameIntervalMilliseconds)
    , m_FrameHandler(CreateInfo_.FrameHandler)
{
    if (!m_Product.IsValid())
    {
        throw std::invalid_argument("ProjectSession requires a valid product definition");
    }
    if (m_ProjectName.empty())
    {
        m_ProjectName = m_Product.GetDisplayNameOrID();
    }
    m_pRepository->AddObserver(m_pRepositoryEventForwarder);
}

iCAX::Project::CProjectSession::~CProjectSession()
{
    Close();
}

const iCAX::Data::uuid& iCAX::Project::CProjectSession::GetProjectID() const
{
    return m_ProjectID;
}

const std::string& iCAX::Project::CProjectSession::GetProjectName() const
{
    return m_ProjectName;
}

void iCAX::Project::CProjectSession::SetProjectName(IN const std::string& strName_)
{
    m_ProjectName = strName_;
}

const std::string& iCAX::Project::CProjectSession::GetProjectPath() const
{
    return m_ProjectPath;
}

void iCAX::Project::CProjectSession::SetProjectPath(IN const std::string& strPath_)
{
    m_ProjectPath = strPath_;
}

const std::string& iCAX::Project::CProjectSession::GetProductID() const
{
    return m_Product.ProductID;
}

const iCAX::Project::CProductDefinition& iCAX::Project::CProjectSession::GetProductDefinition() const
{
    return m_Product;
}

bool iCAX::Project::CProjectSession::IsOpen() const
{
    return m_pRepository != nullptr && m_pUniverseContext != nullptr && m_pUniverse != nullptr;
}

bool iCAX::Project::CProjectSession::IsRunning() const
{
    return GetState() == EProjectSessionState::Running;
}

iCAX::Project::EProjectSessionState iCAX::Project::CProjectSession::GetState() const
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    return m_State;
}

std::optional<iCAX::Project::CProjectSessionFault> iCAX::Project::CProjectSession::GetLastFault() const
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    return m_LastFault;
}

void iCAX::Project::CProjectSession::SetFrameHandler(IN ProjectFrameHandler Handler_)
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    m_FrameHandler = std::move(Handler_);
}

iCAX::Database::IRepository& iCAX::Project::CProjectSession::Database()
{
    EnsureOpen();
    return *m_pRepository;
}

const iCAX::Database::IRepository& iCAX::Project::CProjectSession::Database() const
{
    EnsureOpen();
    return *m_pRepository;
}

iCAX::Behaviour::IUniverse& iCAX::Project::CProjectSession::Universe()
{
    EnsureOpen();
    return *m_pUniverse;
}

const iCAX::Behaviour::IUniverse& iCAX::Project::CProjectSession::Universe() const
{
    EnsureOpen();
    return *m_pUniverse;
}

iCAX::Resource::CResourceLibrary& iCAX::Project::CProjectSession::Resources()
{
    return m_Resources;
}

const iCAX::Resource::CResourceLibrary& iCAX::Project::CProjectSession::Resources() const
{
    return m_Resources;
}

iCAX::Mail::CMailPostOffice iCAX::Project::CProjectSession::GetBackendPostOffice()
{
    EnsureOpen();
    return m_MailChannel.GetEndBPostOffice();
}

iCAX::Mail::CMailPostOffice iCAX::Project::CProjectSession::GetFrontendPostOffice()
{
    EnsureOpen();
    return m_MailChannel.GetEndAPostOffice();
}

void iCAX::Project::CProjectSession::Start()
{
    std::thread _ThreadToJoin;
    {
        std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
        if (m_WorkThread.joinable()
            && (m_State == EProjectSessionState::Stopped || m_State == EProjectSessionState::Faulted))
        {
            _ThreadToJoin = std::move(m_WorkThread);
        }
    }
    if (_ThreadToJoin.joinable())
    {
        _ThreadToJoin.join();
    }

    std::unique_lock<std::mutex> _Lock(m_LifecycleMutex);
    if (m_State == EProjectSessionState::Starting || m_State == EProjectSessionState::Running)
    {
        return;
    }
    if (m_State == EProjectSessionState::Stopping)
    {
        throw std::logic_error("ProjectSession is stopping");
    }

    EnsureOpen();
    m_LastFault.reset();
    m_bStopRequested.store(false, std::memory_order_release);
    m_State = EProjectSessionState::Starting;
    m_WorkThread = std::thread(&CProjectSession::WorkerMain, this);

    m_StopCondition.wait(_Lock, [this]() {
        return m_State == EProjectSessionState::Running
            || m_State == EProjectSessionState::Faulted
            || m_State == EProjectSessionState::Stopped;
        });

    if (m_State == EProjectSessionState::Faulted && m_LastFault && m_LastFault->Exception)
    {
        auto _Exception = m_LastFault->Exception;
        _Lock.unlock();
        Stop();
        std::rethrow_exception(_Exception);
    }
}

void iCAX::Project::CProjectSession::Stop()
{
    std::thread _ThreadToJoin;
    {
        std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
        m_bStopRequested.store(true, std::memory_order_release);
        if ((m_State == EProjectSessionState::Starting || m_State == EProjectSessionState::Running)
            && m_WorkThread.joinable())
        {
            m_State = EProjectSessionState::Stopping;
        }

        if (m_WorkThread.joinable())
        {
            if (m_WorkThread.get_id() == std::this_thread::get_id())
            {
                m_StopCondition.notify_all();
                return;
            }
            _ThreadToJoin = std::move(m_WorkThread);
        }
    }

    m_StopCondition.notify_all();
    if (_ThreadToJoin.joinable())
    {
        _ThreadToJoin.join();
    }
}

void iCAX::Project::CProjectSession::BindStartup()
{
    EnsureOpen();
    if (m_bStartupBound || m_Product.StartupComponent.empty())
    {
        return;
    }

    auto _vecIndexs = iCAX::Behaviour::GetGlobalBehaviourRegistry()->GetTypeIndexByComponentType(m_Product.StartupComponent);
    if (_vecIndexs.empty())
    {
        throw std::runtime_error("startup behaviour to component does not exist: " + m_Product.StartupComponent);
    }
    if (_vecIndexs.size() >= 2)
    {
        throw std::runtime_error("startup behaviour to component is not unique: " + m_Product.StartupComponent);
    }

    m_pUniverse->BindBehaviourByIndex(_vecIndexs[0]);
    m_pRepository->GetMetaEntity()->AddComponent(m_Product.StartupComponent);
    m_bStartupBound = true;
}

void iCAX::Project::CProjectSession::PreSwapPDO()
{
    EnsureOpen();
    m_pUniverse->PreSwapPDO();
}

void iCAX::Project::CProjectSession::Tick()
{
    EnsureOpen();
    m_pUniverse->Tick(*m_pUniverseContext);
    m_pRepository->ResetComponentChangedFlag();
}

void iCAX::Project::CProjectSession::PostSwapPDO()
{
    EnsureOpen();
    m_pUniverse->PostSwapPDO();
}

void iCAX::Project::CProjectSession::Close()
{
    Stop();

    if (m_pUniverse)
    {
        m_pUniverse->Cleanup(true);
        m_pUniverse.reset();
    }
    if (m_pRepository && m_pRepositoryEventForwarder)
    {
        m_pRepository->RemoveObserver(m_pRepositoryEventForwarder);
    }
    m_pRepositoryEventForwarder.reset();
    m_pUniverseContext.reset();
    if (m_pRepository)
    {
        m_pRepository->CleanUp(true);
        m_pRepository.reset();
    }
    m_Resources.Clear();
    m_MailChannel.Reset();
    m_bStartupBound = false;
    if (GetState() != EProjectSessionState::Faulted)
    {
        SetState(EProjectSessionState::Stopped);
    }
}

void iCAX::Project::CProjectSession::WorkerMain()
{
    try
    {
        BindStartup();
        SetState(EProjectSessionState::Running);

        const auto _FrameInterval = std::chrono::milliseconds(m_nFrameIntervalMilliseconds);
        auto _NextFrameTime = std::chrono::steady_clock::now();

        while (!m_bStopRequested.load(std::memory_order_acquire))
        {
            PreSwapPDO();

            auto _Handler = GetFrameHandler();
            if (_Handler)
            {
                _Handler(*this, GetBackendPostOffice());
            }

            Tick();
            PostSwapPDO();

            _NextFrameTime += _FrameInterval;
            {
                std::unique_lock<std::mutex> _Lock(m_LifecycleMutex);
                m_StopCondition.wait_until(
                    _Lock,
                    _NextFrameTime,
                    [this]() { return m_bStopRequested.load(std::memory_order_acquire); });
            }
            if (_NextFrameTime < std::chrono::steady_clock::now() - _FrameInterval)
            {
                _NextFrameTime = std::chrono::steady_clock::now();
            }
        }

        SetState(EProjectSessionState::Stopped);
    }
    catch (...)
    {
        auto _Exception = std::current_exception();
        std::string _Message = "ProjectSession faulted";
        try
        {
            std::rethrow_exception(_Exception);
        }
        catch (const std::exception& _Error)
        {
            _Message = _Error.what();
        }
        catch (...)
        {
            _Message = "Unknown ProjectSession fault";
        }

        RecordFault(_Message, _Exception);
        SetState(EProjectSessionState::Faulted);
    }
}

void iCAX::Project::CProjectSession::OnRepositoryChanging(IN const iCAX::Database::RepositoryEventArgs& Args_)
{
    if (m_pUniverse && m_pUniverseContext)
    {
        m_pUniverse->OnRepositoryChanging(*m_pUniverseContext, Args_);
    }
}

void iCAX::Project::CProjectSession::OnRepositoryChanged(IN const iCAX::Database::RepositoryEventArgs& Args_)
{
    if (m_pUniverse && m_pUniverseContext)
    {
        m_pUniverse->OnRepositoryChanged(*m_pUniverseContext, Args_);
    }
}

void iCAX::Project::CProjectSession::SetState(IN EProjectSessionState State_)
{
    {
        std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
        m_State = State_;
    }
    m_StopCondition.notify_all();
}

void iCAX::Project::CProjectSession::RecordFault(IN const std::string& strMessage_, IN std::exception_ptr Exception_)
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    m_LastFault = CProjectSessionFault{ strMessage_, Exception_ };
}

iCAX::Project::ProjectFrameHandler iCAX::Project::CProjectSession::GetFrameHandler() const
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    return m_FrameHandler;
}

void iCAX::Project::CProjectSession::EnsureOpen() const
{
    if (!IsOpen())
    {
        throw std::logic_error("ProjectSession is closed");
    }
}
