#include "pch.h"
#include "Project.h"

#include "Behaviour/IBehaviourRegistry.h"
#include "Behaviour/UniverseContext.h"
#include "Database/IMetaRegistry.h"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace
{
    std::shared_ptr<iCAX::Database::IMetaRegistry> RequireMetaRegistry(
        IN const iCAX::Project::CProjectCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pMetaRegistry)
        {
            throw std::invalid_argument("Project MetaRegistry cannot be null");
        }
        return CreateInfo_.pMetaRegistry;
    }

    std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> RequireBehaviourRegistry(
        IN const iCAX::Project::CProjectCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pBehaviourRegistry)
        {
            throw std::invalid_argument("Project BehaviourRegistry cannot be null");
        }
        return CreateInfo_.pBehaviourRegistry;
    }

    std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> RequireResourceLoaderRegistry(
        IN const iCAX::Project::CProjectCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pResourceLoaderRegistry)
        {
            throw std::invalid_argument("Project ResourceLoaderRegistry cannot be null");
        }
        return CreateInfo_.pResourceLoaderRegistry;
    }

    std::shared_ptr<iCAX::Services::IMailChannelService> RequireMailChannelService(
        IN const iCAX::Project::CProjectCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pMailChannelService)
        {
            throw std::invalid_argument("MailChannelService cannot be null");
        }
        return CreateInfo_.pMailChannelService;
    }
}

class iCAX::Project::CProject::CRepositoryEventForwarder final
    : public iCAX::Database::IRepositoryEventListener
{
public:
    explicit CRepositoryEventForwarder(IN iCAX::Project::CProject& Project_)
        : m_Project(Project_)
    {
    }

    virtual void OnRepositoryChanging(
        IN void*,
        IN const iCAX::Database::RepositoryEventArgs& Args_) override
    {
        m_Project.OnRepositoryChanging(Args_);
    }

    virtual void OnRepositoryChanged(
        IN void*,
        IN const iCAX::Database::RepositoryEventArgs& Args_) override
    {
        m_Project.OnRepositoryChanged(Args_);
    }

private:
    iCAX::Project::CProject& m_Project;
};

iCAX::Project::CProject::CProject(IN const CProjectCreateInfo& CreateInfo_)
    : m_ProjectID(CreateInfo_.ProjectID.is_nil() ? iCAX::Data::GenerateNewUUID() : CreateInfo_.ProjectID)
    , m_ProjectMailID(CreateInfo_.ProjectMailID.is_nil() ? iCAX::Data::GenerateNewUUID() : CreateInfo_.ProjectMailID)
    , m_Role(CreateInfo_.Role)
    , m_ProjectName(CreateInfo_.ProjectName)
    , m_ProjectPath(CreateInfo_.ProjectPath)
    , m_StartupComponent(CreateInfo_.StartupComponent)
    , m_pApplicationSettings(CreateInfo_.pApplicationSettings ? CreateInfo_.pApplicationSettings : std::make_shared<iCAX::Data::PropertyBag>())
    , m_pMetaRegistry(RequireMetaRegistry(CreateInfo_))
    , m_pBehaviourRegistry(RequireBehaviourRegistry(CreateInfo_))
    , m_pResourceLoaderRegistry(RequireResourceLoaderRegistry(CreateInfo_))
    , m_pMailChannelService(RequireMailChannelService(CreateInfo_))
    , m_pRepository(iCAX::Database::GenerateRepository(m_ProjectID, m_pMetaRegistry))
    , m_pUniverseContext(std::make_shared<iCAX::Behaviour::CUniverseContext>(m_pRepository, m_pApplicationSettings))
    , m_pUniverse(iCAX::Behaviour::GenerateUniverse(m_pBehaviourRegistry))
    , m_pRepositoryEventForwarder(std::make_shared<CRepositoryEventForwarder>(*this))
    , m_Resources(m_pResourceLoaderRegistry)
    , m_nFrameIntervalMilliseconds(CreateInfo_.nFrameIntervalMilliseconds == 0 ? 1 : CreateInfo_.nFrameIntervalMilliseconds)
    , m_FrameHandler(CreateInfo_.FrameHandler)
{
    if (m_ProjectName.empty())
    {
        m_ProjectName = m_Role == EProjectRole::Main ? "Main Project" : "Transient Project";
    }
    m_pRepository->AddObserver(m_pRepositoryEventForwarder);
    if (!m_pMailChannelService->CreateChannel(m_ProjectMailID))
    {
        throw std::runtime_error("Project mail channel already exists");
    }
}

iCAX::Project::CProject::~CProject()
{
    Close();
}

const iCAX::Data::uuid& iCAX::Project::CProject::GetProjectID() const
{
    return m_ProjectID;
}

