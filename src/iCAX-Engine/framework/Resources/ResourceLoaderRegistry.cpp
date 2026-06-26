#include "pch.h"
#include "ResourceLoaderRegistry.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>

bool iCAX::Resource::CResourceLoaderRegistry::RegisterLoader(IN const std::type_info& ResourceType_, IN const std::shared_ptr<IResourceLoader>& pLoader_)
{
    if (!pLoader_)
    {
        throw std::invalid_argument("Resource loader cannot be null");
    }

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    auto& _Loaders = m_Loaders[std::type_index(ResourceType_)];
    for (const auto& _Loader : _Loaders)
    {
        // 同一实例或同一 loader 实现类型只保留一份，避免重复 CanLoad/Load。
        if (_Loader == pLoader_)
        {
            return false;
        }
        if (_Loader && typeid(*_Loader) == typeid(*pLoader_))
        {
            return false;
        }
    }

    _Loaders.push_back(pLoader_);
    return true;
}

std::vector<std::shared_ptr<iCAX::Resource::IResourceLoader>> iCAX::Resource::CResourceLoaderRegistry::GetLoadersFor(IN const std::type_info& ResourceType_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_Loaders.find(std::type_index(ResourceType_));
    if (_Ite != m_Loaders.end())
    {
        return _Ite->second;
    }

    return {};
}

std::shared_ptr<iCAX::Resource::IResourceLoader> iCAX::Resource::CResourceLoaderRegistry::FindLoaderFor(IN const CResourceLoadContext& Context_) const
{
    if (Context_.TargetResourceType == std::type_index(typeid(void)))
    {
        return nullptr;
    }

    std::vector<std::shared_ptr<IResourceLoader>> _Candidates;
    {
        std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
        auto _Ite = m_Loaders.find(Context_.TargetResourceType);
        if (_Ite != m_Loaders.end())
        {
            _Candidates = _Ite->second;
        }
    }

    // 在锁外调用 CanLoad，避免 loader 内部间接注册或查询时造成注册表锁重入。
    for (const auto& _Loader : _Candidates)
    {
        if (_Loader && _Loader->CanLoad(Context_))
        {
            return _Loader;
        }
    }
    return nullptr;
}

iCAX::Resource::CResourceLoadResult iCAX::Resource::CResourceLoaderRegistry::LoadResource(IN const CResourceLoadContext& Context_)
{
    auto _Loader = FindLoaderFor(Context_);
    if (!_Loader)
    {
        return CResourceLoadResult::NoLoader(Context_.TargetKey);
    }

    auto _Result = _Loader->Load(Context_);
    if (!_Result.IsOK())
    {
        return _Result;
    }

    auto _Info = _Result.Info;
    if (!_Info.Key.IsValid())
    {
        _Info.Key = Context_.TargetKey;
    }
    if (!_Info.Key.IsValid())
    {
        return CResourceLoadResult::Invalid(Context_.TargetKey, "Loaded resource must have a valid key");
    }
    if (_Result.pResource && !_Result.RuntimeType.has_value())
    {
        return CResourceLoadResult::Invalid(_Info.Key, "Loaded resource object must carry runtime type");
    }

    _Result.Info = _Info;
    return _Result;
}
