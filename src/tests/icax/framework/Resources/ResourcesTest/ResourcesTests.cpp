#include <gtest/gtest.h>

#include <Resources/ResourcePoolAccess.h>
#include <Resources/Resources.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <typeindex>
#include <utility>
#include <vector>

using namespace iCAX::Resource;

namespace ResourcesTestTypes
{
    struct MeshResource
    {
        int Version = 0;
    };

    struct GpuMeshResource
    {
        int Handle = 0;
    };

    struct TextResource
    {
        std::string Text;
    };

    struct MacroTextResource
    {
        std::string Text;
    };

    struct ImageResource
    {
        int Width = 0;
        int Height = 0;
    };

    struct ModelResource
    {
    };
}

using namespace ResourcesTestTypes;

namespace
{
    class MemoryTextLoader final : public IResourceLoader
    {
    public:
        int nLoadCount = 0;

        bool CanLoad(IN const CResourceLoadContext& Context_) const override
        {
            return Context_.Source.rfind("memory://", 0) == 0;
        }

        CResourceLoadResult Load(IN const CResourceLoadContext& Context_) override
        {
            ++nLoadCount;
            auto _pText = std::make_shared<TextResource>();
            _pText->Text = Context_.Source.substr(std::string("memory://").size());

            auto _Info = Context_.Info;
            _Info.Key = Context_.TargetKey;
            _Info.Source = Context_.Source;
            return CResourceLoadResult::Succeeded(_Info, _pText);
        }
    };

    class ExternalModelLoader final : public IResourceLoader
    {
    public:
        bool CanLoad(IN const CResourceLoadContext& Context_) const override
        {
            return Context_.TargetResourceType == std::type_index(typeid(ModelResource)) && !Context_.Source.empty();
        }

        CResourceLoadResult Load(IN const CResourceLoadContext& Context_) override
        {
            auto _Info = Context_.Info;
            _Info.Key = Context_.TargetKey;
            _Info.Source = Context_.Source;
            _Info.Persistence = EResourcePersistenceMode::External;
            return CResourceLoadResult::Succeeded(_Info);
        }
    };

    class MacroTextLoader final : public IResourceLoader
    {
    public:
        bool CanLoad(IN const CResourceLoadContext& Context_) const override
        {
            return Context_.Source.rfind("macro://", 0) == 0;
        }

        CResourceLoadResult Load(IN const CResourceLoadContext& Context_) override
        {
            auto _pText = std::make_shared<MacroTextResource>();
            _pText->Text = Context_.Source.substr(std::string("macro://").size());

            auto _Info = Context_.Info;
            _Info.Key = Context_.TargetKey;
            _Info.Source = Context_.Source;
            return CResourceLoadResult::Succeeded(_Info, _pText);
        }
    };

    std::shared_ptr<MemoryTextLoader> GetMemoryTextLoader()
    {
        static auto _pLoader = std::make_shared<MemoryTextLoader>();
        static const bool _Registered = CResourceLoaderRegistry::Register(typeid(TextResource), _pLoader);
        (void)_Registered;
        return _pLoader;
    }

    std::shared_ptr<ExternalModelLoader> GetExternalModelLoader()
    {
        static auto _pLoader = std::make_shared<ExternalModelLoader>();
        static const bool _Registered = CResourceLoaderRegistry::Register(typeid(ModelResource), _pLoader);
        (void)_Registered;
        return _pLoader;
    }

    std::shared_ptr<CResourceLoaderRegistry> MakeMemoryTextRegistry()
    {
        auto _pRegistry = std::make_shared<CResourceLoaderRegistry>();
        _pRegistry->RegisterLoader(typeid(TextResource), GetMemoryTextLoader());
        return _pRegistry;
    }
}

ICAX_REGISTER_RESOURCE_LOADER(MacroTextResource, MacroTextLoader)

