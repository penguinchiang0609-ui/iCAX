#include "pch.h"

#include <gtest/gtest.h>

#include <ProjectFile/ProjectFile.h>
#include <Database/IRepository.h>
#include <Database/IMetaRegistry.h>
#include <Resources/BinaryResource.h>
#include <Resources/ResourceLibrary.h>
#include <Data/VariantSerializer.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <limits>
#include <stdexcept>

using namespace iCAX::ProjectFile;

namespace
{
    class CStoreTestComponent final
        : public iCAX::Database::CComponentBase
    {
    public:
        inline static constexpr const char* S_ClassName =
            "CStoreTestComponent";
        inline static constexpr const char* kPersistent =
            "PersistentValue";
        inline static constexpr const char* kRuntime =
            "RuntimeValue";

        explicit CStoreTestComponent(
            std::shared_ptr<iCAX::Database::IEntity> pEntity_)
            : CComponentBase(std::move(pEntity_))
        {
        }

        std::string GetComponentClass() const override
        {
            return S_ClassName;
        }

        std::vector<std::string>
            GetPropertyNameArray() const override
        {
            return {kPersistent, kRuntime};
        }

        iCAX::Data::PropertyValue GetProperty(
            const std::string& strPropertyName_) const override
        {
            if (strPropertyName_ == kPersistent)
            {
                return m_nPersistent;
            }
            if (strPropertyName_ == kRuntime)
            {
                return m_nRuntime;
            }
            throw std::invalid_argument(
                "Unknown store test property");
        }

        void SetRaw(
            const std::string& strPropertyName_,
            const iCAX::Data::PropertyValue& Value_)
        {
            if (strPropertyName_ == kPersistent)
            {
                m_nPersistent = Value_.To<int>();
                return;
            }
            if (strPropertyName_ == kRuntime)
            {
                m_nRuntime = Value_.To<int>();
                return;
            }
            throw std::invalid_argument(
                "Unknown store test property");
        }

    protected:
        void OnSetProperty(
            const std::string& strPropertyName_,
            const iCAX::Data::PropertyValue& Value_) override
        {
            SetRaw(strPropertyName_, Value_);
        }

    private:
        int m_nPersistent = 0;
        int m_nRuntime = 0;
    };

    std::shared_ptr<iCAX::Database::IMetaRegistry>
        CreateStoreTestMetaRegistry()
    {
        using namespace iCAX::Database;
        auto _pMeta = CreateMetaRegistry();
        _pMeta->RegistType(
            CStoreTestComponent::S_ClassName,
            CComponentBase::S_ClassName);
        _pMeta->RegistCreatorByName(
            CStoreTestComponent::S_ClassName,
            [](std::shared_ptr<IEntity> pEntity_)
            {
                return std::make_shared<CStoreTestComponent>(
                    std::move(pEntity_));
            });
        const auto _Register =
            [&_pMeta](
                const char* pName_,
                EPropertyPersistence Persistence_)
            {
                _pMeta->RegistPropertyByName(
                    CStoreTestComponent::S_ClassName,
                    pName_,
                    [pName_](const void* pObject_)
                    {
                        return static_cast<
                            const CStoreTestComponent*>(pObject_)
                            ->GetProperty(pName_);
                    },
                    [pName_](
                        void* pObject_,
                        const iCAX::Data::PropertyValue& Value_)
                    {
                        static_cast<CStoreTestComponent*>(pObject_)
                            ->SetRaw(pName_, Value_);
                    },
                    Persistence_);
            };
        _Register(
            CStoreTestComponent::kPersistent,
            EPropertyPersistence::Persistent);
        _Register(
            CStoreTestComponent::kRuntime,
            EPropertyPersistence::NonPersistent);
        return _pMeta;
    }

    struct CTestPersistentResource final
    {
        uint32_t Value = 0;
    };

