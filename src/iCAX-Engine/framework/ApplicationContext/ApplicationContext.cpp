#include "pch.h"
#include "ApplicationContext.h"

#include <stdexcept>
#include <utility>

iCAX::Application::CApplicationContext::CApplicationContext()
    : m_pServiceProvider(std::make_shared<iCAX::Services::CServiceProvider>())
{
}

iCAX::Application::CApplicationContext::CApplicationContext(
    IN const CApplicationDescriptor& Descriptor_,
    IN const CApplicationPaths& Paths_,
    IN const iCAX::Data::PropertyBag& Settings_)
    : m_Descriptor(Descriptor_)
    , m_Paths(Paths_)
    , m_Settings(Settings_)
    , m_pServiceProvider(std::make_shared<iCAX::Services::CServiceProvider>())
{
}

iCAX::Application::CApplicationContext::CApplicationContext(
    IN const CApplicationDescriptor& Descriptor_,
    IN const CApplicationPaths& Paths_,
    IN std::shared_ptr<IApplicationConfigStore> pConfigStore_,
    IN std::string strConfigPath_)
    : m_Descriptor(Descriptor_)
    , m_Paths(Paths_)
    , m_pConfigStore(std::move(pConfigStore_))
    , m_strConfigPath(std::move(strConfigPath_))
    , m_pServiceProvider(std::make_shared<iCAX::Services::CServiceProvider>())
{
    if (!m_pConfigStore)
    {
        throw std::invalid_argument("Application config store cannot be null");
    }
    if (m_strConfigPath.empty())
    {
        throw std::invalid_argument("Application config path cannot be empty");
    }
    m_Settings = m_pConfigStore->Load(m_strConfigPath);
}

iCAX::Application::CApplicationContext::~CApplicationContext() = default;

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

const iCAX::Services::CServiceProvider& iCAX::Application::CApplicationContext::Services() const
{
    return *m_pServiceProvider;
}

void iCAX::Application::CApplicationContext::ReplaceSettings(IN const iCAX::Data::PropertyBag& Settings_)
{
    std::lock_guard<std::mutex> _Lock(m_SettingsMutex);
    m_Settings = Settings_;
}

void iCAX::Application::CApplicationContext::SetValue(
    IN const std::string& strKey_,
    IN const iCAX::Data::Variant& Value_)
{
    std::lock_guard<std::mutex> _Lock(m_SettingsMutex);
    m_Settings.Set(strKey_, Value_);
}

void iCAX::Application::CApplicationContext::SetValue(
    IN const std::string& strKey_,
    IN const std::string& strPath_,
    IN const iCAX::Data::Variant& Value_)
{
    std::lock_guard<std::mutex> _Lock(m_SettingsMutex);
    m_Settings.Set(strKey_, strPath_, Value_);
}

void iCAX::Application::CApplicationContext::SaveSettings() const
{
    if (!m_pConfigStore)
    {
        throw std::logic_error("Application context has no config store");
    }
    m_pConfigStore->Save(m_strConfigPath, GetSettings());
}

void iCAX::Application::CApplicationContext::ReloadSettings()
{
    if (!m_pConfigStore)
    {
        throw std::logic_error("Application context has no config store");
    }
    ReplaceSettings(m_pConfigStore->Load(m_strConfigPath));
}

iCAX::Services::CServiceProvider& iCAX::Application::CApplicationContext::MutableServices()
{
    return *m_pServiceProvider;
}