TEST(ResourceKeyTest, KeyRequiresNonEmptySource)
{
    EXPECT_FALSE(CResourceKey{}.IsValid());
    EXPECT_FALSE((CResourceKey{ "" }).IsValid());
    EXPECT_TRUE((CResourceKey{ "memory://source" }).IsValid());
}

TEST(ResourceKeyTest, KeyComparisonUsesSourceOnly)
{
    CResourceKey _MeshKey{ "memory://mesh" };
    CResourceKey _SameMeshKey{ "memory://mesh" };
    CResourceKey _OtherKey{ "memory://other" };

    EXPECT_EQ(_MeshKey, _SameMeshKey);
    EXPECT_NE(_MeshKey, _OtherKey);
    EXPECT_EQ("memory://mesh", ToString(_MeshKey));
}

TEST(ResourcePoolTest, SameSourceIsSingleResourceIdentity)
{
    CResourcePool _Pool;

    auto _pMesh = std::make_shared<MeshResource>();
    _pMesh->Version = 3;
    auto _pGpuMesh = std::make_shared<GpuMeshResource>();
    _pGpuMesh->Handle = 42;

    _Pool.Set<MeshResource>("memory://mesh", _pMesh);
    _Pool.Set<GpuMeshResource>("memory://mesh", _pGpuMesh);

    EXPECT_EQ(1u, _Pool.Count());
    EXPECT_EQ(nullptr, _Pool.Get<MeshResource>("memory://mesh"));
    ASSERT_NE(nullptr, _Pool.Get<GpuMeshResource>("memory://mesh"));
    EXPECT_EQ(42, _Pool.Get<GpuMeshResource>("memory://mesh")->Handle);
}

TEST(ResourcePoolTest, RuntimeTypeMismatchReturnsNull)
{
    CResourcePool _Pool;

    auto _pText = std::make_shared<TextResource>();
    _pText->Text = "hello";
    _Pool.Set<TextResource>("memory://text", _pText);

    EXPECT_NE(nullptr, _Pool.Get<TextResource>("memory://text"));
    EXPECT_EQ(nullptr, _Pool.Get<ImageResource>("memory://text"));
}

TEST(ResourcePoolTest, SetReplacesExistingResourceAndTryAddDoesNot)
{
    CResourcePool _Pool;

    auto _pFirst = std::make_shared<TextResource>();
    _pFirst->Text = "first";
    auto _pSecond = std::make_shared<TextResource>();
    _pSecond->Text = "second";

    EXPECT_TRUE(_Pool.TryAdd<TextResource>("memory://replace", _pFirst));
    EXPECT_FALSE(_Pool.TryAdd<TextResource>("memory://replace", _pSecond));
    EXPECT_EQ("first", _Pool.Get<TextResource>("memory://replace")->Text);

    _Pool.Set<TextResource>("memory://replace", _pSecond);
    EXPECT_EQ("second", _Pool.Get<TextResource>("memory://replace")->Text);
}

TEST(ResourcePoolTest, InfoCanBeStoredAndUpdated)
{
    CResourcePool _Pool;
    const CResourceKey _Key{ "memory://readme" };

    CResourceInfo _Info;
    _Info.Name = "readme";
    _Info.ContentHash = "sha256:hello";
    _Info.nSize = 5;
    _Info.Persistence = EResourcePersistenceMode::Embedded;
    _Info.Metadata["encoding"] = "utf-8";

    auto _pText = std::make_shared<TextResource>();
    _pText->Text = "hello";
    _Pool.Set<TextResource>(_Key.Source, _pText, _Info);

    auto _StoredInfo = _Pool.GetInfo(_Key);
    ASSERT_TRUE(_StoredInfo.has_value());
    EXPECT_EQ(_Key, _StoredInfo->Key);
    EXPECT_EQ("memory://readme", _StoredInfo->Source);
    EXPECT_EQ("readme", _StoredInfo->Name);
    EXPECT_EQ("sha256:hello", _StoredInfo->ContentHash);
    EXPECT_TRUE(_StoredInfo->IsEmbedded());
    EXPECT_TRUE(_StoredInfo->IsPersistent());
    EXPECT_EQ("utf-8", _StoredInfo->Metadata["encoding"]);

    CResourceInfo _Updated;
    _Updated.Name = "updated";
    EXPECT_TRUE(_Pool.UpdateInfo(_Key, _Updated));

    _StoredInfo = _Pool.GetInfo(_Key);
    ASSERT_TRUE(_StoredInfo.has_value());
    EXPECT_EQ("updated", _StoredInfo->Name);
    EXPECT_EQ(_Key, _StoredInfo->Key);
    EXPECT_EQ("memory://readme", _StoredInfo->Source);
    EXPECT_TRUE(_StoredInfo->IsRuntimeOnly());
}

