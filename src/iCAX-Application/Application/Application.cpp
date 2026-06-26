#include "pch.h"
#include "Application.h"

#include <stdexcept>

iCAX::Application::CApplication::CApplication() = default;

iCAX::Application::CApplication::~CApplication()
{
    if (IsRunning())
    {
        Stop();
    }
}

void iCAX::Application::CApplication::SetConfig(IN const CApplicationConfig& Config_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    if (m_bStarted)
    {
        throw std::logic_error("Application config cannot be changed after start");
    }
    m_Config = Config_;
}

void iCAX::Application::CApplication::Start()
{
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        if (m_bStarted)
        {
            return;
        }
    }

    m_Engine.SetConfig(m_Config.EngineConfig);
    m_Engine.Start();

    try
    {
        m_FrontendBridge.Attach(m_Engine);
    }
    catch (...)
    {
        m_Engine.Stop();
        throw;
    }

    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        m_bStarted = true;
    }
}

void iCAX::Application::CApplication::Stop()
{
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        if (!m_bStarted)
        {
            return;
        }
        m_bStarted = false;
    }

    m_FrontendBridge.Detach();
    m_Engine.Stop();
}

bool iCAX::Application::CApplication::IsRunning() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_bStarted && m_Engine.IsRunning();
}

iCAX::ApplicationHost::CApplicationHost& iCAX::Application::CApplication::Engine()
{
    return m_Engine;
}

const iCAX::ApplicationHost::CApplicationHost& iCAX::Application::CApplication::Engine() const
{
    return m_Engine;
}

iCAX::Application::CFrontendBridge& iCAX::Application::CApplication::Frontend()
{
    return m_FrontendBridge;
}

const iCAX::Application::CFrontendBridge& iCAX::Application::CApplication::Frontend() const
{
    return m_FrontendBridge;
}
