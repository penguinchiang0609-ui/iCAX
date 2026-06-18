#pragma once

#include "ResourceInfo.h"

#include <map>
#include <string>
#include <typeindex>
#include <typeinfo>

namespace iCAX
{
    namespace Resource
    {
        struct _RESOURCES_EXP CResourceLoadContext final
        {
            CResourceKey TargetKey;
            std::type_index TargetResourceType = std::type_index(typeid(void));
            std::string Source;
            CResourceInfo Info;
            std::map<std::string, std::string> Options;
        };
    }
}