TEST(ResourcePoolTest, RegisterCanCreateMetadataEntryWithoutLoadedObject)
{
    CResourcePool _Pool;
    const CResourceKey _Key{ "file://assets/robot.fbx" };

    CResourceInfo _Info;
    _Info.Key = _Key;
    _Info.Name = "RobotArm";
    _Info.Persistence = EResourcePersistenceMode::External;

    _Pool.Register(_Info);

    EXPECT_TRUE(_Pool.Contains(_Key));
    EXPECT_FALSE(_Pool.HasObject(_Key));
    EXPECT_EQ(nullptr, _Pool.GetUntyped(_Key));
    EXPECT_EQ("", _Pool.GetRuntimeTypeName(_Key));

    const auto _Manifest = _Pool.GetManifest();
    ASSERT_EQ(1u, _Manifest.size());
    EXPECT_EQ(_Key, _Manifest[0].Key);
    EXPECT_EQ(_Key.Source, _Manifest[0].Source);
    EXPECT_TRUE(_Manifest[0].IsExternal());
}

TEST(ResourcePoolTest, UnloadKeepsMetadataAndManifest)
{
    CResourcePool _Pool;
    const CResourceKey _Key{ "memory://preview" };

    CResourceInfo _Info;
    _Info.Name = "Preview";
    _Info.Persistence = EResourcePersistenceMode::Embedded;

    _Pool.Set<ImageResource>(_Key.Source, std::make_shared<ImageResource>(), _Info);

    EXPECT_TRUE(_Pool.HasObject(_Key));
    EXPECT_NE(nullptr, _Pool.Get<ImageResource>(_Key.Source));
    EXPECT_EQ(1u, _Pool.GetManifest().size());

    EXPECT_TRUE(_Pool.Unload(_Key));
    EXPECT_TRUE(_Pool.Contains(_Key));
    EXPECT_FALSE(_Pool.HasObject(_Key));
    EXPECT_EQ(nullptr, _Pool.Get<ImageResource>(_Key.Source));

    const auto _StoredInfo = _Pool.GetInfo(_Key);
    ASSERT_TRUE(_StoredInfo.has_value());
    EXPECT_EQ("Preview", _StoredInfo->Name);
    EXPECT_TRUE(_StoredInfo->IsEmbedded());
    EXPECT_EQ(1u, _Pool.GetManifest().size());
}

TEST(ResourcePoolTest, ManifestSkipsRuntimeOnlyEntriesByDefault)
{
    CResourcePool _Pool;

    CResourceInfo _EmbeddedInfo;
    _EmbeddedInfo.Persistence = EResourcePersistenceMode::Embedded;
    _Pool.Set<TextResource>("memory://embedded-text", std::make_shared<TextResource>(), _EmbeddedInfo);

    CResourceInfo _ExternalInfo;
    _ExternalInfo.Persistence = EResourcePersistenceMode::External;
    _Pool.Set<ImageResource>("file://external-image.png", std::make_shared<ImageResource>(), _ExternalInfo);

    _Pool.Set<TextResource>("memory://runtime-text", std::make_shared<TextResource>());

    EXPECT_EQ(2u, _Pool.GetManifest().size());
    EXPECT_EQ(3u, _Pool.GetManifest(true).size());
    EXPECT_EQ(3u, _Pool.GetInfos().size());
}

