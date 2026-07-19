#include "pch.h"
#include "ProjectRuntime.h"


iCAX::Project::CProjectRuntime::CProjectRuntime(IN std::shared_ptr<CProject> pProject_)
    : m_pProject(std::move(pProject_))
{
    if (!m_pProject)
    {
        throw std::invalid_argument("Project cannot be null");
    }
}

iCAX::Project::CProjectRuntime::~CProjectRuntime()
{
    Close();
}

const iCAX::Data::uuid& iCAX::Project::CProjectRuntime::GetProjectID() const
{
    return RequireProject().GetProjectID();
}

const iCAX::Data::uuid& iCAX::Project::CProjectRuntime::GetMainSceneChannelID() const
{
    return RequireProject().GetMainSceneChannelID();
}

std::string iCAX::Project::CProjectRuntime::GetProjectName() const
{
    return RequireProject().GetProjectName();
}

std::string iCAX::Project::CProjectRuntime::GetProjectPath() const
{
    return RequireProject().GetProjectPath();
}

std::string iCAX::Project::CProjectRuntime::GetStartupComponent() const
{
    return RequireProject().GetStartupComponent();
}

bool iCAX::Project::CProjectRuntime::IsOpen() const
{
    return m_pProject && m_pProject->IsOpen();
}

bool iCAX::Project::CProjectRuntime::IsRunning() const
{
    return m_pProject && m_pProject->IsRunning();
}

iCAX::Project::EProjectState iCAX::Project::CProjectRuntime::GetState() const
{
    return RequireProject().GetState();
}

std::optional<iCAX::Project::CProjectFault> iCAX::Project::CProjectRuntime::GetLastFault() const
{
    return RequireProject().GetLastFault();
}

iCAX::Project::CMainScenePDODescriptor iCAX::Project::CProjectRuntime::GetMainScenePDODescriptor() const
{
    return RequireProject().GetMainScenePDODescriptor();
}

iCAX::Interaction::CFacadeEndpoint iCAX::Project::CProjectRuntime::GetMainSceneFrontendFacadeEndpoint()
{
    return RequireProject().GetMainSceneFrontendFacadeEndpoint();
}

void iCAX::Project::CProjectRuntime::SetSceneFrameHandler(IN SceneRuntimeFrameHandler Handler_)
{
    {
        std::lock_guard<std::mutex> _Lock(m_FrameHandlerMutex);
        m_SceneFrameHandler = std::move(Handler_);
    }

    auto _pProject = m_pProject;
    if (!_pProject)
    {
        return;
    }

    _pProject->SetSceneFrameHandler(
        [this](
            iCAX::Project::CProjectScene& Scene_,
            const iCAX::Interaction::CFacadeEndpoint& BackendEndpoint_) {
            auto _Handler = GetSceneFrameHandler();
            if (_Handler)
            {
                _Handler(*this, Scene_, BackendEndpoint_);
            }
        });
}

void iCAX::Project::CProjectRuntime::Start()
{
    RequireProject().Start();
}

void iCAX::Project::CProjectRuntime::Stop()
{
    if (m_pProject)
    {
        m_pProject->Stop();
    }
}

void iCAX::Project::CProjectRuntime::Close()
{
    {
        std::lock_guard<std::mutex> _Lock(m_FrameHandlerMutex);
        m_SceneFrameHandler = nullptr;
    }

    auto _pProject = std::exchange(m_pProject, nullptr);
    if (_pProject)
    {
        _pProject->SetSceneFrameHandler(nullptr);
        _pProject->Close();
    }
}

std::shared_ptr<iCAX::Project::CProject> iCAX::Project::CProjectRuntime::GetLocalProject() const
{
    return m_pProject;
}

iCAX::Project::SceneRuntimeFrameHandler iCAX::Project::CProjectRuntime::GetSceneFrameHandler() const
{
    std::lock_guard<std::mutex> _Lock(m_FrameHandlerMutex);
    return m_SceneFrameHandler;
}

iCAX::Project::CProject& iCAX::Project::CProjectRuntime::RequireProject() const
{
    if (!m_pProject)
    {
        throw std::logic_error("Project runtime has no local project");
    }
    return *m_pProject;
}

std::shared_ptr<iCAX::Project::IProjectRuntime> iCAX::Project::CreateProjectRuntime(
    IN std::shared_ptr<CProject> pProject_)
{
    return std::make_shared<CProjectRuntime>(std::move(pProject_));
}