    iCAX::Resource::CResourceVersionCodec
        MakeTestPersistenceCodec()
    {
        iCAX::Resource::CResourceVersionCodec _Codec;
        _Codec.Serialize =
            [](const std::shared_ptr<void>& pResource_)
            -> std::optional<std::vector<uint8_t>>
            {
                const auto _pValue =
                    std::static_pointer_cast<
                        CTestPersistentResource>(pResource_);
                if (!_pValue)
                {
                    return std::nullopt;
                }
                return std::vector<uint8_t>{
                    static_cast<uint8_t>(_pValue->Value),
                    static_cast<uint8_t>(_pValue->Value >> 8),
                    static_cast<uint8_t>(_pValue->Value >> 16),
                    static_cast<uint8_t>(_pValue->Value >> 24)};
            };
        _Codec.Deserialize =
            [](std::span<const uint8_t> Bytes_)
            -> std::shared_ptr<void>
            {
                if (Bytes_.size() != 4)
                {
                    return nullptr;
                }
                auto _pValue =
                    std::make_shared<CTestPersistentResource>();
                for (uint32_t i = 0; i < 4; ++i)
                {
                    _pValue->Value |=
                        static_cast<uint32_t>(Bytes_[i]) <<
                        (i * 8);
                }
                return std::static_pointer_cast<void>(_pValue);
            };
        return _Codec;
    }

    iCAX::Data::uuid ParseTestUUID(IN const char* pText_)
    {
        return *iCAX::Data::uuid::from_string(pText_);
    }

    CProjectDocument MakeDocument(IN const uint32_t nRevision_ = 2)
    {
        const auto _EntityA = ParseTestUUID(
            "10000000-0000-4000-8000-000000000001");
        const auto _EntityB = ParseTestUUID(
            "10000000-0000-4000-8000-000000000002");
        const CProjectResourceReference _Geometry{
            "icax://project/geometry", 3};
        const CProjectResourceReference _Toolpath{
            "icax://project/toolpath", 7};
        const CProjectResourceReference _Machine{
            "icax://library/machine", 2};

        CProjectDocument _Document;
        _Document.Info.Magic = "ICAX-CAM-PROJECT";
        _Document.Info.ProductID = "icax.cam";
        _Document.Info.FormatVersion =
            nRevision_ == 1 ? "1.0" : "2.0";
        _Document.Info.nFormatRevision = nRevision_;
        _Document.Info.ProjectID = ParseTestUUID(
            "20000000-0000-4000-8000-000000000001");
        _Document.Info.MainSceneID = ParseTestUUID(
            "30000000-0000-4000-8000-000000000001");
        _Document.Info.ProjectName = "Project \"Alpha\"\nCAM";
        _Document.Info.ProjectSettings["geometry"] =
            MakeResourceReferenceValue(_Geometry);
        _Document.Info.MainSceneSettings["unit"] =
            std::string("mm");

        _Document.Entities = {{_EntityB}, {_EntityA}};
        CProjectComponentRecord _GeometryComponent;
        _GeometryComponent.EntityID = _EntityA;
        _GeometryComponent.ComponentClass = "GeometryComponent";
        _GeometryComponent.Properties["resource"] =
            MakeResourceReferenceValue(_Geometry);
        CProjectComponentRecord _CAMComponent;
        _CAMComponent.EntityID = _EntityB;
        _CAMComponent.ComponentClass = "CAMComponent";
        _CAMComponent.Properties["toolpath"] =
            MakeResourceReferenceValue(_Toolpath);
        iCAX::Data::VariantArray _Inputs;
        _Inputs.push_back(MakeResourceReferenceValue(_Geometry));
        _Inputs.push_back(std::string("stock"));
        _CAMComponent.Properties["inputs"] = _Inputs;
        _Document.Components = {
            std::move(_CAMComponent),
            std::move(_GeometryComponent)};

        CProjectResourceRecord _ToolpathResource;
        _ToolpathResource.Reference = _Toolpath;
        _ToolpathResource.ResourceTypeID = "cam.toolpath";
        _ToolpathResource.nSchemaVersion = 1;
        _ToolpathResource.MediaType = "application/x-icax-toolpath";
        _ToolpathResource.ContentHash = "sha256:toolpath";
        _ToolpathResource.Metadata["operation"] = "roughing";
        _ToolpathResource.Dependencies = {_Geometry, _Machine};
        _ToolpathResource.Body = {0, 1, 2, 3, 0xff};

        CProjectResourceRecord _GeometryResource;
        _GeometryResource.Reference = _Geometry;
        _GeometryResource.ResourceTypeID = "geometry.mesh";
        _GeometryResource.nSchemaVersion = 1;
        _GeometryResource.MediaType = "application/octet-stream";
        _GeometryResource.ContentHash = "sha256:geometry";
        _GeometryResource.Metadata["name"] = "part A";
        _GeometryResource.Body = {10, 20, 30};

        CProjectResourceRecord _MachineResource;
        _MachineResource.Reference = _Machine;
        _MachineResource.ResourceTypeID = "machine.definition";
        _MachineResource.nSchemaVersion = 4;
        _MachineResource.Persistence =
            EProjectResourcePersistence::External;
        _MachineResource.MediaType = "application/x-icax-machine";
        _MachineResource.ContentHash = "sha256:machine";
        _MachineResource.Source = "library://machines/five-axis";

        _Document.Resources = {
            std::move(_ToolpathResource),
            std::move(_MachineResource),
            std::move(_GeometryResource)};
        _Document.Canonicalize();
        return _Document;
    }