TEST(ResourcePoolTest, RemoveClearAndListWork)
{
    CResourcePool _Pool;

    _Pool.Set<TextResource>("memory://text-1", std::make_shared<TextResource>());
    _Pool.Set<TextResource>("memory://text-2", std::make_shared<TextResource>());
    _Pool.Set<ImageResource>("memory://image-1", std::make_shared<ImageResource>());

    EXPECT_EQ(3u, _Pool.GetKeys().size());
    EXPECT_EQ(3u, _Pool.GetInfos().size());

    EXPECT_TRUE(_Pool.Remove(CResourceKey{ "memory://text-1" }));
    EXPECT_FALSE(_Pool.Remove(CResourceKey{ "memory://text-1" }));
    EXPECT_EQ(2u, _Pool.Count());

    _Pool.Clear();
    EXPECT_EQ(0u, _Pool.Count());
}

TEST(ResourcePoolTest, RejectsInvalidKeysAndNullResources)
{
    CResourcePool _Pool;

    EXPECT_THROW(_Pool.Set<TextResource>("", std::make_shared<TextResource>()), std::invalid_argument);
    EXPECT_THROW(_Pool.Set<TextResource>("memory://null-set", nullptr), std::invalid_argument);
    EXPECT_THROW(_Pool.TryAdd<TextResource>("memory://null-add", nullptr), std::invalid_argument);
}

TEST(ResourcePoolTest, SupportsConcurrentSetAndGet)
{
    constexpr int _ThreadCount = 4;
    constexpr int _ItemsPerThread = 64;

    CResourcePool _Pool;
    std::atomic<int> _ReadyWriters = 0;
    std::vector<std::thread> _Writers;

    for (int _ThreadIndex = 0; _ThreadIndex < _ThreadCount; ++_ThreadIndex)
    {
        _Writers.emplace_back([&, _ThreadIndex]() {
            for (int _Index = 0; _Index < _ItemsPerThread; ++_Index)
            {
                const auto _Value = _ThreadIndex * _ItemsPerThread + _Index + 1;
                auto _pText = std::make_shared<TextResource>();
                _pText->Text = std::to_string(_Value);
                _Pool.Set<TextResource>("memory://concurrent/" + std::to_string(_Value), _pText);
            }
            _ReadyWriters.fetch_add(1, std::memory_order_release);
        });
    }

    while (_ReadyWriters.load(std::memory_order_acquire) != _ThreadCount)
    {
        for (int _Value = 1; _Value <= _ThreadCount * _ItemsPerThread; ++_Value)
        {
            auto _pText = _Pool.Get<TextResource>("memory://concurrent/" + std::to_string(_Value));
            if (_pText)
            {
                EXPECT_FALSE(_pText->Text.empty());
            }
        }
        std::this_thread::yield();
    }

    for (auto& _Writer : _Writers)
    {
        _Writer.join();
    }

    EXPECT_EQ(static_cast<size_t>(_ThreadCount * _ItemsPerThread), _Pool.Count());
}

TEST(ResourceLoaderRegistryTest, RegisterFindAndLoadReturnsObject)
{
    auto _pLoader = GetMemoryTextLoader();
    _pLoader->nLoadCount = 0;

    EXPECT_FALSE(CResourceLoaderRegistry::Register(typeid(TextResource), _pLoader));
    EXPECT_EQ(1u, CResourceLoaderRegistry::GetLoaders(typeid(TextResource)).size());

    const CResourceKey _Key{ "memory://hello" };

    CResourceLoadContext _Context;
    _Context.TargetKey = _Key;
    _Context.TargetResourceType = std::type_index(typeid(TextResource));
    _Context.Source = _Key.Source;

    auto _Result = CResourceLoaderRegistry::Load(_Context);
    ASSERT_TRUE(_Result.IsOK());
    EXPECT_EQ(_Context.TargetKey, _Result.Info.Key);
    EXPECT_TRUE(_Result.pResource);
    ASSERT_TRUE(_Result.RuntimeType.has_value());
    EXPECT_EQ(std::type_index(typeid(TextResource)), _Result.RuntimeType.value());

    auto _pText = std::static_pointer_cast<TextResource>(_Result.pResource);
    ASSERT_NE(nullptr, _pText);
    EXPECT_EQ("hello", _pText->Text);
}

