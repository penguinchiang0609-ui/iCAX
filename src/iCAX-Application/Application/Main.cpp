#include "pch.h"
#include "Application.h"
#include "UIContainer/UIContainer.h"
#include <chrono>
#include <exception>


namespace
{
    void EnableProcessDpiAwareness()
    {
        using SetProcessDpiAwarenessContextFunction = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto _SetProcessDpiAwarenessContext = reinterpret_cast<SetProcessDpiAwarenessContextFunction>(
            ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
        if (_SetProcessDpiAwarenessContext
            && _SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        {
            return;
        }

        using SetProcessDPIAwareFunction = BOOL(WINAPI*)();
        auto _SetProcessDPIAware = reinterpret_cast<SetProcessDPIAwareFunction>(
            ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "SetProcessDPIAware"));
        if (_SetProcessDPIAware)
        {
            _SetProcessDPIAware();
        }
    }

    std::string Trim(IN const std::string& Text_)
    {
        const auto _Begin = Text_.find_first_not_of(" \t\r\n");
        if (_Begin == std::string::npos)
        {
            return {};
        }
        const auto _End = Text_.find_last_not_of(" \t\r\n");
        return Text_.substr(_Begin, _End - _Begin + 1);
    }

    std::string ToUTF8(IN const std::filesystem::path& Path_)
    {
        auto _Text = Path_.u8string();
        return std::string(_Text.begin(), _Text.end());
    }

    bool TryReadUIContainerConfigFile(
        IN const std::filesystem::path& ConfigPath_,
        IN OUT iCAX::Frontend::CUIContainerConfig& Config_)
    {
        std::ifstream _Input(ConfigPath_, std::ios::binary);
        if (!_Input)
        {
            return false;
        }

        std::string _Line;
        while (std::getline(_Input, _Line))
        {
            _Line = Trim(_Line);
            if (_Line.empty() || _Line.front() == '#')
            {
                continue;
            }

            const auto _Equal = _Line.find('=');
            if (_Equal == std::string::npos)
            {
                throw std::invalid_argument("Invalid UI container config line: " + _Line);
            }

            const auto _Key = Trim(_Line.substr(0, _Equal));
            const auto _Value = Trim(_Line.substr(_Equal + 1));
            if (_Key == "type")
            {
                Config_.ContainerType = _Value;
            }
            else if (_Key == "modulePath")
            {
                Config_.ModulePath = _Value;
            }
            else if (_Key == "webPageRoot")
            {
                Config_.WebPageRoot = _Value;
            }
            else if (_Key == "startURL")
            {
                Config_.StartURL = _Value;
            }
            else if (_Key == "startupHandshakeTimeoutMS")
            {
                Config_.nStartupHandshakeTimeoutMS = static_cast<uint32_t>(std::stoul(_Value));
            }
            else
            {
                Config_.Properties.emplace_back(_Key, _Value);
            }
        }
        return true;
    }

    std::filesystem::path ExecutableDirectory()
    {
        std::vector<wchar_t> _Buffer(32768);
        while (true)
        {
            const auto _Length = GetModuleFileNameW(
                nullptr, _Buffer.data(), static_cast<DWORD>(_Buffer.size()));
            if (_Length == 0) return {};
            if (_Length < _Buffer.size() - 1)
                return std::filesystem::path(
                    std::wstring(_Buffer.data(), _Length)).parent_path();
            _Buffer.resize(_Buffer.size() * 2);
        }
    }

    std::filesystem::path FindDefaultWebPageRoot()
    {
        const auto _Current = std::filesystem::current_path();
        const auto _Executable = ExecutableDirectory();
        const auto _SourceRoot = std::filesystem::weakly_canonical(
            std::filesystem::path(__FILE__)).parent_path().parent_path().parent_path();
        const std::filesystem::path _Candidates[] = {
            _Executable / "iCAX-UI" / "SDK" / "AppShell",
            _Current / "src" / "iCAX-UI" / "SDK" / "AppShell",
            _Current / "iCAX-UI" / "SDK" / "AppShell",
            _Current / ".." / ".." / "iCAX-UI" / "SDK" / "AppShell",
            _Current / ".." / ".." / ".." / ".." / "iCAX-UI" / "SDK" / "AppShell",
            _SourceRoot / "iCAX-UI" / "SDK" / "AppShell"
        };

        for (const auto& _Candidate : _Candidates)
        {
            if (std::filesystem::exists(_Candidate / "index.html"))
            {
                return std::filesystem::weakly_canonical(_Candidate);
            }
        }
        return {};
    }

    iCAX::Frontend::CUIContainerConfig LoadUIContainerConfig()
    {
        iCAX::Frontend::CUIContainerConfig _Config;
        _Config.ContainerType = "cef";
        _Config.ModulePath = "CefUIContainer.dll";

        if (auto _WebPageRoot = FindDefaultWebPageRoot(); !_WebPageRoot.empty())
        {
            _Config.WebPageRoot = ToUTF8(_WebPageRoot);
        }

        const auto _Current = std::filesystem::current_path();
        const auto _Executable = ExecutableDirectory();
        const std::filesystem::path _ConfigCandidates[] = {
            _Current / "Setting" / "UIContainer.Setting",
            _Current / "UIContainer.Setting",
            _Executable / "Setting" / "UIContainer.Setting",
            _Executable / "UIContainer.Setting"
        };

        for (const auto& _ConfigPath : _ConfigCandidates)
        {
            if (TryReadUIContainerConfigFile(_ConfigPath, _Config))
            {
                break;
            }
        }

        const auto _UserDataText = iCAX::Application::ResolveDefaultUserDataDirectory();
        const auto _UserDataPath = std::filesystem::path(std::u8string(
            _UserDataText.begin(), _UserDataText.end()));
        const auto _BrowserDataPath = _UserDataPath / "Browser";
        // CEF requires cache_path to be a child of root_cache_path.  Keeping
        // the cache under Browser avoids CefInitialize rejecting the config
        // before the product/user data is even loaded.
        const auto _CachePath = _BrowserDataPath / "Cache";
        const auto _LogPath = _UserDataPath / "Logs";
        std::filesystem::create_directories(_BrowserDataPath);
        std::filesystem::create_directories(_CachePath);
        std::filesystem::create_directories(_LogPath);
        _Config.Properties.emplace_back("userDataPath", ToUTF8(_BrowserDataPath));
        _Config.Properties.emplace_back("cachePath", ToUTF8(_CachePath));
        _Config.Properties.emplace_back("logFile", ToUTF8(_LogPath / "cef.log"));
        _Config.Properties.emplace_back("disableGpu", "true");

        return _Config;
    }
}

namespace
{
    void LogShutdownStage(const char* Stage_, const char* State_, long long Milliseconds_ = -1) noexcept
    {
        try
        {
            const auto _Root = iCAX::Application::ResolveDefaultUserDataDirectory();
            const auto _Path = std::filesystem::path(std::u8string(_Root.begin(), _Root.end()))
                / "Logs" / "ApplicationShutdown.log";
            std::ofstream _Output(_Path, std::ios::app);
            _Output << "pid=" << GetCurrentProcessId() << " " << Stage_ << " " << State_;
            if (Milliseconds_ >= 0) _Output << " " << Milliseconds_ << "ms";
            _Output << '\n';
        }
        catch (...) {}
    }

