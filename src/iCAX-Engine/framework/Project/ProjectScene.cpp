#include "pch.h"
#include "ProjectScene.h"

#include "Project.h"

#include "Behaviour/IBehaviourRegistry.h"
#include "Database/IMetaRegistry.h"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace
{
    std::shared_ptr<iCAX::Application::IApplicationContext> RequireApplicationContext(
        IN const iCAX::Project::CProjectSceneCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pApplicationContext)
        {
            throw std::invalid_argument("Scene ApplicationContext cannot be null");
        }
        return CreateInfo_.pApplicationContext;
    }

    std::shared_ptr<iCAX::Product::IProductContext> RequireProductContext(
        IN const iCAX::Project::CProjectSceneCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pProductContext)
        {
            throw std::invalid_argument("Scene ProductContext cannot be null");
        }
        return CreateInfo_.pProductContext;
    }

    std::shared_ptr<iCAX::Services::CServiceProvider> RequireServiceProvider(
        IN const iCAX::Project::CProjectSceneCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pServiceProvider)
        {
            throw std::invalid_argument("Scene ServiceProvider cannot be null");
        }
        return CreateInfo_.pServiceProvider;
    }

    std::shared_ptr<iCAX::Database::IMetaRegistry> RequireMetaRegistry(
        IN const iCAX::Project::CProjectSceneCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pMetaRegistry)
        {
            throw std::invalid_argument("Scene MetaRegistry cannot be null");
        }
        return CreateInfo_.pMetaRegistry;
    }

    std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> RequireBehaviourRegistry(
        IN const iCAX::Project::CProjectSceneCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pBehaviourRegistry)
        {
            throw std::invalid_argument("Scene BehaviourRegistry cannot be null");
        }
        return CreateInfo_.pBehaviourRegistry;
    }

    std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> RequireResourceLoaderRegistry(
        IN const iCAX::Project::CProjectSceneCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pResourceLoaderRegistry)
        {
            throw std::invalid_argument("Scene ResourceLoaderRegistry cannot be null");
        }
        return CreateInfo_.pResourceLoaderRegistry;
    }

    std::shared_ptr<iCAX::Mail::CMailChannelRegistry> RequireMailChannelRegistry(
        IN const iCAX::Project::CProjectSceneCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pMailChannelRegistry)
        {
            throw std::invalid_argument("Scene MailChannelRegistry cannot be null");
        }
        return CreateInfo_.pMailChannelRegistry;
    }
}

class iCAX::Project::CProjectScene::CRepositoryEventForwarder final
    : public iCAX::Database::IRepositoryEventListener
{
public:
    explicit CRepositoryEventForwarder(IN iCAX::Project::CProjectScene& Scene_)
        : m_Scene(Scene_)
    {
    }

    void OnRepositoryChanging(
        IN void*,
        IN const iCAX::Database::RepositoryEventArgs& Args_) override
    {
        m_Scene.OnRepositoryChanging(Args_);
    }

    void OnRepositoryChanged(
        IN void*,
        IN const iCAX::Database::RepositoryEventArgs& Args_) override
    {
        m_Scene.OnRepositoryChanged(Args_);
    }

private:
    iCAX::Project::CProjectScene& m_Scene;
};

