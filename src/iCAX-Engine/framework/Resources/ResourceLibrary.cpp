#include "pch.h"
#include "ResourceLibrary.h"
#include "ResourceLoaderRegistry.h"
#include "ResourcePoolAccess.h"


namespace
{
    iCAX::Resource::CResourceInfo MakeLibraryInfo(
        IN const std::string& strSource_,
        IN const iCAX::Resource::CResourceInfo& Info_)
    {
        auto _Info = Info_;
        _Info.Key = iCAX::Resource::MakeResourceKeyFromSource(strSource_);
        if (const auto _URL =
            iCAX::Resource::TryParseResourceURL(strSource_);
            _URL && _URL->IsResource())
        {
            _Info.ResourceID = _URL->ResourceID;
        }
        else if (_Info.Source.empty())
        {
            _Info.Source = strSource_;
        }
        return _Info;
    }
}

iCAX::Resource::CResourceLibrary::CResourceLibrary()
    : m_pPool(std::make_unique<CResourcePool>())
    , m_pLoaderRegistry(std::make_shared<CResourceLoaderRegistry>())
{
}

iCAX::Resource::CResourceLibrary::CResourceLibrary(
    IN const CResourceVersionStorageOptions&
        VersionStorageOptions_)
    : m_pPool(std::make_unique<CResourcePool>(
        VersionStorageOptions_))
    , m_pLoaderRegistry(
        std::make_shared<CResourceLoaderRegistry>())
{
}

iCAX::Resource::CResourceLibrary::CResourceLibrary(IN std::shared_ptr<CResourceLoaderRegistry> pLoaderRegistry_)
    : m_pPool(std::make_unique<CResourcePool>())
    , m_pLoaderRegistry(std::move(pLoaderRegistry_))
{
}

iCAX::Resource::CResourceLibrary::CResourceLibrary(
    IN std::shared_ptr<CResourceLoaderRegistry>
        pLoaderRegistry_,
    IN const CResourceVersionStorageOptions&
        VersionStorageOptions_)
    : m_pPool(std::make_unique<CResourcePool>(
        VersionStorageOptions_))
    , m_pLoaderRegistry(std::move(pLoaderRegistry_))
{
}

iCAX::Resource::CResourceLibrary::~CResourceLibrary() = default;

iCAX::Resource::CResourceLibrary::CResourceLibrary(IN CResourceLibrary&& Other_) noexcept = default;

iCAX::Resource::CResourceLibrary& iCAX::Resource::CResourceLibrary::operator=(IN CResourceLibrary&& Other_) noexcept = default;

void iCAX::Resource::CResourceLibrary::SetScope(
    IN const CResourceScope& Scope_)
{
    if (!Scope_.IsValid())
    {
        throw std::invalid_argument(
            "Resource library scope is invalid");
    }
    if (m_Scope &&
        ResourceScopeEquals(*m_Scope, Scope_))
    {
        return;
    }
    if (Count() != 0)
    {
        throw std::logic_error(
            "Resource library scope cannot change after resources are stored");
    }
    m_Scope = Scope_;
}

bool iCAX::Resource::CResourceLibrary::HasScope() const noexcept
{
    return m_Scope.has_value();
}

const iCAX::Resource::CResourceScope&
iCAX::Resource::CResourceLibrary::GetScope() const
{
    if (!m_Scope)
    {
        throw std::logic_error(
            "Resource library has no bound scope");
    }
    return *m_Scope;
}

iCAX::Data::uuid
iCAX::Resource::CResourceLibrary::AllocateResourceID() const
{
    (void)GetScope();
    return GenerateResourceID();
}

std::string
iCAX::Resource::CResourceLibrary::GetResourceCollectionURL() const
{
    return MakeResourceCollectionURL(GetScope());
}

std::string
iCAX::Resource::CResourceLibrary::MakeResourceURL(
    IN const iCAX::Data::uuid& ResourceID_) const
{
    return iCAX::Resource::MakeResourceURL(
        GetScope(),
        ResourceID_);
}