TEST(ResourceLoaderRegistryTest, MetadataOnlyLoadReturnsInfoWithoutPoolSideEffect)
{
    auto _pLoader = GetExternalModelLoader();
    ASSERT_NE(nullptr, _pLoader);

    const CResourceKey _Key{ "D:/assets/robot.fbx" };

    CResourceLoadContext _Context;
    _Context.TargetKey = _Key;
    _Context.TargetResourceType = std::type_index(typeid(ModelResource));
    _Context.Source = _Key.Source;

    auto _Result = CResourceLoaderRegistry::Load(_Context);
    ASSERT_TRUE(_Result.IsOK());
    EXPECT_FALSE(_Result.pResource);
    EXPECT_TRUE(_Result.Info.IsExternal());
    EXPECT_EQ(_Key.Source, _Result.Info.Source);
}

TEST(ResourceLoaderRegistryTest, NoLoaderReturnsNoLoaderStatus)
{
    CResourceLoadContext _Context;
    _Context.TargetKey = CResourceKey{ "unknown://image" };
    _Context.TargetResourceType = std::type_index(typeid(ImageResource));
    _Context.Source = _Context.TargetKey.Source;

    auto _Result = CResourceLoaderRegistry::Load(_Context);
    EXPECT_FALSE(_Result.IsOK());
    EXPECT_EQ(EResourceLoadStatus::NoLoader, _Result.Status);
}

TEST(ResourceLoaderRegistryTest, MacroRegistersLoaderAtStaticInitialization)
{
    auto _Loaders = CResourceLoaderRegistry::GetLoaders(typeid(MacroTextResource));
    ASSERT_EQ(1u, _Loaders.size());

    const CResourceKey _Key{ "macro://registered" };

    CResourceLoadContext _Context;
    _Context.TargetKey = _Key;
    _Context.TargetResourceType = std::type_index(typeid(MacroTextResource));
    _Context.Source = _Key.Source;

    auto _Result = CResourceLoaderRegistry::Load(_Context);
    ASSERT_TRUE(_Result.IsOK());

    auto _pText = std::static_pointer_cast<MacroTextResource>(_Result.pResource);
    ASSERT_NE(nullptr, _pText);
    EXPECT_EQ("registered", _pText->Text);
}

TEST(ResourcePoolAccessTest, PoolAccessRegistersMetadataOnlyLoadResult)
{
    auto _pLoader = GetExternalModelLoader();
    ASSERT_NE(nullptr, _pLoader);

    CResourcePool _Pool;
    const CResourceKey _Key{ "D:/assets/metadata-only.fbx" };

    auto _pModel = Load<ModelResource>(_Pool, _Key);

    EXPECT_EQ(nullptr, _pModel);
    EXPECT_TRUE(_Pool.Contains(_Key));
    EXPECT_FALSE(_Pool.HasObject(_Key));

    auto _Info = _Pool.GetInfo(_Key);
    ASSERT_TRUE(_Info.has_value());
    EXPECT_TRUE(_Info->IsExternal());
    EXPECT_EQ(_Key.Source, _Info->Source);
}

TEST(ResourcePoolAccessTest, LoadReturnsExistingObjectWithoutCallingLoader)
{
    auto _pLoader = GetMemoryTextLoader();
    _pLoader->nLoadCount = 0;

    CResourcePool _Pool;
    const CResourceKey _Key{ "memory://existing" };

    auto _pExisting = std::make_shared<TextResource>();
    _pExisting->Text = "existing";
    _Pool.Set<TextResource>(_Key.Source, _pExisting);

    auto _pLoaded = Load<TextResource>(_Pool, _Key);

    ASSERT_NE(nullptr, _pLoaded);
    EXPECT_EQ("existing", _pLoaded->Text);
    EXPECT_EQ(0, _pLoader->nLoadCount);
}