const iCAX::Data::uuid& iCAX::Project::CProject::GetProjectMailID() const
{
    return m_ProjectMailID;
}

const std::string& iCAX::Project::CProject::GetProjectName() const
{
    return m_ProjectName;
}

void iCAX::Project::CProject::SetProjectName(IN const std::string& strName_)
{
    m_ProjectName = strName_;
}

const std::string& iCAX::Project::CProject::GetProjectPath() const
{
    return m_ProjectPath;
}

void iCAX::Project::CProject::SetProjectPath(IN const std::string& strPath_)
{
    m_ProjectPath = strPath_;
}

iCAX::Project::EProjectRole iCAX::Project::CProject::GetRole() const
{
    return m_Role;
}

bool iCAX::Project::CProject::IsMainProject() const
{
    return m_Role == EProjectRole::Main;
}

bool iCAX::Project::CProject::IsTransientProject() const
{
    return m_Role == EProjectRole::Transient;
}

const std::string& iCAX::Project::CProject::GetStartupComponent() const
{
    return m_StartupComponent;
}

bool iCAX::Project::CProject::IsOpen() const
{
    return m_pRepository != nullptr && m_pUniverseContext != nullptr && m_pUniverse != nullptr;
}

bool iCAX::Project::CProject::IsRunning() const
{
    return GetState() == EProjectState::Running;
}

iCAX::Project::EProjectState iCAX::Project::CProject::GetState() const
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    return m_State;
}

std::optional<iCAX::Project::CProjectFault> iCAX::Project::CProject::GetLastFault() const
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    return m_LastFault;
}

void iCAX::Project::CProject::SetFrameHandler(IN ProjectFrameHandler Handler_)
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    m_FrameHandler = std::move(Handler_);
}

iCAX::Database::IRepository& iCAX::Project::CProject::Database()
{
    EnsureOpen();
    return *m_pRepository;
}

const iCAX::Database::IRepository& iCAX::Project::CProject::Database() const
{
    EnsureOpen();
    return *m_pRepository;
}

iCAX::Behaviour::IUniverse& iCAX::Project::CProject::Universe()
{
    EnsureOpen();
    return *m_pUniverse;
}

const iCAX::Behaviour::IUniverse& iCAX::Project::CProject::Universe() const
{
    EnsureOpen();
    return *m_pUniverse;
}

iCAX::Resource::CResourceLibrary& iCAX::Project::CProject::Resources()
{
    return m_Resources;
}

const iCAX::Resource::CResourceLibrary& iCAX::Project::CProject::Resources() const
{
    return m_Resources;
}

iCAX::Mail::CMailPostOffice iCAX::Project::CProject::GetBackendPostOffice()
{
    EnsureOpen();
    return m_pMailChannelService->GetBackendPostOffice(m_ProjectMailID);
}

iCAX::Mail::CMailPostOffice iCAX::Project::CProject::GetFrontendPostOffice()
{
    EnsureOpen();
    return m_pMailChannelService->GetFrontendPostOffice(m_ProjectMailID);
}

void iCAX::Project::CProject::Start()
{
    std::thread _ThreadToJoin;
    {
        std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
        if (m_WorkThread.joinable()
            && (m_State == EProjectState::Stopped || m_State == EProjectState::Faulted))
        {
            _ThreadToJoin = std::move(m_WorkThread);
        }
    }
    if (_ThreadToJoin.joinable())
    {
        // 上一次线程已经结束但还未 join，先回收线程句柄，再启动新线程。
        _ThreadToJoin.join();
    }

    std::unique_lock<std::mutex> _Lock(m_LifecycleMutex);
    if (m_State == EProjectState::Starting || m_State == EProjectState::Running)
    {
        return;
    }
    if (m_State == EProjectState::Stopping)
    {
        throw std::logic_error("Project is stopping");
    }

    EnsureOpen();
    m_LastFault.reset();
    m_bStopRequested.store(false, std::memory_order_release);
    m_State = EProjectState::Starting;
    m_WorkThread = std::thread(&CProject::WorkerMain, this);

    // Start 对调用方表现为同步启动：等待工作线程进入 Running、Stopped 或 Faulted。
    m_StopCondition.wait(_Lock, [this]() {
        return m_State == EProjectState::Running
            || m_State == EProjectState::Faulted
            || m_State == EProjectState::Stopped;
        });

    if (m_State == EProjectState::Faulted && m_LastFault && m_LastFault->Exception)
    {
        auto _Exception = m_LastFault->Exception;
        _Lock.unlock();
        Stop();
        std::rethrow_exception(_Exception);
    }
}

