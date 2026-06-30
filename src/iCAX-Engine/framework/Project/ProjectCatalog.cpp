#include "pch.h"
#include "ProjectCatalog.h"

#include <stdexcept>

iCAX::Project::CProjectCatalog::CProjectCatalog(IN const CProjectCatalogCreateInfo& CreateInfo_)
    : m_CatalogID(CreateInfo_.CatalogID.is_nil() ? iCAX::Data::GenerateNewUUID() : CreateInfo_.CatalogID)
    , m_CatalogName(CreateInfo_.CatalogName.empty() ? "Project Catalog" : CreateInfo_.CatalogName)
    , m_CatalogPath(CreateInfo_.CatalogPath)
    , m_pApplicationContext(CreateInfo_.pApplicationContext)
    , m_pProductContext(CreateInfo_.pProductContext)
    , m_pServiceProvider(CreateInfo_.pServiceProvider)
    , m_pMetaRegistry(CreateInfo_.pMetaRegistry)
    , m_pBehaviourRegistry(CreateInfo_.pBehaviourRegistry)
    , m_pMailChannelRegistry(CreateInfo_.pMailChannelRegistry)
    , m_bEnablePDOHub(CreateInfo_.bEnablePDOHub)
    , m_PDOHubCreateInfo(CreateInfo_.PDOHubCreateInfo)
    , m_ResourceLoaderRegistryFactory(CreateInfo_.ResourceLoaderRegistryFactory)
    , m_Projects()
    , m_MainProjectID(iCAX::Data::GenerateNilUUID())
{
    if (!m_pApplicationContext)
    {
        throw std::invalid_argument("ProjectCatalog ApplicationContext cannot be null");
    }
    if (!m_pProductContext)
    {
        throw std::invalid_argument("ProjectCatalog ProductContext cannot be null");
    }
    if (!m_pServiceProvider)
    {
        throw std::invalid_argument("ProjectCatalog ServiceProvider cannot be null");
    }
    if (!m_pMetaRegistry)
    {
        throw std::invalid_argument("ProjectCatalog MetaRegistry cannot be null");
    }
    if (!m_pBehaviourRegistry)
    {
        throw std::invalid_argument("ProjectCatalog BehaviourRegistry cannot be null");
    }
    if (!m_pMailChannelRegistry)
    {
        throw std::invalid_argument("ProjectCatalog MailChannelRegistry cannot be null");
    }
    if (!m_ResourceLoaderRegistryFactory)
    {
        throw std::invalid_argument("ProjectCatalog ResourceLoaderRegistryFactory cannot be empty");
    }
}

iCAX::Project::CProjectCatalog::~CProjectCatalog()
{
    CloseAll();
}

const iCAX::Data::uuid& iCAX::Project::CProjectCatalog::GetCatalogID() const
{
    return m_CatalogID;
}

std::string iCAX::Project::CProjectCatalog::GetCatalogName() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_CatalogName;
}

void iCAX::Project::CProjectCatalog::SetCatalogName(IN const std::string& strCatalogName_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_CatalogName = strCatalogName_.empty() ? "Project Catalog" : strCatalogName_;
}

std::string iCAX::Project::CProjectCatalog::GetCatalogPath() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_CatalogPath;
}

void iCAX::Project::CProjectCatalog::SetCatalogPath(IN const std::string& strCatalogPath_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_CatalogPath = strCatalogPath_;
}

std::shared_ptr<iCAX::Project::CProject> iCAX::Project::CProjectCatalog::OpenMainProject(
    IN const std::string& strProjectName_,
    IN const std::string& strProjectPath_,
    IN const std::string& strStartupComponent_)
{
    CProjectCreateInfo _Info;
    _Info.Role = EProjectRole::Main;
    _Info.ProjectName = strProjectName_;
    _Info.ProjectPath = strProjectPath_;
    _Info.StartupComponent = strStartupComponent_;
    _Info.pApplicationContext = m_pApplicationContext;
    _Info.pProductContext = m_pProductContext;
    _Info.pServiceProvider = m_pServiceProvider;
    return OpenMainProject(_Info);
}

