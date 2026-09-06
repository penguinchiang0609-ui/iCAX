#include "pch.h"
#include "Application.h"

#include "Product/ProductManifest.h"


namespace
{
    std::filesystem::path _CanonicalDirectory(IN const std::filesystem::path& Path_)
    {
        std::error_code _Error;
        auto _Canonical = std::filesystem::weakly_canonical(Path_, _Error);
        return _Error ? Path_ : _Canonical;
    }

    std::string _PathToUTF8(IN const std::filesystem::path& Path_)
    {
        auto _Text = Path_.u8string();
        return std::string(_Text.begin(), _Text.end());
    }

    std::filesystem::path _FindDefaultProductRoot()
    {
        const auto _Current = std::filesystem::current_path();
        const auto _SourceFile = _CanonicalDirectory(std::filesystem::path(__FILE__));
        const auto _SourceRoot = _SourceFile.parent_path().parent_path().parent_path();

        const std::vector<std::filesystem::path> _Candidates{
            _Current / "src" / "apps",
            _Current / "apps",
            _Current.parent_path() / "apps",
            _Current.parent_path().parent_path() / "apps",
            _SourceRoot / "apps",
        };

        for (const auto& _Candidate : _Candidates)
        {
            if (std::filesystem::exists(_Candidate) && std::filesystem::is_directory(_Candidate))
            {
                return _CanonicalDirectory(_Candidate);
            }
        }
        return {};
    }

    void _LoadProductDefinitions(IN OUT iCAX::Application::CApplicationConfig& Config_)
    {
        const auto _ProductRoot = _FindDefaultProductRoot();
        if (_ProductRoot.empty())
        {
            throw std::runtime_error("找不到产品目录，无法加载 TubeDesigner。");
        }

        // Application controls the enabled products; do not auto-register every installed manifest.
        Config_.RuntimeConfig.Products = {
            iCAX::Product::LoadProductManifest(
                _PathToUTF8(_ProductRoot / "tube-designer" / "product.manifest.json")).Definition,
        };
    }

    iCAX::Application::CApplicationConfig _MakeDefaultApplicationConfig()
    {
        iCAX::Application::CApplicationConfig _Config;
        const auto _Current = std::filesystem::current_path();
        const auto _UserDataPath = iCAX::Application::ResolveDefaultUserDataDirectory();
        const auto _UserDataRoot = std::filesystem::path(std::u8string(
            _UserDataPath.begin(), _UserDataPath.end()));
        const auto _ProfileRoot = _UserDataRoot / "Profiles" / "local-default";
        const auto _CacheRoot = _UserDataRoot / "Cache";
        const auto _TempRoot = _UserDataRoot / "Temp";
        const auto _LogRoot = _UserDataRoot / "Logs";
        std::filesystem::create_directories(_ProfileRoot);
        std::filesystem::create_directories(_CacheRoot);
        std::filesystem::create_directories(_TempRoot);
        std::filesystem::create_directories(_LogRoot);

        const auto _ApplicationSettingsPath = _ProfileRoot / "Application.Setting";
        const auto _LegacyApplicationSettingsPath = _Current / "Setting" / "Application.Setting";
        std::error_code _MigrationError;
        if (!std::filesystem::exists(_ApplicationSettingsPath)
            && std::filesystem::exists(_LegacyApplicationSettingsPath))
        {
            std::filesystem::copy_file(
                _LegacyApplicationSettingsPath,
                _ApplicationSettingsPath,
                std::filesystem::copy_options::skip_existing,
                _MigrationError);
        }
        const auto _LegacyProducts = _Current / "Setting" / "Products";
        const auto _ProfileProducts = _ProfileRoot / "Products";
        if (std::filesystem::exists(_LegacyProducts))
        {
            std::filesystem::create_directories(_ProfileProducts, _MigrationError);
            std::filesystem::copy(
                _LegacyProducts,
                _ProfileProducts,
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing,
                _MigrationError);
        }

        _Config.RuntimeConfig.strApplicationSettingsPath = _PathToUTF8(_ApplicationSettingsPath);
        _Config.RuntimeConfig.Descriptor.AppID = "icax";
        _Config.RuntimeConfig.Descriptor.AppName = "工作台";
        _Config.RuntimeConfig.Paths.InstallDirectory = _PathToUTF8(_Current);
        _Config.RuntimeConfig.Paths.UserConfigDirectory = _PathToUTF8(_ProfileRoot);
        _Config.RuntimeConfig.Paths.UserDataDirectory = _PathToUTF8(_UserDataRoot);
        _Config.RuntimeConfig.Paths.BrowserDataDirectory = _PathToUTF8(_UserDataRoot / "Browser");
        _Config.RuntimeConfig.Paths.CacheDirectory = _PathToUTF8(_CacheRoot);
        _Config.RuntimeConfig.Paths.TempDirectory = _PathToUTF8(_TempRoot);
        _Config.RuntimeConfig.Paths.ResourceVersionDirectory = _PathToUTF8(_TempRoot / "ResourceVersions");
        _Config.RuntimeConfig.Paths.LogDirectory = _PathToUTF8(_LogRoot);
        _Config.RuntimeConfig.nFrameIntervalMilliseconds = 16;

        _LoadProductDefinitions(_Config);
        return _Config;
    }
}

iCAX::Application::CApplication::CApplication()
    : m_Config(_MakeDefaultApplicationConfig())
{
}

iCAX::Application::CApplication::~CApplication()
{
    if (IsRunning())
    {
        Stop();
    }
}

void iCAX::Application::CApplication::SetConfig(const CApplicationConfig& Config_)
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    if (m_bStarted)
    {
        throw std::logic_error("Application config cannot be changed after start");
    }
    m_Config = Config_;
}

void iCAX::Application::CApplication::Start()
{
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        if (m_bStarted)
        {
            return;
        }
    }

    m_Runtime.SetConfig(m_Config.RuntimeConfig);
    m_Runtime.Start();

    try
    {
        m_FrontendBridge.Attach(m_Runtime);
    }
    catch (...)
    {
        m_Runtime.Stop();
        throw;
    }

    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        m_bStarted = true;
    }
}

void iCAX::Application::CApplication::Stop()
{
    {
        std::lock_guard<std::mutex> _Lock(m_Mutex);
        if (!m_bStarted)
        {
            return;
        }
        m_bStarted = false;
    }

    m_FrontendBridge.Detach();
    m_Runtime.Stop();
}

bool iCAX::Application::CApplication::IsRunning() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_bStarted && m_Runtime.IsRunning();
}

iCAX::Application::CApplicationRuntime& iCAX::Application::CApplication::Runtime()
{
    return m_Runtime;
}

const iCAX::Application::CApplicationRuntime& iCAX::Application::CApplication::Runtime() const
{
    return m_Runtime;
}

iCAX::Application::CFrontendBridge& iCAX::Application::CApplication::Frontend()
{
    return m_FrontendBridge;
}

const iCAX::Application::CFrontendBridge& iCAX::Application::CApplication::Frontend() const
{
    return m_FrontendBridge;
}
