#include "pch.h"


#include <ApplicationContext/ApplicationContext.h>
#include <ApplicationContext/ApplicationConfigService.h>
#include <ApplicationContext/FileApplicationConfigStore.h>
#include <ApplicationContext/UserDataStore.h>
#include <Data/VariantSerializer.h>

#include <type_traits>
#include <winsqlite/winsqlite3.h>


using namespace iCAX::Application;

namespace
{
    std::filesystem::path MakeTempConfigPath()
    {
        auto _Now = std::chrono::steady_clock::now().time_since_epoch().count();
        auto _Path = std::filesystem::temp_directory_path() / ("icax_application_context_test_" + std::to_string(_Now) + ".json");
        std::filesystem::remove(_Path);
        return _Path;
    }

    void RemoveSqliteFiles(IN const std::filesystem::path& Path_)
    {
        std::filesystem::remove(Path_);
        std::filesystem::remove(Path_.string() + "-wal");
        std::filesystem::remove(Path_.string() + "-shm");
    }

    std::vector<CUserDataFeatureDescriptor> MakeUserDataDescriptors()
    {
        CUserDataFeatureDescriptor _Customer;
        _Customer.FeatureID = "customer";
        CUserDataRecordTypeDescriptor _CustomerProfile;
        _CustomerProfile.RecordType = "profile";
        _CustomerProfile.SubjectTypes = { "product" };
        _Customer.RecordTypes.emplace_back(std::move(_CustomerProfile));

        CUserDataFeatureDescriptor _Template;
        _Template.FeatureID = "template";
        CUserDataRecordTypeDescriptor _Preset;
        _Preset.RecordType = "parameter-preset";
        _Preset.SubjectTypes = { "template-definition" };
        CUserDataRelationDescriptor _CustomerRelation;
        _CustomerRelation.RelationType = "customer";
        _CustomerRelation.TargetKind = "user-record";
        _CustomerRelation.TargetFeatureID = "customer";
        _CustomerRelation.TargetType = "profile";
        _Preset.Relations.emplace_back(std::move(_CustomerRelation));
        _Template.RecordTypes.emplace_back(std::move(_Preset));

        CUserDataFeatureDescriptor _UI;
        _UI.FeatureID = "ui";
        CUserDataRecordTypeDescriptor _ViewPreference;
        _ViewPreference.RecordType = "view-preference";
        _ViewPreference.SubjectTypes = { "view" };
        _ViewPreference.AllowMultiple = false;
        _UI.RecordTypes.emplace_back(std::move(_ViewPreference));
        return { std::move(_Customer), std::move(_Template), std::move(_UI) };
    }

    void ExecuteSql(IN sqlite3* Database_, IN const char* Sql_)
    {
        char* _Error = nullptr;
        const auto _Result = sqlite3_exec(Database_, Sql_, nullptr, nullptr, &_Error);
        if (_Result == SQLITE_OK) return;
        const std::string _Message = _Error ? _Error : sqlite3_errmsg(Database_);
        if (_Error) sqlite3_free(_Error);
        throw std::runtime_error(_Message);
    }

    void CreateVersion1Database(IN const std::filesystem::path& Path_)
    {
        sqlite3* _Database = nullptr;
        if (sqlite3_open(Path_.string().c_str(), &_Database) != SQLITE_OK)
            throw std::runtime_error("Cannot create legacy user database");
        try
        {
            ExecuteSql(_Database,
                "CREATE TABLE user_records ("
                "data_namespace TEXT NOT NULL, collection_name TEXT NOT NULL, record_id TEXT NOT NULL, "
                "owner_scope TEXT NOT NULL, payload TEXT NOT NULL, revision INTEGER NOT NULL, "
                "created_at TEXT NOT NULL, updated_at TEXT NOT NULL, deleted_at TEXT, "
                "PRIMARY KEY(data_namespace, collection_name, record_id));"
                "PRAGMA user_version=1;");
            const auto _Payload = iCAX::Data::VariantSerializer::Serialize(
                iCAX::Data::Variant(iCAX::Data::ObjectMap{
                    { "name", iCAX::Data::Variant(std::string("Legacy preset")) } }));
            sqlite3_stmt* _Statement = nullptr;
            const char* _Sql =
                "INSERT INTO user_records(data_namespace, collection_name, record_id, owner_scope, "
                "payload, revision, created_at, updated_at, deleted_at) "
                "VALUES('icax.tube-designer', 'parameter-presets', 'legacy-1', 'personal', "
                "?1, 3, '2026-01-01T00:00:00Z', '2026-01-02T00:00:00Z', NULL);";
            if (sqlite3_prepare_v2(_Database, _Sql, -1, &_Statement, nullptr) != SQLITE_OK)
                throw std::runtime_error(sqlite3_errmsg(_Database));
            sqlite3_bind_text(
                _Statement, 1, _Payload.c_str(), static_cast<int>(_Payload.size()), SQLITE_TRANSIENT);
            const auto _StepResult = sqlite3_step(_Statement);
            sqlite3_finalize(_Statement);
            if (_StepResult != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(_Database));
        }
        catch (...)
        {
            sqlite3_close(_Database);
            throw;
        }
        sqlite3_close(_Database);
    }
}

