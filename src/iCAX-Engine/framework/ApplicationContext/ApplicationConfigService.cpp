#include "pch.h"
#include "ApplicationConfigService.h"


iCAX::Application::CApplicationConfigService::CApplicationConfigService(
    IN std::shared_ptr<CApplicationContext> pContext_)
    : m_pContext(std::move(pContext_))
{
    if (!m_pContext)
    {
        throw std::invalid_argument("Application context cannot be null");
    }
}

iCAX::Data::PropertyBag iCAX::Application::CApplicationConfigService::GetSettings() const
{
    return m_pContext->GetSettings();
}

void iCAX::Application::CApplicationConfigService::SetValue(IN const std::string& strKey_, IN const iCAX::Data::Variant& Value_)
{
    m_pContext->SetValue(strKey_, Value_);
}

void iCAX::Application::CApplicationConfigService::SetValue(IN const std::string& strKey_, IN const std::string& strPath_, IN const iCAX::Data::Variant& Value_)
{
    m_pContext->SetValue(strKey_, strPath_, Value_);
}

void iCAX::Application::CApplicationConfigService::Save() const
{
    m_pContext->SaveSettings();
}

void iCAX::Application::CApplicationConfigService::Reload()
{
    m_pContext->ReloadSettings();
}
