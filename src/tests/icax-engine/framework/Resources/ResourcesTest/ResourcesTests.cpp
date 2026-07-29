#include "pch.h"


#include <Resources/ResourcePoolAccess.h>
#include <Resources/ResourceFlatBuffer.h>
#include <Resources/Resources.h>

#include "../../FlatBuffers/Fixtures/Generated/TransportPayload_generated.h"

#include <array>
#include <typeindex>

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
        inline static constexpr const char* kResourceTypeName = "test.text.resource";

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
    CResourceScope MakeTestSceneScope()
    {
        return MakeSceneResourceScope(
            "icax",
            "icax.test",
            *iCAX::Data::uuid::from_string(
                "10000000-0000-4000-8000-000000000001"),
            *iCAX::Data::uuid::from_string(
                "20000000-0000-4000-8000-000000000002"));
    }

    std::string MakeTestResourceURL(
        IN const std::string& strName_)
    {
        iCAX::Data::uuid_name_generator _Generator(
            iCAX::Data::uuid_namespace_url);
        return MakeResourceURL(
            MakeTestSceneScope(),
            _Generator("icax.resources.test/" + strName_));
    }

    CFlatBufferResource MakeTransportFlatBuffer(
        IN const uint64_t nValue_,
        IN const std::string& strLabel_)
    {
        using namespace iCAX::FlatBufferFixtures;

        flatbuffers::FlatBufferBuilder _Builder;
        const std::array<uint32_t, 2> _Values{ 34, 55 };
        const auto _Root = CreateTransportPayload(
            _Builder,
            1,
            nValue_,
            _Builder.CreateString(strLabel_),
            _Builder.CreateVector(_Values.data(), _Values.size()));
        FinishTransportPayloadBuffer(_Builder, _Root);
        return MakeFlatBufferResource(_Builder);
    }

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

    class MemoryTextImporter final : public IResourceImporter
    {
    public:
        std::vector<CResourceFormatDescriptor> GetImportFormats() const override
        {
            return { { "test.text", "Test Text", { ".txt" }, { "text/plain" }, true, false } };
        }

        bool CanImport(IN const CResourceImportRequest& Request_) const override
        {
            return (Request_.FormatID.empty() || Request_.FormatID == "test.text")
                && Request_.SourcePath.rfind("import://", 0) == 0;
        }

        CResourceImportResult Import(IN CResourceLibrary& Library_, IN const CResourceImportRequest& Request_) override
        {
            if (Request_.SourcePath.empty())
            {
                return CResourceImportResult::Invalid(Request_, "source is empty");
            }

            const auto _ResourceID = Request_.TargetResourceID.empty() ? Request_.SourcePath : Request_.TargetResourceID;
            auto _pText = std::make_shared<TextResource>();
            _pText->Text = Request_.SourcePath.substr(std::string("import://").size());

            CResourceInfo _Info;
            _Info.Source = _ResourceID;
            _Info.Name = "imported text";
            _Info.Persistence = Request_.Persistence;
            _Info.nVersion = 1;
            _Info.Metadata["kind"] = "test.text";

            Library_.Set<TextResource>(_ResourceID, _pText, _Info);

            CResourceImportItem _Item;
            _Item.Role = "text";
            _Item.ResourceID = _ResourceID;
            _Item.Info = _Info;
            auto _Result = CResourceImportResult::Succeeded(_ResourceID, { _Item });
            _Result.Metadata["importer"] = "test";
            return _Result;
        }
    };

    class AlternativeTextImporter final : public IResourceImporter
    {
    public:
        std::vector<CResourceFormatDescriptor> GetImportFormats() const override
        {
            return { { "test.text", "Alternative Test Text", { ".txt" }, { "text/plain" }, true, false } };
        }

        bool CanImport(IN const CResourceImportRequest& Request_) const override
        {
            return (Request_.FormatID.empty() || Request_.FormatID == "test.text")
                && Request_.SourcePath.ends_with(".txt");
        }

        CResourceImportResult Import(IN CResourceLibrary& Library_, IN const CResourceImportRequest& Request_) override
        {
            const auto _ResourceID = Request_.TargetResourceID.empty() ? Request_.SourcePath : Request_.TargetResourceID;
            auto _pText = std::make_shared<TextResource>();
            _pText->Text = "alternative";

            CResourceInfo _Info;
            _Info.Source = _ResourceID;
            _Info.Name = "alternative text";
            _Info.Persistence = Request_.Persistence;
            _Info.nVersion = 1;
            _Info.Metadata["kind"] = "test.text.alternative";

            Library_.Set<TextResource>(_ResourceID, _pText, _Info);

            CResourceImportItem _Item;
            _Item.Role = "text";
            _Item.ResourceID = _ResourceID;
            _Item.Info = _Info;
            auto _Result = CResourceImportResult::Succeeded(_ResourceID, { _Item });
            _Result.Metadata["importer"] = "alternative";
            return _Result;
        }
    };

    class MemoryTextExporter final : public IResourceExporter
    {
    public:
        std::vector<CResourceFormatDescriptor> GetExportFormats() const override
        {
            return { { "test.text", "Test Text", { ".txt" }, { "text/plain" }, false, true } };
        }

        bool CanExport(IN const CResourceLibrary& Library_, IN const CResourceExportRequest& Request_) const override
        {
            return (Request_.FormatID.empty() || Request_.FormatID == "test.text")
                && !Request_.TargetPath.empty()
                && Library_.Get<TextResource>(Request_.ResourceID) != nullptr;
        }

        CResourceExportResult Export(IN const CResourceLibrary&, IN const CResourceExportRequest& Request_) override
        {
            return CResourceExportResult::Succeeded(Request_.TargetPath, "test.text", { Request_.ResourceID });
        }
    };

    std::shared_ptr<MemoryTextLoader> GetMemoryTextLoader()
    {
        static auto _pLoader = std::make_shared<MemoryTextLoader>();
        return _pLoader;
    }

    std::shared_ptr<ExternalModelLoader> GetExternalModelLoader()
    {
        static auto _pLoader = std::make_shared<ExternalModelLoader>();
        return _pLoader;
    }

    std::shared_ptr<CResourceLoaderRegistry> MakeMemoryTextRegistry()
    {
        auto _pRegistry = std::make_shared<CResourceLoaderRegistry>();
        _pRegistry->RegisterLoader(typeid(TextResource), GetMemoryTextLoader());
        return _pRegistry;
    }

    std::shared_ptr<CResourceLoaderRegistry> MakeExternalModelRegistry()
    {
        auto _pRegistry = std::make_shared<CResourceLoaderRegistry>();
        _pRegistry->RegisterLoader(typeid(ModelResource), GetExternalModelLoader());
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

TEST(ResourcePoolTest, ResourceVersionTracksContentChanges)
{
    CResourcePool _Pool;
    const CResourceKey _Key{ "memory://runtime-mesh" };

    auto _pText = std::make_shared<TextResource>();
    _pText->Text = "mesh-v0";
    _Pool.Set<TextResource>(_Key.Source, _pText);

    EXPECT_EQ(0u, _Pool.GetVersion(_Key));
    EXPECT_EQ(1u, _Pool.Touch(_Key));
    EXPECT_EQ(1u, _Pool.GetVersion(_Key));
    EXPECT_EQ(2u, _Pool.Touch(_Key));

    CResourceInfo _UpdatedInfo;
    _UpdatedInfo.Name = "RuntimeMesh";
    EXPECT_TRUE(_Pool.UpdateInfo(_Key, _UpdatedInfo));
    EXPECT_EQ(2u, _Pool.GetVersion(_Key));

    auto _StoredInfo = _Pool.GetInfo(_Key);
    ASSERT_TRUE(_StoredInfo.has_value());
    EXPECT_EQ(2u, _StoredInfo->nVersion);
    EXPECT_EQ("RuntimeMesh", _StoredInfo->Name);

    auto _pReplacement = std::make_shared<TextResource>();
    _pReplacement->Text = "mesh-v2-object";
    _Pool.Set<TextResource>(_Key.Source, _pReplacement);
    EXPECT_EQ(2u, _Pool.GetVersion(_Key));

    CResourceInfo _ExplicitVersion;
    _ExplicitVersion.nVersion = 7;
    _Pool.Set<TextResource>(_Key.Source, _pReplacement, _ExplicitVersion);
    EXPECT_EQ(7u, _Pool.GetVersion(_Key));
    EXPECT_EQ(0u, _Pool.Touch(CResourceKey{ "memory://missing" }));
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

TEST(ResourcePoolTest, ClearAndListWork)
{
    CResourcePool _Pool;

    _Pool.Set<TextResource>("memory://text-1", std::make_shared<TextResource>());
    _Pool.Set<TextResource>("memory://text-2", std::make_shared<TextResource>());
    _Pool.Set<ImageResource>("memory://image-1", std::make_shared<ImageResource>());

    EXPECT_EQ(3u, _Pool.GetKeys().size());
    EXPECT_EQ(3u, _Pool.GetInfos().size());

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
    CResourceLoaderRegistry _Registry;

    EXPECT_TRUE(_Registry.RegisterLoader(typeid(TextResource), _pLoader));
    EXPECT_FALSE(_Registry.RegisterLoader(typeid(TextResource), _pLoader));
    EXPECT_EQ(1u, _Registry.GetLoadersFor(typeid(TextResource)).size());

    const CResourceKey _Key{ "memory://hello" };

    CResourceLoadContext _Context;
    _Context.TargetKey = _Key;
    _Context.TargetResourceType = std::type_index(typeid(TextResource));
    _Context.Source = _Key.Source;

    auto _Result = _Registry.LoadResource(_Context);
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
    CResourceLoaderRegistry _Registry;
    EXPECT_TRUE(_Registry.RegisterLoader(typeid(ModelResource), _pLoader));

    const CResourceKey _Key{ "D:/assets/robot.fbx" };

    CResourceLoadContext _Context;
    _Context.TargetKey = _Key;
    _Context.TargetResourceType = std::type_index(typeid(ModelResource));
    _Context.Source = _Key.Source;

    auto _Result = _Registry.LoadResource(_Context);
    ASSERT_TRUE(_Result.IsOK());
    EXPECT_FALSE(_Result.pResource);
    EXPECT_TRUE(_Result.Info.IsExternal());
    EXPECT_EQ(_Key.Source, _Result.Info.Source);
}

TEST(ResourceLoaderRegistryTest, NoLoaderReturnsNoLoaderStatus)
{
    CResourceLoadContext _Context;
    _Context.TargetKey = CResourceKey{ "missing://image" };
    _Context.TargetResourceType = std::type_index(typeid(ImageResource));
    _Context.Source = _Context.TargetKey.Source;

    CResourceLoaderRegistry _Registry;
    auto _Result = _Registry.LoadResource(_Context);
    EXPECT_FALSE(_Result.IsOK());
    EXPECT_EQ(EResourceLoadStatus::NoLoader, _Result.Status);
}

TEST(ResourceLoaderRegistryTest, MacroRegistersLoaderAtStaticInitialization)
{
    CResourceLoaderRegistry _Registry;
    CResourceLoaderRegistrationCatalog::ReplayAll(_Registry);

    auto _Loaders = _Registry.GetLoadersFor(typeid(MacroTextResource));
    ASSERT_EQ(1u, _Loaders.size());

    const CResourceKey _Key{ "macro://registered" };

    CResourceLoadContext _Context;
    _Context.TargetKey = _Key;
    _Context.TargetResourceType = std::type_index(typeid(MacroTextResource));
    _Context.Source = _Key.Source;

    auto _Result = _Registry.LoadResource(_Context);
    ASSERT_TRUE(_Result.IsOK());

    auto _pText = std::static_pointer_cast<MacroTextResource>(_Result.pResource);
    ASSERT_NE(nullptr, _pText);
    EXPECT_EQ("registered", _pText->Text);
}

TEST(ResourceImportExportTest, ResourceLibraryImportUsesRegisteredImporter)
{
    auto _pRegistry = std::make_shared<CResourceLoaderRegistry>();
    ASSERT_TRUE(_pRegistry->RegisterImporter(std::make_shared<MemoryTextImporter>()));

    CResourceLibrary _Library(_pRegistry);

    CResourceImportRequest _Request;
    _Request.SourcePath = "import://hello";
    _Request.TargetResourceID = "memory://imported-text";
    _Request.Persistence = EResourcePersistenceMode::Embedded;

    auto _Result = _Library.Import(_Request);
    ASSERT_TRUE(_Result.IsOK()) << _Result.Error;
    EXPECT_EQ("memory://imported-text", _Result.PrimaryResourceID);
    ASSERT_EQ(1u, _Result.Items.size());
    EXPECT_EQ("text", _Result.Items.front().Role);

    auto _pText = _Library.Get<TextResource>("memory://imported-text");
    ASSERT_NE(nullptr, _pText);
    EXPECT_EQ("hello", _pText->Text);

    auto _Info = _Library.GetInfo("memory://imported-text");
    ASSERT_TRUE(_Info.has_value());
    EXPECT_TRUE(_Info->IsEmbedded());
    EXPECT_EQ(1u, _Library.GetImportFormats().size());
}

TEST(ResourceImportExportTest, TypedImportReturnsRequestedResourceType)
{
    auto _pRegistry = std::make_shared<CResourceLoaderRegistry>();
    ASSERT_TRUE(_pRegistry->RegisterImporter(std::make_shared<MemoryTextImporter>(), "memory", "memory.dll"));

    CResourceLibrary _Library(_pRegistry);

    CResourceImportResult _Result;
    auto _pText = _Library.Import<TextResource>(
        CResourceImportRequest{
            .SourcePath = "import://typed",
            .TargetResourceID = "memory://typed-text",
            .Persistence = EResourcePersistenceMode::Embedded
        },
        &_Result);

    ASSERT_TRUE(_Result.IsOK()) << _Result.Error;
    ASSERT_NE(nullptr, _pText);
    EXPECT_EQ("typed", _pText->Text);
}

TEST(ResourceImportExportTest, SelectionRuleChoosesConfiguredImporterProvider)
{
    auto _pRegistry = std::make_shared<CResourceLoaderRegistry>();
    ASSERT_TRUE(_pRegistry->RegisterImporter(std::make_shared<MemoryTextImporter>(), "memory", "memory.dll"));
    ASSERT_TRUE(_pRegistry->RegisterImporter(std::make_shared<AlternativeTextImporter>(), "alternative", "alternative.dll"));

    CResourceLoaderRegistry::CHandlerSelectionRule _Rule;
    _Rule.Kind = "importer";
    _Rule.ResourceTypeName = "test.text.resource";
    _Rule.Extensions = { ".txt" };
    _Rule.ProviderID = "alternative";
    _Rule.Priority = 100;
    _pRegistry->SetSelectionRules({ _Rule });

    CResourceLibrary _Library(_pRegistry);

    auto _pText = _Library.Import<TextResource>("D:/asset/sample.txt");
    ASSERT_NE(nullptr, _pText);
    EXPECT_EQ("alternative", _pText->Text);
}

TEST(ResourceImportExportTest, ImportWithoutHandlerReturnsNoHandler)
{
    CResourceLibrary _Library(std::make_shared<CResourceLoaderRegistry>());

    CResourceImportRequest _Request;
    _Request.SourcePath = "unknown://asset";

    auto _Result = _Library.Import(_Request);
    EXPECT_FALSE(_Result.IsOK());
    EXPECT_EQ(EResourceImportExportStatus::NoHandler, _Result.Status);
}

TEST(ResourceImportExportTest, ResourceLibraryExportUsesRegisteredExporter)
{
    auto _pRegistry = std::make_shared<CResourceLoaderRegistry>();
    ASSERT_TRUE(_pRegistry->RegisterExporter(std::make_shared<MemoryTextExporter>()));

    CResourceLibrary _Library(_pRegistry);
    auto _pText = std::make_shared<TextResource>();
    _pText->Text = "hello";
    _Library.Set<TextResource>("memory://exported-text", _pText);

    CResourceExportRequest _Request;
    _Request.ResourceID = "memory://exported-text";
    _Request.TargetPath = "export://hello.txt";
    _Request.FormatID = "test.text";

    auto _Result = _Library.Export(_Request);
    ASSERT_TRUE(_Result.IsOK()) << _Result.Error;
    EXPECT_EQ("export://hello.txt", _Result.TargetPath);
    ASSERT_EQ(1u, _Result.ResourceIDs.size());
    EXPECT_EQ("memory://exported-text", _Result.ResourceIDs.front());
    EXPECT_EQ(1u, _Library.GetExportFormats().size());
}

TEST(ResourcePoolAccessTest, PoolAccessRegistersMetadataOnlyLoadResult)
{
    auto _pLoader = GetExternalModelLoader();
    ASSERT_NE(nullptr, _pLoader);

    CResourcePool _Pool;
    const CResourceKey _Key{ "D:/assets/metadata-only.fbx" };
    auto _pRegistry = MakeExternalModelRegistry();

    auto _pModel = Load<ModelResource>(_Pool, *_pRegistry, _Key);

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
    auto _pRegistry = MakeMemoryTextRegistry();

    auto _pExisting = std::make_shared<TextResource>();
    _pExisting->Text = "existing";
    _Pool.Set<TextResource>(_Key.Source, _pExisting);

    auto _pLoaded = Load<TextResource>(_Pool, *_pRegistry, _Key);

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
    auto _pRegistry = MakeMemoryTextRegistry();

    CResourceInfo _Info;
    _Info.Key = _Key;
    _Pool.Register(_Info);

    auto _pLoaded = Load<TextResource>(_Pool, *_pRegistry, _Key);

    ASSERT_NE(nullptr, _pLoaded);
    EXPECT_EQ("registered", _pLoaded->Text);
    EXPECT_EQ(1, _pLoader->nLoadCount);
}

TEST(ResourcePoolAccessTest, LoadUsesSourceAndReturnsLoadedObject)
{
    auto _pLoader = GetMemoryTextLoader();
    _pLoader->nLoadCount = 0;

    CResourcePool _Pool;
    auto _pRegistry = MakeMemoryTextRegistry();

    auto _pLoaded = Load<TextResource>(_Pool, *_pRegistry, "memory://explicit");

    ASSERT_NE(nullptr, _pLoaded);
    EXPECT_EQ("explicit", _pLoaded->Text);
    EXPECT_EQ(1, _pLoader->nLoadCount);
    EXPECT_TRUE(_Pool.HasObject(CResourceKey{ "memory://explicit" }));
}

TEST(ResourcePoolAccessTest, LoadCanUseExplicitKeyWithExplicitRegistry)
{
    auto _pLoader = GetMemoryTextLoader();
    _pLoader->nLoadCount = 0;

    CResourcePool _Pool;
    const CResourceKey _Key{ "memory://by-key" };
    auto _pRegistry = MakeMemoryTextRegistry();

    auto _pLoaded = Load<TextResource>(_Pool, *_pRegistry, _Key);

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
    auto _pRegistry = MakeMemoryTextRegistry();

    auto _pFirst = Load<TextResource>(_Pool, *_pRegistry, _Source);
    auto _pSecond = Load<TextResource>(_Pool, *_pRegistry, _Source);
    auto _pWrongType = Load<ImageResource>(_Pool, *_pRegistry, _Source);

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

TEST(ResourceURLTest, BuildsAndParsesHierarchicalGUIDURLs)
{
    const auto _ProjectID =
        *iCAX::Data::uuid::from_string(
            "30000000-0000-4000-8000-000000000003");
    const auto _SceneID =
        *iCAX::Data::uuid::from_string(
            "40000000-0000-4000-8000-000000000004");
    const auto _ResourceID =
        *iCAX::Data::uuid::from_string(
            "50000000-0000-4000-8000-000000000005");

    const auto _Application =
        MakeApplicationResourceScope("icax");
    const auto _Product =
        MakeProductResourceScope("icax", "icax.cam");
    const auto _Project =
        MakeProjectResourceScope(
            "icax",
            "icax.cam",
            _ProjectID);
    const auto _Scene =
        MakeSceneResourceScope(
            "icax",
            "icax.cam",
            _ProjectID,
            _SceneID);

    EXPECT_EQ(
        "resource://icax/resources",
        MakeResourceCollectionURL(_Application));
    EXPECT_EQ(
        "resource://icax/products/icax.cam/resources",
        MakeResourceCollectionURL(_Product));
    EXPECT_EQ(
        "resource://icax/products/icax.cam/projects/"
        "30000000-0000-4000-8000-000000000003/resources",
        MakeResourceCollectionURL(_Project));

    const auto _URL = MakeResourceURL(_Scene, _ResourceID);
    EXPECT_EQ(
        "resource://icax/products/icax.cam/projects/"
        "30000000-0000-4000-8000-000000000003/scenes/"
        "40000000-0000-4000-8000-000000000004/resources/"
        "50000000-0000-4000-8000-000000000005",
        _URL);

    const auto _Parsed = ParseResourceURL(_URL);
    EXPECT_TRUE(_Parsed.IsResource());
    EXPECT_TRUE(ResourceScopeEquals(_Scene, _Parsed.Owner));
    EXPECT_EQ(_ResourceID, _Parsed.ResourceID);
    EXPECT_TRUE(
        ParseResourceURL(
            MakeResourceCollectionURL(_Scene))
        .IsCollection());
}

TEST(ResourceURLTest, RejectsAmbiguousOrNonCanonicalIdentity)
{
    EXPECT_THROW(
        ParseResourceURL(
            "resource://icax/product/project/resource"),
        std::invalid_argument);
    EXPECT_THROW(
        ParseResourceURL(
            "resource://icax/resources/"
            "50000000-0000-4000-8000-000000000005#mesh"),
        std::invalid_argument);
    EXPECT_THROW(
        ParseResourceURL(
            "resource://icax/resources/"
            "00000000-0000-0000-0000-000000000000"),
        std::invalid_argument);
    EXPECT_THROW(
        ParseResourceURL(
            "resource://icax/resources/"
            "50000000-0000-4000-8000-000000000005/"
            "extra"),
        std::invalid_argument);
    EXPECT_THROW(
        ParseResourceURL("resource://icax/resources/"),
        std::invalid_argument);
    EXPECT_THROW(
        ParseResourceURL(
            "resource://icax//resources/"
            "50000000-0000-4000-8000-000000000005"),
        std::invalid_argument);
    EXPECT_THROW(
        MakeApplicationResourceScope("icax..unsafe"),
        std::invalid_argument);
}

TEST(ResourceLibraryTest, AllocatesGUIDIdentityWithinBoundScope)
{
    CResourceLibrary _Resources;
    _Resources.SetScope(MakeTestSceneScope());

    const auto _ResourceID =
        _Resources.AllocateResourceID();
    ASSERT_FALSE(_ResourceID.is_nil());
    const auto _URL =
        _Resources.MakeResourceURL(_ResourceID);
    const auto _Parsed = ParseResourceURL(_URL);
    EXPECT_EQ(_ResourceID, _Parsed.ResourceID);
    EXPECT_TRUE(
        ResourceScopeEquals(
            _Resources.GetScope(),
            _Parsed.Owner));

    auto _pText = std::make_shared<TextResource>();
    _Resources.Set<TextResource>(_URL, _pText);
    const auto _Info = _Resources.GetInfo(_URL);
    ASSERT_TRUE(_Info.has_value());
    EXPECT_EQ(_ResourceID, _Info->ResourceID);
    EXPECT_TRUE(_Info->Source.empty());
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
    _Resources.SetScope(MakeTestSceneScope());
    const auto _URL =
        _Resources.AllocateResourceURL();

    CResourceInfo _Info;
    _Info.Name = "ProjectManifest";
    _Info.Source = "file://asset/project.fbx";
    _Info.Persistence = EResourcePersistenceMode::External;

    _Resources.Register(_URL, _Info);

    EXPECT_TRUE(_Resources.Contains(_URL));
    EXPECT_FALSE(_Resources.HasObject(_URL));

    const auto _StoredInfo = _Resources.GetInfo(_URL);
    ASSERT_TRUE(_StoredInfo.has_value());
    EXPECT_EQ("ProjectManifest", _StoredInfo->Name);
    EXPECT_EQ("file://asset/project.fbx", _StoredInfo->Source);

    const auto _Manifest = _Resources.GetManifest();
    ASSERT_EQ(1u, _Manifest.size());
    EXPECT_TRUE(_Manifest[0].IsExternal());

    EXPECT_EQ(
        204,
        _Resources.Delete(
            _URL).nStatus);
    EXPECT_FALSE(_Resources.Contains(_URL));
}

TEST(ResourceLibraryTest, ExposesResourceVersionForRuntimeGeneratedResources)
{
    CResourceLibrary _Resources;

    auto _pText = std::make_shared<TextResource>();
    _pText->Text = "generated";
    _Resources.Set<TextResource>("memory://generated/mesh", _pText);

    EXPECT_EQ(0u, _Resources.GetVersion("memory://generated/mesh"));
    const auto _Version1 = _Resources.Touch("memory://generated/mesh");
    EXPECT_EQ(1u, _Version1);
    EXPECT_EQ(_Version1, _Resources.GetVersion("memory://generated/mesh"));

    _pText->Text = "generated-updated";
    const auto _Version2 = _Resources.Touch("memory://generated/mesh");
    EXPECT_EQ(2u, _Version2);

    CResourceInfo _Info;
    _Info.Name = "Generated Mesh";
    EXPECT_TRUE(_Resources.UpdateInfo("memory://generated/mesh", _Info));
    EXPECT_EQ(_Version2, _Resources.GetVersion("memory://generated/mesh"));

    const auto _StoredInfo = _Resources.GetInfo("memory://generated/mesh");
    ASSERT_TRUE(_StoredInfo.has_value());
    EXPECT_EQ(_Version2, _StoredInfo->nVersion);
    EXPECT_EQ("Generated Mesh", _StoredInfo->Name);
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

TEST(FlatBufferResourceTest, OwnsImmutableVerifiedGoogleFlatBuffer)
{
    using namespace iCAX::FlatBufferFixtures;

    flatbuffers::FlatBufferBuilder _Builder;
    const std::array<uint32_t, 2> _Values{ 34, 55 };
    const auto _Root = CreateTransportPayload(
        _Builder,
        1,
        233,
        _Builder.CreateString("resource"),
        _Builder.CreateVector(_Values.data(), _Values.size()));
    FinishTransportPayloadBuffer(_Builder, _Root);

    auto _Resource = MakeFlatBufferResource(_Builder);
    auto _SharedResource = _Resource;
    EXPECT_FALSE(_Resource.Empty());
    EXPECT_EQ(_Builder.GetSize(), _Resource.Size());
    EXPECT_EQ(_Resource.Data(), _SharedResource.Data());

    const auto* _Payload =
        TryGetFlatBufferResourceRoot<TransportPayload>(
            _Resource,
            TransportPayloadIdentifier());
    ASSERT_NE(nullptr, _Payload);
    EXPECT_EQ(233u, _Payload->value());
    ASSERT_NE(nullptr, _Payload->label());
    EXPECT_EQ("resource", _Payload->label()->str());
    EXPECT_FALSE(VerifyFlatBufferResource<TransportPayload>(
        _Resource,
        nullptr));

    auto _TruncatedBytes = std::vector<uint8_t>(
        _Resource.Bytes().begin(),
        _Resource.Bytes().end() - 1);
    CFlatBufferResource _Truncated(std::move(_TruncatedBytes));
    EXPECT_EQ(
        nullptr,
        TryGetFlatBufferResourceRoot<TransportPayload>(
            _Truncated,
            TransportPayloadIdentifier()));
}

TEST(ResourceAccessTest, SupportsRestHeadGetPutDeleteAndOptions)
{
    using namespace iCAX::FlatBufferFixtures;

    CResourceLibrary _Library;
    _Library.SetScope(MakeTestSceneScope());
    const std::string _URL =
        MakeTestResourceURL("transport");

    CResourceRequest _Put;
    _Put.Method = EResourceMethod::Put;
    _Put.URL = _URL;
    _Put.Body = MakeTransportFlatBuffer(233, "created");
    _Put.Headers["Content-Type"] =
        "application/vnd.icax.flatbuffer";
    _Put.Headers["ICAX-Resource-Type"] =
        "test.transport";
    _Put.Headers["ICAX-Schema-Version"] = "1";
    _Put.Headers["ICAX-Min-Reader-Version"] = "1";
    _Put.Headers["ICAX-FlatBuffer-Identifier"] =
        TransportPayloadIdentifier();
    _Put.Headers["If-None-Match"] = "*";

    const auto _Created =
        _Library.Put(_URL, _Put.Body, _Put.Headers);
    EXPECT_EQ(201, _Created.nStatus);
    EXPECT_FALSE(_Created.HasBody());
    EXPECT_EQ(
        _URL,
        GetResourceHeader(_Created.Headers, "location").value_or(""));
    EXPECT_EQ(
        "\"icax-v1\"",
        GetResourceHeader(_Created.Headers, "etag").value_or(""));

    const auto _Capabilities = _Library.Options(_URL);
    EXPECT_EQ(204, _Capabilities.nStatus);
    EXPECT_EQ(
        "HEAD, GET, POST, PUT, DELETE, OPTIONS",
        GetResourceHeader(_Capabilities.Headers, "Allow").value_or(""));

    const auto _Description = _Library.Head(_URL);
    EXPECT_EQ(200, _Description.nStatus);
    EXPECT_FALSE(_Description.HasBody());
    EXPECT_EQ(
        std::to_string(_Put.Body.Size()),
        GetResourceHeader(_Description.Headers, "content-length").value_or(""));
    EXPECT_EQ(
        "test.transport",
        GetResourceHeader(_Description.Headers, "icax-resource-type").value_or(""));
    EXPECT_EQ(
        "1",
        GetResourceHeader(_Description.Headers, "icax-schema-version").value_or(""));
    EXPECT_EQ(
        TransportPayloadIdentifier(),
        GetResourceHeader(
            _Description.Headers,
            "icax-flatbuffer-identifier").value_or(""));

    CResourceHeaders _GetHeaders{
        { "Accept", "application/vnd.icax.flatbuffer" }
    };
    const auto _Fetched = _Library.Get(_URL, _GetHeaders);
    EXPECT_EQ(200, _Fetched.nStatus);
    EXPECT_EQ(_Put.Body.Size(), _Fetched.Body.Size());
    const auto* _Payload =
        TryGetFlatBufferResourceRoot<TransportPayload>(
            _Fetched.Body,
            TransportPayloadIdentifier());
    ASSERT_NE(nullptr, _Payload);
    EXPECT_EQ(233u, _Payload->value());

    _GetHeaders["If-None-Match"] = "\"icax-v1\"";
    const auto _NotModified = _Library.Get(_URL, _GetHeaders);
    EXPECT_EQ(304, _NotModified.nStatus);
    EXPECT_FALSE(_NotModified.HasBody());

    CResourceRequest _Replace = _Put;
    _Replace.Body = MakeTransportFlatBuffer(377, "replaced");
    _Replace.Headers.erase("If-None-Match");
    _Replace.Headers["If-Match"] = "\"icax-v0\"";
    EXPECT_EQ(412, _Library.Request(_Replace).nStatus);

    _Replace.Headers["If-Match"] = "\"icax-v1\"";
    const auto _Replaced = _Library.Request(_Replace);
    EXPECT_EQ(204, _Replaced.nStatus);
    EXPECT_EQ(
        "\"icax-v2\"",
        GetResourceHeader(_Replaced.Headers, "ETag").value_or(""));

    const auto _Historical = _Library.Get(_URL, 1);
    EXPECT_EQ(200, _Historical.nStatus);
    EXPECT_EQ(
        "1",
        GetResourceHeader(
            _Historical.Headers,
            "ICAX-Resource-Version").value_or(""));
    const auto* _HistoricalPayload =
        TryGetFlatBufferResourceRoot<TransportPayload>(
            _Historical.Body,
            TransportPayloadIdentifier());
    ASSERT_NE(nullptr, _HistoricalPayload);
    EXPECT_EQ(233u, _HistoricalPayload->value());

    CResourceHeaders _DeleteHeaders{
        { "If-Match", "\"icax-v1\"" }
    };
    EXPECT_EQ(412, _Library.Delete(_URL, _DeleteHeaders).nStatus);

    _DeleteHeaders["If-Match"] = "\"icax-v2\"";
    EXPECT_EQ(204, _Library.Delete(_URL, _DeleteHeaders).nStatus);
    EXPECT_EQ(404, _Library.Get(_URL).nStatus);

    const auto _DeletedCurrentVersion = _Library.Get(_URL, 2);
    EXPECT_EQ(200, _DeletedCurrentVersion.nStatus);
    const auto* _DeletedCurrentPayload =
        TryGetFlatBufferResourceRoot<TransportPayload>(
            _DeletedCurrentVersion.Body,
            TransportPayloadIdentifier());
    ASSERT_NE(nullptr, _DeletedCurrentPayload);
    EXPECT_EQ(377u, _DeletedCurrentPayload->value());
    EXPECT_EQ(
        (std::vector<uint64_t>{ 1, 2 }),
        _Library.GetVersions(_URL));

    const auto _Recreated =
        _Library.Request(_Put);
    EXPECT_EQ(201, _Recreated.nStatus);
    EXPECT_EQ(
        "\"icax-v3\"",
        GetResourceHeader(
            _Recreated.Headers,
            "ETag").value_or(""));
    EXPECT_EQ(
        (std::vector<uint64_t>{ 1, 2, 3 }),
        _Library.GetVersions(_URL));
}

TEST(ResourceAccessTest, RejectsInvalidOrUnrepresentableResources)
{
    CResourceLibrary _Library;
    _Library.SetScope(MakeTestSceneScope());

    CResourceRequest _InvalidPut;
    _InvalidPut.Method = EResourceMethod::Put;
    _InvalidPut.URL = MakeTestResourceURL("invalid");
    _InvalidPut.Body = CFlatBufferResource({ 1, 2, 3, 4 });
    EXPECT_EQ(400, _Library.Request(_InvalidPut).nStatus);

    CResourceRequest _AmbiguousMediaPut;
    _AmbiguousMediaPut.Method = EResourceMethod::Put;
    _AmbiguousMediaPut.URL =
        MakeTestResourceURL("ambiguous-media");
    _AmbiguousMediaPut.Body =
        MakeTransportFlatBuffer(1, "media");
    _AmbiguousMediaPut.Headers["Content-Type"] =
        "application/octet-stream";
    EXPECT_EQ(415, _Library.Request(_AmbiguousMediaPut).nStatus);

    CResourceRequest _WrongIdentifierPut;
    _WrongIdentifierPut.Method = EResourceMethod::Put;
    _WrongIdentifierPut.URL =
        MakeTestResourceURL("wrong-identifier");
    _WrongIdentifierPut.Body =
        MakeTransportFlatBuffer(1, "identifier");
    _WrongIdentifierPut.Headers["ICAX-FlatBuffer-Identifier"] = "NOPE";
    EXPECT_EQ(422, _Library.Request(_WrongIdentifierPut).nStatus);

    _Library.Set<TextResource>(
        MakeTestResourceURL("legacy"),
        std::make_shared<TextResource>());
    CResourceRequest _LegacyGet;
    _LegacyGet.Method = EResourceMethod::Get;
    _LegacyGet.URL = MakeTestResourceURL("legacy");
    EXPECT_EQ(406, _Library.Request(_LegacyGet).nStatus);

    CResourceRequest _MissingURL;
    _MissingURL.Method = EResourceMethod::Get;
    EXPECT_EQ(400, _Library.Request(_MissingURL).nStatus);

    const auto _OtherScopeURL = MakeResourceURL(
        MakeApplicationResourceScope("other-app"),
        GenerateResourceID());
    EXPECT_EQ(404, _Library.Get(_OtherScopeURL).nStatus);
}

TEST(ResourceAccessTest, HeadUsesManifestWithoutLoadingTheBody)
{
    CResourceLibrary _Library;
    _Library.SetScope(MakeTestSceneScope());
    const std::string _URL =
        MakeTestResourceURL("manifest-only");

    CResourceInfo _Info;
    _Info.MediaType = "application/vnd.example.mesh+flatbuffer; version=3";
    _Info.ResourceTypeID = "example.mesh";
    _Info.FlatBufferIdentifier = "MESH";
    _Info.nSchemaVersion = 3;
    _Info.nMinimumReaderVersion = 2;
    _Info.nSize = 4096;
    _Library.Register(_URL, _Info);

    const auto _Description = _Library.Head(
        _URL,
        { { "Accept", "application/vnd.example.mesh+flatbuffer" } });
    EXPECT_EQ(200, _Description.nStatus);
    EXPECT_FALSE(_Description.HasBody());
    EXPECT_EQ(
        "4096",
        GetResourceHeader(
            _Description.Headers,
            "Content-Length").value_or(""));
    EXPECT_EQ(
        "3",
        GetResourceHeader(
            _Description.Headers,
            "ICAX-Schema-Version").value_or(""));
    EXPECT_EQ(406, _Library.Get(_URL).nStatus);
}

TEST(ResourceAccessTest, PostAllocatesGUIDAndReturnsLocation)
{
    CResourceLibrary _Library;
    _Library.SetScope(MakeTestSceneScope());

    const auto _Body =
        MakeTransportFlatBuffer(610, "post");
    const auto _Created = _Library.Post(
        _Library.GetResourceCollectionURL(),
        _Body,
        {
            {
                "Content-Type",
                "application/vnd.icax.flatbuffer"
            }
        });
    EXPECT_EQ(201, _Created.nStatus);

    const auto _Location =
        GetResourceHeader(
            _Created.Headers,
            "Location");
    ASSERT_TRUE(_Location.has_value());
    const auto _Parsed =
        ParseResourceURL(*_Location);
    EXPECT_TRUE(_Parsed.IsResource());
    EXPECT_TRUE(
        ResourceScopeEquals(
            _Library.GetScope(),
            _Parsed.Owner));
    EXPECT_FALSE(_Parsed.ResourceID.is_nil());
    EXPECT_EQ(
        iCAX::Data::to_string(_Parsed.ResourceID),
        GetResourceHeader(
            _Created.Headers,
            "ICAX-Resource-ID").value_or(""));
    EXPECT_EQ(200, _Library.Get(*_Location).nStatus);
}

TEST(ResourceVersionStorageTest, ColdStoresAndReloadsExpiredVersions)
{
    using namespace iCAX::FlatBufferFixtures;

    const auto _TestRoot =
        std::filesystem::temp_directory_path() /
        ("icax-resource-version-test-" +
            std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()));
    CResourceVersionStorageOptions _Options;
    _Options.TemporaryRootDirectory = _TestRoot;

    std::filesystem::path _PoolDirectory;
    {
        CResourcePool _Pool(_Options);
        _PoolDirectory =
            _Pool.GetVersionStorageDirectory();
        ASSERT_FALSE(_PoolDirectory.empty());
        EXPECT_TRUE(
            std::filesystem::is_directory(
                _PoolDirectory));

        const CResourceKey _Key{
            MakeTestResourceURL("cold-history")
        };
        CResourceInfo _Info;
        _Info.nSize = 128;

        auto _pVersion1 =
            std::make_shared<CFlatBufferResource>(
                MakeTransportFlatBuffer(101, "version-1"));
        CResourceInfo _StoredVersion1;
        EXPECT_EQ(
            EResourceMutationResult::Created,
            _Pool.PutUntypedVersioned(
                _Key,
                std::static_pointer_cast<void>(_pVersion1),
                typeid(CFlatBufferResource),
                _Info,
                EResourceVersionCondition::None,
                0,
                &_StoredVersion1));
        EXPECT_EQ(1u, _StoredVersion1.nVersion);

        auto _pVersion2 =
            std::make_shared<CFlatBufferResource>(
                MakeTransportFlatBuffer(202, "version-2"));
        CResourceInfo _StoredVersion2;
        EXPECT_EQ(
            EResourceMutationResult::Replaced,
            _Pool.PutUntypedVersioned(
                _Key,
                std::static_pointer_cast<void>(_pVersion2),
                typeid(CFlatBufferResource),
                _Info,
                EResourceVersionCondition::VersionMatches,
                1,
                &_StoredVersion2));
        EXPECT_EQ(2u, _StoredVersion2.nVersion);

        _pVersion1.reset();
        EXPECT_TRUE(_Pool.ContainsVersion(_Key, 1));
        EXPECT_TRUE(_Pool.IsVersionCold(_Key, 1));

        const auto _Stats =
            _Pool.GetVersionStorageStats();
        EXPECT_EQ(1u, _Stats.nArchivedVersionCount);
        EXPECT_EQ(1u, _Stats.nColdVersionCount);
        EXPECT_EQ(0u, _Stats.nResidentVersionCount);
        EXPECT_GT(_Stats.nColdBytes, 0u);

        const auto _Version1 =
            _Pool.Get<CFlatBufferResource>(
                _Key.Source,
                1);
        ASSERT_NE(nullptr, _Version1);
        const auto* _Version1Payload =
            TryGetFlatBufferResourceRoot<TransportPayload>(
                *_Version1,
                TransportPayloadIdentifier());
        ASSERT_NE(nullptr, _Version1Payload);
        EXPECT_EQ(101u, _Version1Payload->value());

        EXPECT_EQ(
            (std::vector<uint64_t>{ 1, 2 }),
            _Pool.GetVersions(_Key));

        _Pool.Clear();
        EXPECT_EQ(0u, _Pool.Count());
        EXPECT_TRUE(
            std::filesystem::is_directory(
                _PoolDirectory));
        EXPECT_TRUE(
            std::filesystem::is_empty(
                _PoolDirectory));
    }

    EXPECT_FALSE(
        std::filesystem::exists(_PoolDirectory));
    std::error_code _CleanupError;
    std::filesystem::remove_all(
        _TestRoot,
        _CleanupError);
}

TEST(ResourceVersionStorageTest, KeepsUnsupportedTypesResidentInsteadOfLosingThem)
{
    CResourcePool _Pool;
    const CResourceKey _Key{
        MakeTestResourceURL("custom-history")
    };

    CResourceInfo _Info;
    _Info.nSize = 64;
    auto _pVersion1 =
        std::make_shared<TextResource>();
    _pVersion1->Text = "version-1";
    EXPECT_EQ(
        EResourceMutationResult::Created,
        _Pool.PutUntypedVersioned(
            _Key,
            std::static_pointer_cast<void>(_pVersion1),
            typeid(TextResource),
            _Info,
            EResourceVersionCondition::None,
            0));

    auto _pVersion2 =
        std::make_shared<TextResource>();
    _pVersion2->Text = "version-2";
    EXPECT_EQ(
        EResourceMutationResult::Replaced,
        _Pool.PutUntypedVersioned(
            _Key,
            std::static_pointer_cast<void>(_pVersion2),
            typeid(TextResource),
            _Info,
            EResourceVersionCondition::VersionMatches,
            1));
    _pVersion1.reset();

    const auto _Stats =
        _Pool.GetVersionStorageStats();
    EXPECT_EQ(1u, _Stats.nArchivedVersionCount);
    EXPECT_EQ(0u, _Stats.nColdVersionCount);
    EXPECT_EQ(1u, _Stats.nResidentVersionCount);
    EXPECT_EQ(64u, _Stats.nResidentBytes);

    const auto _Restored =
        _Pool.Get<TextResource>(_Key.Source, 1);
    ASSERT_NE(nullptr, _Restored);
    EXPECT_EQ("version-1", _Restored->Text);
}

TEST(ResourceVersionStorageTest, PluginCodecMovesBusinessResourceHistoryToDisk)
{
    CResourceLibrary _Library;
    CResourceVersionCodec _TextCodec;
    _TextCodec.Serialize =
        [](const std::shared_ptr<void>& pResource_)
        -> std::optional<std::vector<uint8_t>>
        {
            const auto _pText =
                std::static_pointer_cast<TextResource>(
                    pResource_);
            return std::vector<uint8_t>(
                _pText->Text.begin(),
                _pText->Text.end());
        };
    _TextCodec.Deserialize =
        [](const std::span<const uint8_t> Bytes_)
        -> std::shared_ptr<void>
        {
            auto _pText =
                std::make_shared<TextResource>();
            _pText->Text.assign(
                Bytes_.begin(),
                Bytes_.end());
            return std::static_pointer_cast<void>(_pText);
        };
    EXPECT_TRUE(
        _Library.RegisterVersionCodec(
            typeid(TextResource),
            std::move(_TextCodec)));

    const std::string _URL =
        MakeTestResourceURL("plugin-history");
    CResourceInfo _Info;
    _Info.Key = MakeResourceKeyFromSource(_URL);
    _Info.nSize = 9;

    auto _pVersion1 =
        std::make_shared<TextResource>();
    _pVersion1->Text = "version-1";
    EXPECT_EQ(
        EResourceMutationResult::Created,
        _Library.PutVersioned<TextResource>(
            _URL,
            _pVersion1,
            _Info));

    auto _pVersion2 =
        std::make_shared<TextResource>();
    _pVersion2->Text = "version-2";
    EXPECT_EQ(
        EResourceMutationResult::Replaced,
        _Library.PutVersioned<TextResource>(
            _URL,
            _pVersion2,
            _Info,
            EResourceVersionCondition::VersionMatches,
            1));
    _pVersion1.reset();

    const auto _Stats =
        _Library.GetVersionStorageStats();
    EXPECT_EQ(1u, _Stats.nArchivedVersionCount);
    EXPECT_EQ(1u, _Stats.nColdVersionCount);
    EXPECT_EQ(0u, _Stats.nResidentVersionCount);

    const auto _Restored =
        _Library.Get<TextResource>(_URL, 1);
    ASSERT_NE(nullptr, _Restored);
    EXPECT_EQ("version-1", _Restored->Text);
}

TEST(ResourceDependencyTest, RebuildsOnlyChangedPathAndSharesUnchangedVersions)
{
    CResourceLibrary _Library;

    const std::string _Texture =
        MakeTestResourceURL("texture");
    const std::string _Material =
        MakeTestResourceURL("material");
    const std::string _UnchangedPart =
        MakeTestResourceURL("unchanged-part");
    const std::string _Assembly =
        MakeTestResourceURL("assembly");

    const auto _Put =
        [&_Library](
            const std::string& URL_,
            const std::string& Text_,
            std::vector<CResourceReference> Dependencies_,
            const uint64_t nExpectedVersion_ = 0)
        {
            auto _pResource =
                std::make_shared<TextResource>();
            _pResource->Text = Text_;
            CResourceInfo _Info;
            _Info.Dependencies =
                std::move(Dependencies_);
            CResourceInfo _Stored;
            const auto _Result =
                _Library.PutVersioned<TextResource>(
                    URL_,
                    _pResource,
                    _Info,
                    nExpectedVersion_ == 0
                        ? EResourceVersionCondition::None
                        : EResourceVersionCondition::VersionMatches,
                    nExpectedVersion_,
                    &_Stored);
            EXPECT_TRUE(
                _Result == EResourceMutationResult::Created ||
                _Result == EResourceMutationResult::Replaced);
            return _Stored;
        };

    const auto _TextureV1 =
        _Put(_Texture, "texture-v1", {});
    const auto _PartV1 =
        _Put(_UnchangedPart, "part-v1", {});
    const auto _MaterialV1 =
        _Put(
            _Material,
            "material-v1",
            { { _Texture, _TextureV1.nVersion } });
    const auto _AssemblyV1 =
        _Put(
            _Assembly,
            "assembly-v1",
            {
                { _Material, _MaterialV1.nVersion },
                { _UnchangedPart, _PartV1.nVersion }
            });

    const auto _TextureV2 =
        _Put(
            _Texture,
            "texture-v2",
            {},
            _TextureV1.nVersion);
    CResourceInfo _MaterialV2;
    ASSERT_EQ(
        EResourceMutationResult::Replaced,
        _Library.RebindDependencyVersioned(
            { _Material, _MaterialV1.nVersion },
            { _Texture, _TextureV1.nVersion },
            { _Texture, _TextureV2.nVersion },
            &_MaterialV2));
    CResourceInfo _AssemblyV2;
    ASSERT_EQ(
        EResourceMutationResult::Replaced,
        _Library.RebindDependencyVersioned(
            { _Assembly, _AssemblyV1.nVersion },
            { _Material, _MaterialV1.nVersion },
            { _Material, _MaterialV2.nVersion },
            &_AssemblyV2));
    EXPECT_EQ(
        _Library.Get<TextResource>(
            _Material,
            _MaterialV1.nVersion),
        _Library.Get<TextResource>(
            _Material,
            _MaterialV2.nVersion));
    EXPECT_EQ(
        _Library.Get<TextResource>(
            _Assembly,
            _AssemblyV1.nVersion),
        _Library.Get<TextResource>(
            _Assembly,
            _AssemblyV2.nVersion));

    // 把共享节点的 v1 转成历史版本，验证引用保护同样覆盖冷/历史记录。
    const auto _PartV2 =
        _Put(
            _UnchangedPart,
            "part-v2",
            {},
            _PartV1.nVersion);
    EXPECT_EQ(2u, _PartV2.nVersion);

    const auto _Closure =
        _Library.CollectReachable({
            { _Assembly, _AssemblyV1.nVersion },
            { _Assembly, _AssemblyV2.nVersion }
        });
    ASSERT_TRUE(_Closure.IsComplete());
    EXPECT_EQ(7u, _Closure.Resources.size());

    const auto _SharedCount =
        std::count_if(
            _Closure.Resources.begin(),
            _Closure.Resources.end(),
            [&_UnchangedPart, &_PartV1](
                const CResourceInfo& Info_)
            {
                return Info_.Key.Source ==
                    _UnchangedPart &&
                    Info_.nVersion ==
                    _PartV1.nVersion;
            });
    EXPECT_EQ(1, _SharedCount);

    const auto _AssemblyV1Info =
        _Library.GetInfo(
            _Assembly,
            _AssemblyV1.nVersion);
    const auto _AssemblyV2Info =
        _Library.GetInfo(
            _Assembly,
            _AssemblyV2.nVersion);
    ASSERT_TRUE(_AssemblyV1Info.has_value());
    ASSERT_TRUE(_AssemblyV2Info.has_value());
    EXPECT_NE(
        _AssemblyV1Info->Dependencies[0],
        _AssemblyV2Info->Dependencies[0]);
    EXPECT_EQ(
        _AssemblyV1Info->Dependencies[1],
        _AssemblyV2Info->Dependencies[1]);

    const CResourceReference _SharedPart{
        _UnchangedPart,
        _PartV1.nVersion };
    const auto _Dependents =
        _Library.GetDependents(_SharedPart);
    EXPECT_EQ(2u, _Dependents.size());
    EXPECT_TRUE(
        _Library.Contains(
            _SharedPart.URL,
            _SharedPart.nVersion));
}

TEST(ResourceDependencyTest, RejectsMissingAndMutableDependencies)
{
    CResourceLibrary _Library;
    const std::string _Child =
        MakeTestResourceURL("dependency-child");
    const std::string _OtherChild =
        MakeTestResourceURL("dependency-other-child");
    const std::string _Parent =
        MakeTestResourceURL("dependency-parent");

    auto _pChild = std::make_shared<TextResource>();
    CResourceInfo _ChildInfo;
    CResourceInfo _StoredChild;
    ASSERT_EQ(
        EResourceMutationResult::Created,
        _Library.PutVersioned<TextResource>(
            _Child,
            _pChild,
            _ChildInfo,
            EResourceVersionCondition::None,
            0,
            &_StoredChild));

    auto _pParent = std::make_shared<TextResource>();
    CResourceInfo _MissingDependencyInfo;
    _MissingDependencyInfo.Dependencies = {
        {
            MakeTestResourceURL("missing"),
            1
        }
    };
    EXPECT_THROW(
        _Library.PutVersioned<TextResource>(
            _Parent,
            _pParent,
            _MissingDependencyInfo),
        std::invalid_argument);

    CResourceInfo _PersistentParentInfo;
    _PersistentParentInfo.Persistence =
        EResourcePersistenceMode::Embedded;
    _PersistentParentInfo.Dependencies = {
        { _Child, _StoredChild.nVersion }
    };
    EXPECT_THROW(
        _Library.PutVersioned<TextResource>(
            _Parent,
            _pParent,
            _PersistentParentInfo),
        std::invalid_argument);

    CResourceInfo _ParentInfo;
    _ParentInfo.Dependencies = {
        { _Child, _StoredChild.nVersion }
    };
    CResourceInfo _StoredParent;
    ASSERT_EQ(
        EResourceMutationResult::Created,
        _Library.PutVersioned<TextResource>(
            _Parent,
            _pParent,
            _ParentInfo,
            EResourceVersionCondition::None,
            0,
            &_StoredParent));

    auto _pOtherChild =
        std::make_shared<TextResource>();
    CResourceInfo _StoredOtherChild;
    ASSERT_EQ(
        EResourceMutationResult::Created,
        _Library.PutVersioned<TextResource>(
            _OtherChild,
            _pOtherChild,
            {},
            EResourceVersionCondition::None,
            0,
            &_StoredOtherChild));

    auto _MutatedParentInfo = _StoredParent;
    _MutatedParentInfo.Dependencies = {
        {
            _OtherChild,
            _StoredOtherChild.nVersion
        }
    };
    EXPECT_THROW(
        _Library.Register(
            _Parent,
            _MutatedParentInfo),
        std::invalid_argument);

    CResourceInfo _MetadataOnlyUpdate;
    _MetadataOnlyUpdate.Name = "renamed";
    _MetadataOnlyUpdate.Dependencies = {
        {
            _OtherChild,
            _StoredOtherChild.nVersion
        }
    };
    EXPECT_THROW(
        _Library.UpdateInfo(
            _Parent,
            _MetadataOnlyUpdate),
        std::invalid_argument);
    const auto _UpdatedParent =
        _Library.GetInfo(_Parent);
    ASSERT_TRUE(_UpdatedParent.has_value());
    EXPECT_EQ(
        _StoredParent.Dependencies,
        _UpdatedParent->Dependencies);

    const auto _Incomplete =
        _Library.CollectReachable({
            {
                MakeTestResourceURL("missing-root"),
                1
            }
        });
    EXPECT_FALSE(_Incomplete.IsComplete());
    ASSERT_EQ(1u, _Incomplete.Missing.size());
    EXPECT_EQ(
        MakeTestResourceURL("missing-root"),
        _Incomplete.Missing.front().URL);
}
