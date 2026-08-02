#include "pch.h"

#include "ProjectMigrationRegistration.h"

namespace
{
    std::mutex& RegistrationMutex()
    {
        static std::mutex _Mutex;
        return _Mutex;
    }

    std::string NormalizeModulePath(IN const std::string& Path_)
    {
        if (Path_.empty())
        {
            return {};
        }
        const auto _Required = ::GetFullPathNameA(
            Path_.c_str(), 0, nullptr, nullptr);
        if (_Required == 0)
        {
            return Path_;
        }
        std::vector<char> _Buffer(
            static_cast<size_t>(_Required) + 1);
        const auto _Length = ::GetFullPathNameA(
            Path_.c_str(),
            static_cast<DWORD>(_Buffer.size()),
            _Buffer.data(),
            nullptr);
        if (_Length == 0 || _Length >= _Buffer.size())
        {
            return Path_;
        }
        std::string _Result(_Buffer.data(), _Length);
        std::replace(_Result.begin(), _Result.end(), '/', '\\');
        std::transform(
            _Result.begin(),
            _Result.end(),
            _Result.begin(),
            [](const unsigned char Character_)
            {
                return static_cast<char>(
                    std::tolower(Character_));
            });
        return _Result;
    }

    std::string ModulePathFromAddress(IN const void* pAddress_)
    {
        if (!pAddress_)
        {
            return {};
        }
        HMODULE _Module = nullptr;
        if (!::GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(pAddress_),
                &_Module))
        {
            return {};
        }
        std::vector<char> _Buffer(MAX_PATH);
        while (true)
        {
            const auto _Length = ::GetModuleFileNameA(
                _Module,
                _Buffer.data(),
                static_cast<DWORD>(_Buffer.size()));
            if (_Length == 0)
            {
                return {};
            }
            if (_Length < _Buffer.size() - 1)
            {
                return NormalizeModulePath(
                    std::string(_Buffer.data(), _Length));
            }
            _Buffer.resize(_Buffer.size() * 2);
        }
    }
}

void iCAX::ProjectFile::CProjectMigrationRegistrationCatalog::Register(
    IN ReplayFunc Func_,
    IN const void* pModuleAddress_)
{
    if (!Func_)
    {
        throw std::invalid_argument(
            "Project migration replay function is empty");
    }
    std::lock_guard<std::mutex> _Lock(RegistrationMutex());
    GetRegistrations().push_back({
        ModulePathFromAddress(pModuleAddress_),
        std::move(Func_)});
}

void iCAX::ProjectFile::CProjectMigrationRegistrationCatalog::ReplayAll(
    IN CProjectMigrationRegistry& Registry_)
{
    std::vector<CRegistrationRecord> _Registrations;
    {
        std::lock_guard<std::mutex> _Lock(RegistrationMutex());
        _Registrations = GetRegistrations();
    }
    for (const auto& _Registration : _Registrations)
    {
        _Registration.Replay(Registry_);
    }
}

void iCAX::ProjectFile::CProjectMigrationRegistrationCatalog::
ReplayByModulePaths(
    IN CProjectMigrationRegistry& Registry_,
    IN const std::vector<std::string>& ModulePaths_)
{
    std::set<std::string> _Paths;
    for (const auto& _Path : ModulePaths_)
    {
        const auto _Normalized = NormalizeModulePath(_Path);
        if (!_Normalized.empty())
        {
            _Paths.insert(_Normalized);
        }
    }
    if (_Paths.empty())
    {
        return;
    }

    std::vector<CRegistrationRecord> _Registrations;
    {
        std::lock_guard<std::mutex> _Lock(RegistrationMutex());
        _Registrations = GetRegistrations();
    }
    for (const auto& _Registration : _Registrations)
    {
        if (_Paths.contains(_Registration.ModulePath))
        {
            _Registration.Replay(Registry_);
        }
    }
}

size_t iCAX::ProjectFile::
CProjectMigrationRegistrationCatalog::Count()
{
    std::lock_guard<std::mutex> _Lock(RegistrationMutex());
    return GetRegistrations().size();
}

std::vector<iCAX::ProjectFile::
CProjectMigrationRegistrationCatalog::CRegistrationRecord>&
iCAX::ProjectFile::CProjectMigrationRegistrationCatalog::
GetRegistrations()
{
    static std::vector<CRegistrationRecord> _Registrations;
    return _Registrations;
}