void iCAX::Project::CProject::Stop()
{
    std::thread _ThreadToJoin;
    {
        std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
        m_bStopRequested.store(true, std::memory_order_release);
        if ((m_State == EProjectState::Starting || m_State == EProjectState::Running)
            && m_WorkThread.joinable())
        {
            m_State = EProjectState::Stopping;
        }

        if (m_WorkThread.joinable())
        {
            if (m_WorkThread.get_id() == std::this_thread::get_id())
            {
                // 避免工作线程内部调用 Stop 时自 join 死锁，只设置停止标记并返回。
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

void iCAX::Project::CProject::BindStartup()
{
    EnsureOpen();
    if (m_bStartupBound || m_StartupComponent.empty())
    {
        return;
    }

    // 启动组件约定只能对应一个 Behaviour；否则项目启动入口不明确。
    auto _vecIndexs = m_pBehaviourRegistry->GetTypeIndexByComponentType(m_StartupComponent);
    if (_vecIndexs.empty())
    {
        throw std::runtime_error("startup behaviour to component does not exist: " + m_StartupComponent);
    }
    if (_vecIndexs.size() >= 2)
    {
        throw std::runtime_error("startup behaviour to component is not unique: " + m_StartupComponent);
    }

    m_pUniverse->BindBehaviourByIndex(_vecIndexs[0]);
    // MetaEntity 承载项目级启动组件，使行为系统能通过普通 EC 机制感知启动入口。
    m_pRepository->GetMetaEntity()->AddComponent(m_StartupComponent);
    m_bStartupBound = true;
}

void iCAX::Project::CProject::PreSwapPDO()
{
    EnsureOpen();
    m_pUniverse->PreSwapPDO();
}

void iCAX::Project::CProject::Tick()
{
    EnsureOpen();
    m_pUniverse->Tick(*m_pUniverseContext);
    m_pRepository->ResetComponentChangedFlag();
}

void iCAX::Project::CProject::PostSwapPDO()
{
    EnsureOpen();
    m_pUniverse->PostSwapPDO();
}

void iCAX::Project::CProject::Close()
{
    Stop();

    // 关闭顺序从行为到数据：先停止线程，再释放 Universe，最后清理 Repository 和资源。
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
    if (m_pMailChannelService && !m_ProjectMailID.is_nil())
    {
        (void)m_pMailChannelService->RemoveChannel(m_ProjectMailID);
    }
    m_bStartupBound = false;
    if (GetState() != EProjectState::Faulted)
    {
        SetState(EProjectState::Stopped);
    }
}

void iCAX::Project::CProject::WorkerMain()
{
    try
    {
        BindStartup();
        SetState(EProjectState::Running);

        const auto _FrameInterval = std::chrono::milliseconds(m_nFrameIntervalMilliseconds);
        auto _NextFrameTime = std::chrono::steady_clock::now();

        while (!m_bStopRequested.load(std::memory_order_acquire))
        {
            PreSwapPDO();

            auto _Handler = GetFrameHandler();
            if (_Handler)
            {
                // ProductRuntime 在这里接入项目邮箱分发；Project 本身不依赖 CommandHandler。
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
                // 如果一帧耗时过长，丢弃积压帧，避免线程忙追历史时间点。
                _NextFrameTime = std::chrono::steady_clock::now();
            }
        }

        SetState(EProjectState::Stopped);
    }
    catch (...)
    {
        // 项目线程异常只标记当前项目 Faulted，不向其他项目或产品线程传播。
        auto _Exception = std::current_exception();
        std::string _Message = "Project faulted";
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
            _Message = "Unknown Project fault";
        }

        RecordFault(_Message, _Exception);
        SetState(EProjectState::Faulted);
    }
}

void iCAX::Project::CProject::OnRepositoryChanging(IN const iCAX::Database::RepositoryEventArgs& Args_)
{
    if (m_pUniverse && m_pUniverseContext)
    {
        m_pUniverse->OnRepositoryChanging(*m_pUniverseContext, Args_);
    }
}

void iCAX::Project::CProject::OnRepositoryChanged(IN const iCAX::Database::RepositoryEventArgs& Args_)
{
    if (m_pUniverse && m_pUniverseContext)
    {
        m_pUniverse->OnRepositoryChanged(*m_pUniverseContext, Args_);
    }
}

void iCAX::Project::CProject::SetState(IN EProjectState State_)
{
    {
        std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
        m_State = State_;
    }
    m_StopCondition.notify_all();
}

void iCAX::Project::CProject::RecordFault(IN const std::string& strMessage_, IN std::exception_ptr Exception_)
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    m_LastFault = CProjectFault{ strMessage_, Exception_ };
}

iCAX::Project::ProjectFrameHandler iCAX::Project::CProject::GetFrameHandler() const
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    return m_FrameHandler;
}

void iCAX::Project::CProject::EnsureOpen() const
{
    if (!IsOpen())
    {
        throw std::logic_error("Project is closed");
    }
}