TEST(ApplicationDescriptorTest, SupportsMagicAndVersions)
{
    CApplicationDescriptor _Descriptor;
    _Descriptor.AppID = "icax";
    _Descriptor.AppName = "iCAX";
    _Descriptor.SupportedProjectMagics = { "ICAX_PROJECT" };
    _Descriptor.SupportedProjectVersions = { 1, 2 };
    _Descriptor.DefaultProjectExtension = ".icax";

    EXPECT_TRUE(_Descriptor.SupportsProjectMagic("ICAX_PROJECT"));
    EXPECT_FALSE(_Descriptor.SupportsProjectMagic("OTHER"));
    EXPECT_TRUE(_Descriptor.SupportsProjectVersion(1));
    EXPECT_FALSE(_Descriptor.SupportsProjectVersion(3));
}

TEST(ApplicationContextTest, HoldsDescriptorPathsAndSettings)
{
    CApplicationDescriptor _Descriptor;
    _Descriptor.AppName = "iCAX";

    CApplicationPaths _Paths;
    _Paths.UserConfigDirectory = "Setting";
    _Paths.ResourceVersionDirectory =
        "Temp/ResourceVersions";

    iCAX::Data::PropertyBag _Settings;
    _Settings.Set("ui.theme", iCAX::Data::Variant(std::string("dark")));

    CApplicationContext _Context(_Descriptor, _Paths, _Settings);

    EXPECT_EQ("iCAX", _Context.GetDescriptor().AppName);
    EXPECT_EQ("Setting", _Context.GetPaths().UserConfigDirectory);
    EXPECT_EQ(
        "Temp/ResourceVersions",
        _Context.GetPaths().ResourceVersionDirectory);
    EXPECT_EQ("dark", _Context.GetSettings().Get("ui.theme").To<std::string>());
    const IApplicationContext& _ReadView = _Context;
    static_assert(std::is_same_v<
        decltype(_ReadView.Services()),
        const iCAX::Services::CServiceProvider&>);
    EXPECT_EQ(&_Context.Services(), &_ReadView.Services());
}

TEST(UserDataStoreTest, PersistsVersionsQueriesAndSoftDeletesRecords)
{
    auto _Path = MakeTempConfigPath();
    _Path.replace_extension(".db");
    RemoveSqliteFiles(_Path);

    {
        CSqliteUserDataStore _Store(_Path.string());
        iCAX::Data::ObjectMap _Payload;
        _Payload["name"] = std::string("304 standard");

        CUserDataRecord _Record;
        _Record.ProductID = "icax.tube-designer";
        _Record.FeatureID = "template";
        _Record.RecordType = "parameter-preset";
        _Record.SubjectType = "template-definition";
        _Record.SubjectID = "single-face-security-window";
        _Record.RecordID = "preset-1";
        _Record.OwnerScope = "personal";
        _Record.Payload = iCAX::Data::Variant(_Payload);

        const auto _Created = _Store.Put(_Record, 0);
        EXPECT_EQ(1u, _Created.Revision);
        EXPECT_FALSE(_Created.CreatedAt.empty());
        EXPECT_EQ("304 standard", _Created.Payload.To<iCAX::Data::ObjectMap>().at("name").To<std::string>());

        _Payload["name"] = std::string("304 reinforced");
        _Record.Payload = iCAX::Data::Variant(_Payload);
        const auto _Updated = _Store.Put(_Record, _Created.Revision);
        EXPECT_EQ(2u, _Updated.Revision);
        EXPECT_THROW((void)_Store.Put(_Record, _Created.Revision), std::runtime_error);

        CUserDataQuery _Query;
        _Query.ProductID = _Record.ProductID;
        _Query.FeatureID = _Record.FeatureID;
        _Query.RecordType = _Record.RecordType;
        _Query.SubjectType = _Record.SubjectType;
        _Query.SubjectID = _Record.SubjectID;
        _Query.OwnerScope = "personal";
        const auto _Records = _Store.List(_Query);
        ASSERT_EQ(1u, _Records.size());
        EXPECT_EQ("preset-1", _Records.front().RecordID);

        EXPECT_TRUE(_Store.Delete(
            _Record.ProductID,
            _Record.FeatureID,
            _Record.RecordType,
            _Record.RecordID,
            _Updated.Revision));
        EXPECT_FALSE(_Store.Get(
            _Record.ProductID, _Record.FeatureID, _Record.RecordType, _Record.RecordID));
        const auto _Deleted = _Store.Get(
            _Record.ProductID,
            _Record.FeatureID,
            _Record.RecordType,
            _Record.RecordID,
            true);
        ASSERT_TRUE(_Deleted.has_value());
        EXPECT_FALSE(_Deleted->DeletedAt.empty());
        EXPECT_EQ(3u, _Deleted->Revision);
    }

    RemoveSqliteFiles(_Path);
}

