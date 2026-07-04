#include "pch.h"
#include "Project.h"

#include "Behaviour/IBehaviourRegistry.h"
#include "Database/IMetaRegistry.h"
#include "Mailbox/MailPayload.h"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace
{
    std::shared_ptr<iCAX::Application::IApplicationContext> RequireApplicationContext(
        IN const iCAX::Project::CProjectCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pApplicationContext)
        {
            throw std::invalid_argument("Project ApplicationContext cannot be null");
        }
        return CreateInfo_.pApplicationContext;
    }

    std::shared_ptr<iCAX::Product::IProductContext> RequireProductContext(
        IN const iCAX::Project::CProjectCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pProductContext)
        {
            throw std::invalid_argument("Project ProductContext cannot be null");
        }
        return CreateInfo_.pProductContext;
    }

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

    std::shared_ptr<iCAX::Mail::CMailChannelRegistry> RequireMailChannelRegistry(
        IN const iCAX::Project::CProjectCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pMailChannelRegistry)
        {
            throw std::invalid_argument("MailChannelRegistry cannot be null");
        }
        return CreateInfo_.pMailChannelRegistry;
    }

    std::shared_ptr<iCAX::Services::CServiceProvider> RequireServiceProvider(
        IN const iCAX::Project::CProjectCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pServiceProvider)
        {
            throw std::invalid_argument("Project ServiceProvider cannot be null");
        }
        return CreateInfo_.pServiceProvider;
    }

    bool IsExternalPath(IN const std::string& strPath_)
    {
        return strPath_.find("://") != std::string::npos;
    }

    std::string MakeDefaultQuickSaveLogPath(IN const std::string& strProjectPath_)
    {
        if (strProjectPath_.empty() || IsExternalPath(strProjectPath_))
        {
            return {};
        }
        return strProjectPath_ + ".log";
    }

    void EnsureParentDirectory(IN const std::string& strPath_)
    {
        const std::filesystem::path _Path(strPath_);
        auto _Parent = _Path.parent_path();
        if (!_Parent.empty())
        {
            std::filesystem::create_directories(_Parent);
        }
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
    , m_ProjectChannelID(CreateInfo_.ProjectChannelID.is_nil() ? iCAX::Data::GenerateNewUUID() : CreateInfo_.ProjectChannelID)
    , m_Role(CreateInfo_.Role)
    , m_ProjectName(CreateInfo_.ProjectName)
    , m_ProjectPath(CreateInfo_.ProjectPath)
    , m_Settings(CreateInfo_.Settings)
    , m_QuickSaveLogPath(CreateInfo_.QuickSaveLogPath)
    , m_StartupComponent(CreateInfo_.StartupComponent)
    , m_pApplicationContext(RequireApplicationContext(CreateInfo_))
    , m_pProductContext(RequireProductContext(CreateInfo_))
    , m_pServiceProvider(RequireServiceProvider(CreateInfo_))
    , m_pMetaRegistry(RequireMetaRegistry(CreateInfo_))
    , m_pBehaviourRegistry(RequireBehaviourRegistry(CreateInfo_))
    , m_pResourceLoaderRegistry(RequireResourceLoaderRegistry(CreateInfo_))
    , m_pMailChannelRegistry(RequireMailChannelRegistry(CreateInfo_))
    , m_pRepository(iCAX::Database::GenerateRepository(m_ProjectID, m_pMetaRegistry))
    , m_pUniverse(iCAX::Behaviour::GenerateUniverse(m_pBehaviourRegistry))
    , m_pPDOHub(CreateInfo_.bEnablePDOHub ? iCAX::PDO::GeneratePDOHub(CreateInfo_.PDOHubCreateInfo) : nullptr)
    , m_pRepositoryEventForwarder(std::make_shared<CRepositoryEventForwarder>(*this))
    , m_Resources(m_pResourceLoaderRegistry)
    , m_nFrameIntervalMilliseconds(CreateInfo_.nFrameIntervalMilliseconds == 0 ? 1 : CreateInfo_.nFrameIntervalMilliseconds)
    , m_RuntimeScheduler(m_nFrameIntervalMilliseconds)
    , m_FrameHandler(CreateInfo_.FrameHandler)
{
    if (m_ProjectName.empty())
    {
        m_ProjectName = m_Role == EProjectRole::Main ? "Main Project" : "Transient Project";
    }
    m_pRepository->AddObserver(m_pRepositoryEventForwarder);
    if (!m_pMailChannelRegistry->CreateChannel(m_ProjectChannelID))
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

const iCAX::Data::uuid& iCAX::Project::CProject::GetProjectChannelID() const
{
    return m_ProjectChannelID;
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

iCAX::Data::PropertyBag& iCAX::Project::CProject::Settings()
{
    EnsureProjectThreadAccess("Project::Settings");
    return m_Settings;
}

const iCAX::Data::PropertyBag& iCAX::Project::CProject::Settings() const
{
    EnsureProjectThreadAccess("Project::Settings");
    return m_Settings;
}

void iCAX::Project::CProject::SetProjectPath(IN const std::string& strPath_)
{
    m_ProjectPath = strPath_;
}

std::string iCAX::Project::CProject::GetQuickSaveLogPath() const
{
    return !m_QuickSaveLogPath.empty()
        ? m_QuickSaveLogPath
        : MakeDefaultQuickSaveLogPath(m_ProjectPath);
}

void iCAX::Project::CProject::SetQuickSaveLogPath(IN const std::string& strPath_)
{
    m_QuickSaveLogPath = strPath_;
}

void iCAX::Project::CProject::ReplayQuickSaveLog(IN const std::string& strMagic_, IN uint32_t nVersion_)
{
    EnsureOpen();
    auto _LogPath = GetQuickSaveLogPath();
    if (_LogPath.empty() || !std::filesystem::exists(_LogPath) || std::filesystem::file_size(_LogPath) == 0)
    {
        return;
    }

    const auto _bWasLogOpen = m_pRepository->HasOperationLog();
    if (_bWasLogOpen)
    {
        m_pRepository->CloseOperationLog();
    }

    try
    {
        m_pRepository->ReplayOperationLog(_LogPath, strMagic_, nVersion_);
        if (_bWasLogOpen)
        {
            m_pRepository->OpenOperationLog(_LogPath, false, strMagic_, nVersion_);
        }
    }
    catch (...)
    {
        if (_bWasLogOpen)
        {
            try
            {
                m_pRepository->OpenOperationLog(_LogPath, false, strMagic_, nVersion_);
            }
            catch (...)
            {
            }
        }
        throw;
    }
}

void iCAX::Project::CProject::OpenQuickSaveLog(IN bool bTruncate_, IN const std::string& strMagic_, IN uint32_t nVersion_)
{
    EnsureOpen();
    auto _LogPath = GetQuickSaveLogPath();
    if (_LogPath.empty())
    {
        throw std::invalid_argument("Project quick save log path is empty");
    }

    EnsureParentDirectory(_LogPath);
    m_pRepository->OpenOperationLog(_LogPath, bTruncate_, strMagic_, nVersion_);
}

void iCAX::Project::CProject::MarkProjectFileSaved(
    IN const std::string& strProjectPath_,
    IN const std::string& strMagic_,
    IN uint32_t nVersion_)
{
    EnsureOpen();
    if (!strProjectPath_.empty())
    {
        SetProjectPath(strProjectPath_);
    }

    auto _LogPath = GetQuickSaveLogPath();
    if (_LogPath.empty())
    {
        throw std::invalid_argument("Project quick save log path is empty");
    }

    m_pRepository->CloseOperationLog();
    EnsureParentDirectory(_LogPath);
    m_pRepository->OpenOperationLog(_LogPath, true, strMagic_, nVersion_);
}

void iCAX::Project::CProject::CloseQuickSaveLog()
{
    if (m_pRepository)
    {
        m_pRepository->CloseOperationLog();
    }
}

bool iCAX::Project::CProject::IsQuickSaveLogOpen() const
{
    return m_pRepository && m_pRepository->HasOperationLog();
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
    return m_pRepository != nullptr && m_pUniverse != nullptr;
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
    EnsureProjectThreadAccess("Project::Database");
    return *m_pRepository;
}

const iCAX::Database::IRepository& iCAX::Project::CProject::Database() const
{
    EnsureProjectThreadAccess("Project::Database");
    return *m_pRepository;
}

iCAX::Behaviour::IUniverse& iCAX::Project::CProject::Universe()
{
    EnsureProjectThreadAccess("Project::Universe");
    return *m_pUniverse;
}

const iCAX::Behaviour::IUniverse& iCAX::Project::CProject::Universe() const
{
    EnsureProjectThreadAccess("Project::Universe");
    return *m_pUniverse;
}

iCAX::Resource::CResourceLibrary& iCAX::Project::CProject::Resources()
{
    EnsureProjectThreadAccess("Project::Resources");
    return m_Resources;
}

const iCAX::Resource::CResourceLibrary& iCAX::Project::CProject::Resources() const
{
    EnsureProjectThreadAccess("Project::Resources");
    return m_Resources;
}

bool iCAX::Project::CProject::HasPDOHub() const
{
    return m_pPDOHub != nullptr;
}

iCAX::Project::CProjectPDODescriptor iCAX::Project::CProject::GetPDODescriptor() const
{
    CProjectPDODescriptor _Descriptor;
    auto _pPDOHub = m_pPDOHub;
    if (!_pPDOHub)
    {
        return _Descriptor;
    }

    _Descriptor.bEnabled = true;
    _Descriptor.SharedArenaName = _pPDOHub->GetSharedArenaName();
    _Descriptor.nSharedArenaSize = _pPDOHub->GetSharedArenaSize();
    _Descriptor.Declarations = _pPDOHub->GetPDODeclarations();
    return _Descriptor;
}

iCAX::PDO::IPDOHub& iCAX::Project::CProject::PDOHub()
{
    EnsureProjectThreadAccess("Project::PDOHub");
    if (!m_pPDOHub)
    {
        throw std::logic_error("Project PDO hub is not configured");
    }
    return *m_pPDOHub;
}

const iCAX::PDO::IPDOHub& iCAX::Project::CProject::PDOHub() const
{
    EnsureProjectThreadAccess("Project::PDOHub");
    if (!m_pPDOHub)
    {
        throw std::logic_error("Project PDO hub is not configured");
    }
    return *m_pPDOHub;
}

iCAX::Services::CServiceProvider& iCAX::Project::CProject::Services() const
{
    if (!m_pServiceProvider)
    {
        throw std::logic_error("Project service provider is not available");
    }
    return *m_pServiceProvider;
}

iCAX::Mail::CMailPostOffice iCAX::Project::CProject::GetBackendPostOffice() const
{
    EnsureOpen();
    return m_pMailChannelRegistry->GetBackendPostOffice(m_ProjectChannelID);
}

void iCAX::Project::CProject::SendFrontendEvent(
    IN uint64_t nTypeCode_,
    IN const std::string& strPayloadText_)
{
    EnsureOpen();

    iCAX::Mail::MailHeader _Header;
    _Header.nMailId = m_nNextBackendMailID.fetch_add(1, std::memory_order_relaxed);
    _Header.nOriginId = 0;
    _Header.nTypeCode = nTypeCode_;
    _Header.nStamp = iCAX::Mail::kMailOk;

    GetBackendPostOffice().SendText(_Header, strPayloadText_);
}

iCAX::Mail::CMailPostOffice iCAX::Project::CProject::GetFrontendPostOffice() const
{
    EnsureOpen();
    return m_pMailChannelRegistry->GetFrontendPostOffice(m_ProjectChannelID);
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
    m_bHasEnteredRunning.store(false, std::memory_order_release);
    m_State = EProjectState::Starting;
    m_WorkThread = std::thread(&CProject::WorkerMain, this);

    // Start 对调用方表现为同步启动：等待工作线程进入 Running、Stopped 或 Faulted。
    m_StopCondition.wait(_Lock, [this]() {
        return m_State == EProjectState::Running
            || m_State == EProjectState::Faulted
            || m_State == EProjectState::Stopped;
        });

    if (m_State == EProjectState::Faulted
        && !m_bHasEnteredRunning.load(std::memory_order_acquire)
        && m_LastFault
        && m_LastFault->Exception)
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
    if (m_pPDOHub)
    {
        m_pPDOHub->SwapInSlot();
    }
    m_pUniverse->PreSwapPDO();
}

void iCAX::Project::CProject::Tick()
{
    EnsureOpen();
    const auto _FrameTime = m_RuntimeScheduler.Tick();
    m_pUniverse->Tick(*m_pApplicationContext, *m_pProductContext, *this, _FrameTime.DeltaTime, _FrameTime.TotalTime);
    m_pRepository->ResetComponentChangedFlag();
}

void iCAX::Project::CProject::PostSwapPDO()
{
    EnsureOpen();
    m_pUniverse->PostSwapPDO();
    if (m_pPDOHub)
    {
        m_pPDOHub->SwapOutSlot();
    }
}

void iCAX::Project::CProject::Close()
{
    Stop();

    // 关闭顺序从行为到数据：先停止线程并关闭快速保存日志，
    // 再释放 Universe，最后清理 Repository 和资源。这样关闭过程中的 Cleanup 不会被写入快速保存日志。
    CloseQuickSaveLog();
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
    if (m_pRepository)
    {
        m_pRepository->CleanUp(true);
        m_pRepository.reset();
    }
    m_pPDOHub.reset();
    m_Resources.Clear();
    if (m_pMailChannelRegistry && !m_ProjectChannelID.is_nil())
    {
        (void)m_pMailChannelRegistry->RemoveChannel(m_ProjectChannelID);
    }
    m_bStartupBound = false;
    if (GetState() != EProjectState::Faulted)
    {
        SetState(EProjectState::Stopped);
    }
}

void iCAX::Project::CProject::WorkerMain()
{
    {
        std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
        m_WorkThreadID = std::this_thread::get_id();
    }

    try
    {
        BindStartup();
        m_bHasEnteredRunning.store(true, std::memory_order_release);
        SetState(EProjectState::Running);

        m_RuntimeScheduler.Reset(m_nFrameIntervalMilliseconds);

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

            m_RuntimeScheduler.AdvanceFrameDeadline();
            {
                std::unique_lock<std::mutex> _Lock(m_LifecycleMutex);
                m_StopCondition.wait_until(
                    _Lock,
                    m_RuntimeScheduler.GetNextFrameTime(),
                    [this]() { return m_bStopRequested.load(std::memory_order_acquire); });
            }
            m_RuntimeScheduler.DropBacklogIfNeeded();
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
            _Message = "Project caught a non-standard exception";
        }

        RecordFault(_Message, _Exception);
        SetState(EProjectState::Faulted);
    }

    {
        std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
        m_WorkThreadID = std::thread::id();
    }
}

void iCAX::Project::CProject::OnRepositoryChanging(IN const iCAX::Database::RepositoryEventArgs& Args_)
{
    if (m_pUniverse && m_pApplicationContext && m_pProductContext)
    {
        m_pUniverse->OnRepositoryChanging(*m_pApplicationContext, *m_pProductContext, *this, Args_);
    }
}

void iCAX::Project::CProject::OnRepositoryChanged(IN const iCAX::Database::RepositoryEventArgs& Args_)
{
    if (m_pUniverse && m_pApplicationContext && m_pProductContext)
    {
        m_pUniverse->OnRepositoryChanged(*m_pApplicationContext, *m_pProductContext, *this, Args_);
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

void iCAX::Project::CProject::EnsureProjectThreadAccess(IN const char* strApiName_) const
{
    EnsureOpen();

    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    const bool _bThreadOwned =
        (m_State == EProjectState::Starting || m_State == EProjectState::Running)
        && m_WorkThreadID != std::thread::id();
    if (!_bThreadOwned)
    {
        return;
    }

    if (m_WorkThreadID == std::this_thread::get_id())
    {
        return;
    }

    std::string _ApiName = strApiName_ ? strApiName_ : "Project API";
    throw std::logic_error(_ApiName + " can only be accessed from the project worker thread while the project is running");
}