std::shared_ptr<iCAX::Project::CProject> iCAX::Project::CProjectCatalog::OpenMainProject(IN const CProjectCreateInfo& CreateInfo_)
{
    CProjectCreateInfo _Info = CreateInfo_;
    _Info.Role = EProjectRole::Main;
    return CreateProject(_Info);
}

std::shared_ptr<iCAX::Project::CProject> iCAX::Project::CProjectCatalog::OpenTransientProject(
    IN const std::string& strProjectName_,
    IN const std::string& strProjectPath_,
    IN const std::string& strStartupComponent_)
{
    CProjectCreateInfo _Info;
    _Info.Role = EProjectRole::Transient;
    _Info.ProjectName = strProjectName_;
    _Info.ProjectPath = strProjectPath_;
    _Info.StartupComponent = strStartupComponent_;
    _Info.pApplicationContext = m_pApplicationContext;
    _Info.pProductContext = m_pProductContext;
    _Info.pServiceProvider = m_pServiceProvider;
    return OpenTransientProject(_Info);
}

std::shared_ptr<iCAX::Project::CProject> iCAX::Project::CProjectCatalog::OpenTransientProject(IN const CProjectCreateInfo& CreateInfo_)
{
    CProjectCreateInfo _Info = CreateInfo_;
    _Info.Role = EProjectRole::Transient;
    return CreateProject(_Info);
}

bool iCAX::Project::CProjectCatalog::CloseMainProject()
{
    iCAX::Data::uuid _MainProjectID;
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        _MainProjectID = m_MainProjectID;
    }

    if (_MainProjectID.is_nil())
    {
        return false;
    }
    return CloseProject(_MainProjectID);
}

bool iCAX::Project::CProjectCatalog::CloseProject(IN const iCAX::Data::uuid& ProjectID_)
{
    std::shared_ptr<CProject> _pProject;
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        auto _Iter = m_Projects.find(ProjectID_);
        if (_Iter == m_Projects.end())
        {
            return false;
        }
        _pProject = _Iter->second;
        m_Projects.erase(_Iter);

        if (m_MainProjectID == ProjectID_)
        {
            m_MainProjectID = iCAX::Data::GenerateNilUUID();
        }
    }

    if (_pProject)
    {
        _pProject->Close();
    }
    return true;
}

void iCAX::Project::CProjectCatalog::CloseTransientProjects()
{
    auto _Projects = GetTransientProjects();
    for (auto& _pProject : _Projects)
    {
        if (_pProject)
        {
            (void)CloseProject(_pProject->GetProjectID());
        }
    }
}

void iCAX::Project::CProjectCatalog::CloseAll()
{
    auto _Projects = SnapshotProjects();
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        m_Projects.clear();
        m_MainProjectID = iCAX::Data::GenerateNilUUID();
    }

    for (auto& _pProject : _Projects)
    {
        if (_pProject)
        {
            _pProject->Close();
        }
    }
}

std::shared_ptr<iCAX::Project::CProject> iCAX::Project::CProjectCatalog::FindProject(IN const iCAX::Data::uuid& ProjectID_) const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto _Iter = m_Projects.find(ProjectID_);
    if (_Iter == m_Projects.end())
    {
        return nullptr;
    }
    return _Iter->second;
}

std::shared_ptr<iCAX::Project::CProject> iCAX::Project::CProjectCatalog::GetMainProject() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto _Iter = m_Projects.find(m_MainProjectID);
    if (_Iter == m_Projects.end())
    {
        return nullptr;
    }
    return _Iter->second;
}

std::vector<std::shared_ptr<iCAX::Project::CProject>> iCAX::Project::CProjectCatalog::GetProjects() const
{
    return SnapshotProjects();
}

std::vector<std::shared_ptr<iCAX::Project::CProject>> iCAX::Project::CProjectCatalog::GetTransientProjects() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    std::vector<std::shared_ptr<CProject>> _Projects;
    for (const auto& [_ID, _Project] : m_Projects)
    {
        if (_Project && _Project->IsTransientProject())
        {
            _Projects.push_back(_Project);
        }
    }
    return _Projects;
}

std::vector<iCAX::Data::uuid> iCAX::Project::CProjectCatalog::GetProjectIDs() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    std::vector<iCAX::Data::uuid> _IDs;
    _IDs.reserve(m_Projects.size());
    for (const auto& [_ID, _Project] : m_Projects)
    {
        _IDs.push_back(_ID);
    }
    return _IDs;
}

