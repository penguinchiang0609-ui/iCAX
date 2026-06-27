#include "pch.h"
#include "UIContainer.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace
{
    constexpr uint32_t _CommandHash32(IN const char* pText_) noexcept
    {
        uint32_t _Hash = 2166136261u;
        while (*pText_)
        {
            _Hash ^= static_cast<uint8_t>(*pText_);
            _Hash *= 16777619u;
            ++pText_;
        }
        return _Hash;
    }

    constexpr uint64_t _MakeCommandCode(IN const char* pMainName_, IN const char* pSubName_) noexcept
    {
        return (static_cast<uint64_t>(_CommandHash32(pMainName_)) << 32)
            | static_cast<uint64_t>(_CommandHash32(pSubName_));
    }

    constexpr uint64_t kApplicationGetStateCommand = _MakeCommandCode("App", "GetState");
    constexpr uint64_t kStartupHandshakeRequestID = 0x4955435841505055ull;
    constexpr const char* kEmptyObjectPayloadText = "{\"__variant_type\":\"Object\",\"value\":{}}";

    std::string _Trim(IN const std::string& Text_)
    {
        const auto _Begin = std::find_if_not(Text_.begin(), Text_.end(), [](unsigned char Ch_) {
            return std::isspace(Ch_) != 0;
        });
        const auto _End = std::find_if_not(Text_.rbegin(), Text_.rend(), [](unsigned char Ch_) {
            return std::isspace(Ch_) != 0;
        }).base();
        if (_Begin >= _End)
        {
            return {};
        }
        return std::string(_Begin, _End);
    }

    std::string _ToLower(IN std::string Text_)
    {
        std::transform(Text_.begin(), Text_.end(), Text_.begin(), [](unsigned char Ch_) {
            return static_cast<char>(std::tolower(Ch_));
        });
        return Text_;
    }

    std::wstring _UTF8ToWide(IN const std::string& Text_)
    {
        if (Text_.empty())
        {
            return {};
        }

        const int _Length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text_.data(), static_cast<int>(Text_.size()), nullptr, 0);
        if (_Length <= 0)
        {
            throw std::runtime_error("Invalid UTF-8 text in UI container module path");
        }

        std::wstring _Result(static_cast<size_t>(_Length), L'\0');
        const int _Written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text_.data(), static_cast<int>(Text_.size()), _Result.data(), _Length);
        if (_Written != _Length)
        {
            throw std::runtime_error("Failed to convert UI container module path");
        }
        return _Result;
    }

    std::string _ResolveModulePath(IN const iCAX::Frontend::CUIContainerConfig& Config_)
    {
        if (!Config_.ModulePath.empty())
        {
            return Config_.ModulePath;
        }

        const auto _Type = _ToLower(_Trim(Config_.ContainerType));
        if (_Type == "cef")
        {
            return "CefUIContainer.dll";
        }
        if (_Type == "wpf")
        {
            return "WpfUIContainer.dll";
        }
        throw std::invalid_argument("UI container module path is required for container type: " + Config_.ContainerType);
    }

    using UIContainerModuleRegisterFunction = bool(__cdecl*)();

    void _TryRegisterLoadedModule(IN HMODULE hModule_)
    {
        auto _pRegister = reinterpret_cast<UIContainerModuleRegisterFunction>(
            GetProcAddress(hModule_, "ICAXRegisterUIContainerModule"));
        if (_pRegister)
        {
            (void)_pRegister();
        }
    }

    struct CUIContainerRegistry final
    {
        std::mutex Mutex;
        std::map<std::string, iCAX::Frontend::CUIContainerRegistration> Registrations;
        std::vector<HMODULE> LoadedModules;
    };

    CUIContainerRegistry& _Registry()
    {
        static CUIContainerRegistry _Instance;
        return _Instance;
    }

    iCAX::Frontend::CUIContainerRegistration _RequireRegistration(IN const std::string& strContainerType_)
    {
        auto& _RegistryRef = _Registry();
        std::lock_guard<std::mutex> _Lock(_RegistryRef.Mutex);
        auto _Iter = _RegistryRef.Registrations.find(strContainerType_);
        if (_Iter == _RegistryRef.Registrations.end())
        {
            throw std::runtime_error("UI container type is not registered: " + strContainerType_);
        }
        return _Iter->second;
    }

    bool _IsRegistered(IN const std::string& strContainerType_)
    {
        auto& _RegistryRef = _Registry();
        std::lock_guard<std::mutex> _Lock(_RegistryRef.Mutex);
        return _RegistryRef.Registrations.find(strContainerType_) != _RegistryRef.Registrations.end();
    }

    void _RememberLoadedModule(IN HMODULE hModule_)
    {
        auto& _RegistryRef = _Registry();
        std::lock_guard<std::mutex> _Lock(_RegistryRef.Mutex);
        _RegistryRef.LoadedModules.push_back(hModule_);
    }

    void _LoadRegistrationModule(
        IN const std::string& strContainerType_,
        IN const iCAX::Frontend::CUIContainerConfig& Config_)
    {
        if (_IsRegistered(strContainerType_))
        {
            return;
        }

        const auto _ModulePath = _ResolveModulePath(Config_);
        const auto _ModulePathW = _UTF8ToWide(_ModulePath);
        HMODULE _hModule = LoadLibraryW(_ModulePathW.c_str());
        if (!_hModule)
        {
            throw std::runtime_error("Failed to load UI container module: " + _ModulePath);
        }

        _RememberLoadedModule(_hModule);
        _TryRegisterLoadedModule(_hModule);
        if (!_IsRegistered(strContainerType_))
        {
            throw std::runtime_error("UI container module did not register requested type: " + strContainerType_);
        }
    }

    void _ValidateStartupResponse(IN const iCAX::Frontend::CFrontendMailEnvelope& Response_)
    {
        if (Response_.nStamp != 0)
        {
            throw std::runtime_error("ApplicationProxy startup handshake failed with stamp: " + std::to_string(Response_.nStamp));
        }
        if (Response_.PayloadText.find("applicationChannelId") == std::string::npos
            || Response_.PayloadText.find("\"state\"") == std::string::npos)
        {
            throw std::runtime_error("ApplicationProxy startup handshake returned an invalid application state payload");
        }
    }

    class CHeadlessUIContainer final : public iCAX::Frontend::IUIContainer
    {
    public:
        CHeadlessUIContainer() = default;
        ~CHeadlessUIContainer() override = default;

        CHeadlessUIContainer(const CHeadlessUIContainer&) = delete;
        CHeadlessUIContainer& operator=(const CHeadlessUIContainer&) = delete;

    public:
        void SetConfig(IN const iCAX::Frontend::CUIContainerConfig& Config_) override
        {
            std::lock_guard<std::mutex> _Lock(m_Mutex);
            if (m_bRunning)
            {
                throw std::logic_error("UIContainer config cannot be changed after start");
            }
            m_Config = Config_;
        }

        void Start() override
        {
            iCAX::Frontend::CUIContainerConfig _Config;
            {
                std::lock_guard<std::mutex> _Lock(m_Mutex);
                if (m_bRunning)
                {
                    return;
                }
                _Config = m_Config;
            }

            _RunApplicationProxyHandshake(_Config);

            std::lock_guard<std::mutex> _Lock(m_Mutex);
            m_bRunning = true;
        }

        void Stop() override
        {
            iCAX::Frontend::IFrontendBridge* _pBridge = nullptr;
            {
                std::lock_guard<std::mutex> _Lock(m_Mutex);
                if (!m_bRunning)
                {
                    return;
                }
                _pBridge = m_Config.pFrontendBridge;
                m_bRunning = false;
            }

            if (_pBridge)
            {
                _pBridge->SetMailHandler(nullptr);
            }
        }

        void WaitForExit() override
        {
        }

        bool IsRunning() const override
        {
            std::lock_guard<std::mutex> _Lock(m_Mutex);
            return m_bRunning;
        }

    private:
        static void _RunApplicationProxyHandshake(IN const iCAX::Frontend::CUIContainerConfig& Config_)
        {
            if (!Config_.pFrontendBridge)
            {
                throw std::logic_error("UIContainer requires a FrontendBridge");
            }
            if (!Config_.pFrontendBridge->IsAttached())
            {
                throw std::logic_error("UIContainer requires an attached FrontendBridge");
            }

            const auto _ApplicationChannelID = Config_.pFrontendBridge->GetApplicationChannelIDText();
            if (_ApplicationChannelID.empty())
            {
                throw std::logic_error("Application channel id cannot be empty");
            }

            iCAX::Frontend::CFrontendMailEnvelope _Request;
            _Request.ChannelID = _ApplicationChannelID;
            _Request.nID = kStartupHandshakeRequestID;
            _Request.nOriginID = 0;
            _Request.nTypeCode = kApplicationGetStateCommand;
            _Request.nStamp = 0;
            _Request.PayloadText = kEmptyObjectPayloadText;

            Config_.pFrontendBridge->PostMail(_Request);

            const auto _Timeout = std::chrono::milliseconds(Config_.nStartupHandshakeTimeoutMS);
            const auto _Deadline = std::chrono::steady_clock::now() + _Timeout;
            while (std::chrono::steady_clock::now() < _Deadline)
            {
                auto _Mails = Config_.pFrontendBridge->PollMails();
                for (const auto& _Mail : _Mails)
                {
                    if (_Mail.ChannelID == _ApplicationChannelID && _Mail.nOriginID == kStartupHandshakeRequestID)
                    {
                        _ValidateStartupResponse(_Mail);
                        return;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            throw std::runtime_error("ApplicationProxy startup handshake timed out");
        }

    private:
        mutable std::mutex m_Mutex;
        iCAX::Frontend::CUIContainerConfig m_Config;
        bool m_bRunning = false;
    };
}

class iCAX::Frontend::CUIContainerInstance::Impl final
{
public:
    IUIContainer* pContainer = nullptr;
    UIContainerDestroyFunction pDestroyFunction = nullptr;
};

iCAX::Frontend::CUIContainerInstance::CUIContainerInstance()
    : m_pImpl(std::make_unique<Impl>())
{
}

iCAX::Frontend::CUIContainerInstance::CUIContainerInstance(
    IN IUIContainer* pContainer_,
    IN UIContainerDestroyFunction pDestroyFunction_)
    : m_pImpl(std::make_unique<Impl>())
{
    m_pImpl->pContainer = pContainer_;
    m_pImpl->pDestroyFunction = pDestroyFunction_;
}

iCAX::Frontend::CUIContainerInstance::~CUIContainerInstance()
{
    Reset();
}

void iCAX::Frontend::CUIContainerInstance::Reset() noexcept
{
    if (!m_pImpl)
    {
        return;
    }
    if (m_pImpl->pContainer)
    {
        auto _pContainer = std::exchange(m_pImpl->pContainer, nullptr);
        auto _pDestroyFunction = std::exchange(m_pImpl->pDestroyFunction, nullptr);
        if (_pDestroyFunction)
        {
            _pDestroyFunction(_pContainer);
        }
    }
}

iCAX::Frontend::CUIContainerInstance::CUIContainerInstance(CUIContainerInstance&& Right_) noexcept
    : m_pImpl(std::move(Right_.m_pImpl))
{
}

iCAX::Frontend::CUIContainerInstance& iCAX::Frontend::CUIContainerInstance::operator=(CUIContainerInstance&& Right_) noexcept
{
    if (this == &Right_)
    {
        return *this;
    }
    Reset();
    m_pImpl = std::move(Right_.m_pImpl);
    return *this;
}

iCAX::Frontend::IUIContainer& iCAX::Frontend::CUIContainerInstance::Get() const
{
    if (!m_pImpl || !m_pImpl->pContainer)
    {
        throw std::logic_error("UIContainer instance is empty");
    }
    return *m_pImpl->pContainer;
}

iCAX::Frontend::IUIContainer* iCAX::Frontend::CUIContainerInstance::operator->() const
{
    return &Get();
}

bool iCAX::Frontend::CUIContainerInstance::IsValid() const noexcept
{
    return m_pImpl && m_pImpl->pContainer;
}

bool iCAX::Frontend::CUIContainerFactory::Register(IN const CUIContainerRegistration& Registration_)
{
    auto _Type = _ToLower(_Trim(Registration_.ContainerType));
    if (_Type.empty())
    {
        throw std::invalid_argument("UI container type cannot be empty");
    }
    if (!Registration_.pCreateFunction || !Registration_.pDestroyFunction)
    {
        throw std::invalid_argument("UI container registration requires create and destroy functions: " + _Type);
    }

    auto& _RegistryRef = _Registry();
    std::lock_guard<std::mutex> _Lock(_RegistryRef.Mutex);
    auto _Registration = Registration_;
    _Registration.ContainerType = _Type;
    return _RegistryRef.Registrations.emplace(_Type, _Registration).second;
}

std::vector<std::string> iCAX::Frontend::CUIContainerFactory::ListRegisteredTypes()
{
    auto& _RegistryRef = _Registry();
    std::lock_guard<std::mutex> _Lock(_RegistryRef.Mutex);
    std::vector<std::string> _Types;
    _Types.reserve(_RegistryRef.Registrations.size());
    for (const auto& _Item : _RegistryRef.Registrations)
    {
        _Types.push_back(_Item.first);
    }
    return _Types;
}

int iCAX::Frontend::CUIContainerFactory::ExecuteSubProcessIfNeeded(
    IN const CUIContainerConfig& Config_,
    IN void* pNativeInstanceHandle_)
{
    auto _Type = _ToLower(_Trim(Config_.ContainerType));
    if (_Type.empty() || _Type == "headless")
    {
        return -1;
    }

    _LoadRegistrationModule(_Type, Config_);
    auto _Registration = _RequireRegistration(_Type);
    if (!_Registration.pExecuteSubProcessFunction)
    {
        return -1;
    }

    return _Registration.pExecuteSubProcessFunction(pNativeInstanceHandle_);
}

iCAX::Frontend::CUIContainerInstance iCAX::Frontend::CUIContainerFactory::Create(IN const CUIContainerConfig& Config_)
{
    auto _Type = _ToLower(_Trim(Config_.ContainerType));
    if (_Type.empty())
    {
        _Type = "headless";
    }

    _LoadRegistrationModule(_Type, Config_);
    auto _Registration = _RequireRegistration(_Type);

    auto _pContainer = _Registration.pCreateFunction();
    if (!_pContainer)
    {
        throw std::runtime_error("UI container factory returned null: " + _Type);
    }

    try
    {
        _pContainer->SetConfig(Config_);
    }
    catch (...)
    {
        _Registration.pDestroyFunction(_pContainer);
        throw;
    }

    return CUIContainerInstance(_pContainer, _Registration.pDestroyFunction);
}

iCAX::Frontend::CUIContainerRegistrar::CUIContainerRegistrar(IN const CUIContainerRegistration& Registration_)
{
    if (!CUIContainerFactory::Register(Registration_))
    {
        throw std::runtime_error("UI container type is already registered: " + Registration_.ContainerType);
    }
}

ICAX_REGISTER_UI_CONTAINER("headless", CHeadlessUIContainer)
