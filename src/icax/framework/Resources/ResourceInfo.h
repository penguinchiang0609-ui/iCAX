#pragma once

#include "ResourceKey.h"

#include <cstdint>
#include <map>
#include <string>

namespace iCAX
{
    namespace Resource
    {
        enum class EResourcePersistenceMode : uint8_t
        {
            RuntimeOnly = 0,
            Embedded = 1,
            External = 2
        };

        struct _RESOURCES_EXP CResourceInfo final
        {
            CResourceKey Key;
            std::string Name;
            std::string Source;
            std::string ContentHash;
            uint64_t nSize = 0;
            uint32_t nFlags = 0;
            EResourcePersistenceMode Persistence = EResourcePersistenceMode::RuntimeOnly;
            std::map<std::string, std::string> Metadata;

            bool IsRuntimeOnly() const noexcept
            {
                return Persistence == EResourcePersistenceMode::RuntimeOnly;
            }

            bool IsEmbedded() const noexcept
            {
                return Persistence == EResourcePersistenceMode::Embedded;
            }

            bool IsExternal() const noexcept
            {
                return Persistence == EResourcePersistenceMode::External;
            }

            bool IsPersistent() const noexcept
            {
                return Persistence != EResourcePersistenceMode::RuntimeOnly;
            }
        };
    }
}
