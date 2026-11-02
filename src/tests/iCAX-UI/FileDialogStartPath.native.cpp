#include "../../iCAX-UI/CefUIContainer/FileDialogStartPath.h"

#include <windows.h>
#include <commdlg.h>
#include <dlgs.h>
#include <objbase.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;
using iCAX::Frontend::Cef::Detail::CFileDialogStartPath;
using iCAX::Frontend::Cef::Detail::ResolveFileDialogStartPath;

namespace
{
    constexpr UINT kSelectFile = WM_APP + 51;
    constexpr UINT_PTR kCancelTimer = 44153;
    int gChecks = 0;

    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
        ++gChecks;
    }

    std::wstring ForwardSlashes(std::wstring path)
    {
        std::replace(path.begin(), path.end(), L'\\', L'/');
        return path;
    }

    struct HookState
    {
        bool Shown = false;
        bool TimedOut = false;
        int SelectAttempts = 0;
        std::wstring SelectPath;
    };

    UINT_PTR CALLBACK DialogHook(HWND window, UINT message, WPARAM, LPARAM parameter)
    {
        auto* state = reinterpret_cast<HookState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_INITDIALOG)
        {
            state = reinterpret_cast<HookState*>(reinterpret_cast<OPENFILENAMEW*>(parameter)->lCustData);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            state->Shown = true;
            if (state->SelectPath.empty())
                PostMessageW(GetParent(window), WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
            else
                SetTimer(window, kCancelTimer, 5000, nullptr);
        }
        else if (state && message == WM_NOTIFY
            && (reinterpret_cast<OFNOTIFYW*>(parameter)->hdr.code == CDN_INITDONE
                || (reinterpret_cast<OFNOTIFYW*>(parameter)->hdr.code == CDN_FOLDERCHANGE
                    && state->SelectAttempts > 0))
            && !state->SelectPath.empty())
        {
            PostMessageW(window, kSelectFile, 0, 0);
        }
        else if (state && message == kSelectFile)
        {
            if (++state->SelectAttempts > 3) return 0;
            SendMessageW(GetParent(window), CDM_SETCONTROLTEXT, edt1,
                reinterpret_cast<LPARAM>(state->SelectPath.c_str()));
            PostMessageW(GetParent(window), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
        }
        else if (state && message == WM_TIMER)
        {
            state->TimedOut = true;
            KillTimer(window, kCancelTimer);
            PostMessageW(GetParent(window), WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
        }
        return 0;
    }

    struct DialogResult
    {
        bool Selected;
        DWORD Error;
        bool Shown;
        bool TimedOut;
        std::wstring Path;
    };

    DialogResult RunDialog(const CFileDialogStartPath& start, bool save,
        const std::wstring& selectPath = {})
    {
        std::vector<wchar_t> buffer(32768, L'\0');
        Check(start.FileName.size() < buffer.size(), "File name exceeds dialog buffer");
        std::copy(start.FileName.begin(), start.FileName.end(), buffer.begin());
        HookState state{false, false, 0, selectPath};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.lpstrFilter = L"Authorization (*.tdact)\0*.tdact\0All files (*.*)\0*.*\0\0";
        dialog.lpstrFile = buffer.data();
        dialog.nMaxFile = static_cast<DWORD>(buffer.size());
        dialog.lpstrInitialDir = start.InitialDirectory.empty() ? nullptr : start.InitialDirectory.c_str();
        dialog.lpstrDefExt = L"tdact";
        dialog.nFilterIndex = 1;
        dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_ENABLEHOOK
            | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        dialog.lpfnHook = DialogHook;
        dialog.lCustData = reinterpret_cast<LPARAM>(&state);
        const bool selected = (save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog)) != FALSE;
        const auto error = selected ? 0u : CommDlgExtendedError();
        return {selected, error, state.Shown, state.TimedOut, buffer.data()};
    }

    void CheckCancel(const char* name, const std::wstring& defaultPath,
        const std::wstring& initialDirectory, bool save = false)
    {
        const auto result = RunDialog(ResolveFileDialogStartPath(defaultPath, initialDirectory), save);
        std::cout << name << ": selected=" << result.Selected << " error=" << result.Error
            << " shown=" << result.Shown << std::endl;
        Check(result.Shown && !result.Selected && !result.TimedOut && result.Error == 0,
            "Dialog failed before ordinary cancellation");
    }

    void CheckSelect(const char* name, const std::wstring& initialDirectory, const fs::path& fixture)
    {
        const auto result = RunDialog(ResolveFileDialogStartPath({}, initialDirectory), false, fixture.wstring());
        std::cout << name << ": selected=" << result.Selected << " error=" << result.Error
            << " shown=" << result.Shown << std::endl;
        Check(result.Shown && result.Selected && !result.TimedOut && result.Error == 0,
            "Selecting an authorization fixture failed");
        Check(fs::equivalent(fs::path(result.Path), fixture), "Dialog returned a different selected file");
    }
}