iCAX::Project::CProjectScene::CProjectScene(
    IN CProject& Project_,
    IN const CProjectSceneCreateInfo& CreateInfo_)
    : m_Project(Project_)
    , m_SceneID(CreateInfo_.SceneID.is_nil() ? iCAX::Data::GenerateNewUUID() : CreateInfo_.SceneID)
    , m_SceneChannelID(CreateInfo_.SceneChannelID.is_nil() ? iCAX::Data::GenerateNewUUID() : CreateInfo_.SceneChannelID)
    , m_ParentSceneID(CreateInfo_.ParentSceneID)
    , m_Role(CreateInfo_.Role)
    , m_SceneName(CreateInfo_.SceneName)
    , m_SceneSettings(CreateInfo_.Settings)
    , m_StartupComponent(CreateInfo_.StartupComponent)
    , m_pApplicationContext(RequireApplicationContext(CreateInfo_))
    , m_pProductContext(RequireProductContext(CreateInfo_))
    , m_pServiceProvider(RequireServiceProvider(CreateInfo_))
    , m_pMetaRegistry(RequireMetaRegistry(CreateInfo_))
    , m_pBehaviourRegistry(RequireBehaviourRegistry(CreateInfo_))
    , m_pResourceLoaderRegistry(RequireResourceLoaderRegistry(CreateInfo_))
    , m_pMailChannelRegistry(RequireMailChannelRegistry(CreateInfo_))
    , m_pRepository(iCAX::Database::GenerateRepository(m_SceneID, m_pMetaRegistry))
    , m_pUniverse(iCAX::Behaviour::GenerateUniverse(m_pBehaviourRegistry))
    , m_pPDOHub(CreateInfo_.bEnablePDOHub ? iCAX::PDO::GeneratePDOHub(CreateInfo_.PDOHubCreateInfo) : nullptr)
    , m_pRepositoryEventForwarder(std::make_shared<CRepositoryEventForwarder>(*this))
    , m_Resources(m_pResourceLoaderRegistry)
    , m_nFrameIntervalMilliseconds(CreateInfo_.nFrameIntervalMilliseconds == 0 ? 1 : CreateInfo_.nFrameIntervalMilliseconds)
    , m_RuntimeScheduler(m_nFrameIntervalMilliseconds)
    , m_FrameHandler(CreateInfo_.FrameHandler)
{
    if (m_SceneName.empty())
    {
        m_SceneName = m_Role == ESceneRole::Main ? "Main Scene" : "Transient Scene";
    }
    m_pRepository->AddObserver(m_pRepositoryEventForwarder);
    if (!m_pMailChannelRegistry->CreateChannel(m_SceneChannelID))
    {
        throw std::runtime_error("Scene mail channel already exists");
    }
}

iCAX::Project::CProjectScene::~CProjectScene()
{
    Close();
}

iCAX::Project::CProject& iCAX::Project::CProjectScene::Project()
{
    return m_Project;
}

const iCAX::Project::CProject& iCAX::Project::CProjectScene::Project() const
{
    return m_Project;
}

const iCAX::Data::uuid& iCAX::Project::CProjectScene::GetSceneID() const
{
    return m_SceneID;
}

const iCAX::Data::uuid& iCAX::Project::CProjectScene::GetSceneChannelID() const
{
    return m_SceneChannelID;
}

const iCAX::Data::uuid& iCAX::Project::CProjectScene::GetParentSceneID() const
{
    return m_ParentSceneID;
}

const std::string& iCAX::Project::CProjectScene::GetSceneName() const
{
    return m_SceneName;
}

void iCAX::Project::CProjectScene::SetSceneName(IN const std::string& strName_)
{
    m_SceneName = strName_;
}

iCAX::Data::PropertyBag& iCAX::Project::CProjectScene::SceneSettings()
{
    EnsureSceneThreadAccess("Scene::SceneSettings");
    return m_SceneSettings;
}

const iCAX::Data::PropertyBag& iCAX::Project::CProjectScene::SceneSettings() const
{
    EnsureSceneThreadAccess("Scene::SceneSettings");
    return m_SceneSettings;
}

iCAX::Project::ESceneRole iCAX::Project::CProjectScene::GetRole() const
{
    return m_Role;
}

bool iCAX::Project::CProjectScene::IsMainScene() const
{
    return m_Role == ESceneRole::Main;
}

bool iCAX::Project::CProjectScene::IsTransientScene() const
{
    return m_Role == ESceneRole::Transient;
}

const std::string& iCAX::Project::CProjectScene::GetStartupComponent() const
{
    return m_StartupComponent;
}

bool iCAX::Project::CProjectScene::IsOpen() const
{
    return m_pRepository != nullptr && m_pUniverse != nullptr;
}

bool iCAX::Project::CProjectScene::IsRunning() const
{
    return GetState() == ESceneState::Running;
}

iCAX::Project::ESceneState iCAX::Project::CProjectScene::GetState() const
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    return m_State;
}

std::optional<iCAX::Project::CSceneFault> iCAX::Project::CProjectScene::GetLastFault() const
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    return m_LastFault;
}

void iCAX::Project::CProjectScene::SetFrameHandler(IN SceneFrameHandler Handler_)
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    m_FrameHandler = std::move(Handler_);
}

iCAX::Database::IRepository& iCAX::Project::CProjectScene::Database()
{
    EnsureSceneThreadAccess("Scene::Database");
    return *m_pRepository;
}

const iCAX::Database::IRepository& iCAX::Project::CProjectScene::Database() const
{
    EnsureSceneThreadAccess("Scene::Database");
    return *m_pRepository;
}

iCAX::Behaviour::IUniverse& iCAX::Project::CProjectScene::Universe()
{
    EnsureSceneThreadAccess("Scene::Universe");
    return *m_pUniverse;
}

const iCAX::Behaviour::IUniverse& iCAX::Project::CProjectScene::Universe() const
{
    EnsureSceneThreadAccess("Scene::Universe");
    return *m_pUniverse;
}

