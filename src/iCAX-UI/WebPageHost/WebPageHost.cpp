#include "pch.h"
#include "WebPageHost.h"

#include <stdexcept>

iCAX::Frontend::CWebPageHost::CWebPageHost() = default;

iCAX::Frontend::CWebPageHost::~CWebPageHost()
{
    if (IsRunning())
    {
        Stop();
    }
}

void iCAX::Frontend::CWebPageHost::SetConfig(const CWebPageHostConfig& Config_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    if (m_bStarted)
    {
        throw std::logic_error("WebPageHost config cannot be changed after start");
    }

    m_Config = Config_;
}

void iCAX::Frontend::CWebPageHost::Start()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    if (m_bStarted)
    {
        return;
    }
    if (!m_Config.pFrontendBridge)
    {
        throw std::logic_error("WebPageHost requires a FrontendBridge");
    }
    if (!m_Config.pFrontendBridge->IsAttached())
    {
        throw std::logic_error("WebPageHost requires an attached FrontendBridge");
    }
    m_bStarted = true;
}

void iCAX::Frontend::CWebPageHost::Stop()
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    if (!m_bStarted)
    {
        return;
    }
    if (m_Config.pFrontendBridge)
    {
        m_Config.pFrontendBridge->SetMailHandler(nullptr);
    }
    m_bStarted = false;
}

bool iCAX::Frontend::CWebPageHost::IsRunning() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_bStarted;
}

std::string iCAX::Frontend::CWebPageHost::GetApplicationChannelIDText() const
{
    return RequireBridge().GetApplicationChannelIDText();
}

std::string iCAX::Frontend::CWebPageHost::RegisterProductChannel(const std::string& strProductID_)
{
    return RequireBridge().RegisterProductChannel(strProductID_);
}

std::string iCAX::Frontend::CWebPageHost::RegisterProjectChannel(const std::string& strProjectID_)
{
    return RequireBridge().RegisterProjectChannel(strProjectID_);
}

void iCAX::Frontend::CWebPageHost::PostMail(const CWebPageMailEnvelope& Envelope_)
{
    RequireBridge().PostMail(Envelope_);
}

std::vector<iCAX::Frontend::CWebPageMailEnvelope> iCAX::Frontend::CWebPageHost::PollMails()
{
    return RequireBridge().PollMails();
}

void iCAX::Frontend::CWebPageHost::SetMailHandler(WebPageMailHandler Handler_)
{
    RequireBridge().SetMailHandler(std::move(Handler_));
}

iCAX::Application::CFrontendBridge& iCAX::Frontend::CWebPageHost::RequireBridge() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    if (!m_bStarted)
    {
        throw std::logic_error("WebPageHost is not started");
    }
    if (!m_Config.pFrontendBridge)
    {
        throw std::logic_error("WebPageHost requires a FrontendBridge");
    }
    return *m_Config.pFrontendBridge;
}
