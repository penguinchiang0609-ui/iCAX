#include "pch.h"
#include "ApplicationContext.h"

iCAX::Application::CApplicationContext::CApplicationContext(IN const CApplicationDescriptor& Descriptor_, IN const CApplicationPaths& Paths_, IN const CApplicationSettings& Settings_)
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

const iCAX::Application::CApplicationSettings& iCAX::Application::CApplicationContext::GetSettings() const
{
    return m_Settings;
}

void iCAX::Application::CApplicationContext::ReplaceSettings(IN const CApplicationSettings& Settings_)
{
    m_Settings = Settings_;
}
