#include "pch.h"
#include "ApplicationSettings.h"

iCAX::Application::CApplicationSettings::CApplicationSettings()
    : m_Bag()
{
}

iCAX::Application::CApplicationSettings::CApplicationSettings(IN const iCAX::Data::PropertySet& Properties_)
    : m_Bag(Properties_)
{
}

iCAX::Application::CApplicationSettings::~CApplicationSettings() = default;

void iCAX::Application::CApplicationSettings::Set(IN const std::string& strKey_, IN const iCAX::Data::Variant& Value_)
{
    m_Bag.Set(strKey_, Value_);
}

void iCAX::Application::CApplicationSettings::Set(IN const std::string& strKey_, IN const std::string& strPath_, IN const iCAX::Data::Variant& Value_)
{
    m_Bag.Set(strKey_, strPath_, Value_);
}

iCAX::Data::Variant iCAX::Application::CApplicationSettings::Get(IN const std::string& strKey_, IN const iCAX::Data::Variant& Default_) const
{
    return m_Bag.Get(strKey_, Default_);
}

iCAX::Data::Variant iCAX::Application::CApplicationSettings::Get(IN const std::string& strKey_, IN const std::string& strPath_, IN const iCAX::Data::Variant& Default_) const
{
    return m_Bag.Get(strKey_, strPath_, Default_);
}

const iCAX::Data::PropertySet& iCAX::Application::CApplicationSettings::GetProperties() const
{
    return m_Bag.GetProperties();
}
