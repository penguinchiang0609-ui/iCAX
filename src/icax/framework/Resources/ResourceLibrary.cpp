#include "pch.h"
#include "ResourceLibrary.h"
#include "ResourceLoaderRegistry.h"
#include "ResourcePoolAccess.h"

#include <stdexcept>

namespace
{
    iCAX::Resource::CResourceInfo MakeLibraryInfo(
        IN const std::string& strSource_,
        IN const iCAX::Resource::CResourceInfo& Info_)
    {
        auto _Info = Info_;
        _Info.Key = iCAX::Resource::MakeResourceKeyFromSource(strSource_);
        if (_Info.Source.empty())
        {
            _Info.Source = strSource_;
        }
        return _Info;
    }
}

iCAX::Resource::CResourceLibrary::CResourceLibrary()
    : m_pPool(std::make_unique<CResourcePool>())
    , m_pLoaderRegistry(nullptr)
{
}

iCAX::Resource::CResourceLibrary::CResourceLibrary(IN std::shared_ptr<CResourceLoaderRegistry> pLoaderRegistry_)
    : m_pPool(std::make_unique<CResourcePool>())
    , m_pLoaderRegistry(std::move(pLoaderRegistry_))
{
}

iCAX::Resource::CResourceLibrary::~CResourceLibrary() = default;

iCAX::Resource::CResourceLibrary::CResourceLibrary(IN CResourceLibrary&& Other_) noexcept = default;

iCAX::Resource::CResourceLibrary& iCAX::Resource::CResourceLibrary::operator=(IN CResourceLibrary&& Other_) noexcept = default;

void iCAX::Resource::CResourceLibrary::Register(IN const std::string& strSource_, IN const CResourceInfo& Info_)
{
    GetPool().Register(MakeLibraryInfo(strSource_, Info_));
}

bool iCAX::Resource::CResourceLibrary::TryRegister(IN const std::string& strSource_, IN const CResourceInfo& Info_)
{
    return GetPool().TryRegister(MakeLibraryInfo(strSource_, Info_));
}

bool iCAX::Resource::CResourceLibrary::Contains(IN const std::string& strSource_) const
{
    return GetPool().Contains(MakeResourceKeyFromSource(strSource_));
}

bool iCAX::Resource::CResourceLibrary::HasObject(IN const std::string& strSource_) const
{
    return GetPool().HasObject(MakeResourceKeyFromSource(strSource_));
}

bool iCAX::Resource::CResourceLibrary::Unload(IN const std::string& strSource_)
{
    return GetPool().Unload(MakeResourceKeyFromSource(strSource_));
}

bool iCAX::Resource::CResourceLibrary::Remove(IN const std::string& strSource_)
{
    return GetPool().Remove(MakeResourceKeyFromSource(strSource_));
}

void iCAX::Resource::CResourceLibrary::Clear()
{
    GetPool().Clear();
}

size_t iCAX::Resource::CResourceLibrary::Count() const
{
    return GetPool().Count();
}

std::optional<iCAX::Resource::CResourceInfo> iCAX::Resource::CResourceLibrary::GetInfo(IN const std::string& strSource_) const
{
    return GetPool().GetInfo(MakeResourceKeyFromSource(strSource_));
}

bool iCAX::Resource::CResourceLibrary::UpdateInfo(IN const std::string& strSource_, IN const CResourceInfo& Info_)
{
    return GetPool().UpdateInfo(MakeResourceKeyFromSource(strSource_), Info_);
}

std::vector<iCAX::Resource::CResourceKey> iCAX::Resource::CResourceLibrary::GetKeys() const
{
    return GetPool().GetKeys();
}

std::vector<iCAX::Resource::CResourceInfo> iCAX::Resource::CResourceLibrary::GetInfos() const
{
    return GetPool().GetInfos();
}

std::vector<iCAX::Resource::CResourceInfo> iCAX::Resource::CResourceLibrary::GetManifest(IN bool bIncludeRuntimeOnly_) const
{
    return GetPool().GetManifest(bIncludeRuntimeOnly_);
}

iCAX::Resource::CResourcePool& iCAX::Resource::CResourceLibrary::GetPool()
{
    if (!m_pPool)
    {
        m_pPool = std::make_unique<CResourcePool>();
    }
    return *m_pPool;
}

const iCAX::Resource::CResourcePool& iCAX::Resource::CResourceLibrary::GetPool() const
{
    if (!m_pPool)
    {
        throw std::logic_error("Resource library has no resource pool");
    }
    return *m_pPool;
}

std::shared_ptr<void> iCAX::Resource::CResourceLibrary::LoadUntyped(
    IN const std::string& strSource_,
    IN const std::type_info& RuntimeType_,
    IN const CResourceInfo& Info_,
    IN const std::map<std::string, std::string>& Options_)
{
    if (strSource_.empty())
    {
        return nullptr;
    }

    auto& _Pool = GetPool();
    const auto _Key = MakeResourceKeyFromSource(strSource_);

    auto _pExisting = _Pool.GetUntyped(_Key, RuntimeType_);
    if (_pExisting)
    {
        return _pExisting;
    }

    if (_Pool.HasObject(_Key))
    {
        return nullptr;
    }

    auto _Context = MakeLoadContext(_Pool, _Key, std::type_index(RuntimeType_), strSource_, Info_, Options_);
    auto _Result = m_pLoaderRegistry
        ? m_pLoaderRegistry->LoadResource(_Context)
        : CResourceLoaderRegistry::Load(_Context);
    if (!_Result.IsOK())
    {
        return nullptr;
    }
    if (!StoreLoadResult(_Pool, _Result))
    {
        return nullptr;
    }

    return _Pool.GetUntyped(_Key, RuntimeType_);
}

void iCAX::Resource::CResourceLibrary::SetUntyped(
    IN const std::string& strSource_,
    IN std::shared_ptr<void> pResource_,
    IN const std::type_info& RuntimeType_,
    IN const CResourceInfo& Info_)
{
    GetPool().SetUntyped(MakeResourceKeyFromSource(strSource_), std::move(pResource_), RuntimeType_, Info_);
}

bool iCAX::Resource::CResourceLibrary::TryAddUntyped(
    IN const std::string& strSource_,
    IN std::shared_ptr<void> pResource_,
    IN const std::type_info& RuntimeType_,
    IN const CResourceInfo& Info_)
{
    return GetPool().TryAddUntyped(MakeResourceKeyFromSource(strSource_), std::move(pResource_), RuntimeType_, Info_);
}

std::shared_ptr<void> iCAX::Resource::CResourceLibrary::GetUntyped(
    IN const std::string& strSource_,
    IN const std::type_info& RuntimeType_) const
{
    return GetPool().GetUntyped(MakeResourceKeyFromSource(strSource_), RuntimeType_);
}