    std::vector<uint8_t> ReadBytes(
        IN const std::filesystem::path& Path_)
    {
        std::ifstream _Input(Path_, std::ios::binary | std::ios::ate);
        const auto _Size = _Input.tellg();
        std::vector<uint8_t> _Bytes(static_cast<size_t>(_Size));
        _Input.seekg(0);
        _Input.read(
            reinterpret_cast<char*>(_Bytes.data()),
            static_cast<std::streamsize>(_Bytes.size()));
        return _Bytes;
    }

}

TEST(ProjectFileCodec, ASCIIAndBinaryRoundTripSameDocument)
{
    const auto _Source = MakeDocument();
    for (const auto _Encoding : {
        EProjectFileEncoding::ASCII,
        EProjectFileEncoding::Binary})
    {
        const auto _Bytes = CProjectFileCodec::Encode(
            _Source,
            _Encoding);
        const auto _Result = CProjectFileCodec::Decode(_Bytes);
        EXPECT_EQ(_Result.nContainerVersion, kCurrentContainerVersion);
        EXPECT_EQ(_Result.Encoding, _Encoding);
        EXPECT_EQ(_Result.Document, _Source);
    }
}

TEST(ProjectFileCodec, ASCIIIsReadableAndStartsWithProductMagic)
{
    const auto _Bytes = CProjectFileCodec::Encode(
        MakeDocument(),
        EProjectFileEncoding::ASCII);
    const std::string _Text(_Bytes.begin(), _Bytes.end());
    EXPECT_EQ(_Text.find("ICAX-CAM-PROJECT\n"), 0u);
    EXPECT_NE(_Text.find("HEADER;"), std::string::npos);
    EXPECT_NE(_Text.find("COMPONENT("), std::string::npos);
    EXPECT_NE(_Text.find("RESOURCE_BODY("), std::string::npos);
}

TEST(ProjectFileCodec, RejectsTruncatedAndUnknownContainer)
{
    auto _Bytes = CProjectFileCodec::Encode(
        MakeDocument(),
        EProjectFileEncoding::Binary);
    _Bytes.pop_back();
    EXPECT_THROW(CProjectFileCodec::Decode(_Bytes), std::invalid_argument);

    auto _ASCII = CProjectFileCodec::Encode(
        MakeDocument(),
        EProjectFileEncoding::ASCII);
    std::string _Text(_ASCII.begin(), _ASCII.end());
    const auto _Position = _Text.find(
        "CONTAINER_VERSION(" +
        std::to_string(kCurrentContainerVersion) + ")");
    ASSERT_NE(_Position, std::string::npos);
    const auto _End = _Text.find(')', _Position);
    ASSERT_NE(_End, std::string::npos);
    _Text.replace(
        _Position,
        _End - _Position + 1,
        "CONTAINER_VERSION(999)");
    EXPECT_THROW(
        CProjectFileCodec::Decode(std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(_Text.data()),
            _Text.size())),
        std::invalid_argument);
}

