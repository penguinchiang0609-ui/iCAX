#include "pch.h"
#include "ResourcePool.h"


void iCAX::Resource::CResourcePool::Register(IN const CResourceInfo& Info_)
{
    ValidateKey(Info_.Key);

    auto _Info = NormalizeInfo(Info_.Key, Info_);

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(_Info.Key);
    if (_Ite == m_mapResources.end())
    {
        CResourceRecord _Record;
        _Record.Info = std::move(_Info);
        const auto _Key = _Record.Info.Key;
        m_mapResources.emplace(_Key, std::move(_Record));
        return;
    }

    if (_Info.nVersion == 0)
    {
        _Info.nVersion = _Ite->second.Info.nVersion;
    }
    _Ite->second.Info = std::move(_Info);
}

bool iCAX::Resource::CResourcePool::TryRegister(IN const CResourceInfo& Info_)
{
    ValidateKey(Info_.Key);

    CResourceRecord _Record;
    _Record.Info = NormalizeInfo(Info_.Key, Info_);
    const auto _Key = _Record.Info.Key;

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    return m_mapResources.emplace(_Key, std::move(_Record)).second;
}

void iCAX::Resource::CResourcePool::SetUntyped(IN const CResourceKey& Key_, IN std::shared_ptr<void> pResource_, IN const std::type_info& RuntimeType_, IN const CResourceInfo& Info_)
{
    SetUntyped(Key_, std::move(pResource_), std::type_index(RuntimeType_), Info_);
}

void iCAX::Resource::CResourcePool::SetUntyped(IN const CResourceKey& Key_, IN std::shared_ptr<void> pResource_, IN std::type_index RuntimeType_, IN const CResourceInfo& Info_)
{
    ValidateKey(Key_);
    if (!pResource_)
    {
        throw std::invalid_argument("Resource pointer cannot be null");
    }

    CResourceRecord _Record;
    _Record.Info = NormalizeInfo(Key_, Info_);
    _Record.RuntimeType = RuntimeType_;
    _Record.pResource = std::move(pResource_);

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    if (_Ite != m_mapResources.end() && _Record.Info.nVersion == 0)
    {
        _Record.Info.nVersion = _Ite->second.Info.nVersion;
    }
    m_mapResources[Key_] = std::move(_Record);
}

bool iCAX::Resource::CResourcePool::TryAddUntyped(IN const CResourceKey& Key_, IN std::shared_ptr<void> pResource_, IN const std::type_info& RuntimeType_, IN const CResourceInfo& Info_)
{
    return TryAddUntyped(Key_, std::move(pResource_), std::type_index(RuntimeType_), Info_);
}

bool iCAX::Resource::CResourcePool::TryAddUntyped(IN const CResourceKey& Key_, IN std::shared_ptr<void> pResource_, IN std::type_index RuntimeType_, IN const CResourceInfo& Info_)
{
    ValidateKey(Key_);
    if (!pResource_)
    {
        throw std::invalid_argument("Resource pointer cannot be null");
    }

    CResourceRecord _Record;
    _Record.Info = NormalizeInfo(Key_, Info_);
    _Record.RuntimeType = RuntimeType_;
    _Record.pResource = std::move(pResource_);

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    return m_mapResources.emplace(Key_, std::move(_Record)).second;
}

std::shared_ptr<void> iCAX::Resource::CResourcePool::GetUntyped(IN const CResourceKey& Key_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    if (_Ite == m_mapResources.end() || !_Ite->second.pResource)
    {
        return nullptr;
    }
    return _Ite->second.pResource;
}

std::shared_ptr<void> iCAX::Resource::CResourcePool::GetUntyped(IN const CResourceKey& Key_, IN const std::type_info& ExpectedRuntimeType_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    if (_Ite == m_mapResources.end()
        || !_Ite->second.pResource
        || !_Ite->second.RuntimeType.has_value()
        || _Ite->second.RuntimeType.value() != std::type_index(ExpectedRuntimeType_))
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

bool iCAX::Resource::CResourcePool::HasObject(IN const CResourceKey& Key_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    return _Ite != m_mapResources.end() && _Ite->second.pResource != nullptr;
}

bool iCAX::Resource::CResourcePool::Unload(IN const CResourceKey& Key_)
{
    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    if (_Ite == m_mapResources.end())
    {
        return false;
    }

    _Ite->second.pResource.reset();
    _Ite->second.RuntimeType.reset();
    return true;
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

uint64_t iCAX::Resource::CResourcePool::GetVersion(IN const CResourceKey& Key_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    if (_Ite == m_mapResources.end())
    {
        return 0;
    }
    return _Ite->second.Info.nVersion;
}

uint64_t iCAX::Resource::CResourcePool::Touch(IN const CResourceKey& Key_)
{
    ValidateKey(Key_);

    std::unique_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    if (_Ite == m_mapResources.end())
    {
        return 0;
    }

    if (_Ite->second.Info.nVersion == (std::numeric_limits<uint64_t>::max)())
    {
        throw std::overflow_error("Resource version overflow");
    }
    ++_Ite->second.Info.nVersion;
    return _Ite->second.Info.nVersion;
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
    auto _Info = NormalizeInfo(Key_, Info_);
    if (_Info.nVersion == 0)
    {
        _Info.nVersion = _Ite->second.Info.nVersion;
    }
    _Ite->second.Info = std::move(_Info);
    return true;
}

std::vector<iCAX::Resource::CResourceKey> iCAX::Resource::CResourcePool::GetKeys() const
{
    std::vector<CResourceKey> _Keys;

    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    for (const auto& _Pair : m_mapResources)
    {
        _Keys.push_back(_Pair.first);
    }
    return _Keys;
}

std::vector<iCAX::Resource::CResourceInfo> iCAX::Resource::CResourcePool::GetInfos() const
{
    std::vector<CResourceInfo> _Infos;

    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    for (const auto& _Pair : m_mapResources)
    {
        _Infos.push_back(_Pair.second.Info);
    }
    return _Infos;
}

std::string iCAX::Resource::CResourcePool::GetRuntimeTypeName(IN const CResourceKey& Key_) const
{
    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    auto _Ite = m_mapResources.find(Key_);
    if (_Ite == m_mapResources.end() || !_Ite->second.RuntimeType.has_value())
    {
        return std::string();
    }
    return _Ite->second.RuntimeType->name();
}

std::vector<iCAX::Resource::CResourceInfo> iCAX::Resource::CResourcePool::GetManifest(IN bool bIncludeRuntimeOnly_) const
{
    std::vector<CResourceInfo> _Infos;

    std::shared_lock<std::shared_mutex> _Lock(m_Mutex);
    for (const auto& _Pair : m_mapResources)
    {
        if (bIncludeRuntimeOnly_ || _Pair.second.Info.IsPersistent())
        {
            _Infos.push_back(_Pair.second.Info);
        }
    }
    return _Infos;
}

void iCAX::Resource::CResourcePool::ValidateKey(IN const CResourceKey& Key_)
{
    if (!Key_.IsValid())
    {
        throw std::invalid_argument("Resource source cannot be empty");
    }
}

iCAX::Resource::CResourceInfo iCAX::Resource::CResourcePool::NormalizeInfo(IN const CResourceKey& Key_, IN const CResourceInfo& Info_)
{
    auto _Info = Info_;
    _Info.Key = Key_;
    if (_Info.Source.empty())
    {
        _Info.Source = Key_.Source;
    }
    return _Info;
}
