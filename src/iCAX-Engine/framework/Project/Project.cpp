#include "pch.h"
#include "Project.h"


namespace
{
    std::shared_ptr<const iCAX::Application::IApplicationContext> RequireApplicationContext(
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

    std::shared_ptr<iCAX::Services::CServiceProvider> RequireServiceProvider(
        IN const iCAX::Project::CProjectCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pServiceProvider)
        {
            throw std::invalid_argument("Project ServiceProvider cannot be null");
        }
        return CreateInfo_.pServiceProvider;
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

    std::shared_ptr<iCAX::Interaction::CFacadeChannelRegistry> RequireFacadeChannelRegistry(
        IN const iCAX::Project::CProjectCreateInfo& CreateInfo_)
    {
        if (!CreateInfo_.pFacadeChannelRegistry)
        {
            throw std::invalid_argument("Project FacadeChannelRegistry cannot be null");
        }
        return CreateInfo_.pFacadeChannelRegistry;
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

    std::filesystem::path PathFromUTF8(IN const std::string& strPath_)
    {
        std::u8string _Text(strPath_.begin(), strPath_.end());
        return std::filesystem::path(_Text);
    }

    void EnsureParentDirectory(IN const std::string& strPath_)
    {
        const auto _Path = PathFromUTF8(strPath_);
        const auto _Parent = _Path.parent_path();
        if (!_Parent.empty())
        {
            std::filesystem::create_directories(_Parent);
        }
    }
}

iCAX::Project::CProject::CProject(IN const CProjectCreateInfo& CreateInfo_)
    : m_ProjectID(CreateInfo_.ProjectID.is_nil() ? iCAX::Data::GenerateNewUUID() : CreateInfo_.ProjectID)
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
    , m_pFacadeChannelRegistry(RequireFacadeChannelRegistry(CreateInfo_))
    , m_SceneFrameHandler(CreateInfo_.OnSceneFrame)
{
    if (m_ProjectName.empty())
    {
        m_ProjectName = "Project";
    }

    auto _SceneInfo = MakeMainSceneCreateInfo(CreateInfo_);
    auto _pMainScene = std::make_shared<CProjectScene>(*this, _SceneInfo);
    m_MainSceneID = _pMainScene->GetSceneID();
    m_Scenes.emplace(m_MainSceneID, _pMainScene);
}

iCAX::Project::CProject::~CProject()
{
    Close();
}

const iCAX::Data::uuid& iCAX::Project::CProject::GetProjectID() const
{
    return m_ProjectID;
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

iCAX::Data::PropertyBag& iCAX::Project::CProject::Settings()
{
    return m_Settings;
}

const iCAX::Data::PropertyBag& iCAX::Project::CProject::Settings() const
{
    return m_Settings;
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
    auto _LogPath = GetQuickSaveLogPath();
    const auto _FilePath = PathFromUTF8(_LogPath);
    if (_LogPath.empty() || !std::filesystem::exists(_FilePath) || std::filesystem::file_size(_FilePath) == 0)
    {
        return;
    }

    auto& _Repository = GetMainScene().Database();
    const auto _bWasLogOpen = _Repository.HasOperationLog();
    if (_bWasLogOpen)
    {
        _Repository.CloseOperationLog();
    }

    try
    {
        _Repository.ReplayOperationLog(_LogPath, strMagic_, nVersion_);
        if (_bWasLogOpen)
        {
            _Repository.OpenOperationLog(_LogPath, false, strMagic_, nVersion_);
        }
    }
    catch (...)
    {
        if (_bWasLogOpen)
        {
            try
            {
                _Repository.OpenOperationLog(_LogPath, false, strMagic_, nVersion_);
            }
            catch (...)
            {
            }
        }
        throw;
    }
}

void iCAX::Project::CProject::OpenQuickSaveLog(
    IN bool bTruncate_,
    IN const std::string& strMagic_,
    IN uint32_t nVersion_)
{
    auto _LogPath = GetQuickSaveLogPath();
    if (_LogPath.empty())
    {
        throw std::invalid_argument("Project quick save log path is empty");
    }

    EnsureParentDirectory(_LogPath);
    GetMainScene().Database().OpenOperationLog(_LogPath, bTruncate_, strMagic_, nVersion_);
    {
        std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
        m_bQuickSaveLogOpen = true;
    }
}

void iCAX::Project::CProject::MarkProjectFileSaved(
    IN const std::string& strProjectPath_,
    IN const std::string& strMagic_,
    IN uint32_t nVersion_)
{
    if (!strProjectPath_.empty())
    {
        SetProjectPath(strProjectPath_);
    }

    auto _LogPath = GetQuickSaveLogPath();
    if (_LogPath.empty())
    {
        throw std::invalid_argument("Project quick save log path is empty");
    }

    auto& _Repository = GetMainScene().Database();
    _Repository.CloseOperationLog();
    {
        std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
        m_bQuickSaveLogOpen = false;
    }
    EnsureParentDirectory(_LogPath);
    _Repository.OpenOperationLog(_LogPath, true, strMagic_, nVersion_);
    {
        std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
        m_bQuickSaveLogOpen = true;
    }
}

void iCAX::Project::CProject::CloseQuickSaveLog()
{
    auto _pMainScene = GetScene(m_MainSceneID);
    if (_pMainScene)
    {
        _pMainScene->Database().CloseOperationLog();
    }
    std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
    m_bQuickSaveLogOpen = false;
}

bool iCAX::Project::CProject::IsQuickSaveLogOpen() const
{
    std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
    return m_bQuickSaveLogOpen;
}

const std::string& iCAX::Project::CProject::GetStartupComponent() const
{
    return m_StartupComponent;
}

const iCAX::Data::uuid& iCAX::Project::CProject::GetMainSceneID() const
{
    return m_MainSceneID;
}

iCAX::Project::CProjectScene& iCAX::Project::CProject::GetMainScene()
{
    return *RequireMainScene();
}

const iCAX::Project::CProjectScene& iCAX::Project::CProject::GetMainScene() const
{
    return *RequireMainScene();
}

std::shared_ptr<iCAX::Project::CProjectScene> iCAX::Project::CProject::GetScene(
    IN const iCAX::Data::uuid& SceneID_) const
{
    std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
    auto _Iter = m_Scenes.find(SceneID_);
    return _Iter == m_Scenes.end() ? nullptr : _Iter->second;
}

std::vector<std::shared_ptr<iCAX::Project::CProjectScene>> iCAX::Project::CProject::GetScenes() const
{
    std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
    std::vector<std::shared_ptr<CProjectScene>> _Scenes;
    _Scenes.reserve(m_Scenes.size());
    for (const auto& [_ID, _pScene] : m_Scenes)
    {
        _Scenes.push_back(_pScene);
    }
    return _Scenes;
}

std::shared_ptr<iCAX::Project::CProjectScene> iCAX::Project::CProject::OpenChildScene(
    IN const iCAX::Data::uuid& ParentSceneID_,
    IN CProjectSceneCreateInfo CreateInfo_)
{
    std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
    if (m_Scenes.find(ParentSceneID_) == m_Scenes.end())
    {
        throw std::invalid_argument("Parent scene does not exist");
    }

    CreateInfo_.SceneID = CreateInfo_.SceneID.is_nil() ? iCAX::Data::GenerateNewUUID() : CreateInfo_.SceneID;
    CreateInfo_.SceneChannelID = CreateInfo_.SceneChannelID.is_nil() ? iCAX::Data::GenerateNewUUID() : CreateInfo_.SceneChannelID;
    CreateInfo_.ParentSceneID = ParentSceneID_;
    CreateInfo_.Role = ESceneRole::Transient;
    if (CreateInfo_.SceneName.empty())
    {
        CreateInfo_.SceneName = "Transient Scene";
    }
    CreateInfo_.pApplicationContext = CreateInfo_.pApplicationContext ? CreateInfo_.pApplicationContext : m_pApplicationContext;
    CreateInfo_.pProductContext = CreateInfo_.pProductContext ? CreateInfo_.pProductContext : m_pProductContext;
    CreateInfo_.pServiceProvider = CreateInfo_.pServiceProvider ? CreateInfo_.pServiceProvider : m_pServiceProvider;
    CreateInfo_.pMetaRegistry = CreateInfo_.pMetaRegistry ? CreateInfo_.pMetaRegistry : m_pMetaRegistry;
    CreateInfo_.pBehaviourRegistry = CreateInfo_.pBehaviourRegistry ? CreateInfo_.pBehaviourRegistry : m_pBehaviourRegistry;
    CreateInfo_.pResourceLoaderRegistry = CreateInfo_.pResourceLoaderRegistry ? CreateInfo_.pResourceLoaderRegistry : m_pResourceLoaderRegistry;
    CreateInfo_.pFacadeChannelRegistry = CreateInfo_.pFacadeChannelRegistry ? CreateInfo_.pFacadeChannelRegistry : m_pFacadeChannelRegistry;
    if (!CreateInfo_.FrameHandler)
    {
        CreateInfo_.FrameHandler = [this](IN CProjectScene& Scene_, IN const iCAX::Interaction::CFacadeEndpoint& Endpoint_) {
            SceneFrameHandler _Handler;
            {
                std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
                _Handler = m_SceneFrameHandler;
            }
            if (_Handler)
            {
                _Handler(Scene_, Endpoint_);
            }
        };
    }

    auto _pScene = std::make_shared<CProjectScene>(*this, CreateInfo_);
    if (!m_Scenes.emplace(_pScene->GetSceneID(), _pScene).second)
    {
        throw std::runtime_error("Scene already exists");
    }
    return _pScene;
}

bool iCAX::Project::CProject::CloseScene(IN const iCAX::Data::uuid& SceneID_)
{
    if (SceneID_ == m_MainSceneID)
    {
        throw std::invalid_argument("Main scene cannot be closed by CloseScene; close the project instead");
    }

    std::shared_ptr<CProjectScene> _pScene;
    {
        std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
        auto _Iter = m_Scenes.find(SceneID_);
        if (_Iter == m_Scenes.end())
        {
            return false;
        }
        _pScene = _Iter->second;
        m_Scenes.erase(_Iter);
    }
    _pScene->Close();
    return true;
}

const iCAX::Data::uuid& iCAX::Project::CProject::GetMainSceneChannelID() const
{
    return GetMainScene().GetSceneChannelID();
}

bool iCAX::Project::CProject::IsOpen() const
{
    return GetScene(m_MainSceneID) != nullptr;
}

bool iCAX::Project::CProject::IsRunning() const
{
    auto _pMainScene = GetScene(m_MainSceneID);
    return _pMainScene && _pMainScene->IsRunning();
}

iCAX::Project::EProjectState iCAX::Project::CProject::GetState() const
{
    auto _pMainScene = GetScene(m_MainSceneID);
    return _pMainScene ? ConvertSceneState(_pMainScene->GetState()) : EProjectState::Stopped;
}

std::optional<iCAX::Project::CProjectFault> iCAX::Project::CProject::GetLastFault() const
{
    auto _pMainScene = GetScene(m_MainSceneID);
    auto _Fault = _pMainScene ? _pMainScene->GetLastFault() : std::nullopt;
    return _Fault ? std::make_optional(ConvertSceneFault(*_Fault)) : std::nullopt;
}

void iCAX::Project::CProject::SetSceneFrameHandler(IN SceneFrameHandler Handler_)
{
    std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
    m_SceneFrameHandler = std::move(Handler_);
}

iCAX::Database::IRepository& iCAX::Project::CProject::MainSceneDatabase()
{
    return GetMainScene().Database();
}

const iCAX::Database::IRepository& iCAX::Project::CProject::MainSceneDatabase() const
{
    return GetMainScene().Database();
}

iCAX::Behaviour::IUniverse& iCAX::Project::CProject::MainSceneUniverse()
{
    return GetMainScene().Universe();
}

const iCAX::Behaviour::IUniverse& iCAX::Project::CProject::MainSceneUniverse() const
{
    return GetMainScene().Universe();
}

iCAX::Resource::CResourceLibrary& iCAX::Project::CProject::MainSceneResources()
{
    return GetMainScene().Resources();
}

const iCAX::Resource::CResourceLibrary& iCAX::Project::CProject::MainSceneResources() const
{
    return GetMainScene().Resources();
}

bool iCAX::Project::CProject::MainSceneHasPDOHub() const
{
    auto _pMainScene = GetScene(m_MainSceneID);
    return _pMainScene ? _pMainScene->HasPDOHub() : false;
}

iCAX::Project::CMainScenePDODescriptor iCAX::Project::CProject::GetMainScenePDODescriptor() const
{
    auto _pMainScene = GetScene(m_MainSceneID);
    return _pMainScene ? _pMainScene->GetPDODescriptor() : CMainScenePDODescriptor{};
}

iCAX::PDO::IPDOHub& iCAX::Project::CProject::MainScenePDOHub()
{
    return GetMainScene().PDOHub();
}

const iCAX::PDO::IPDOHub& iCAX::Project::CProject::MainScenePDOHub() const
{
    return GetMainScene().PDOHub();
}

iCAX::Services::CServiceProvider& iCAX::Project::CProject::MainSceneServices() const
{
    return GetMainScene().Services();
}

iCAX::Interaction::CFacadeEndpoint iCAX::Project::CProject::GetMainSceneBackendFacadeEndpoint() const
{
    return GetMainScene().GetBackendFacadeEndpoint();
}

void iCAX::Project::CProject::SendMainSceneFrontendEvent(
    IN uint64_t nMethodCode_,
    IN const std::string& strPayloadText_)
{
    GetMainScene().SendFrontendEvent(nMethodCode_, strPayloadText_);
}

iCAX::Interaction::CFacadeEndpoint iCAX::Project::CProject::GetMainSceneFrontendFacadeEndpoint() const
{
    return GetMainScene().GetFrontendFacadeEndpoint();
}

void iCAX::Project::CProject::Start()
{
    GetMainScene().Start();
}

void iCAX::Project::CProject::Stop()
{
    auto _Scenes = GetScenes();
    for (auto& _pScene : _Scenes)
    {
        if (_pScene)
        {
            _pScene->Stop();
        }
    }
}

void iCAX::Project::CProject::BindStartup()
{
    GetMainScene().BindStartup();
}

void iCAX::Project::CProject::PreSwapMainScenePDO()
{
    GetMainScene().PreSwapPDO();
}

void iCAX::Project::CProject::TickMainScene()
{
    GetMainScene().Tick();
}

void iCAX::Project::CProject::PostSwapMainScenePDO()
{
    GetMainScene().PostSwapPDO();
}

void iCAX::Project::CProject::Close()
{
    std::vector<std::shared_ptr<CProjectScene>> _Scenes;
    {
        std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
        for (const auto& [_ID, _pScene] : m_Scenes)
        {
            if (_pScene && _ID != m_MainSceneID)
            {
                _Scenes.push_back(_pScene);
            }
        }
        auto _Iter = m_Scenes.find(m_MainSceneID);
        if (_Iter != m_Scenes.end() && _Iter->second)
        {
            _Scenes.push_back(_Iter->second);
        }
        m_Scenes.clear();
        m_bQuickSaveLogOpen = false;
    }

    for (auto& _pScene : _Scenes)
    {
        _pScene->Close();
    }
}

iCAX::Project::CProjectSceneCreateInfo iCAX::Project::CProject::MakeMainSceneCreateInfo(
    IN const CProjectCreateInfo& CreateInfo_) const
{
    CProjectSceneCreateInfo _SceneInfo;
    _SceneInfo.SceneID = m_ProjectID;
    _SceneInfo.SceneChannelID = CreateInfo_.MainSceneChannelID.is_nil()
        ? iCAX::Data::GenerateNewUUID()
        : CreateInfo_.MainSceneChannelID;
    _SceneInfo.ParentSceneID = iCAX::Data::GenerateNilUUID();
    _SceneInfo.Role = ESceneRole::Main;
    _SceneInfo.SceneName = m_ProjectName;
    _SceneInfo.StartupComponent = m_StartupComponent;
    _SceneInfo.pApplicationContext = m_pApplicationContext;
    _SceneInfo.pProductContext = m_pProductContext;
    _SceneInfo.pServiceProvider = m_pServiceProvider;
    _SceneInfo.pMetaRegistry = m_pMetaRegistry;
    _SceneInfo.pBehaviourRegistry = m_pBehaviourRegistry;
    _SceneInfo.pResourceLoaderRegistry = m_pResourceLoaderRegistry;
    _SceneInfo.pFacadeChannelRegistry = m_pFacadeChannelRegistry;
    _SceneInfo.bEnablePDOHub = CreateInfo_.bEnablePDOHub;
    _SceneInfo.PDOHubCreateInfo = CreateInfo_.PDOHubCreateInfo;
    _SceneInfo.nFrameIntervalMilliseconds = CreateInfo_.nFrameIntervalMilliseconds;
    _SceneInfo.FrameHandler = [this](IN CProjectScene& Scene_, IN const iCAX::Interaction::CFacadeEndpoint& Endpoint_) {
        SceneFrameHandler _Handler;
        {
            std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
            _Handler = m_SceneFrameHandler;
        }
        if (_Handler)
        {
            _Handler(Scene_, Endpoint_);
        }
    };
    return _SceneInfo;
}

std::shared_ptr<iCAX::Project::CProjectScene> iCAX::Project::CProject::RequireMainScene() const
{
    auto _pScene = GetScene(m_MainSceneID);
    if (!_pScene)
    {
        throw std::logic_error("Project main scene is closed");
    }
    return _pScene;
}

iCAX::Project::EProjectState iCAX::Project::CProject::ConvertSceneState(IN ESceneState State_)
{
    switch (State_)
    {
    case ESceneState::Created:
        return EProjectState::Created;
    case ESceneState::Starting:
        return EProjectState::Starting;
    case ESceneState::Running:
        return EProjectState::Running;
    case ESceneState::Stopping:
        return EProjectState::Stopping;
    case ESceneState::Stopped:
        return EProjectState::Stopped;
    case ESceneState::Faulted:
        return EProjectState::Faulted;
    default:
        throw std::invalid_argument("Invalid scene state");
    }
}

iCAX::Project::CProjectFault iCAX::Project::CProject::ConvertSceneFault(IN const CSceneFault& Fault_)
{
    return CProjectFault{ Fault_.Message, Fault_.Exception };
}