TEST(ProjectFileCodec, RejectsObsoleteContainerAndResourceLayout)
{
    auto _Binary = CProjectFileCodec::Encode(
        MakeDocument(), EProjectFileEncoding::Binary);
    ASSERT_GE(_Binary.size(), 12u);
    _Binary[8] = 1;
    _Binary[9] = _Binary[10] = _Binary[11] = 0;
    EXPECT_THROW(CProjectFileCodec::Decode(_Binary), std::invalid_argument);

    const auto _ASCII = CProjectFileCodec::Encode(
        MakeDocument(), EProjectFileEncoding::ASCII);
    std::string _Text(_ASCII.begin(), _ASCII.end());
    const auto _Version = "CONTAINER_VERSION(" +
        std::to_string(kCurrentContainerVersion) + ")";
    const auto _VersionPosition = _Text.find(_Version);
    ASSERT_NE(_VersionPosition, std::string::npos);
    auto _Obsolete = _Text;
    _Obsolete.replace(_VersionPosition, _Version.size(), "CONTAINER_VERSION(1)");
    const auto _Decode = [](const std::string& Text_) {
        return CProjectFileCodec::Decode(std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(Text_.data()), Text_.size()));
    };
    EXPECT_THROW(_Decode(_Obsolete), std::invalid_argument);

    const auto _ResourcePosition = _Text.find("RESOURCE(");
    ASSERT_NE(_ResourcePosition, std::string::npos);
    const auto _ResourceEnd = _Text.find('\n', _ResourcePosition);
    ASSERT_NE(_ResourceEnd, std::string::npos);
    _Text.replace(_ResourcePosition, _ResourceEnd - _ResourcePosition,
        R"(RESOURCE("icax://project/geometry",3,"geometry.mesh",1,EMBEDDED,"model/mesh","abc","","{}");)");
    EXPECT_THROW(_Decode(_Text), std::invalid_argument);
}

TEST(ProjectFileStore, RejectsOtherProductFormatVersionsBeforeRestore)
{
    const CProjectFile _File({
        .Magic = "ICAX-CAM-PROJECT",
        .ProductID = "icax.cam",
        .CurrentFormatVersion = "1.0",
        .nCurrentFormatRevision = 1
    });
    const auto _Path = std::filesystem::temp_directory_path() /
        ("icax_current_format_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".icax");
    for (const auto _Encoding : {EProjectFileEncoding::ASCII, EProjectFileEncoding::Binary})
    {
        auto _Document = MakeDocument();
        _Document.Info.FormatVersion = "0.9";
        CProjectFileCodec::WriteAtomic(_Path, _Document, _Encoding);
        EXPECT_THROW((void)_File.PrepareOpen(_Path), std::invalid_argument);
        _Document.Info.FormatVersion = "1.0";
        _Document.Info.nFormatRevision = 2;
        CProjectFileCodec::WriteAtomic(_Path, _Document, _Encoding);
        EXPECT_THROW((void)_File.PrepareOpen(_Path), std::invalid_argument);
        _Document.Info.nFormatRevision = 1;
        CProjectFileCodec::WriteAtomic(_Path, _Document, _Encoding);
        EXPECT_EQ(_File.PrepareOpen(_Path).Result().Info, _Document.Info);
    }
    std::filesystem::remove(_Path);
}

TEST(ProjectFileCodec, AtomicWritePreservesExistingFileOnInvalidInput)
{
    const auto _Path = std::filesystem::temp_directory_path() /
        ("icax-project-file-test-" +
            iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()) +
            ".icax");
    const auto _Source = MakeDocument();
    CProjectFileCodec::WriteAtomic(
        _Path,
        _Source,
        EProjectFileEncoding::Binary);
    const auto _Before = ReadBytes(_Path);
    EXPECT_EQ(CProjectFileCodec::Read(_Path).Document, _Source);

    auto _Invalid = _Source;
    _Invalid.Info.ProjectID = {};
    EXPECT_THROW(
        CProjectFileCodec::WriteAtomic(
            _Path,
            _Invalid,
            EProjectFileEncoding::ASCII),
        std::invalid_argument);
    EXPECT_EQ(ReadBytes(_Path), _Before);

    std::error_code _Ignored;
    std::filesystem::remove(_Path, _Ignored);
}