std::string
iCAX::Resource::CResourceLibrary::AllocateResourceURL() const
{
    return MakeResourceURL(AllocateResourceID());
}

std::string
iCAX::Resource::CResourceLibrary::MakeNamedResourceURL(
    IN const std::string& strStableName_) const
{
    if (strStableName_.empty())
    {
        throw std::invalid_argument(
            "Stable resource name cannot be empty");
    }
    iCAX::Data::uuid_name_generator _Generator(
        iCAX::Data::uuid_namespace_url);
    return MakeResourceURL(
        _Generator(
            GetResourceCollectionURL() +
            "\n" +
            strStableName_));
}

std::string
iCAX::Resource::CResourceLibrary::MakeDerivedResourceURL(
    IN const std::string& strParentResourceURL_,
    IN const std::string& strRole_) const
{
    const auto _Parent =
        ParseResourceURL(strParentResourceURL_);
    if (!_Parent.IsResource() ||
        !ResourceScopeEquals(GetScope(), _Parent.Owner))
    {
        throw std::invalid_argument(
            "Parent resource URL is outside this resource library scope");
    }
    if (strRole_.empty())
    {
        throw std::invalid_argument(
            "Derived resource role cannot be empty");
    }

    iCAX::Data::uuid_name_generator _Generator(
        iCAX::Data::uuid_namespace_url);
    return MakeResourceURL(
        _Generator(
            strParentResourceURL_ +
            "\n" +
            strRole_));
}

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

bool iCAX::Resource::CResourceLibrary::Contains(
    IN const std::string& strSource_,
    IN const uint64_t nVersion_) const
{
    return GetPool().ContainsVersion(
        MakeResourceKeyFromSource(strSource_),
        nVersion_);
}

bool iCAX::Resource::CResourceLibrary::HasObject(IN const std::string& strSource_) const
{
    return GetPool().HasObject(MakeResourceKeyFromSource(strSource_));
}

