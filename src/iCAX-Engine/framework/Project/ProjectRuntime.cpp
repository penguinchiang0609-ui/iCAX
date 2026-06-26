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

const iCAX::Data::uuid& iCAX::Project::CLocalProjectRuntime::GetProjectChannelID() const
{
    return RequireProject().GetProjectChannelID();
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

iCAX::Project::EProjectRole iCAX::Project::CLocalProjectRuntime::GetRole() const
{
    return RequireProject().GetRole();
}

bool iCAX::Project::CLocalProjectRuntime::IsMainProject() const
{
    return RequireProject().IsMainProject();
}

bool iCAX::Project::CLocalProjectRuntime::IsTransientProject() const
{
    return RequireProject().IsTransientProject();
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

iCAX::Project::CProjectPDODescriptor iCAX::Project::CLocalProjectRuntime::GetPDODescriptor() const
{
    return RequireProject().GetPDODescriptor();
}

iCAX::Mail::CMailPostOffice iCAX::Project::CLocalProjectRuntime::GetFrontendPostOffice()
{
    return RequireProject().GetFrontendPostOffice();
}

void iCAX::Project::CLocalProjectRuntime::SetFrameHandler(IN ProjectRuntimeFrameHandler Handler_)
{
    {
        std::lock_guard<std::mutex> _Lock(m_FrameHandlerMutex);
        m_FrameHandler = std::move(Handler_);
    }

    auto _pProject = m_pProject;
    if (!_pProject)
    {
        return;
    }

    _pProject->SetFrameHandler(
        [this](
            iCAX::Project::CProject&,
            const iCAX::Mail::CMailPostOffice& BackendPostOffice_) {
            auto _Handler = GetFrameHandler();
            if (_Handler)
            {
                _Handler(*this, BackendPostOffice_);
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
        m_FrameHandler = nullptr;
    }

    auto _pProject = std::exchange(m_pProject, nullptr);
    if (_pProject)
    {
        _pProject->SetFrameHandler(nullptr);
        _pProject->Close();
    }
}

std::shared_ptr<iCAX::Project::CProject> iCAX::Project::CLocalProjectRuntime::GetLocalProject() const
{
    return m_pProject;
}

iCAX::Project::ProjectRuntimeFrameHandler iCAX::Project::CLocalProjectRuntime::GetFrameHandler() const
{
    std::lock_guard<std::mutex> _Lock(m_FrameHandlerMutex);
    return m_FrameHandler;
}

iCAX::Project::CProject& iCAX::Project::CLocalProjectRuntime::RequireProject() const
{
    if (!m_pProject)
    {
        throw std::logic_error("ProjectRuntime is closed");
    }
    return *m_pProject;
}

std::shared_ptr<iCAX::Project::IProjectRuntime> iCAX::Project::CreateLocalProjectRuntime(
    IN std::shared_ptr<CProject> pProject_)
{
    return std::make_shared<CLocalProjectRuntime>(std::move(pProject_));
}