TEST(ProjectFileCodec, LargeComponentPropertiesPreserveTypesAndBytes)
{
    using namespace iCAX::Data;
    auto _Source = MakeDocument();
    auto& _Properties = _Source.Components.front().Properties;
    _Properties["recipe"] = std::string(64ull * 1024ull * 1024ull, 'g');
    _Properties["typedFacts"] = ObjectMap{
        {"boolean", true},
        {"signed", static_cast<long long>(-9)},
        {"unsigned", (std::numeric_limits<unsigned long long>::max)()},
        {"real", 1.2345678901234567},
        {"uuid", ParseTestUUID("40000000-0000-4000-8000-000000000001")},
        {"escaped", std::string("typed \"recipe\"\\\n\t")},
        {"array", VariantArray{Variant{}, false, 3.5, ObjectMap{{"nested", std::string("mm")}}}}};
    ASSERT_GT(VariantSerializer::Serialize(Variant(_Properties)).size(),
        64ull * 1024ull * 1024ull);
    const auto _Bytes = CProjectFileCodec::Encode(_Source, EProjectFileEncoding::Binary);
    const auto _Decoded = CProjectFileCodec::Decode(_Bytes);
    ASSERT_EQ(_Decoded.Document, _Source);
    EXPECT_EQ(CProjectFileCodec::Encode(_Decoded.Document, EProjectFileEncoding::Binary), _Bytes);

    const auto _Path = std::filesystem::temp_directory_path() /
        ("icax-project-large-properties-" + to_string(GenerateNewUUID()) + ".icax");
    CProjectFileCodec::WriteAtomic(_Path, _Source, EProjectFileEncoding::Binary);
    EXPECT_EQ(ReadBytes(_Path), _Bytes);
    EXPECT_EQ(CProjectFileCodec::Read(_Path).Document, _Source);
    std::error_code _Ignored;
    std::filesystem::remove(_Path, _Ignored);
}

TEST(ProjectFileCodec, OrdinaryProjectNameStillRejectsMoreThan64MiB)
{
    auto _Source = MakeDocument();
    _Source.Info.ProjectName = std::string(64ull * 1024ull * 1024ull + 1, 'n');
    const auto _Bytes = CProjectFileCodec::Encode(_Source, EProjectFileEncoding::Binary);
    try
    {
        CProjectFileCodec::Decode(_Bytes);
        FAIL() << "Ordinary project names retain the 64 MiB limit";
    }
    catch (const std::invalid_argument& _Error)
    {
        EXPECT_STREQ(_Error.what(), "Binary project string is too large");
    }
}

