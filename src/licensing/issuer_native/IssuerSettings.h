#pragma once
#include "LicenseCore.h"
#include <filesystem>

namespace tube::license::issuer_native {
struct Settings {
    std::filesystem::path storeDirectory;
    Bytes password;
    std::wstring message;
    bool remembered{};
};
std::wstring Utf8ToWide(std::string_view value);
std::string WideToUtf8(std::wstring_view value);
std::filesystem::path DiscoverStore(const ProductDescriptor& product);
Settings LoadSettings(const ProductDescriptor& product);
void SaveSettings(const ProductDescriptor& product, const std::filesystem::path& directory,
    std::span<const unsigned char> password, bool remember);
}