bool iCAX::Resource::CResourceLibrary::Unload(IN const std::string& strSource_)
{
    return GetPool().Unload(MakeResourceKeyFromSource(strSource_));
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

std::optional<iCAX::Resource::CResourceInfo>
iCAX::Resource::CResourceLibrary::GetInfo(
    IN const std::string& strSource_,
    IN const uint64_t nVersion_) const
{
    return GetPool().GetInfo(
        MakeResourceKeyFromSource(strSource_),
        nVersion_);
}

uint64_t iCAX::Resource::CResourceLibrary::GetVersion(IN const std::string& strSource_) const
{
    return GetPool().GetVersion(MakeResourceKeyFromSource(strSource_));
}

uint64_t iCAX::Resource::CResourceLibrary::Touch(IN const std::string& strSource_)
{
    return GetPool().Touch(MakeResourceKeyFromSource(strSource_));
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

std::vector<uint64_t>
iCAX::Resource::CResourceLibrary::GetVersions(
    IN const std::string& strSource_) const
{
    return GetPool().GetVersions(
        MakeResourceKeyFromSource(strSource_));
}

std::vector<iCAX::Resource::CResourceReference>
iCAX::Resource::CResourceLibrary::GetDependents(
    IN const CResourceReference& Target_) const
{
    return GetPool().GetDependents(Target_);
}

iCAX::Resource::CResourceReachabilityResult
iCAX::Resource::CResourceLibrary::CollectReachable(
    IN const std::vector<CResourceReference>& Roots_) const
{
    return GetPool().CollectReachable(Roots_);
}

iCAX::Resource::EResourceMutationResult
iCAX::Resource::CResourceLibrary::RebindDependencyVersioned(
    IN const CResourceReference& Parent_,
    IN const CResourceReference& OldDependency_,
    IN const CResourceReference& NewDependency_,
    OUT CResourceInfo* pStoredInfo_)
{
    return GetPool().RebindDependencyVersioned(
        Parent_,
        OldDependency_,
        NewDependency_,
        pStoredInfo_);
}

bool iCAX::Resource::CResourceLibrary::RegisterVersionCodec(
    IN const std::type_info& RuntimeType_,
    IN CResourceVersionCodec Codec_,
    IN const bool bReplaceExisting_)
{
    return GetPool().RegisterVersionCodec(
        RuntimeType_,
        std::move(Codec_),
        bReplaceExisting_);
}

iCAX::Resource::CResourceVersionStorageStats
iCAX::Resource::CResourceLibrary::GetVersionStorageStats() const
{
    return GetPool().GetVersionStorageStats();
}

std::filesystem::path
iCAX::Resource::CResourceLibrary::GetVersionStorageDirectory() const
{
    return GetPool().GetVersionStorageDirectory();
}

iCAX::Resource::CResourceResponse iCAX::Resource::CResourceLibrary::Request(
    IN const CResourceRequest& Request_)
{
    return CResourceAccessService(*this).Request(Request_);
}

iCAX::Resource::CResourceResponse iCAX::Resource::CResourceLibrary::Head(
    IN const std::string& strURL_,
    IN const CResourceHeaders& Headers_)
{
    return Request(CResourceRequest{
        EResourceMethod::Head,
        strURL_,
        Headers_,
        {}
    });
}

iCAX::Resource::CResourceResponse
iCAX::Resource::CResourceLibrary::Head(
    IN const std::string& strURL_,
    IN const uint64_t nVersion_,
    IN const CResourceHeaders& Headers_)
{
    auto _Headers = Headers_;
    SetResourceHeader(
        _Headers,
        "ICAX-Resource-Version",
        std::to_string(nVersion_));
    return Head(strURL_, _Headers);
}

iCAX::Resource::CResourceResponse iCAX::Resource::CResourceLibrary::Get(
    IN const std::string& strURL_,
    IN const CResourceHeaders& Headers_)
{
    return Request(CResourceRequest{
        EResourceMethod::Get,
        strURL_,
        Headers_,
        {}
    });
}

iCAX::Resource::CResourceResponse
iCAX::Resource::CResourceLibrary::Post(
    IN const std::string& strCollectionURL_,
    IN const CFlatBufferResource& Body_,
    IN const CResourceHeaders& Headers_)
{
    return Request(CResourceRequest{
        EResourceMethod::Post,
        strCollectionURL_,
        Headers_,
        Body_
    });
}

iCAX::Resource::CResourceResponse
iCAX::Resource::CResourceLibrary::Get(
    IN const std::string& strURL_,
    IN const uint64_t nVersion_,
    IN const CResourceHeaders& Headers_)
{
    auto _Headers = Headers_;
    SetResourceHeader(
        _Headers,
        "ICAX-Resource-Version",
        std::to_string(nVersion_));
    return Get(strURL_, _Headers);
}

iCAX::Resource::CResourceResponse iCAX::Resource::CResourceLibrary::Put(
    IN const std::string& strURL_,
    IN const CFlatBufferResource& Body_,
    IN const CResourceHeaders& Headers_)
{
    return Request(CResourceRequest{
        EResourceMethod::Put,
        strURL_,
        Headers_,
        Body_
    });
}

iCAX::Resource::CResourceResponse iCAX::Resource::CResourceLibrary::Delete(
    IN const std::string& strURL_,
    IN const CResourceHeaders& Headers_)
{
    return Request(CResourceRequest{
        EResourceMethod::Delete,
        strURL_,
        Headers_,
        {}
    });
}

iCAX::Resource::CResourceResponse iCAX::Resource::CResourceLibrary::Options(
    IN const std::string& strURL_,
    IN const CResourceHeaders& Headers_)
{
    return Request(CResourceRequest{
        EResourceMethod::Options,
        strURL_,
        Headers_,
        {}
    });
}

std::vector<iCAX::Resource::CResourceFormatDescriptor> iCAX::Resource::CResourceLibrary::GetImportFormats() const
{
    if (!m_pLoaderRegistry)
    {
        throw std::logic_error("Resource library has no loader registry");
    }
    return m_pLoaderRegistry->GetImportFormats();
}

std::vector<iCAX::Resource::CResourceFormatDescriptor> iCAX::Resource::CResourceLibrary::GetExportFormats() const
{
    if (!m_pLoaderRegistry)
    {
        throw std::logic_error("Resource library has no loader registry");
    }
    return m_pLoaderRegistry->GetExportFormats();
}

iCAX::Resource::CResourceImportResult iCAX::Resource::CResourceLibrary::Import(IN const CResourceImportRequest& Request_)
{
    if (!m_pLoaderRegistry)
    {
        throw std::logic_error("Resource library has no loader registry");
    }
    auto _Request = Request_;
    if (HasScope())
    {
        if (_Request.TargetResourceID.empty())
        {
            _Request.TargetResourceID =
                AllocateResourceURL();
        }
        else
        {
            const auto _URL =
                TryParseResourceURL(
                    _Request.TargetResourceID);
            if (!_URL ||
                !_URL->IsResource() ||
                !ResourceScopeEquals(
                    GetScope(),
                    _URL->Owner))
            {
                return CResourceImportResult::Invalid(
                    _Request,
                    "TargetResourceID is not a canonical URL in the target resource scope");
            }
        }
    }
    return m_pLoaderRegistry->ImportResource(*this, _Request);
}

iCAX::Resource::CResourceExportResult iCAX::Resource::CResourceLibrary::Export(IN const CResourceExportRequest& Request_) const
{
    if (!m_pLoaderRegistry)
    {
        throw std::logic_error("Resource library has no loader registry");
    }
    return m_pLoaderRegistry->ExportResource(*this, Request_);
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

    // 资源加载优先走项目本地缓存；typeid 不匹配时不能复用同 key 对象。
    auto _pExisting = _Pool.GetUntyped(_Key, RuntimeType_);
    if (_pExisting)
    {
        return _pExisting;
    }

    // 同 key 已加载成其他 C++ 类型时返回空，避免 static_pointer_cast 到错误类型。
    if (_Pool.HasObject(_Key))
    {
        return nullptr;
    }

    auto _Context = MakeLoadContext(_Pool, _Key, std::type_index(RuntimeType_), strSource_, Info_, Options_);
    if (!m_pLoaderRegistry)
    {
        throw std::logic_error("Resource library has no loader registry");
    }

    // LoaderRegistry 只负责找 loader 和规范化加载结果；写回资源池由 ResourceLibrary 统一完成。
    auto _Result = m_pLoaderRegistry->LoadResource(_Context);
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

iCAX::Resource::EResourceMutationResult
iCAX::Resource::CResourceLibrary::PutUntypedVersioned(
    IN const std::string& strSource_,
    IN std::shared_ptr<void> pResource_,
    IN const std::type_info& RuntimeType_,
    IN const CResourceInfo& Info_,
    IN const EResourceVersionCondition Condition_,
    IN const uint64_t nExpectedVersion_,
    OUT CResourceInfo* pStoredInfo_)
{
    return GetPool().PutUntypedVersioned(
        MakeResourceKeyFromSource(strSource_),
        std::move(pResource_),
        RuntimeType_,
        Info_,
        Condition_,
        nExpectedVersion_,
        pStoredInfo_);
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

std::shared_ptr<void>
iCAX::Resource::CResourceLibrary::GetUntyped(
    IN const std::string& strSource_,
    IN const uint64_t nVersion_,
    IN const std::type_info& RuntimeType_) const
{
    return GetPool().GetUntyped(
        MakeResourceKeyFromSource(strSource_),
        nVersion_,
        RuntimeType_);
}