TEST(ProjectFileCodec, ComponentPropertiesRejectForgedLengthAndTruncatedPayload)
{
    auto _Source = MakeDocument();
    constexpr std::string_view _ClassName = "ComponentPropertiesLengthSentinel";
    _Source.Components.front().ComponentClass = _ClassName;
    const auto _Original = CProjectFileCodec::Encode(_Source, EProjectFileEncoding::Binary);
    const auto _Name = std::search(_Original.begin(), _Original.end(), _ClassName.begin(), _ClassName.end());
    ASSERT_NE(_Name, _Original.end());
    const auto _NamePosition = static_cast<std::size_t>(_Name - _Original.begin());
    // Component payload: UUID, class string length/name, enabled, properties
    // string length/value. Record length precedes that payload.
    const auto _PropertiesLengthPosition = _NamePosition + _ClassName.size() + 1;
    const auto _RecordLengthPosition = _NamePosition - 8 - 16 - 8;
    const auto _ReadUInt64 = [](const auto& _Bytes, const std::size_t _Position) {
        uint64_t _Value = 0;
        for (std::size_t i = 0; i < 8; ++i) _Value |= static_cast<uint64_t>(_Bytes.at(_Position + i)) << (8 * i);
        return _Value;
    };
    const auto _WriteUInt64 = [](auto& _Bytes, const std::size_t _Position, const uint64_t _Value) {
        for (std::size_t i = 0; i < 8; ++i) _Bytes.at(_Position + i) = static_cast<uint8_t>(_Value >> (8 * i));
    };
    for (const uint64_t _Length : {
        256ull * 1024ull * 1024ull + 1,
        (std::numeric_limits<uint64_t>::max)()})
    {
        auto _Forged = _Original;
        _WriteUInt64(_Forged, _PropertiesLengthPosition, _Length);
        try
        {
            CProjectFileCodec::Decode(_Forged);
            FAIL() << "Forged properties length must be rejected";
        }
        catch (const std::invalid_argument& _Error)
        {
            EXPECT_STREQ(_Error.what(), "Binary project string is too large");
        }
    }
    auto _MissingPayload = _Original;
    _WriteUInt64(_MissingPayload, _PropertiesLengthPosition, 64ull * 1024ull * 1024ull + 1);
    EXPECT_THROW(CProjectFileCodec::Decode(_MissingPayload), std::invalid_argument);
    auto _TruncatedPayload = _Original;
    _WriteUInt64(_TruncatedPayload, _RecordLengthPosition,
        _ReadUInt64(_TruncatedPayload, _RecordLengthPosition) - 1);
    try
    {
        CProjectFileCodec::Decode(_TruncatedPayload);
        FAIL() << "A component's declared properties must fit its bounded record";
    }
    catch (const std::invalid_argument& _Error)
    {
        EXPECT_STREQ(_Error.what(), "Binary project record is truncated");
    }
}

TEST(ProjectDocumentValidation, RejectsMissingReferencesAndCycles)
{
    auto _Missing = MakeDocument();
    _Missing.Resources.erase(_Missing.Resources.begin());
    EXPECT_FALSE(ValidateProjectDocument(_Missing).empty());
    EXPECT_THROW(
        RequireValidProjectDocument(_Missing),
        std::invalid_argument);

    auto _Cycle = MakeDocument();
    auto* _pGeometry = _Cycle.FindResource(
        {"icax://project/geometry", 3});
    ASSERT_NE(_pGeometry, nullptr);
    _pGeometry->Dependencies.push_back(
        {"icax://project/toolpath", 7});
    EXPECT_THROW(
        RequireValidProjectDocument(_Cycle),
        std::invalid_argument);
}

TEST(ProjectDocumentValidation, NestedRecipeReferencesAreCheckedWithoutChangingTheDocument)
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::Variant;
    using iCAX::Data::VariantArray;
    auto document=MakeDocument();
    VariantArray branches;
    for(std::size_t index=0;index<128;++index) {
        Variant nested=ObjectMap{{"payload",std::string(4096,'x')},
            {"reference",MakeResourceReferenceValue({"icax://project/geometry",3})}};
        for(std::size_t depth=0;depth<24;++depth)
            nested=ObjectMap{{"children",VariantArray{std::move(nested)}}};
        branches.emplace_back(std::move(nested));
    }
    document.Components.front().Properties["frozenRecipe"]=std::move(branches);
    const auto original=document;
    EXPECT_TRUE(ValidateProjectDocument(document).empty());
    EXPECT_EQ(document,original);
    auto& recipes=std::get<VariantArray>(document.Components.front().Properties.at("frozenRecipe").m_Value);
    recipes.emplace_back(ObjectMap{{"children",VariantArray{
        MakeResourceReferenceValue({"icax://project/missing",9})}}});
    const auto invalid=document;
    EXPECT_FALSE(ValidateProjectDocument(document).empty());
    EXPECT_EQ(document,invalid);
    recipes.back()=ObjectMap{{"children",VariantArray{ObjectMap{
        {"__project_file_type",std::string(kResourceReferenceValueTag)},{"url",true},{"version",9ull}}}}};
    EXPECT_THROW(RequireValidProjectDocument(document),std::invalid_argument);
}

