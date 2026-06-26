#pragma once

#include "ResourceLoadContext.h"
#include "ResourceLoadResult.h"

namespace iCAX
{
    namespace Resource
    {
        /*
        * @brief 资源加载器接口。
        * @details
        *   加载器按目标资源 C++ 类型注册。ResourceLibrary::Load<T> 会通过 typeid(T)
        *   找到候选加载器，再调用 CanLoad 判断当前来源和选项是否支持。
        */
        class _RESOURCES_EXP IResourceLoader
        {
        public:
            IResourceLoader() = default;
            virtual ~IResourceLoader() = default;

            IResourceLoader(IN const IResourceLoader&) = delete;
            IResourceLoader& operator=(IN const IResourceLoader&) = delete;

        public:
            /*
            * @brief 判断加载器是否能处理当前加载请求。
            * @param [in] Context_ 加载上下文，包含目标类型、来源、资源信息和选项。
            * @return true 表示可以尝试 Load。
            */
            virtual bool CanLoad(IN const CResourceLoadContext& Context_) const = 0;

            /*
            * @brief 执行加载。
            * @param [in] Context_ 加载上下文。
            * @return 加载结果；成功结果必须携带合法 Info.Key，若包含资源对象则必须携带 RuntimeType。
            */
            virtual CResourceLoadResult Load(IN const CResourceLoadContext& Context_) = 0;
        };
    }
}
