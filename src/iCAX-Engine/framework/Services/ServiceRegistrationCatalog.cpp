#include "pch.h"
#include "ServiceRegistrationCatalog.h"

#include "ServiceProvider.h"



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

        const auto _RequiredLength = GetFullPathNameA(strPath_.c_str(), 0, nullptr, nullptr);
        if (_RequiredLength == 0)
        {
            return strPath_;
        }

        std::vector<char> _Buffer(static_cast<size_t>(_RequiredLength) + 1);
        const auto _Length = GetFullPathNameA(strPath_.c_str(), static_cast<DWORD>(_Buffer.size()), _Buffer.data(), nullptr);
        if (_Length == 0 || _Length >= _Buffer.size())
        {
            return strPath_;
        }

        std::string _Normalized(_Buffer.data(), _Length);
        std::replace(_Normalized.begin(), _Normalized.end(), '/', '\\');
        std::transform(
            _Normalized.begin(),
            _Normalized.end(),
            _Normalized.begin(),
            [](IN const unsigned char ch_) { return static_cast<char>(std::tolower(ch_)); });
        return _Normalized;
    }

    std::string GetModulePathFromAddress(IN const void* pAddress_)
    {
        if (!pAddress_)
        {
            return {};
        }

        HMODULE _Module = nullptr;
        if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(pAddress_),
            &_Module))
        {
            return {};
        }

        std::vector<char> _Buffer(MAX_PATH);
        while (true)
        {
            const auto _Length = GetModuleFileNameA(_Module, _Buffer.data(), static_cast<DWORD>(_Buffer.size()));
            if (_Length == 0)
            {
                return {};
            }
            if (_Length < _Buffer.size() - 1)
            {
                return NormalizeModulePath(std::string(_Buffer.data(), _Length));
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

void iCAX::Services::CServiceRegistrationCatalog::Register(IN ReplayFunc Func_)
{
    Register(std::move(Func_), nullptr);
}

void iCAX::Services::CServiceRegistrationCatalog::Register(IN ReplayFunc Func_, IN const void* pModuleAddress_)
{
    if (!Func_)
    {
        throw std::invalid_argument("Service registration replay function cannot be empty");
    }

    std::lock_guard<std::mutex> _Lock(GetRegistrationMutex());
    GetRegistrations().push_back({ GetModulePathFromAddress(pModuleAddress_), std::move(Func_) });
}

void iCAX::Services::CServiceRegistrationCatalog::ReplayAll(IN CServiceProvider& Provider_)
{
    (void)ReplayFrom(0, Provider_);
}

size_t iCAX::Services::CServiceRegistrationCatalog::ReplayFrom(IN size_t nFirstIndex_, IN CServiceProvider& Provider_)
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

    // 回放函数可能继续触发服务解析或间接加载模块，因此不要在持有 catalog 锁时执行用户注册逻辑。
    for (const auto& _Registration : _Registrations)
    {
        _Registration.Replay(Provider_);
    }
    return nFirstIndex_;
}

void iCAX::Services::CServiceRegistrationCatalog::ReplayByModulePaths(
    IN CServiceProvider& Provider_,
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

    // 全局 catalog 保存所有模块的注册动作；ProductRuntime 只回放自己已加载模块的记录，实现产品级隔离。
    for (const auto& _Registration : _Registrations)
    {
        if (std::find(_ModulePaths.begin(), _ModulePaths.end(), _Registration.ModulePath) != _ModulePaths.end())
        {
            _Registration.Replay(Provider_);
        }
    }
}

size_t iCAX::Services::CServiceRegistrationCatalog::Count()
{
    std::lock_guard<std::mutex> _Lock(GetRegistrationMutex());
    return GetRegistrations().size();
}

std::vector<iCAX::Services::CServiceRegistrationCatalog::RegistrationRecord>& iCAX::Services::CServiceRegistrationCatalog::GetRegistrations()
{
    static std::vector<RegistrationRecord> _Registrations;
    return _Registrations;
}
