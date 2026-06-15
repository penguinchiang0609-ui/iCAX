#include "pch.h"
#include "ApplicationDescriptor.h"

#include <algorithm>

bool iCAX::Application::CApplicationDescriptor::SupportsProjectMagic(IN const std::string& strMagic_) const
{
    return std::find(SupportedProjectMagics.begin(), SupportedProjectMagics.end(), strMagic_) != SupportedProjectMagics.end();
}

bool iCAX::Application::CApplicationDescriptor::SupportsProjectVersion(IN uint32_t nVersion_) const
{
    return std::find(SupportedProjectVersions.begin(), SupportedProjectVersions.end(), nVersion_) != SupportedProjectVersions.end();
}
