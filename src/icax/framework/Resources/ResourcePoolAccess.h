#pragma once

#include "ResourceLoaderRegistry.h"
#include "ResourcePool.h"

#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <typeindex>
#include <utility>

namespace iCAX
{
    namespace Resource
    {
        /*
        * @brief 合并调用方传入的资源信息和池中已有资源信息。
        * @param [in] Pool_ 资源池。
        * @param [in] Key_ 目标资源键。
        * @param [in] strSource_ 本次加载来源。
        * @param [in] Info_ 调用方传入的覆盖信息。
        * @return 补齐 Key/Source 后的资源信息。
        * @details
        *   已登记信息作为基础，本次传入的非空/非零字段覆盖旧值。
        *   这样可以先登记资源清单，真正加载时再补充名称、hash 或其他元数据。
        */
        inline CResourceInfo BuildLoadInfo(
            IN const CResourcePool& Pool_,
            IN const CResourceKey& Key_,
            IN const std::string& strSource_,
            IN const CResourceInfo& Info_)
        {
            auto _Info = Info_;

            auto _StoredInfo = Pool_.GetInfo(Key_);
            if (_StoredInfo.has_value())
            {
                auto _Merged = _StoredInfo.value();
                if (!_Info.Name.empty())
                {
                    _Merged.Name = _Info.Name;
                }
                if (!_Info.Source.empty())
                {
                    _Merged.Source = _Info.Source;
                }
                if (!_Info.ContentHash.empty())
                {
                    _Merged.ContentHash = _Info.ContentHash;
                }
                if (_Info.nSize != 0)
                {
                    _Merged.nSize = _Info.nSize;
                }
                if (_Info.nFlags != 0)
                {
                    _Merged.nFlags = _Info.nFlags;
                }
                if (_Info.Persistence != EResourcePersistenceMode::RuntimeOnly)
                {
                    _Merged.Persistence = _Info.Persistence;
                }
                for (const auto& _Pair : _Info.Metadata)
                {
                    _Merged.Metadata[_Pair.first] = _Pair.second;
                }
                _Info = std::move(_Merged);
            }

            _Info.Key = Key_;
            if (!strSource_.empty())
            {
                _Info.Source = strSource_;
            }
            else if (_Info.Source.empty())
            {
                _Info.Source = Key_.Source;
            }
            return _Info;
        }

        /*
        * @brief 创建资源加载上下文。
        * @param [in,out] Pool_ 资源池，用于读取已有资源信息。
        * @param [in] Key_ 目标资源键。
        * @param [in] TargetResourceType_ 调用方请求的 C++ 资源类型。
        * @param [in] strSource_ 加载来源。
        * @param [in] Info_ 调用方传入的资源信息。
        * @param [in] Options_ 加载选项。
        * @return 可传给 IResourceLoader 的上下文。
        */
        inline CResourceLoadContext MakeLoadContext(
            IN CResourcePool& Pool_,
            IN const CResourceKey& Key_,
            IN std::type_index TargetResourceType_,
            IN const std::string& strSource_,
            IN const CResourceInfo& Info_,
            IN const std::map<std::string, std::string>& Options_)
        {
            CResourceLoadContext _Context;
            _Context.TargetKey = Key_;
            _Context.TargetResourceType = TargetResourceType_;
            _Context.Info = BuildLoadInfo(Pool_, Key_, strSource_, Info_);
            _Context.Source = strSource_.empty() ? _Context.Info.Source : strSource_;
            _Context.Options = Options_;
            return _Context;
        }

        /*
        * @brief 将加载结果写回资源池。
        * @param [in,out] Pool_ 资源池。
        * @param [in] Result_ 加载结果。
        * @return true 表示成功写回资源信息或对象。
        * @details
        *   如果 Result 只包含 Info，则只登记资源清单；
        *   如果包含 pResource，则必须带 RuntimeType，并将对象写入资源池。
        */
        inline bool StoreLoadResult(IN CResourcePool& Pool_, IN const CResourceLoadResult& Result_)
        {
            if (!Result_.IsOK() || !Result_.Info.Key.IsValid())
            {
                return false;
            }

            if (Result_.pResource)
            {
                if (!Result_.RuntimeType.has_value())
                {
                    return false;
                }
                Pool_.SetUntyped(Result_.Info.Key, Result_.pResource, Result_.RuntimeType.value(), Result_.Info);
            }
            else
            {
                Pool_.Register(Result_.Info);
            }
            return true;
        }

