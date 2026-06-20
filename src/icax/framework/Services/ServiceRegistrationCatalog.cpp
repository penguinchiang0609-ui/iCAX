#include "pch.h"
#include "ServiceRegistrationCatalog.h"

#include "ServiceProvider.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <utility>

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

        char _Buffer[MAX_PATH]{};
        const auto _Length = GetFullPathNameA(strPath_.c_str(), MAX_PATH, _Buffer, nullptr);
        if (_Length == 0 || _Length >= MAX_PATH)
        {
            return strPath_;
        }
        return std::string(_Buffer, _Length);
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

        char _Buffer[MAX_PATH]{};
        const auto _Length = GetModuleFileNameA(_Module, _Buffer, MAX_PATH);
        if (_Length == 0 || _Length >= MAX_PATH)
        {
            return {};
        }
        return NormalizeModulePath(std::string(_Buffer, _Length));
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
