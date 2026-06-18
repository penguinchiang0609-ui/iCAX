#pragma once

#include "ResourcesExport.h"

#include <string>

namespace iCAX
{
    namespace Resource
    {
        struct _RESOURCES_EXP CResourceKey final
        {
            std::string Source;

            bool IsValid() const;
        };

        inline CResourceKey MakeResourceKeyFromSource(IN const std::string& strSource_)
        {
            return CResourceKey{ strSource_ };
        }

        _RESOURCES_EXP bool operator==(IN const CResourceKey& Left_, IN const CResourceKey& Right_) noexcept;
        _RESOURCES_EXP bool operator!=(IN const CResourceKey& Left_, IN const CResourceKey& Right_) noexcept;
        _RESOURCES_EXP bool operator<(IN const CResourceKey& Left_, IN const CResourceKey& Right_) noexcept;

        _RESOURCES_EXP std::string ToString(IN const CResourceKey& Key_);
    }
}
