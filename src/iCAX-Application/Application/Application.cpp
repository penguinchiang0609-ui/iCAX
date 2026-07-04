#include "pch.h"
#include "Application.h"

#include "Product/ProductManifest.h"

#include <filesystem>
#include <stdexcept>
#include <utility>
#include <vector>

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
            return;
        }

        auto _Products = iCAX::Product::LoadProductDefinitions(_PathToUTF8(_ProductRoot));
        if (!_Products.empty())
        {
            Config_.EngineConfig.Products = std::move(_Products);
        }
    }

    iCAX::Application::CApplicationConfig _MakeDefaultApplicationConfig()
    {
        iCAX::Application::CApplicationConfig _Config;
        _Config.EngineConfig.strApplicationSettingsPath = "Setting/Application.Setting";
        _Config.EngineConfig.Descriptor.AppID = "icax";
        _Config.EngineConfig.Descriptor.AppName = "iCAX";
        _Config.EngineConfig.Paths.InstallDirectory = _PathToUTF8(std::filesystem::current_path());
        _Config.EngineConfig.Paths.UserConfigDirectory = "Setting";
        _Config.EngineConfig.Paths.CacheDirectory = "Cache";
        _Config.EngineConfig.Paths.TempDirectory = "Temp";
        _Config.EngineConfig.Paths.LogDirectory = "Log";
        _Config.EngineConfig.nFrameIntervalMilliseconds = 16;

        iCAX::Product::CProductDefinition _DefaultProduct;
        _DefaultProduct.ProductID = "icax.default";
        _DefaultProduct.ProductName = "iCAX Default Product";
        _DefaultProduct.ProductVersion = "1.0";
        _DefaultProduct.ProjectFile.Magic = "ICAX_DEFAULT";
        _DefaultProduct.ProjectFile.FormatVersion = "1.0";
        _DefaultProduct.ProjectFile.FileExtensions.push_back(".icax");
        _Config.EngineConfig.Products.push_back(_DefaultProduct);

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

    m_Engine.SetConfig(m_Config.EngineConfig);
    m_Engine.Start();

    try
    {
        m_FrontendBridge.Attach(m_Engine);
    }
    catch (...)
    {
        m_Engine.Stop();
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
    m_Engine.Stop();
}

bool iCAX::Application::CApplication::IsRunning() const
{
    std::lock_guard<std::mutex> _Lock(m_Mutex);
    return m_bStarted && m_Engine.IsRunning();
}

iCAX::ApplicationHost::CApplicationHost& iCAX::Application::CApplication::Engine()
{
    return m_Engine;
}

const iCAX::ApplicationHost::CApplicationHost& iCAX::Application::CApplication::Engine() const
{
    return m_Engine;
}

iCAX::Application::CFrontendBridge& iCAX::Application::CApplication::Frontend()
{
    return m_FrontendBridge;
}

const iCAX::Application::CFrontendBridge& iCAX::Application::CApplication::Frontend() const
{
    return m_FrontendBridge;
}
