#include "pch.h"
#include "ProjectManager.h"

#include <stdexcept>

iCAX::Project::CProjectManager::CProjectManager()
    : CProjectManager(std::make_shared<iCAX::Data::PropertyBag>())
{
}

iCAX::Project::CProjectManager::CProjectManager(IN std::shared_ptr<iCAX::Data::PropertyBag> pApplicationSettings_)
    : m_ProductCatalog()
    , m_pApplicationSettings(pApplicationSettings_ ? pApplicationSettings_ : std::make_shared<iCAX::Data::PropertyBag>())
    , m_Projects()
    , m_ActiveProjectID(iCAX::Data::GenerateNilUUID())
{
}

iCAX::Project::CProjectManager::~CProjectManager()
{
    CloseAll();
}

iCAX::Project::CProductCatalog& iCAX::Project::CProjectManager::Products()
{
    return m_ProductCatalog;
}

const iCAX::Project::CProductCatalog& iCAX::Project::CProjectManager::Products() const
{
    return m_ProductCatalog;
}

void iCAX::Project::CProjectManager::SetApplicationSettings(IN std::shared_ptr<iCAX::Data::PropertyBag> pApplicationSettings_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    m_pApplicationSettings = pApplicationSettings_ ? pApplicationSettings_ : std::make_shared<iCAX::Data::PropertyBag>();
}

std::shared_ptr<iCAX::Project::CProjectSession> iCAX::Project::CProjectManager::OpenProject(
    IN const std::string& strProductID_,
    IN const std::string& strProjectName_,
    IN const std::string& strProjectPath_)
{
    CProjectSessionCreateInfo _Info;
    _Info.Product = m_ProductCatalog.Require(strProductID_);
    _Info.ProjectName = strProjectName_;
    _Info.ProjectPath = strProjectPath_;
    _Info.pApplicationSettings = m_pApplicationSettings;
    return OpenProject(_Info);
}

std::shared_ptr<iCAX::Project::CProjectSession> iCAX::Project::CProjectManager::OpenProject(IN const CProjectSessionCreateInfo& CreateInfo_)
{
    auto _pProject = std::make_shared<CProjectSession>(CreateInfo_);
    const auto _ProjectID = _pProject->GetProjectID();

    std::lock_guard<std::mutex> _Lock(m_Mutex);
    if (m_Projects.find(_ProjectID) != m_Projects.end())
    {
        throw std::runtime_error("Project already exists");
    }

    m_Projects.emplace(_ProjectID, _pProject);
    if (m_ActiveProjectID.is_nil())
    {
        m_ActiveProjectID = _ProjectID;
    }
    return _pProject;
}

bool iCAX::Project::CProjectManager::CloseProject(IN const iCAX::Data::uuid& ProjectID_)
{
    std::shared_ptr<CProjectSession> _pProject;
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        auto _Iter = m_Projects.find(ProjectID_);
        if (_Iter == m_Projects.end())
        {
            return false;
        }
        _pProject = _Iter->second;
        m_Projects.erase(_Iter);

        if (m_ActiveProjectID == ProjectID_)
        {
            m_ActiveProjectID = m_Projects.empty() ? iCAX::Data::GenerateNilUUID() : m_Projects.begin()->first;
        }
    }

    if (_pProject)
    {
        _pProject->Close();
    }
    return true;
}

void iCAX::Project::CProjectManager::CloseAll()
{
    auto _Projects = SnapshotProjects();
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        m_Projects.clear();
        m_ActiveProjectID = iCAX::Data::GenerateNilUUID();
    }

    for (auto& _pProject : _Projects)
    {
        if (_pProject)
        {
            _pProject->Close();
        }
    }
}

std::shared_ptr<iCAX::Project::CProjectSession> iCAX::Project::CProjectManager::FindProject(IN const iCAX::Data::uuid& ProjectID_) const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto _Iter = m_Projects.find(ProjectID_);
    if (_Iter == m_Projects.end())
    {
        return nullptr;
    }
    return _Iter->second;
}

std::shared_ptr<iCAX::Project::CProjectSession> iCAX::Project::CProjectManager::GetActiveProject() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    auto _Iter = m_Projects.find(m_ActiveProjectID);
    if (_Iter == m_Projects.end())
    {
        return nullptr;
    }
    return _Iter->second;
}

bool iCAX::Project::CProjectManager::SetActiveProject(IN const iCAX::Data::uuid& ProjectID_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    if (m_Projects.find(ProjectID_) == m_Projects.end())
    {
        return false;
    }
    m_ActiveProjectID = ProjectID_;
    return true;
}

std::vector<std::shared_ptr<iCAX::Project::CProjectSession>> iCAX::Project::CProjectManager::GetProjects() const
{
    return SnapshotProjects();
}

std::vector<iCAX::Data::uuid> iCAX::Project::CProjectManager::GetProjectIDs() const
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

size_t iCAX::Project::CProjectManager::Count() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_Projects.size();
}

std::vector<std::shared_ptr<iCAX::Project::CProjectSession>> iCAX::Project::CProjectManager::SnapshotProjects() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    std::vector<std::shared_ptr<CProjectSession>> _Projects;
    _Projects.reserve(m_Projects.size());
    for (const auto& [_ID, _Project] : m_Projects)
    {
        _Projects.push_back(_Project);
    }
    return _Projects;
}