iCAX::Resource::CResourceLibrary& iCAX::Project::CProjectScene::Resources()
{
    EnsureSceneThreadAccess("Scene::Resources");
    return m_Resources;
}

const iCAX::Resource::CResourceLibrary& iCAX::Project::CProjectScene::Resources() const
{
    EnsureSceneThreadAccess("Scene::Resources");
    return m_Resources;
}

bool iCAX::Project::CProjectScene::HasPDOHub() const
{
    return m_pPDOHub != nullptr;
}

iCAX::Project::CScenePDODescriptor iCAX::Project::CProjectScene::GetPDODescriptor() const
{
    CScenePDODescriptor _Descriptor;
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

iCAX::PDO::IPDOHub& iCAX::Project::CProjectScene::PDOHub()
{
    EnsureSceneThreadAccess("Scene::PDOHub");
    if (!m_pPDOHub)
    {
        throw std::logic_error("Scene PDO hub is not configured");
    }
    return *m_pPDOHub;
}

const iCAX::PDO::IPDOHub& iCAX::Project::CProjectScene::PDOHub() const
{
    EnsureSceneThreadAccess("Scene::PDOHub");
    if (!m_pPDOHub)
    {
        throw std::logic_error("Scene PDO hub is not configured");
    }
    return *m_pPDOHub;
}

iCAX::Services::CServiceProvider& iCAX::Project::CProjectScene::Services() const
{
    if (!m_pServiceProvider)
    {
        throw std::logic_error("Scene service provider is not available");
    }
    return *m_pServiceProvider;
}

iCAX::Mail::CMailPostOffice iCAX::Project::CProjectScene::GetBackendPostOffice() const
{
    EnsureOpen();
    return m_pMailChannelRegistry->GetBackendPostOffice(m_SceneChannelID);
}

iCAX::Mail::CMailPostOffice iCAX::Project::CProjectScene::GetFrontendPostOffice() const
{
    EnsureOpen();
    return m_pMailChannelRegistry->GetFrontendPostOffice(m_SceneChannelID);
}

void iCAX::Project::CProjectScene::SendFrontendEvent(
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

std::shared_ptr<iCAX::Project::CProjectScene> iCAX::Project::CProjectScene::OpenChildScene(
    IN CProjectSceneCreateInfo CreateInfo_)
{
    return m_Project.OpenChildScene(m_SceneID, std::move(CreateInfo_));
}

void iCAX::Project::CProjectScene::Start()
{
    std::thread _ThreadToJoin;
    {
        std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
        if (m_WorkThread.joinable()
            && (m_State == ESceneState::Stopped || m_State == ESceneState::Faulted))
        {
            _ThreadToJoin = std::move(m_WorkThread);
        }
    }
    if (_ThreadToJoin.joinable())
    {
        _ThreadToJoin.join();
    }

    std::unique_lock<std::mutex> _Lock(m_LifecycleMutex);
    if (m_State == ESceneState::Starting || m_State == ESceneState::Running)
    {
        return;
    }
    if (m_State == ESceneState::Stopping)
    {
        throw std::logic_error("Scene is stopping");
    }

    EnsureOpen();
    m_LastFault.reset();
    m_bStopRequested.store(false, std::memory_order_release);
    m_bHasEnteredRunning.store(false, std::memory_order_release);
    m_State = ESceneState::Starting;
    m_WorkThread = std::thread(&CProjectScene::WorkerMain, this);

    m_StopCondition.wait(_Lock, [this]() {
        return m_State == ESceneState::Running
            || m_State == ESceneState::Faulted
            || m_State == ESceneState::Stopped;
        });

    if (m_State == ESceneState::Faulted
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

void iCAX::Project::CProjectScene::Stop()
{
    std::thread _ThreadToJoin;
    {
        std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
        m_bStopRequested.store(true, std::memory_order_release);
        if ((m_State == ESceneState::Starting || m_State == ESceneState::Running)
            && m_WorkThread.joinable())
        {
            m_State = ESceneState::Stopping;
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

void iCAX::Project::CProjectScene::BindStartup()
{
    EnsureOpen();
    if (m_bStartupBound)
    {
        return;
    }

    if (!m_StartupComponent.empty())
    {
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
    }

    for (const auto& _Type : m_pBehaviourRegistry->ListBehaviourTypes())
    {
        (void)m_pUniverse->BindBehaviourByIndex(_Type);
    }

    if (!m_StartupComponent.empty())
    {
        m_pRepository->GetMetaEntity()->AddComponent(m_StartupComponent);
    }
    m_bStartupBound = true;
}

void iCAX::Project::CProjectScene::PreSwapPDO()
{
    EnsureOpen();
    if (m_pPDOHub)
    {
        m_pPDOHub->SwapInSlot();
    }
    m_pUniverse->PreSwapPDO();
}

void iCAX::Project::CProjectScene::Tick()
{
    EnsureOpen();
    const auto _FrameTime = m_RuntimeScheduler.Tick();
    m_pUniverse->Tick(
        *m_pApplicationContext,
        *m_pProductContext,
        m_Project,
        *this,
        _FrameTime.DeltaTime,
        _FrameTime.TotalTime);
    m_pServiceProvider->UpdateSceneServices(
        *m_pApplicationContext,
        *m_pProductContext,
        m_Project,
        *this,
        _FrameTime.DeltaTime,
        _FrameTime.TotalTime);
    m_pRepository->ResetComponentChangedFlag();
}

void iCAX::Project::CProjectScene::PostSwapPDO()
{
    EnsureOpen();
    m_pUniverse->PostSwapPDO();
    if (m_pPDOHub)
    {
        m_pPDOHub->SwapOutSlot();
    }
}

void iCAX::Project::CProjectScene::Close()
{
    Stop();

    if (m_pRepository)
    {
        m_pRepository->CloseOperationLog();
    }
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
    if (m_pMailChannelRegistry && !m_SceneChannelID.is_nil())
    {
        (void)m_pMailChannelRegistry->RemoveChannel(m_SceneChannelID);
    }
    m_bStartupBound = false;
    if (GetState() != ESceneState::Faulted)
    {
        SetState(ESceneState::Stopped);
    }
}

void iCAX::Project::CProjectScene::WorkerMain()
{
    {
        std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
        m_WorkThreadID = std::this_thread::get_id();
    }

    try
    {
        BindStartup();
        m_bHasEnteredRunning.store(true, std::memory_order_release);
        SetState(ESceneState::Running);

        m_RuntimeScheduler.Reset(m_nFrameIntervalMilliseconds);

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

        SetState(ESceneState::Stopped);
    }
    catch (...)
    {
        auto _Exception = std::current_exception();
        std::string _Message = "Scene faulted";
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
            _Message = "Scene caught a non-standard exception";
        }

        RecordFault(_Message, _Exception);
        SetState(ESceneState::Faulted);
    }

    {
        std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
        m_WorkThreadID = std::thread::id();
    }
}

void iCAX::Project::CProjectScene::OnRepositoryChanging(
    IN const iCAX::Database::RepositoryEventArgs& Args_)
{
    if (m_pUniverse && m_pApplicationContext && m_pProductContext)
    {
        m_pUniverse->OnRepositoryChanging(*m_pApplicationContext, *m_pProductContext, m_Project, *this, Args_);
    }
}

void iCAX::Project::CProjectScene::OnRepositoryChanged(
    IN const iCAX::Database::RepositoryEventArgs& Args_)
{
    if (m_pUniverse && m_pApplicationContext && m_pProductContext)
    {
        m_pUniverse->OnRepositoryChanged(*m_pApplicationContext, *m_pProductContext, m_Project, *this, Args_);
    }
}

void iCAX::Project::CProjectScene::SetState(IN ESceneState State_)
{
    {
        std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
        m_State = State_;
    }
    m_StopCondition.notify_all();
}

void iCAX::Project::CProjectScene::RecordFault(
    IN const std::string& strMessage_,
    IN std::exception_ptr Exception_)
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    m_LastFault = CSceneFault{ strMessage_, Exception_ };
}

iCAX::Project::SceneFrameHandler iCAX::Project::CProjectScene::GetFrameHandler() const
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    return m_FrameHandler;
}

void iCAX::Project::CProjectScene::EnsureOpen() const
{
    if (!IsOpen())
    {
        throw std::logic_error("Scene is closed");
    }
}

void iCAX::Project::CProjectScene::EnsureSceneThreadAccess(IN const char* strApiName_) const
{
    EnsureOpen();

    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    const bool _bThreadOwned =
        (m_State == ESceneState::Starting || m_State == ESceneState::Running)
        && m_WorkThreadID != std::thread::id();
    if (!_bThreadOwned)
    {
        return;
    }

    if (m_WorkThreadID == std::this_thread::get_id())
    {
        return;
    }

    std::string _ApiName = strApiName_ ? strApiName_ : "Scene API";
    throw std::logic_error(_ApiName + " can only be accessed from the scene worker thread while the scene is running");
}
