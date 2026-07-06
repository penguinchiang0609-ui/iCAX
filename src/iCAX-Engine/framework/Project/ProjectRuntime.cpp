#include "pch.h"
#include "ProjectRuntime.h"

#include <stdexcept>
#include <utility>

iCAX::Project::CLocalProjectRuntime::CLocalProjectRuntime(IN std::shared_ptr<CProject> pProject_)
    : m_pProject(std::move(pProject_))
{
    if (!m_pProject)
    {
        throw std::invalid_argument("Project cannot be null");
    }
}

iCAX::Project::CLocalProjectRuntime::~CLocalProjectRuntime()
{
    Close();
}

const iCAX::Data::uuid& iCAX::Project::CLocalProjectRuntime::GetProjectID() const
{
    return RequireProject().GetProjectID();
}

const iCAX::Data::uuid& iCAX::Project::CLocalProjectRuntime::GetMainSceneChannelID() const
{
    return RequireProject().GetMainSceneChannelID();
}

std::string iCAX::Project::CLocalProjectRuntime::GetProjectName() const
{
    return RequireProject().GetProjectName();
}

std::string iCAX::Project::CLocalProjectRuntime::GetProjectPath() const
{
    return RequireProject().GetProjectPath();
}

std::string iCAX::Project::CLocalProjectRuntime::GetStartupComponent() const
{
    return RequireProject().GetStartupComponent();
}

bool iCAX::Project::CLocalProjectRuntime::IsOpen() const
{
    return m_pProject && m_pProject->IsOpen();
}

bool iCAX::Project::CLocalProjectRuntime::IsRunning() const
{
    return m_pProject && m_pProject->IsRunning();
}

iCAX::Project::EProjectState iCAX::Project::CLocalProjectRuntime::GetState() const
{
    return RequireProject().GetState();
}

std::optional<iCAX::Project::CProjectFault> iCAX::Project::CLocalProjectRuntime::GetLastFault() const
{
    return RequireProject().GetLastFault();
}

iCAX::Project::CMainScenePDODescriptor iCAX::Project::CLocalProjectRuntime::GetMainScenePDODescriptor() const
{
    return RequireProject().GetMainScenePDODescriptor();
}

iCAX::Mail::CMailPostOffice iCAX::Project::CLocalProjectRuntime::GetMainSceneFrontendPostOffice()
{
    return RequireProject().GetMainSceneFrontendPostOffice();
}

void iCAX::Project::CLocalProjectRuntime::SetSceneFrameHandler(IN SceneRuntimeFrameHandler Handler_)
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
            const iCAX::Mail::CMailPostOffice& BackendPostOffice_) {
            auto _Handler = GetSceneFrameHandler();
            if (_Handler)
            {
                _Handler(*this, Scene_, BackendPostOffice_);
            }
        });
}

void iCAX::Project::CLocalProjectRuntime::Start()
{
    RequireProject().Start();
}

void iCAX::Project::CLocalProjectRuntime::Stop()
{
    if (m_pProject)
    {
        m_pProject->Stop();
    }
}

void iCAX::Project::CLocalProjectRuntime::Close()
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

std::shared_ptr<iCAX::Project::CProject> iCAX::Project::CLocalProjectRuntime::GetLocalProject() const
{
    return m_pProject;
}

iCAX::Project::SceneRuntimeFrameHandler iCAX::Project::CLocalProjectRuntime::GetSceneFrameHandler() const
{
    std::lock_guard<std::mutex> _Lock(m_FrameHandlerMutex);
    return m_SceneFrameHandler;
}

iCAX::Project::CProject& iCAX::Project::CLocalProjectRuntime::RequireProject() const
{
    if (!m_pProject)
    {
        throw std::logic_error("Project runtime has no local project");
    }
    return *m_pProject;
}

std::shared_ptr<iCAX::Project::IProjectRuntime> iCAX::Project::CreateLocalProjectRuntime(
    IN std::shared_ptr<CProject> pProject_)
{
    return std::make_shared<CLocalProjectRuntime>(std::move(pProject_));
}