TEST(ResourcePoolAccessTest, LoadUsesRegisteredSourceWhenObjectIsMissing)
{
    auto _pLoader = GetMemoryTextLoader();
    _pLoader->nLoadCount = 0;

    CResourcePool _Pool;
    const CResourceKey _Key{ "memory://registered" };

    CResourceInfo _Info;
    _Info.Key = _Key;
    _Pool.Register(_Info);

    auto _pLoaded = Load<TextResource>(_Pool, _Key);

    ASSERT_NE(nullptr, _pLoaded);
    EXPECT_EQ("registered", _pLoaded->Text);
    EXPECT_EQ(1, _pLoader->nLoadCount);
}

TEST(ResourcePoolAccessTest, LoadUsesSourceAndReturnsLoadedObject)
{
    auto _pLoader = GetMemoryTextLoader();
    _pLoader->nLoadCount = 0;

    CResourcePool _Pool;

    auto _pLoaded = Load<TextResource>(_Pool, "memory://explicit");

    ASSERT_NE(nullptr, _pLoaded);
    EXPECT_EQ("explicit", _pLoaded->Text);
    EXPECT_EQ(1, _pLoader->nLoadCount);
    EXPECT_TRUE(_Pool.HasObject(CResourceKey{ "memory://explicit" }));
}

TEST(ResourcePoolAccessTest, LoadCanUseExplicitKeyWithStaticRegistry)
{
    auto _pLoader = GetMemoryTextLoader();
    _pLoader->nLoadCount = 0;

    CResourcePool _Pool;
    const CResourceKey _Key{ "memory://by-key" };

    auto _pLoaded = Load<TextResource>(_Pool, _Key);

    ASSERT_NE(nullptr, _pLoaded);
    EXPECT_EQ("by-key", _pLoaded->Text);
    EXPECT_EQ(1, _pLoader->nLoadCount);
}

TEST(ResourcePoolAccessTest, LoadCanUseSourcePathAsKey)
{
    auto _pLoader = GetMemoryTextLoader();
    _pLoader->nLoadCount = 0;

    CResourcePool _Pool;
    const std::string _Source = "memory://path-key";

    auto _pFirst = Load<TextResource>(_Pool, _Source);
    auto _pSecond = Load<TextResource>(_Pool, _Source);
    auto _pWrongType = Load<ImageResource>(_Pool, _Source);

    ASSERT_NE(nullptr, _pFirst);
    ASSERT_NE(nullptr, _pSecond);
    EXPECT_EQ(nullptr, _pWrongType);
    EXPECT_EQ("path-key", _pFirst->Text);
    EXPECT_EQ("path-key", _pSecond->Text);
    EXPECT_EQ(_pFirst.get(), _pSecond.get());
    EXPECT_EQ(1, _pLoader->nLoadCount);

    const auto _Key = MakeResourceKeyFromSource(_Source);
    EXPECT_TRUE(_Pool.HasObject(_Key));
    EXPECT_TRUE(_Pool.Contains(CResourceKey{ _Source }));
}

TEST(ResourceKeyTest, SourcePathKeyIsStable)
{
    const auto _KeyA = MakeResourceKeyFromSource("memory://stable");
    const auto _KeyB = MakeResourceKeyFromSource("memory://stable");
    const auto _KeyC = MakeResourceKeyFromSource("memory://other");

    EXPECT_EQ(_KeyA, _KeyB);
    EXPECT_NE(_KeyA, _KeyC);
    EXPECT_EQ("memory://stable", _KeyA.Source);
}

