#include "pch.h"
#include "ResourceLoaderRegistrationCatalog.h"

#include "ResourceLoaderRegistry.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#include <Windows.h>

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

void iCAX::Resource::CResourceLoaderRegistrationCatalog::Register(IN ReplayFunc Func_)
{
    Register(std::move(Func_), nullptr);
}

void iCAX::Resource::CResourceLoaderRegistrationCatalog::Register(IN ReplayFunc Func_, IN const void* pModuleAddress_)
{
    if (!Func_)
    {
        throw std::invalid_argument("Resource loader registration replay function cannot be empty");
    }

    std::lock_guard<std::mutex> _Lock(GetRegistrationMutex());
    GetRegistrations().push_back({ GetModulePathFromAddress(pModuleAddress_), std::move(Func_) });
}

void iCAX::Resource::CResourceLoaderRegistrationCatalog::ReplayAll(IN CResourceLoaderRegistry& Registry_)
{
    (void)ReplayFrom(0, Registry_);
}

size_t iCAX::Resource::CResourceLoaderRegistrationCatalog::ReplayFrom(IN size_t nFirstIndex_, IN CResourceLoaderRegistry& Registry_)
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

    // 回放用户注册函数前释放 catalog 锁，避免注册函数内部再次触发 catalog 查询或模块加载。
    for (const auto& _Registration : _Registrations)
    {
        _Registration.Replay(Registry_);
    }
    return nFirstIndex_;
}

void iCAX::Resource::CResourceLoaderRegistrationCatalog::ReplayByModulePaths(
    IN CResourceLoaderRegistry& Registry_,
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

    // ProductRuntime 把已加载模块路径传进来，只回放本产品模块贡献的 loader。
    for (const auto& _Registration : _Registrations)
    {
        if (std::find(_ModulePaths.begin(), _ModulePaths.end(), _Registration.ModulePath) != _ModulePaths.end())
        {
            _Registration.Replay(Registry_);
        }
    }
}

size_t iCAX::Resource::CResourceLoaderRegistrationCatalog::Count()
{
    std::lock_guard<std::mutex> _Lock(GetRegistrationMutex());
    return GetRegistrations().size();
}

std::vector<iCAX::Resource::CResourceLoaderRegistrationCatalog::RegistrationRecord>& iCAX::Resource::CResourceLoaderRegistrationCatalog::GetRegistrations()
{
    static std::vector<RegistrationRecord> _Registrations;
    return _Registrations;
}
