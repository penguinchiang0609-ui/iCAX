#include "pch.h"
#include "BehaviourRegistrationCatalog.h"

#include "IBehaviourRegistry.h"

#include <filesystem>


namespace
{
    std::mutex& GetRegistrationMutex()
    {
        static std::mutex _Mutex;
        return _Mutex;
    }

    std::string NormalizeModulePath(IN const std::string& strPath_)
    {
        if (strPath_.empty())
        {
            return {};
        }

        const auto _WidePath = std::filesystem::path(std::u8string(strPath_.begin(), strPath_.end())).wstring();
        const auto _RequiredLength = GetFullPathNameW(_WidePath.c_str(), 0, nullptr, nullptr);
        if (_RequiredLength == 0)
        {
            return strPath_;
        }
        std::vector<wchar_t> _Buffer(static_cast<size_t>(_RequiredLength) + 1);
        const auto _Length = GetFullPathNameW(_WidePath.c_str(), static_cast<DWORD>(_Buffer.size()), _Buffer.data(), nullptr);
        if (_Length == 0 || _Length >= _Buffer.size())
        {
            return strPath_;
        }

        const auto _PathText = std::filesystem::path(std::wstring(_Buffer.data(), _Length)).u8string();
        std::string _Normalized(_PathText.begin(), _PathText.end());
        std::replace(_Normalized.begin(), _Normalized.end(), '/', '\\');
        std::transform(
            _Normalized.begin(),
            _Normalized.end(),
            _Normalized.begin(),
            [](IN const char ch_) { return ch_ >= 'A' && ch_ <= 'Z' ? static_cast<char>(ch_ - 'A' + 'a') : ch_; });
        return _Normalized;
    }

    std::string GetModulePathFromAddress(IN const void* pAddress_)
    {
        if (!pAddress_)
        {
            return {};
        }

        HMODULE _Module = nullptr;
        if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(pAddress_),
            &_Module))
        {
            return {};
        }

        std::vector<wchar_t> _Buffer(MAX_PATH);
        while (true)
        {
            const auto _Length = GetModuleFileNameW(_Module, _Buffer.data(), static_cast<DWORD>(_Buffer.size()));
            if (_Length == 0)
            {
                return {};
            }
            if (_Length < _Buffer.size() - 1)
            {
                const auto _PathText = std::filesystem::path(std::wstring(_Buffer.data(), _Length)).u8string();
                return NormalizeModulePath(std::string(_PathText.begin(), _PathText.end()));
            }
            _Buffer.resize(_Buffer.size() * 2);
        }
    }

    std::vector<std::string> NormalizeModulePaths(IN const std::vector<std::string>& ModulePaths_)
    {
        std::vector<std::string> _Result;
        _Result.reserve(ModulePaths_.size());
        for (const auto& _Path : ModulePaths_)
        {
            auto _Normalized = NormalizeModulePath(_Path);
            if (!_Normalized.empty() && std::find(_Result.begin(), _Result.end(), _Normalized) == _Result.end())
            {
                _Result.push_back(std::move(_Normalized));
            }
        }
        return _Result;
    }
}

void iCAX::Behaviour::CBehaviourRegistrationCatalog::Register(IN ReplayFunc Func_)
{
    Register(std::move(Func_), nullptr);
}

void iCAX::Behaviour::CBehaviourRegistrationCatalog::Register(IN ReplayFunc Func_, IN const void* pModuleAddress_)
{
    if (!Func_)
    {
        throw std::invalid_argument("Behaviour registration replay function cannot be empty");
    }

    std::lock_guard<std::mutex> _Lock(GetRegistrationMutex());
    GetRegistrations().push_back({ GetModulePathFromAddress(pModuleAddress_), std::move(Func_) });
}

void iCAX::Behaviour::CBehaviourRegistrationCatalog::ReplayAll(IN IBehaviourRegistry& Registry_)
{
    (void)ReplayFrom(0, Registry_);
}

size_t iCAX::Behaviour::CBehaviourRegistrationCatalog::ReplayFrom(IN size_t nFirstIndex_, IN IBehaviourRegistry& Registry_)
{
    std::vector<RegistrationRecord> _Registrations;
    {
        std::lock_guard<std::mutex> _Lock(GetRegistrationMutex());
        const auto& _AllRegistrations = GetRegistrations();
        if (nFirstIndex_ > _AllRegistrations.size())
        {
            nFirstIndex_ = _AllRegistrations.size();
        }
        _Registrations.assign(_AllRegistrations.begin() + nFirstIndex_, _AllRegistrations.end());
        nFirstIndex_ = _AllRegistrations.size();
    }

    // 回放前释放 catalog 锁，避免注册 Behaviour 时又触发模块加载或注册表访问造成死锁。
    for (const auto& _Registration : _Registrations)
    {
        _Registration.Replay(Registry_);
    }
    return nFirstIndex_;
}

void iCAX::Behaviour::CBehaviourRegistrationCatalog::ReplayByModulePaths(
    IN IBehaviourRegistry& Registry_,
    IN const std::vector<std::string>& ModulePaths_)
{
    const auto _ModulePaths = NormalizeModulePaths(ModulePaths_);
    if (_ModulePaths.empty())
    {
        return;
    }

    std::vector<RegistrationRecord> _Registrations;
    {
        std::lock_guard<std::mutex> _Lock(GetRegistrationMutex());
        _Registrations = GetRegistrations();
    }

    // ProductRuntime 只回放本产品模块贡献的 Behaviour，以实现产品级行为隔离。
    for (const auto& _Registration : _Registrations)
    {
        if (std::find(_ModulePaths.begin(), _ModulePaths.end(), _Registration.ModulePath) != _ModulePaths.end())
        {
            _Registration.Replay(Registry_);
        }
    }
}

size_t iCAX::Behaviour::CBehaviourRegistrationCatalog::Count()
{
    std::lock_guard<std::mutex> _Lock(GetRegistrationMutex());
    return GetRegistrations().size();
}

std::vector<iCAX::Behaviour::CBehaviourRegistrationCatalog::RegistrationRecord>& iCAX::Behaviour::CBehaviourRegistrationCatalog::GetRegistrations()
{
    static std::vector<RegistrationRecord> _Registrations;
    return _Registrations;
}