TEST(UserDataStoreTest, ProductScopeOwnsTheIdentityAndPreventsCrossProductAccess)
{
    auto _Path = MakeTempConfigPath();
    _Path.replace_extension(".db");
    RemoveSqliteFiles(_Path);

    {
        auto _RawStore = std::make_shared<CSqliteUserDataStore>(_Path.string());
        CProductUserDataStore _TubeDesigner(
            _RawStore, "icax.tube-designer", MakeUserDataDescriptors());
        CProductUserDataStore _TubeOne(
            _RawStore, "icax.tube-one", MakeUserDataDescriptors());

        CProductUserDataRecord _Record;
        _Record.FeatureID = "template";
        _Record.RecordType = "parameter-preset";
        _Record.SubjectType = "template-definition";
        _Record.SubjectID = "single-face-security-window";
        _Record.RecordID = "preset-1";
        iCAX::Data::ObjectMap _Payload;
        _Payload["name"] = std::string("customer standard");
        _Record.Payload = iCAX::Data::Variant(_Payload);

        const auto _Saved = _TubeDesigner.Put(_Record, 0);
        EXPECT_EQ("icax.tube-designer", _TubeDesigner.GetProductID());
        EXPECT_EQ("icax.tube-one", _TubeOne.GetProductID());
        EXPECT_EQ(1u, _Saved.Revision);
        EXPECT_TRUE(_TubeDesigner.Get("template", "parameter-preset", "preset-1").has_value());
        EXPECT_FALSE(_TubeOne.Get("template", "parameter-preset", "preset-1").has_value());

        CProductUserDataQuery _TubeOneQuery;
        _TubeOneQuery.FeatureID = "template";
        _TubeOneQuery.RecordType = "parameter-preset";
        EXPECT_TRUE(_TubeOne.List(_TubeOneQuery).empty());
        EXPECT_THROW(
            (void)_TubeDesigner.Get("nesting", "strategy-preset", "preset-1"),
            std::invalid_argument);
    }

    RemoveSqliteFiles(_Path);
}

TEST(UserDataStoreTest, PersistsExplicitRelationsAndRestrictsDanglingDeletes)
{
    auto _Path = MakeTempConfigPath();
    _Path.replace_extension(".db");
    RemoveSqliteFiles(_Path);

    {
        auto _RawStore = std::make_shared<CSqliteUserDataStore>(_Path.string());
        CProductUserDataStore _Store(
            _RawStore, "icax.tube-designer", MakeUserDataDescriptors());

        CProductUserDataRecord _Customer;
        _Customer.FeatureID = "customer";
        _Customer.RecordType = "profile";
        _Customer.SubjectType = "product";
        _Customer.SubjectID = "icax.tube-designer";
        _Customer.RecordID = "customer-1";
        _Customer.Payload = iCAX::Data::Variant(iCAX::Data::ObjectMap{
            { "name", iCAX::Data::Variant(std::string("Customer A")) } });
        const auto _SavedCustomer = _Store.Put(_Customer, 0);

        CProductUserDataRecord _Preset;
        _Preset.FeatureID = "template";
        _Preset.RecordType = "parameter-preset";
        _Preset.SubjectType = "template-definition";
        _Preset.SubjectID = "single-face-security-window";
        _Preset.RecordID = "preset-1";
        CUserDataLink _Link;
        _Link.RelationType = "customer";
        _Link.TargetKind = "user-record";
        _Link.TargetFeatureID = "customer";
        _Link.TargetType = "profile";
        _Link.TargetID = "customer-1";
        _Preset.Links.emplace_back(std::move(_Link));
        const auto _SavedPreset = _Store.Put(_Preset, 0);
        ASSERT_EQ(1u, _SavedPreset.Links.size());
        EXPECT_EQ("customer-1", _SavedPreset.Links.front().TargetID);

        EXPECT_THROW(
            (void)_Store.Delete(
                "customer", "profile", "customer-1", _SavedCustomer.Revision),
            std::runtime_error);
        EXPECT_TRUE(_Store.Delete(
            "template", "parameter-preset", "preset-1", _SavedPreset.Revision));
        EXPECT_TRUE(_Store.Delete(
            "customer", "profile", "customer-1", _SavedCustomer.Revision));
    }

    RemoveSqliteFiles(_Path);
}

