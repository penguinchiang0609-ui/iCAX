#pragma once

#include "ResourcePoolExport.h"

#include <Data/uuid.h>

#include <string>

namespace iCAX
{
    namespace Resource
    {
        struct _RESOURCE_POOL_EXP CResourceKey final
        {
            std::string Type;
            iCAX::Data::uuid ID;

            bool IsValid() const;
        };

        _RESOURCE_POOL_EXP bool operator==(IN const CResourceKey& Left_, IN const CResourceKey& Right_) noexcept;
        _RESOURCE_POOL_EXP bool operator!=(IN const CResourceKey& Left_, IN const CResourceKey& Right_) noexcept;
        _RESOURCE_POOL_EXP bool operator<(IN const CResourceKey& Left_, IN const CResourceKey& Right_) noexcept;

        _RESOURCE_POOL_EXP std::string ToString(IN const CResourceKey& Key_);
    }
}
