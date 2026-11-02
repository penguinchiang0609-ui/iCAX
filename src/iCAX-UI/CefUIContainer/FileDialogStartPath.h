#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace iCAX::Frontend::Cef::Detail
{
    struct CFileDialogStartPath
    {
        std::wstring FileName;
        std::wstring InitialDirectory;
    };

    inline std::wstring ExistingDialogDirectory(std::filesystem::path Path_)
    {
        std::error_code _Error;
        if (Path_.empty() || !std::filesystem::is_directory(Path_, _Error)) return {};
        auto _Absolute = std::filesystem::absolute(Path_, _Error);
        return _Error ? std::wstring{} : _Absolute.make_preferred().wstring();
    }

    inline CFileDialogStartPath ResolveFileDialogStartPath(
        std::wstring DefaultPath_, std::wstring InitialDirectory_)
    {
        std::replace(DefaultPath_.begin(), DefaultPath_.end(), L'/', L'\\');
        std::replace(InitialDirectory_.begin(), InitialDirectory_.end(), L'/', L'\\');
        CFileDialogStartPath _Result;
        _Result.InitialDirectory = ExistingDialogDirectory(InitialDirectory_);
        if (DefaultPath_.empty()) return _Result;

        const std::filesystem::path _Path(DefaultPath_);
        auto _Directory = ExistingDialogDirectory(_Path);
        if (!_Directory.empty())
        {
            _Result.InitialDirectory = std::move(_Directory);
            return _Result;
        }

        // A remembered directory is a navigation hint, never a file name.
        // Removed folders and unavailable drives must not prevent the chooser
        // from opening. Keep a suggested save name even if its folder is gone.
        _Result.FileName = _Path.filename().wstring();
        _Directory = ExistingDialogDirectory(_Path.parent_path());
        if (!_Directory.empty()) _Result.InitialDirectory = std::move(_Directory);
        return _Result;
    }
}