        template <typename T>
        /*
        * @brief 使用指定加载器注册表加载资源。
        * @param [in,out] Pool_ 项目资源池。
        * @param [in,out] LoaderRegistry_ 加载器注册表。
        * @param [in] Key_ 资源键。
        * @param [in] Info_ 资源信息。
        * @param [in] Options_ 加载选项。
        * @return 类型匹配的资源对象；失败、类型冲突或 key 无效时返回 nullptr。
        */
        std::shared_ptr<T> Load(
            IN CResourcePool& Pool_,
            IN CResourceLoaderRegistry& LoaderRegistry_,
            IN const CResourceKey& Key_,
            IN const CResourceInfo& Info_ = CResourceInfo(),
            IN const std::map<std::string, std::string>& Options_ = {})
        {
            if (!Key_.IsValid())
            {
                return nullptr;
            }

            using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
            auto _pExisting = Pool_.GetUntyped(Key_, typeid(TResource));
            if (_pExisting)
            {
                return std::static_pointer_cast<TResource>(_pExisting);
            }

            if (Pool_.HasObject(Key_))
            {
                return nullptr;
            }

            const auto _TargetResourceType = std::type_index(typeid(TResource));
            auto _Context = MakeLoadContext(Pool_, Key_, _TargetResourceType, Key_.Source, Info_, Options_);
            auto _Result = LoaderRegistry_.LoadResource(_Context);
            if (!_Result.IsOK())
            {
                return nullptr;
            }
            if (!StoreLoadResult(Pool_, _Result))
            {
                return nullptr;
            }

            auto _pLoaded = Pool_.GetUntyped(Key_, typeid(TResource));
            if (!_pLoaded)
            {
                return nullptr;
            }
            return std::static_pointer_cast<TResource>(_pLoaded);
        }

        template <typename T>
        /*
        * @brief 使用全局加载器注册表加载资源。
        * @return 类型匹配的资源对象；失败时返回 nullptr。
        * @details 优先用于简单场景；项目运行时推荐显式传入自己的 LoaderRegistry。
        */
        std::shared_ptr<T> Load(
            IN CResourcePool& Pool_,
            IN const CResourceKey& Key_,
            IN const CResourceInfo& Info_ = CResourceInfo(),
            IN const std::map<std::string, std::string>& Options_ = {})
        {
            if (!Key_.IsValid())
            {
                return nullptr;
            }

            using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
            auto _pExisting = Pool_.GetUntyped(Key_, typeid(TResource));
            if (_pExisting)
            {
                return std::static_pointer_cast<TResource>(_pExisting);
            }

            if (Pool_.HasObject(Key_))
            {
                return nullptr;
            }

            const auto _TargetResourceType = std::type_index(typeid(TResource));
            auto _Context = MakeLoadContext(Pool_, Key_, _TargetResourceType, Key_.Source, Info_, Options_);
            auto _Result = CResourceLoaderRegistry::Load(_Context);
            if (!_Result.IsOK())
            {
                return nullptr;
            }
            if (!StoreLoadResult(Pool_, _Result))
            {
                return nullptr;
            }

            auto _pLoaded = Pool_.GetUntyped(Key_, typeid(TResource));
            if (!_pLoaded)
            {
                return nullptr;
            }
            return std::static_pointer_cast<TResource>(_pLoaded);
        }

        template <typename T>
        /*
        * @brief 使用指定加载器注册表按来源字符串加载资源。
        * @param [in] strSource_ 资源来源，同时作为 key。
        * @return 类型匹配的资源对象；来源为空或加载失败时返回 nullptr。
        */
        std::shared_ptr<T> Load(
            IN CResourcePool& Pool_,
            IN CResourceLoaderRegistry& LoaderRegistry_,
            IN const std::string& strSource_,
            IN const CResourceInfo& Info_ = CResourceInfo(),
            IN const std::map<std::string, std::string>& Options_ = {})
        {
            if (strSource_.empty())
            {
                return nullptr;
            }

            return Load<T>(Pool_, LoaderRegistry_, MakeResourceKeyFromSource(strSource_), Info_, Options_);
        }

        template <typename T>
        /*
        * @brief 使用全局加载器注册表按来源字符串加载资源。
        * @return 类型匹配的资源对象；来源为空或加载失败时返回 nullptr。
        */
        std::shared_ptr<T> Load(
            IN CResourcePool& Pool_,
            IN const std::string& strSource_,
            IN const CResourceInfo& Info_ = CResourceInfo(),
            IN const std::map<std::string, std::string>& Options_ = {})
        {
            if (strSource_.empty())
            {
                return nullptr;
            }

            return Load<T>(Pool_, MakeResourceKeyFromSource(strSource_), Info_, Options_);
        }
    }
}
