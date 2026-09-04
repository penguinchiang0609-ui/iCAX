#include "TemplateContracts.h"

std::string iCAX::TemplateRuntime::SLocalizedText::Resolve(
    const std::string& strLocale_) const
{
    if (const auto _Iterator = Translations.find(strLocale_);
        _Iterator != Translations.end())
    {
        return _Iterator->second;
    }
    if (!Default.empty())
    {
        return Default;
    }
    return Translations.empty() ? std::string() : Translations.begin()->second;
}