bool iCAX::Project::CProjectCatalog::HasMainProject() const
{
    return GetMainProject() != nullptr;
}

size_t iCAX::Project::CProjectCatalog::Count() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_Projects.size();
}

size_t iCAX::Project::CProjectCatalog::TransientCount() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    size_t _Count = 0;
    for (const auto& [_ID, _Project] : m_Projects)
    {
        if (_Project && _Project->IsTransientProject())
        {
            ++_Count;
        }
    }
    return _Count;
}

std::shared_ptr<iCAX::Project::CProject> iCAX::Project::CProjectCatalog::CreateProject(IN CProjectCreateInfo CreateInfo_)
{
    if (!CreateInfo_.pApplicationContext)
    {
        CreateInfo_.pApplicationContext = m_pApplicationContext;
    }
    if (!CreateInfo_.pProductContext)
    {
        CreateInfo_.pProductContext = m_pProductContext;
    }
    if (!CreateInfo_.pServiceProvider)
    {
        CreateInfo_.pServiceProvider = m_pServiceProvider;
    }
    if (!CreateInfo_.pMetaRegistry)
    {
        CreateInfo_.pMetaRegistry = m_pMetaRegistry;
    }
    if (!CreateInfo_.pBehaviourRegistry)
    {
        CreateInfo_.pBehaviourRegistry = m_pBehaviourRegistry;
    }
    if (!CreateInfo_.pMailChannelRegistry)
    {
        CreateInfo_.pMailChannelRegistry = m_pMailChannelRegistry;
    }
    if (!CreateInfo_.pResourceLoaderRegistry && m_ResourceLoaderRegistryFactory)
    {
        CreateInfo_.pResourceLoaderRegistry = m_ResourceLoaderRegistryFactory();
    }
    if (!CreateInfo_.bEnablePDOHub && m_bEnablePDOHub)
    {
        CreateInfo_.bEnablePDOHub = true;
        CreateInfo_.PDOHubCreateInfo = m_PDOHubCreateInfo;
    }
    if (!CreateInfo_.pMetaRegistry)
    {
        throw std::invalid_argument("ProjectCatalog MetaRegistry cannot be null");
    }
    if (!CreateInfo_.pApplicationContext)
    {
        throw std::invalid_argument("ProjectCatalog ApplicationContext cannot be null");
    }
    if (!CreateInfo_.pProductContext)
    {
        throw std::invalid_argument("ProjectCatalog ProductContext cannot be null");
    }
    if (!CreateInfo_.pServiceProvider)
    {
        throw std::invalid_argument("ProjectCatalog ServiceProvider cannot be null");
    }
    if (!CreateInfo_.pBehaviourRegistry)
    {
        throw std::invalid_argument("ProjectCatalog BehaviourRegistry cannot be null");
    }
    if (!CreateInfo_.pMailChannelRegistry)
    {
        throw std::invalid_argument("ProjectCatalog MailChannelRegistry cannot be null");
    }
    if (!CreateInfo_.pResourceLoaderRegistry)
    {
        throw std::invalid_argument("ProjectCatalog ResourceLoaderRegistry cannot be null");
    }

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    if (CreateInfo_.Role == EProjectRole::Main && !m_MainProjectID.is_nil())
    {
        throw std::logic_error("Main project already exists");
    }

    auto _pProject = std::make_shared<CProject>(CreateInfo_);
    const auto _ProjectID = _pProject->GetProjectID();
    if (m_Projects.find(_ProjectID) != m_Projects.end())
    {
        throw std::runtime_error("Project already exists");
    }

    m_Projects.emplace(_ProjectID, _pProject);
    if (_pProject->IsMainProject())
    {
        m_MainProjectID = _ProjectID;
    }
    return _pProject;
}

std::vector<std::shared_ptr<iCAX::Project::CProject>> iCAX::Project::CProjectCatalog::SnapshotProjects() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    std::vector<std::shared_ptr<CProject>> _Projects;
    _Projects.reserve(m_Projects.size());
    for (const auto& [_ID, _Project] : m_Projects)
    {
        _Projects.push_back(_Project);
    }
    return _Projects;
}

