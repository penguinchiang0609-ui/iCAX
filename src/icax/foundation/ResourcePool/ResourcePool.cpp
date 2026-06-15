#include "pch.h"
#include "ResourcePool.h"

#include <mutex>
#include <stdexcept>

void iCAX::Resource::CResourcePool::SetUntyped(IN const CResourceKey& Key_, IN std::shared_ptr<void> pResource_, IN const std::type_info& RuntimeType_, IN const CResourceInfo& Info_)
{
    ValidateKey(Key_);
    if (!pResource_)
    {
        throw std::invalid_argument("Resource pointer cannot be null");
    }

    CResourceRecord _Record;
    _Record.Info = NormalizeInfo(Key_, Info_);
    _Record.RuntimeType = std::type_index(RuntimeType_);
    _Record.pResource = std::move(pResource_);

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    m_mapResources[Key_] = std::move(_Record);
}

bool iCAX::Resource::CResourcePool::TryAddUntyped(IN const CResourceKey& Key_, IN std::shared_ptr<void> pResource_, IN const std::type_info& RuntimeType_, IN const CResourceInfo& Info_)
{
    ValidateKey(Key_);
    if (!pResource_)
    {
        throw std::invalid_argument("Resource pointer cannot be null");
    }

    CResourceRecord _Record;
    _Record.Info = NormalizeInfo(Key_, Info_);
    _Record.RuntimeType = std::type_index(RuntimeType_);
    _Record.pResource = std::move(pResource_);

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    return m_mapResources.emplace(Key_, std::move(_Record)).second;
}

std::shared_ptr<void> iCAX::Resource::CResourcePool::GetUntyped(IN const CResourceKey& Key_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    if (_Ite == m_mapResources.end())
    {
        return nullptr;
    }
    return _Ite->second.pResource;
}

std::shared_ptr<void> iCAX::Resource::CResourcePool::GetUntyped(IN const CResourceKey& Key_, IN const std::type_info& ExpectedRuntimeType_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    if (_Ite == m_mapResources.end() || _Ite->second.RuntimeType != std::type_index(ExpectedRuntimeType_))
    {
        return nullptr;
    }
    return _Ite->second.pResource;
}

bool iCAX::Resource::CResourcePool::Contains(IN const CResourceKey& Key_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    return m_mapResources.find(Key_) != m_mapResources.end();
}

bool iCAX::Resource::CResourcePool::Remove(IN const CResourceKey& Key_)
{
    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    return m_mapResources.erase(Key_) > 0;
}

void iCAX::Resource::CResourcePool::Clear()
{
    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    m_mapResources.clear();
}

size_t iCAX::Resource::CResourcePool::Count() const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    return m_mapResources.size();
}

std::optional<iCAX::Resource::CResourceInfo> iCAX::Resource::CResourcePool::GetInfo(IN const CResourceKey& Key_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    if (_Ite == m_mapResources.end())
    {
        return std::nullopt;
    }
    return _Ite->second.Info;
}

bool iCAX::Resource::CResourcePool::UpdateInfo(IN const CResourceKey& Key_, IN const CResourceInfo& Info_)
{
    ValidateKey(Key_);

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    if (_Ite == m_mapResources.end())
    {
        return false;
    }
    _Ite->second.Info = NormalizeInfo(Key_, Info_);
    return true;
}

std::vector<iCAX::Resource::CResourceKey> iCAX::Resource::CResourcePool::GetKeys(IN const std::string& strType_) const
{
    std::vector<CResourceKey> _Keys;

    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    for (const auto& _Pair : m_mapResources)
    {
        if (strType_.empty() || _Pair.first.Type == strType_)
        {
            _Keys.push_back(_Pair.first);
        }
    }
    return _Keys;
}

std::vector<iCAX::Resource::CResourceInfo> iCAX::Resource::CResourcePool::GetInfos(IN const std::string& strType_) const
{
    std::vector<CResourceInfo> _Infos;

    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    for (const auto& _Pair : m_mapResources)
    {
        if (strType_.empty() || _Pair.first.Type == strType_)
        {
            _Infos.push_back(_Pair.second.Info);
        }
    }
    return _Infos;
}

std::string iCAX::Resource::CResourcePool::GetRuntimeTypeName(IN const CResourceKey& Key_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    if (_Ite == m_mapResources.end())
    {
        return std::string();
    }
    return _Ite->second.RuntimeType.name();
}

void iCAX::Resource::CResourcePool::ValidateKey(IN const CResourceKey& Key_)
{
    if (!Key_.IsValid())
    {
        throw std::invalid_argument("Resource key must contain non-empty type and non-nil id");
    }
}

iCAX::Resource::CResourceInfo iCAX::Resource::CResourcePool::NormalizeInfo(IN const CResourceKey& Key_, IN const CResourceInfo& Info_)
{
    auto _Info = Info_;
    _Info.Key = Key_;
    return _Info;
}
