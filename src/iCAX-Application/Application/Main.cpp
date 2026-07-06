#include "pch.h"
#include "Application.h"
#include "UIContainer/UIContainer.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
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

    std::filesystem::path FindDefaultWebPageRoot()
    {
        const auto _Current = std::filesystem::current_path();
        const auto _SourceRoot = std::filesystem::weakly_canonical(
            std::filesystem::path(__FILE__)).parent_path().parent_path().parent_path();
        const std::filesystem::path _Candidates[] = {
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
        const std::filesystem::path _ConfigCandidates[] = {
            _Current / "Setting" / "UIContainer.Setting",
            _Current / "UIContainer.Setting"
        };

        for (const auto& _ConfigPath : _ConfigCandidates)
        {
            if (TryReadUIContainerConfigFile(_ConfigPath, _Config))
            {
                break;
            }
        }

        const auto _UserDataPath = std::filesystem::weakly_canonical(_Current / "UserData");
        const auto _CachePath = std::filesystem::weakly_canonical(_UserDataPath / "Cache");
        std::filesystem::create_directories(_UserDataPath);
        std::filesystem::create_directories(_CachePath);
        _Config.Properties.emplace_back("userDataPath", ToUTF8(_UserDataPath));
        _Config.Properties.emplace_back("cachePath", ToUTF8(_CachePath));
        _Config.Properties.emplace_back("logFile", ToUTF8(std::filesystem::absolute(_Current / "cef.log")));
        _Config.Properties.emplace_back("disableGpu", "true");

        return _Config;
    }
}

namespace
{
    int RunApplication(IN iCAX::Frontend::CUIContainerConfig UIConfig_)
    {
        iCAX::Application::CApplication _Application;
        _Application.Start();

        iCAX::Frontend::CUIContainerInstance _UIContainer;
        try
        {
            UIConfig_.pFrontendBridge = &_Application.Frontend();
            _UIContainer = iCAX::Frontend::CUIContainerFactory::Create(UIConfig_);
            _UIContainer->Start();
            _UIContainer->WaitForExit();

            _UIContainer->Stop();
            _Application.Stop();
        }
        catch (...)
        {
            if (_UIContainer.IsValid() && _UIContainer->IsRunning())
            {
                _UIContainer->Stop();
            }
            _Application.Stop();
            throw;
        }
        return 0;
    }
}

int WINAPI wWinMain(HINSTANCE hInstance_, HINSTANCE, PWSTR, int)
{
    try
    {
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
