#include <gtest/gtest.h>

#include <ApplicationContext/ApplicationContext.h>
#include <ApplicationContext/ApplicationSettingsService.h>
#include <ApplicationContext/FileApplicationConfigStore.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

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

    CApplicationSettings _Settings;
    _Settings.Set("ui.theme", iCAX::Data::Variant(std::string("dark")));

    CApplicationContext _Context(_Descriptor, _Paths, _Settings);

    EXPECT_EQ("iCAX", _Context.GetDescriptor().AppName);
    EXPECT_EQ("Setting", _Context.GetPaths().UserConfigDirectory);
    EXPECT_EQ("dark", _Context.GetSettings().Get("ui.theme").To<std::string>());
}

TEST(ApplicationConfigStoreTest, SaveAndLoadSettings)
{
    auto _Path = MakeTempConfigPath();

    CApplicationSettings _Settings;
    _Settings.Set("ui.theme", iCAX::Data::Variant(std::string("light")));
    _Settings.Set("project.defaultVersion", iCAX::Data::Variant(2));

    CFileApplicationConfigStore _Store;
    _Store.Save(_Path.string(), _Settings);

    auto _Loaded = _Store.Load(_Path.string());
    EXPECT_EQ("light", _Loaded.Get("ui.theme").To<std::string>());
    EXPECT_EQ(2, _Loaded.Get("project.defaultVersion").To<int>());

    std::filesystem::remove(_Path);
}

TEST(ApplicationSettingsServiceTest, UpdatesContextAndPersistsSettings)
{
    auto _Path = MakeTempConfigPath();

    CApplicationDescriptor _Descriptor;
    CApplicationPaths _Paths;
    CApplicationSettings _Settings;
    _Settings.Set("ui.theme", iCAX::Data::Variant(std::string("light")));

    auto _pContext = std::make_shared<CApplicationContext>(_Descriptor, _Paths, _Settings);
    auto _pStore = std::make_shared<CFileApplicationConfigStore>();
    CApplicationSettingsService _Service(_pContext, _pStore, _Path.string());

    _Service.SetValue("ui.theme", iCAX::Data::Variant(std::string("dark")));
    EXPECT_EQ("dark", _pContext->GetSettings().Get("ui.theme").To<std::string>());

    _Service.Save();
    _pContext->ReplaceSettings(CApplicationSettings());
    EXPECT_EQ("default", _pContext->GetSettings().Get("ui.theme", iCAX::Data::Variant(std::string("default"))).To<std::string>());

    _Service.Reload();
    EXPECT_EQ("dark", _pContext->GetSettings().Get("ui.theme").To<std::string>());

    std::filesystem::remove(_Path);
}