TEST(UserDataStoreTest, ManifestDescriptorEnforcesSingletonRecordIdentity)
{
    auto _RawStore = std::make_shared<CSqliteUserDataStore>(":memory:");
    CProductUserDataStore _Store(
        _RawStore, "icax.tube-designer", MakeUserDataDescriptors());

    CProductUserDataRecord _Preference;
    _Preference.FeatureID = "ui";
    _Preference.RecordType = "view-preference";
    _Preference.SubjectType = "view";
    _Preference.SubjectID = "template-list";
    _Preference.RecordID = "random-record";
    EXPECT_THROW((void)_Store.Put(_Preference, 0), std::invalid_argument);

    _Preference.RecordID = _Preference.SubjectID;
    const auto _Saved = _Store.Put(_Preference, 0);
    EXPECT_EQ("template-list", _Saved.RecordID);
    EXPECT_EQ(1u, _Saved.Revision);
}

TEST(UserDataStoreTest, Version1DatabaseUpgradesWithoutDiscardingLegacyRecords)
{
    auto _Path = MakeTempConfigPath();
    _Path.replace_extension(".db");
    RemoveSqliteFiles(_Path);
    CreateVersion1Database(_Path);

    {
        CSqliteUserDataStore _Store(_Path.string());
        CUserDataQuery _Query;
        _Query.ProductID = "icax.tube-designer";
        _Query.FeatureID = "_legacy";
        _Query.RecordType = "parameter-presets";
        const auto _Records = _Store.List(_Query);
        ASSERT_EQ(1u, _Records.size());
        EXPECT_EQ("product", _Records.front().SubjectType);
        EXPECT_EQ("icax.tube-designer", _Records.front().SubjectID);
        EXPECT_EQ(3u, _Records.front().Revision);
        EXPECT_EQ(
            "Legacy preset",
            _Records.front().Payload.To<iCAX::Data::ObjectMap>()
                .at("name").To<std::string>());
    }

    RemoveSqliteFiles(_Path);
}

TEST(ApplicationConfigStoreTest, SaveAndLoadSettings)
{
    auto _Path = MakeTempConfigPath();

    iCAX::Data::PropertyBag _Settings;
    _Settings.Set("ui.theme", iCAX::Data::Variant(std::string("light")));
    _Settings.Set("project.defaultVersion", iCAX::Data::Variant(2));

    CFileApplicationConfigStore _Store;
    _Store.Save(_Path.string(), _Settings);

    auto _Loaded = _Store.Load(_Path.string());
    EXPECT_EQ("light", _Loaded.Get("ui.theme").To<std::string>());
    EXPECT_EQ(2, _Loaded.Get("project.defaultVersion").To<int>());

    std::filesystem::remove(_Path);
}

TEST(ApplicationConfigServiceTest, UpdatesContextAndPersistsSettings)
{
    auto _Path = MakeTempConfigPath();

    CApplicationDescriptor _Descriptor;
    CApplicationPaths _Paths;
    iCAX::Data::PropertyBag _Settings;
    _Settings.Set("ui.theme", iCAX::Data::Variant(std::string("light")));

    auto _pStore = std::make_shared<CFileApplicationConfigStore>();
    _pStore->Save(_Path.string(), _Settings);
    auto _pContext = std::make_shared<CApplicationContext>(
        _Descriptor,
        _Paths,
        _pStore,
        _Path.string());
    CApplicationConfigService _Service(_pContext);

    _Service.SetValue("ui.theme", iCAX::Data::Variant(std::string("dark")));
    EXPECT_EQ("dark", _pContext->GetSettings().Get("ui.theme").To<std::string>());

    _Service.Save();
    _Service.SetValue("ui.theme", iCAX::Data::Variant(std::string("temporary")));
    EXPECT_EQ("temporary", _pContext->GetSettings().Get("ui.theme").To<std::string>());

    _Service.Reload();
    EXPECT_EQ("dark", _pContext->GetSettings().Get("ui.theme").To<std::string>());

    std::filesystem::remove(_Path);
}