TEST(ProjectFileStore, SavesAndOpensDatabaseAndResourcesDirectly)
{
    const auto _SceneID = ParseTestUUID(
        "30000000-0000-4000-8000-000000000099");
    const auto _EntityID = ParseTestUUID(
        "10000000-0000-4000-8000-000000000099");
    const auto _ProjectID = ParseTestUUID(
        "20000000-0000-4000-8000-000000000099");
    const std::string _ResourceURL =
        "icax://project/store-test-blob";
    const std::string _CustomResourceURL =
        "icax://project/store-test-custom";

    auto _pSourceDatabase = iCAX::Database::GenerateRepository(
        _SceneID,
        CreateStoreTestMetaRegistry());
    const auto _pSourceEntity =
        _pSourceDatabase->CreateEntity(_EntityID);
    const auto _pSourceComponent =
        std::dynamic_pointer_cast<CStoreTestComponent>(
            _pSourceEntity->AddComponent(
                CStoreTestComponent::S_ClassName));
    ASSERT_NE(_pSourceComponent, nullptr);
    ASSERT_TRUE(_pSourceComponent->SetProperties({
        {CStoreTestComponent::kPersistent, 42},
        {CStoreTestComponent::kRuntime, 99}}));
    _pSourceComponent->Disable();

    iCAX::Resource::CResourceLibrary _SourceResources;
    ASSERT_TRUE(_SourceResources.RegisterPersistenceCodec<
        CTestPersistentResource>(
            "test.persistent",
            MakeTestPersistenceCodec()));
    auto _pBlob =
        std::make_shared<iCAX::Resource::CBinaryResource>();
    _pBlob->DisplayName = "saved blob";
    _pBlob->Content = {1, 3, 5, 7};
    iCAX::Resource::CResourceInfo _ResourceInfo;
    _ResourceInfo.Name = "blob";
    _ResourceInfo.ResourceTypeID =
        iCAX::Resource::CBinaryResource::kResourceTypeName;
    _ResourceInfo.nVersion = 3;
    _ResourceInfo.nSchemaVersion = 1;
    _ResourceInfo.nSize = _pBlob->Content.size();
    _ResourceInfo.Persistence =
        iCAX::Resource::EResourcePersistenceMode::Embedded;
    _SourceResources.Set(
        _ResourceURL,
        _pBlob,
        _ResourceInfo);
    iCAX::Resource::CResourceInfo _CustomInfo;
    _CustomInfo.ResourceTypeID = "test.persistent";
    _CustomInfo.nVersion = 2;
    _CustomInfo.nSchemaVersion = 1;
    _CustomInfo.Persistence =
        iCAX::Resource::EResourcePersistenceMode::Embedded;
    _SourceResources.Set(
        _CustomResourceURL,
        std::make_shared<CTestPersistentResource>(
            CTestPersistentResource{0x12345678}),
        _CustomInfo);

    CProjectFile _File({
        .Magic = "ICAX-STORE-TEST",
        .ProductID = "icax.store-test",
        .CurrentFormatVersion = "1.0",
        .nCurrentFormatRevision = 1});
    CProjectDocumentInfo _Info;
    _Info.ProjectID = _ProjectID;
    _Info.MainSceneID = _SceneID;
    _Info.ProjectName = "direct store";

    const auto _Path = std::filesystem::temp_directory_path() /
        ("icax-project-store-test-" +
            iCAX::Data::to_string(
                iCAX::Data::GenerateNewUUID()) +
            ".icax");
    _File.Save(
        _Path,
        _Info,
        *_pSourceDatabase,
        _SourceResources);

    auto _pTargetDatabase = iCAX::Database::GenerateRepository(
        _SceneID,
        CreateStoreTestMetaRegistry());
    iCAX::Resource::CResourceLibrary _TargetResources;
    ASSERT_TRUE(_TargetResources.RegisterPersistenceCodec<
        CTestPersistentResource>(
            "test.persistent",
            MakeTestPersistenceCodec()));
    const auto _Opened = _File.Open(
        _Path,
        *_pTargetDatabase,
        _TargetResources);

    EXPECT_EQ(_Opened.Info.ProjectName, "direct store");
    EXPECT_TRUE(_pTargetDatabase->HasEntity(_EntityID));
    const auto _pLoadedComponent =
        std::dynamic_pointer_cast<CStoreTestComponent>(
            _pTargetDatabase->GetEntity(_EntityID)
                ->GetComponent(CStoreTestComponent::S_ClassName));
    ASSERT_NE(_pLoadedComponent, nullptr);
    EXPECT_EQ(
        _pLoadedComponent->GetProperty(
            CStoreTestComponent::kPersistent).To<int>(),
        42);
    EXPECT_EQ(
        _pLoadedComponent->GetProperty(
            CStoreTestComponent::kRuntime).To<int>(),
        0);
    EXPECT_FALSE(_pLoadedComponent->IsEnable());
    const auto _pLoadedBlob =
        _TargetResources.Get<iCAX::Resource::CBinaryResource>(
            _ResourceURL,
            3);
    ASSERT_NE(_pLoadedBlob, nullptr);
    EXPECT_EQ(_pLoadedBlob->DisplayName, "saved blob");
    EXPECT_EQ(_pLoadedBlob->Content,
        (std::vector<uint8_t>{1, 3, 5, 7}));
    const auto _pLoadedCustom =
        _TargetResources.Get<CTestPersistentResource>(
            _CustomResourceURL,
            2);
    ASSERT_NE(_pLoadedCustom, nullptr);
    EXPECT_EQ(_pLoadedCustom->Value, 0x12345678u);

    std::error_code _Error;
    std::filesystem::remove(_Path, _Error);
}

