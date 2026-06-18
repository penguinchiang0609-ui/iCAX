#pragma once

#include "ResourceLoadContext.h"
#include "ResourceLoadResult.h"

namespace iCAX
{
    namespace Resource
    {
        class _RESOURCES_EXP IResourceLoader
        {
        public:
            IResourceLoader() = default;
            virtual ~IResourceLoader() = default;

            IResourceLoader(IN const IResourceLoader&) = delete;
            IResourceLoader& operator=(IN const IResourceLoader&) = delete;

        public:
            virtual bool CanLoad(IN const CResourceLoadContext& Context_) const = 0;
            virtual CResourceLoadResult Load(IN const CResourceLoadContext& Context_) = 0;
        };
    }
}
