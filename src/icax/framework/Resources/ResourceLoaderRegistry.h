#pragma once

#include "IResourceLoader.h"
#include "ResourceLoaderRegistrationCatalog.h"

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
        /*
        * @brief 资源加载器注册表。
        * @details
        *   Registry 按资源 C++ 类型保存 loader 列表。Load<T> 会用 typeid(T) 找到候选 loader，
        *   再由 loader 的 CanLoad 判断来源、选项是否适配。
        *   具体 Project 可以持有自己的 Registry，从而隔离产品或项目级加载能力。
        */
        class _RESOURCES_EXP CResourceLoaderRegistry final
        {
        public:
            CResourceLoaderRegistry() = default;
            ~CResourceLoaderRegistry() = default;

            CResourceLoaderRegistry(IN const CResourceLoaderRegistry&) = delete;
            CResourceLoaderRegistry& operator=(IN const CResourceLoaderRegistry&) = delete;

        public:
            /*
            * @brief 注册加载器。
            * @param [in] ResourceType_ 资源 C++ 类型。
            * @param [in] pLoader_ 加载器实例，不能为空。
            * @return true 表示注册成功；false 表示相同实例或相同 loader 类型已存在。
            */
            bool RegisterLoader(IN const std::type_info& ResourceType_, IN const std::shared_ptr<IResourceLoader>& pLoader_);

            template <typename TResource>
            /*
            * @brief 按模板资源类型注册加载器。
            * @param [in] pLoader_ 加载器实例。
            * @return true 表示注册成功。
            */
            bool RegisterLoader(IN const std::shared_ptr<IResourceLoader>& pLoader_)
            {
                using TActualResource = std::remove_cv_t<std::remove_reference_t<TResource>>;
                return RegisterLoader(typeid(TActualResource), pLoader_);
            }

            /*
            * @brief 获取指定资源类型的全部加载器。
            * @return 候选加载器列表；不存在时返回空数组。
            */
            std::vector<std::shared_ptr<IResourceLoader>> GetLoadersFor(IN const std::type_info& ResourceType_) const;

            template <typename TResource>
            /*
            * @brief 获取模板资源类型的全部加载器。
            */
            std::vector<std::shared_ptr<IResourceLoader>> GetLoadersFor() const
            {
                using TActualResource = std::remove_cv_t<std::remove_reference_t<TResource>>;
                return GetLoadersFor(typeid(TActualResource));
            }

            /*
            * @brief 为当前上下文查找第一个可用加载器。
            * @param [in] Context_ 加载上下文。
            * @return 可处理该上下文的加载器；找不到时返回 nullptr。
            */
            std::shared_ptr<IResourceLoader> FindLoaderFor(IN const CResourceLoadContext& Context_) const;

            /*
            * @brief 执行资源加载。
            * @param [in] Context_ 加载上下文。
            * @return 加载结果；找不到 loader 时返回 NoLoader。
            */
            CResourceLoadResult LoadResource(IN const CResourceLoadContext& Context_);

        public:
            /*
            * @brief 向全局加载器注册表注册加载器。
            * @details 主要用于简单场景；项目级资源库优先注入自己的 Registry。
            */
            static bool Register(IN const std::type_info& ResourceType_, IN const std::shared_ptr<IResourceLoader>& pLoader_);

            template <typename TResource>
            /*
            * @brief 向全局加载器注册表注册模板资源类型加载器。
            */
            static bool Register(IN const std::shared_ptr<IResourceLoader>& pLoader_)
            {
                using TActualResource = std::remove_cv_t<std::remove_reference_t<TResource>>;
                return Register(typeid(TActualResource), pLoader_);
            }

            /*
            * @brief 从全局加载器注册表获取加载器。
            */
            static std::vector<std::shared_ptr<IResourceLoader>> GetLoaders(IN const std::type_info& ResourceType_);

            template <typename TResource>
            /*
            * @brief 从全局加载器注册表获取模板资源类型加载器。
            */
            static std::vector<std::shared_ptr<IResourceLoader>> GetLoaders()
            {
                using TActualResource = std::remove_cv_t<std::remove_reference_t<TResource>>;
                return GetLoaders(typeid(TActualResource));
            }

            /*
            * @brief 从全局注册表查找加载器。
            */
            static std::shared_ptr<IResourceLoader> FindLoader(IN const CResourceLoadContext& Context_);

            /*
            * @brief 使用全局注册表执行加载。
            */
            static CResourceLoadResult Load(IN const CResourceLoadContext& Context_);

        private:
            /*
            * @brief 向列表追加不重复加载器。
            */
            static void AppendUnique(
                IN OUT std::vector<std::shared_ptr<IResourceLoader>>& Loaders_,
                IN const std::shared_ptr<IResourceLoader>& pLoader_);

            /*
            * @brief 获取全局加载器注册表。
            * @details 会增量回放 ResourceLoaderRegistrationCatalog 中的新注册记录。
            */
            static CResourceLoaderRegistry& GetGlobalRegistry();

        private:
            mutable std::shared_mutex m_Mutex;
            std::map<std::type_index, std::vector<std::shared_ptr<IResourceLoader>>> m_Loaders;
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
                ::iCAX::Resource::CResourceLoaderRegistrationCatalog::Register([](::iCAX::Resource::CResourceLoaderRegistry& Registry_) \
                { \
                    Registry_.RegisterLoader(typeid(ResourceClass), std::make_shared<LoaderType>()); \
                }, this); \
            } \
        }; \
        const ICAX_RESOURCE_DETAIL_JOIN(CAutoResourceLoaderRegistration_, UniqueID) ICAX_RESOURCE_DETAIL_JOIN(g_AutoResourceLoaderRegistration_, UniqueID); \
    }
