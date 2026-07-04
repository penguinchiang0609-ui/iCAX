#include "pch.h"
#include "ApplicationContext.h"

#include "Services/ServiceProvider.h"

#include <stdexcept>
#include <utility>

iCAX::Application::CApplicationContext::CApplicationContext(
    IN const CApplicationDescriptor& Descriptor_,
    IN const CApplicationPaths& Paths_,
    IN const iCAX::Data::PropertyBag& Settings_)
    : m_Descriptor(Descriptor_)
    , m_Paths(Paths_)
    , m_Settings(Settings_)
{
}

const iCAX::Application::CApplicationDescriptor& iCAX::Application::CApplicationContext::GetDescriptor() const
{
    return m_Descriptor;
}

const iCAX::Application::CApplicationPaths& iCAX::Application::CApplicationContext::GetPaths() const
{
    return m_Paths;
}

iCAX::Data::PropertyBag iCAX::Application::CApplicationContext::GetSettings() const
{
    std::lock_guard<std::mutex> _Lock(m_SettingsMutex);
    return m_Settings;
}

void iCAX::Application::CApplicationContext::ReplaceSettings(IN const iCAX::Data::PropertyBag& Settings_)
{
    std::lock_guard<std::mutex> _Lock(m_SettingsMutex);
    m_Settings = Settings_;
}

void iCAX::Application::CApplicationContext::BindRuntimeCapabilities(
    IN const iCAX::Data::uuid& ApplicationChannelID_,
    IN std::weak_ptr<iCAX::Services::CServiceProvider> pServiceProvider_,
    IN PostOfficeProvider BackendPostOfficeProvider_,
    IN PostOfficeProvider FrontendPostOfficeProvider_)
{
    if (ApplicationChannelID_.is_nil())
    {
        throw std::invalid_argument("Application channel id cannot be nil");
    }
    if (!BackendPostOfficeProvider_)
    {
        throw std::invalid_argument("Application backend post office provider cannot be empty");
    }
    if (!FrontendPostOfficeProvider_)
    {
        throw std::invalid_argument("Application frontend post office provider cannot be empty");
    }

    m_ApplicationChannelID = ApplicationChannelID_;
    m_pServiceProvider = std::move(pServiceProvider_);
    m_BackendPostOfficeProvider = std::move(BackendPostOfficeProvider_);
    m_FrontendPostOfficeProvider = std::move(FrontendPostOfficeProvider_);
}

const iCAX::Data::uuid& iCAX::Application::CApplicationContext::GetApplicationChannelID() const
{
    return m_ApplicationChannelID;
}

iCAX::Mail::CMailPostOffice iCAX::Application::CApplicationContext::GetBackendPostOffice() const
{
    if (!m_BackendPostOfficeProvider)
    {
        throw std::logic_error("Application backend post office is not configured");
    }
    return m_BackendPostOfficeProvider();
}

iCAX::Mail::CMailPostOffice iCAX::Application::CApplicationContext::GetFrontendPostOffice() const
{
    if (!m_FrontendPostOfficeProvider)
    {
        throw std::logic_error("Application frontend post office is not configured");
    }
    return m_FrontendPostOfficeProvider();
}

iCAX::Services::CServiceProvider& iCAX::Application::CApplicationContext::Services() const
{
    auto _pServiceProvider = m_pServiceProvider.lock();
    if (!_pServiceProvider)
    {
        throw std::logic_error("Application service provider is not available");
    }
    return *_pServiceProvider;
}
