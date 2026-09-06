#include "pch.h"
#include "ApplicationPaths.h"

namespace
{
    std::filesystem::path _ReadPathEnvironmentVariable(IN const wchar_t* pName_)
    {
        std::size_t _Size = 0;
        if (_wgetenv_s(&_Size, nullptr, 0, pName_) != 0 || _Size <= 1) return {};
        std::wstring _Value(_Size, L'\0');
        if (_wgetenv_s(&_Size, _Value.data(), _Value.size(), pName_) != 0) return {};
        if (!_Value.empty() && _Value.back() == L'\0') _Value.pop_back();
        return _Value.empty() ? std::filesystem::path() : std::filesystem::path(_Value);
    }

    std::string _PathToUTF8(IN const std::filesystem::path& Path_)
    {
        const auto _Text = Path_.u8string();
        return std::string(_Text.begin(), _Text.end());
    }
}

std::string iCAX::Application::ResolveDefaultUserDataDirectory()
{
    auto _Root = _ReadPathEnvironmentVariable(L"ICAX_USER_DATA_ROOT");
    if (_Root.empty())
    {
        const auto _LocalAppData = _ReadPathEnvironmentVariable(L"LOCALAPPDATA");
        _Root = _LocalAppData.empty()
            ? std::filesystem::current_path() / "UserData" / "iCAX"
            : _LocalAppData / "iCAX";
    }
    if (_Root.is_relative()) _Root = std::filesystem::absolute(_Root);
    return _PathToUTF8(_Root.lexically_normal());
}