TEST(ResourceLibraryTest, LoadsWithoutExposingPool)
{
    CResourceLibrary _Resources(MakeMemoryTextRegistry());
    auto _pLoader = GetMemoryTextLoader();
    _pLoader->nLoadCount = 0;

    auto _pFirst = _Resources.Load<TextResource>("memory://library");
    auto _pSecond = _Resources.Get<TextResource>("memory://library");
    auto _pThird = _Resources.Load<TextResource>("memory://library");

    ASSERT_NE(nullptr, _pFirst);
    ASSERT_NE(nullptr, _pSecond);
    ASSERT_NE(nullptr, _pThird);
    EXPECT_EQ("library", _pFirst->Text);
    EXPECT_EQ(_pFirst.get(), _pSecond.get());
    EXPECT_EQ(_pFirst.get(), _pThird.get());
    EXPECT_TRUE(_Resources.Contains("memory://library"));
    EXPECT_TRUE(_Resources.HasObject("memory://library"));
    EXPECT_EQ(1, _pLoader->nLoadCount);
}

TEST(ResourceLibraryTest, ManagesMetadataAndLifetime)
{
    CResourceLibrary _Resources;

    CResourceInfo _Info;
    _Info.Name = "ProjectManifest";
    _Info.Persistence = EResourcePersistenceMode::External;

    _Resources.Register("file://asset/project.fbx", _Info);

    EXPECT_TRUE(_Resources.Contains("file://asset/project.fbx"));
    EXPECT_FALSE(_Resources.HasObject("file://asset/project.fbx"));

    const auto _StoredInfo = _Resources.GetInfo("file://asset/project.fbx");
    ASSERT_TRUE(_StoredInfo.has_value());
    EXPECT_EQ("ProjectManifest", _StoredInfo->Name);
    EXPECT_EQ("file://asset/project.fbx", _StoredInfo->Source);

    const auto _Manifest = _Resources.GetManifest();
    ASSERT_EQ(1u, _Manifest.size());
    EXPECT_TRUE(_Manifest[0].IsExternal());

    EXPECT_TRUE(_Resources.Remove("file://asset/project.fbx"));
    EXPECT_FALSE(_Resources.Contains("file://asset/project.fbx"));
}

TEST(ResourceLibraryTest, SeparateLibrariesDoNotShareResourceObjects)
{
    auto _pLoader = GetMemoryTextLoader();
    _pLoader->nLoadCount = 0;

    CResourceLibrary _ProjectAResources(MakeMemoryTextRegistry());
    CResourceLibrary _ProjectBResources(MakeMemoryTextRegistry());

    auto _pA = _ProjectAResources.Load<TextResource>("memory://shared-source");
    auto _pB = _ProjectBResources.Load<TextResource>("memory://shared-source");

    ASSERT_NE(nullptr, _pA);
    ASSERT_NE(nullptr, _pB);
    EXPECT_NE(_pA.get(), _pB.get());
    EXPECT_EQ("shared-source", _pA->Text);
    EXPECT_EQ("shared-source", _pB->Text);
    EXPECT_EQ(1u, _ProjectAResources.Count());
    EXPECT_EQ(1u, _ProjectBResources.Count());
    EXPECT_EQ(2, _pLoader->nLoadCount);
}

TEST(ResourceLibraryTest, DefaultLibraryDoesNotUseGlobalLoaders)
{
    auto _pLoader = GetMemoryTextLoader();
    _pLoader->nLoadCount = 0;

    CResourceLibrary _Resources;
    auto _pText = _Resources.Load<TextResource>("memory://default-library");

    EXPECT_EQ(nullptr, _pText);
    EXPECT_EQ(0, _pLoader->nLoadCount);
}

TEST(ResourceLibraryTest, CanMoveProjectResourceLibrary)
{
    CResourceLibrary _Original;
    auto _pText = std::make_shared<TextResource>();
    _pText->Text = "moved";
    _Original.Set<TextResource>("memory://move", _pText);

    CResourceLibrary _Moved = std::move(_Original);

    auto _pLoaded = _Moved.Get<TextResource>("memory://move");
    ASSERT_NE(nullptr, _pLoaded);
    EXPECT_EQ("moved", _pLoaded->Text);
}
