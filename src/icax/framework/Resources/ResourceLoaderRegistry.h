#pragma once

#include "IResourceLoader.h"

#include <map>
#include <memory>
#include <shared_mutex>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <vector>

namespace iCAX
{
    namespace Resource
    {
        class _RESOURCES_EXP CResourceLoaderRegistry final
        {
        private:
            CResourceLoaderRegistry() = delete;
            ~CResourceLoaderRegistry() = delete;

        public:
            static bool Register(IN const std::type_info& ResourceType_, IN const std::shared_ptr<IResourceLoader>& pLoader_);

            template <typename TResource>
            static bool Register(IN const std::shared_ptr<IResourceLoader>& pLoader_)
            {
                using TActualResource = std::remove_cv_t<std::remove_reference_t<TResource>>;
                return Register(typeid(TActualResource), pLoader_);
            }

            static std::vector<std::shared_ptr<IResourceLoader>> GetLoaders(IN const std::type_info& ResourceType_);

            template <typename TResource>
            static std::vector<std::shared_ptr<IResourceLoader>> GetLoaders()
            {
                using TActualResource = std::remove_cv_t<std::remove_reference_t<TResource>>;
                return GetLoaders(typeid(TActualResource));
            }

            static std::shared_ptr<IResourceLoader> FindLoader(IN const CResourceLoadContext& Context_);
            static CResourceLoadResult Load(IN const CResourceLoadContext& Context_);

        private:
            static void AppendUnique(
                IN OUT std::vector<std::shared_ptr<IResourceLoader>>& Loaders_,
                IN const std::shared_ptr<IResourceLoader>& pLoader_);
            static std::shared_mutex& GetMutex();
            static std::map<std::type_index, std::vector<std::shared_ptr<IResourceLoader>>>& GetLoaderMap();
        };
    }
}

#define ICAX_RESOURCE_DETAIL_JOIN_IMPL(x, y) x##y
#define ICAX_RESOURCE_DETAIL_JOIN(x, y) ICAX_RESOURCE_DETAIL_JOIN_IMPL(x, y)

#define ICAX_REGISTER_RESOURCE_LOADER(ResourceClass, LoaderType) ICAX_REGISTER_RESOURCE_LOADER_IMPL(ResourceClass, LoaderType, __COUNTER__)
#define ICAX_REGISTER_RESOURCE_LOADER_IMPL(ResourceClass, LoaderType, UniqueID) \
    namespace \
    { \
        struct ICAX_RESOURCE_DETAIL_JOIN(CAutoResourceLoaderRegistration_, UniqueID) final \
        { \
            ICAX_RESOURCE_DETAIL_JOIN(CAutoResourceLoaderRegistration_, UniqueID)() \
            { \
                ::iCAX::Resource::CResourceLoaderRegistry::Register(typeid(ResourceClass), std::make_shared<LoaderType>()); \
            } \
        }; \
        const ICAX_RESOURCE_DETAIL_JOIN(CAutoResourceLoaderRegistration_, UniqueID) ICAX_RESOURCE_DETAIL_JOIN(g_AutoResourceLoaderRegistration_, UniqueID); \
    }