int wmain()
{
    const auto comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    try
    {
        Check(SUCCEEDED(comResult), "COM initialization failed");
        wchar_t executable[MAX_PATH]{};
        Check(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "Executable path unavailable");
        const auto fixtures = fs::path(executable).parent_path() / L"fixtures";
        const auto chinese = fixtures / L"客户授权";
        fs::create_directories(chinese);
        const auto fixture = chinese / L"授权测试.tdact";
        std::ofstream stream(fixture, std::ios::binary | std::ios::trunc);
        stream << "file-dialog-test-fixture\n";
        stream.close();
        Check(fs::is_regular_file(fixture), "Fixture creation failed");
        const auto missing = fixtures / L"removed-folder";
        Check(!fs::exists(missing), "Missing-folder fixture must remain absent");

        wchar_t unavailableDrive = L'Z';
        const auto drives = GetLogicalDrives();
        while (unavailableDrive >= L'D' && (drives & (1u << (unavailableDrive - L'A')))) --unavailableDrive;
        Check(unavailableDrive >= L'D', "No unavailable drive is available for regression");
        const std::wstring unavailable = std::wstring(1, unavailableDrive) + L":\\Customer\\License";
        const std::wstring invalid = L"C:\\invalid<folder>\\License";

        auto start = ResolveFileDialogStartPath({}, ForwardSlashes(chinese.wstring()));
        Check(start.FileName.empty(), "Remembered directory became a file name");
        Check(fs::equivalent(fs::path(start.InitialDirectory), chinese), "Chinese initial directory was lost");
        Check(start.InitialDirectory.find(L'/') == std::wstring::npos, "Forward slashes were not normalized");
        start = ResolveFileDialogStartPath(ForwardSlashes(chinese.wstring()) + L"/", {});
        Check(start.FileName.empty() && fs::equivalent(fs::path(start.InitialDirectory), chinese),
            "Existing directory default was not separated from the file name");
        start = ResolveFileDialogStartPath(ForwardSlashes(fixture.wstring()), {});
        Check(start.FileName == fixture.filename().wstring(), "Open-file default basename was lost");
        Check(fs::equivalent(fs::path(start.InitialDirectory), chinese), "Open-file parent was lost");
        start = ResolveFileDialogStartPath((missing / L"saved-license.tdact").wstring(), {});
        Check(start.FileName == L"saved-license.tdact" && start.InitialDirectory.empty(),
            "Suggested save name was lost when its folder disappeared");
        start = ResolveFileDialogStartPath(L"saved-license.tdact", ForwardSlashes(chinese.wstring()));
        Check(start.FileName == L"saved-license.tdact"
            && fs::equivalent(fs::path(start.InitialDirectory), chinese),
            "Explicit initial directory or save basename was lost");
        for (const auto& directory : {missing.wstring(), unavailable, invalid})
        {
            start = ResolveFileDialogStartPath({}, ForwardSlashes(directory));
            Check(start.FileName.empty() && start.InitialDirectory.empty(),
                "An invalid or unavailable remembered directory was retained");
        }

        // Diagnostic reproduction of the original lpstrFile assignment. It is
        // intentionally not asserted because Windows may accept this on newer builds.
        const auto baseline = RunDialog({ForwardSlashes(chinese.wstring()), {}}, false);
        std::cout << "original_forward_slash_directory: error=" << baseline.Error
            << " shown=" << baseline.Shown << std::endl;

        CheckCancel("empty_open", {}, {});
        CheckCancel("existing_directory_default", ForwardSlashes(chinese.wstring()), {});
        CheckCancel("existing_directory_default_trailing", chinese.wstring() + L"\\", {});
        CheckCancel("existing_file_default", ForwardSlashes(fixture.wstring()), {});
        CheckCancel("chinese_remembered_slash", {}, ForwardSlashes(chinese.wstring()));
        CheckCancel("removed_remembered_backslash", {}, missing.wstring());
        CheckCancel("removed_remembered_slash", {}, ForwardSlashes(missing.wstring()));
        CheckCancel("unavailable_drive_backslash", {}, unavailable);
        CheckCancel("unavailable_drive_slash", {}, ForwardSlashes(unavailable));
        CheckCancel("invalid_remembered_directory", {}, invalid);
        CheckCancel("save_basename", L"customer-license.tdact", ForwardSlashes(chinese.wstring()), true);
        CheckCancel("save_missing_parent", ForwardSlashes((missing / L"customer-license.tdact").wstring()), {}, true);
        CheckCancel("save_unavailable_drive", ForwardSlashes(unavailable + L"\\customer-license.tdact"), {}, true);
        CheckSelect("select_from_removed_directory", ForwardSlashes(missing.wstring()), fixture);
        CheckSelect("select_from_unavailable_drive", ForwardSlashes(unavailable), fixture);
        CheckSelect("select_from_wrong_existing_directory", fixtures.wstring(), fixture);

        std::cout << "PASS: " << gChecks << " checks; 16 real Windows dialogs" << std::endl;
        CoUninitialize();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << std::endl;
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 1;
    }
}
