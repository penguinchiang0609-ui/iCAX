#pragma once
#include "LicenseBundle.h"
#include <filesystem>
#include <commctrl.h>
#include <shlobj.h>

namespace tube::license {
struct FileHandle final {
    HANDLE value{INVALID_HANDLE_VALUE};
    explicit FileHandle(HANDLE h) : value(h) { Require(h != INVALID_HANDLE_VALUE, "Cannot access license file"); }
    FileHandle(const FileHandle&) = delete;
    ~FileHandle() { CloseHandle(value); }
};
inline Bytes ReadFileBounded(const std::filesystem::path& path, DWORD maximum) {
    FileHandle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    LARGE_INTEGER size{}; Require(GetFileSizeEx(file.value, &size) && size.QuadPart > 0 && size.QuadPart <= maximum, "Invalid file size");
    Bytes bytes(static_cast<std::size_t>(size.QuadPart)); DWORD read{};
    Require(ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size(), "Cannot read license file");
    return bytes;
}
inline void WriteNewFile(const std::filesystem::path& path, std::span<const unsigned char> bytes,
    DWORD maximum = 65536) {
    Require(maximum <= MaxAuthorizationFileBytes && bytes.size() <= maximum, "Output too large");
    FileHandle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)); DWORD written{};
    Require(WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size()
        && FlushFileBuffers(file.value), "Cannot commit license file");
}
inline std::filesystem::path LicenseDirectory() {
    PWSTR raw{}; Require(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &raw)), "Cannot locate application data");
    const std::filesystem::path root(raw); CoTaskMemFree(raw);
    return root / L"TubeDesigner" / L"Licensing";
}
inline void InstallCertificateAtDirectory(const Bytes& certificate, const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    const auto nonce = RandomChallenge(); std::wstring suffix;
    constexpr wchar_t hex[] = L"0123456789abcdef";
    for (auto b : nonce) { suffix += hex[b >> 4]; suffix += hex[b & 15]; }
    const auto temporary = folder / (L"activate-" + suffix + L".tmp");
    WriteNewFile(temporary, certificate);
    // Validation happens before this function. Atomic replacement leaves either
    // the old signed certificate or the new signed certificate, never partial data.
    if (!MoveFileExW(temporary.c_str(), (folder / L"license.tdlic").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str()); throw std::runtime_error("Cannot install certificate");
    }
}
inline void InstallCertificate(const Bytes& certificate) {
    InstallCertificateAtDirectory(certificate, LicenseDirectory());
}
inline std::filesystem::path AuthorizationSourcePath(const Certificate& certificate,
    const std::filesystem::path& folder) {
    std::wstring name = L"authorization-";
    constexpr wchar_t digits[] = L"0123456789abcdef";
    for (const auto byte : certificate.bodyDigest) { name += digits[byte >> 4]; name += digits[byte & 15]; }
    return folder / (name + L".tdact");
}
inline void InstallAuthorizationSourceAtDirectory(std::span<const unsigned char> file,
    const Certificate& certificate, const std::filesystem::path& folder) {
    Require(!file.empty() && file.size() <= MaxAuthorizationFileBytes, "Invalid authorization source size");
    std::filesystem::create_directories(folder);
    const auto target = AuthorizationSourcePath(certificate, folder);
    const auto nonce = RandomChallenge(); std::wstring suffix;
    constexpr wchar_t digits[] = L"0123456789abcdef";
    for (const auto byte : nonce) { suffix += digits[byte >> 4]; suffix += digits[byte & 15]; }
    const auto temporary = folder / (L"source-" + suffix + L".tmp");
    try {
        WriteNewFile(temporary, file, static_cast<DWORD>(MaxAuthorizationFileBytes));
        Require(MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH),
            "Cannot preserve the complete authorization file");
    } catch (...) { DeleteFileW(temporary.c_str()); throw; }
}
inline void RequireCertificateReplacementAtDirectory(const Bytes& certificate,
    const std::filesystem::path& folder, std::string_view issuer,
    std::span<const unsigned char> trustedKey) {
    const auto next = VerifyCertificate(certificate, issuer, trustedKey);
    const auto installed = folder / L"license.tdlic";
    if (std::filesystem::exists(installed)) {
        const auto previous = VerifyCertificate(ReadFileBounded(installed,
            static_cast<DWORD>(MaxCertificateBytes)), issuer, trustedKey);
        RequireAuthorizationUpgrade(previous, next);
    }
}
}
