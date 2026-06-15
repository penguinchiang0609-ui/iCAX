#include "pch.h"
#include "ApplicationSettingsService.h"

#include <stdexcept>
#include <utility>

iCAX::Application::CApplicationSettingsService::CApplicationSettingsService(IN std::shared_ptr<CApplicationContext> pContext_, IN std::shared_ptr<IApplicationConfigStore> pStore_, IN const std::string& strConfigPath_)
    : m_pContext(std::move(pContext_))
    , m_pStore(std::move(pStore_))
    , m_strConfigPath(strConfigPath_)
{
    if (!m_pContext)
    {
        throw std::invalid_argument("Application context cannot be null");
    }
    if (!m_pStore)
    {
        throw std::invalid_argument("Application config store cannot be null");
    }
}

const iCAX::Application::CApplicationSettings& iCAX::Application::CApplicationSettingsService::GetSettings() const
{
    return m_pContext->GetSettings();
}

void iCAX::Application::CApplicationSettingsService::SetValue(IN const std::string& strKey_, IN const iCAX::Data::Variant& Value_)
{
    auto _Settings = m_pContext->GetSettings();
    _Settings.Set(strKey_, Value_);
    m_pContext->ReplaceSettings(_Settings);
}

void iCAX::Application::CApplicationSettingsService::SetValue(IN const std::string& strKey_, IN const std::string& strPath_, IN const iCAX::Data::Variant& Value_)
{
    auto _Settings = m_pContext->GetSettings();
    _Settings.Set(strKey_, strPath_, Value_);
    m_pContext->ReplaceSettings(_Settings);
}

void iCAX::Application::CApplicationSettingsService::Save() const
{
    m_pStore->Save(m_strConfigPath, m_pContext->GetSettings());
}

void iCAX::Application::CApplicationSettingsService::Reload()
{
    m_pContext->ReplaceSettings(m_pStore->Load(m_strConfigPath));
}