TEST(ProjectFileStore, FailedMaterializationLeavesTargetsEmpty)
{
    const auto _SceneID = ParseTestUUID(
        "30000000-0000-4000-8000-000000000088");
    const auto _EntityID = ParseTestUUID(
        "10000000-0000-4000-8000-000000000088");
    auto _Document = MakeDocument();
    _Document.Info.Magic = "ICAX-STORE-ROLLBACK";
    _Document.Info.ProductID = "icax.store-rollback";
    _Document.Info.FormatVersion = "1.0";
    _Document.Info.nFormatRevision = 1;
    _Document.Info.MainSceneID = _SceneID;
    _Document.Entities = {{_SceneID}, {_EntityID}};
    _Document.Components.clear();
    _Document.Resources.clear();
    _Document.Info.ProjectSettings.clear();
    CProjectComponentRecord _Unknown;
    _Unknown.EntityID = _EntityID;
    _Unknown.ComponentClass = "CUnavailableComponent";
    _Document.Components.push_back(std::move(_Unknown));
    _Document.Canonicalize();

    const auto _Path = std::filesystem::temp_directory_path() /
        ("icax-project-store-rollback-test-" +
            iCAX::Data::to_string(
                iCAX::Data::GenerateNewUUID()) +
            ".icax");
    CProjectFileCodec::WriteAtomic(
        _Path,
        _Document,
        EProjectFileEncoding::Binary);

    CProjectFile _File({
        .Magic = "ICAX-STORE-ROLLBACK",
        .ProductID = "icax.store-rollback",
        .CurrentFormatVersion = "1.0",
        .nCurrentFormatRevision = 1});
    auto _pDatabase = iCAX::Database::GenerateRepository(
        _SceneID,
        iCAX::Database::CreateMetaRegistry());
    iCAX::Resource::CResourceLibrary _Resources;

    EXPECT_THROW(
        _File.Open(_Path, *_pDatabase, _Resources),
        std::runtime_error);
    EXPECT_EQ(_pDatabase->EntityCount(), 1);
    EXPECT_FALSE(_pDatabase->HasEntity(_EntityID));
    EXPECT_EQ(_Resources.Count(), 0u);

    std::error_code _Error;
    std::filesystem::remove(_Path, _Error);
}