    int RunApplication(IN iCAX::Frontend::CUIContainerConfig UIConfig_)
    {
        iCAX::Application::CApplication _Application;
        _Application.Start();

        iCAX::Frontend::CUIContainerInstance _UIContainer;
        const auto _StopApplication = [&]()
        {
            std::exception_ptr _Failure;
            const auto _Stage = [&](const char* Name_, const auto& Stop_)
            {
                const auto _Start = std::chrono::steady_clock::now();
                LogShutdownStage(Name_, "begin");
                try
                {
                    Stop_();
                    LogShutdownStage(Name_, "end", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - _Start).count());
                }
                catch (...)
                {
                    LogShutdownStage(Name_, "failed", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - _Start).count());
                    if (!_Failure) _Failure = std::current_exception();
                }
            };
            _Stage("ui-stop", [&]() {
                if (_UIContainer.IsValid() && _UIContainer->IsRunning()) _UIContainer->Stop();
            });
            _Stage("ui-runtime-shutdown", [&]() {
                iCAX::Frontend::CUIContainerFactory::ShutdownRuntime(UIConfig_);
            });
            _Stage("application-stop", [&]() { _Application.Stop(); });
            if (_Failure) std::rethrow_exception(_Failure);
        };
        try
        {
            UIConfig_.pFrontendBridge = &_Application.Frontend();
            UIConfig_.Properties.insert(UIConfig_.Properties.begin(), { "windowIconPath", _Application.GetWindowIconPath() });
            _UIContainer = iCAX::Frontend::CUIContainerFactory::Create(UIConfig_);
            _UIContainer->Start();
            _UIContainer->WaitForExit();

        }
        catch (...)
        {
            const auto _Failure = std::current_exception();
            try { _StopApplication(); } catch (...) {}
            std::rethrow_exception(_Failure);
        }
        _StopApplication();
        LogShutdownStage("run-return", "end");
        return 0;
    }
}

int WINAPI wWinMain(HINSTANCE hInstance_, HINSTANCE, PWSTR, int)
{
    try
    {
        EnableProcessDpiAwareness();

        auto _UIConfig = LoadUIContainerConfig();
        const int _nSubProcessResult = iCAX::Frontend::CUIContainerFactory::ExecuteSubProcessIfNeeded(
            _UIConfig,
            hInstance_);
        if (_nSubProcessResult >= 0)
        {
            return _nSubProcessResult;
        }

        return RunApplication(std::move(_UIConfig));
    }
    catch (const std::exception& Error_)
    {
        std::ofstream _Output("ApplicationStartupFailure.log", std::ios::binary | std::ios::trunc);
        _Output << Error_.what();
        return 1;
    }
    catch (...)
    {
        std::ofstream _Output("ApplicationStartupFailure.log", std::ios::binary | std::ios::trunc);
        _Output << "Unknown startup failure";
        return 1;
    }
}
