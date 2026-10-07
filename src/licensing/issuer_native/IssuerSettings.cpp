#include "IssuerSettings.h"
#include <wincrypt.h>
#include <shlobj.h>
#include <algorithm>
#include <fstream>
#include <map>

namespace tube::license::issuer_native {
std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) return {};
    Require(value.size() <= 1024 * 1024, "Text is too long");
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    Require(size > 0, "Invalid UTF-8 text");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    Require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size) == size, "Cannot decode UTF-8");
    return result;
}
std::string WideToUtf8(std::wstring_view value) {
    if (value.empty()) return {};
    Require(value.size() <= 1024 * 1024, "Text is too long");
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    Require(size > 0, "Invalid Unicode text");
    std::string result(static_cast<std::size_t>(size), '\0');
    Require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size, nullptr, nullptr) == size,
        "Cannot encode UTF-8");
    return result;
}
namespace {
Bytes ReadBounded(const std::filesystem::path& path, std::size_t limit) {
    std::ifstream stream(path, std::ios::binary);
    Require(static_cast<bool>(stream), "Cannot read issuer configuration");
    Bytes data(limit + 1);
    stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    const auto amount = stream.gcount();
    Require(amount >= 0 && static_cast<std::size_t>(amount) <= limit, "Issuer configuration is too large");
    data.resize(static_cast<std::size_t>(amount));
    return data;
}
// Metadata identifies local paths and credentials; it never chooses a product
// trust anchor. Parse the whole document and reject duplicate keys.
class Metadata {
    std::string data_;
    std::size_t at_{};
    std::map<std::string, std::string> strings_;
    void Space() {
        while (at_ < data_.size() && (data_[at_] == ' ' || data_[at_] == '\r'
            || data_[at_] == '\n' || data_[at_] == '\t')) ++at_;
    }
    unsigned char Take() {
        Require(at_ < data_.size(), "Truncated issuer metadata");
        return static_cast<unsigned char>(data_[at_++]);
    }
    unsigned short Hex() {
        unsigned short value{};
        for (int i = 0; i < 4; ++i) {
            const auto ch = Take();
            const int digit = ch >= '0' && ch <= '9' ? ch - '0'
                : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
                : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
            Require(digit >= 0, "Invalid metadata Unicode escape");
            value = static_cast<unsigned short>((value << 4) | digit);
        }
        return value;
    }
    std::string String() {
        Require(Take() == '"', "Expected metadata string");
        std::string result;
        for (;;) {
            const auto ch = Take();
            if (ch == '"') break;
            Require(ch >= 32, "Invalid metadata string");
            if (ch != '\\') { result.push_back(static_cast<char>(ch)); continue; }
            switch (Take()) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': {
                const auto first = Hex();
                std::wstring wide(1, static_cast<wchar_t>(first));
                if (first >= 0xd800 && first <= 0xdbff) {
                    Require(Take() == '\\' && Take() == 'u', "Incomplete metadata surrogate pair");
                    const auto second = Hex();
                    Require(second >= 0xdc00 && second <= 0xdfff, "Invalid metadata surrogate pair");
                    wide.push_back(static_cast<wchar_t>(second));
                } else Require(first < 0xdc00 || first > 0xdfff, "Invalid metadata surrogate");
                result += WideToUtf8(wide);
                break;
            }
            default: throw std::runtime_error("Invalid metadata escape");
            }
        }
        (void)Utf8ToWide(result);
        Require(result.find('\0') == std::string::npos, "Metadata contains a null character");
        return result;
    }
    void Object(const std::string& prefix, int depth) {
        Require(depth <= 4 && Take() == '{', "Invalid issuer metadata object");
        std::map<std::string, bool> seen;
        Space();
        if (at_ < data_.size() && data_[at_] == '}') { ++at_; return; }
        for (;;) {
            const auto key = String();
            Require(seen.emplace(key, true).second, "Duplicate issuer metadata key");
            Space(); Require(Take() == ':', "Invalid metadata separator"); Space();
            const auto full = prefix.empty() ? key : prefix + "." + key;
            Require(at_ < data_.size(), "Missing metadata value");
            if (data_[at_] == '"') strings_.emplace(full, String());
            else if (data_[at_] == '{') Object(full, depth + 1);
            else {
                const auto begin = at_;
                while (at_ < data_.size() && data_[at_] >= '0' && data_[at_] <= '9') ++at_;
                Require(at_ > begin, "Unexpected metadata value");
                Require(at_ - begin == 1 || data_[begin] != '0', "Invalid metadata integer");
            }
            Space();
            const auto end = Take();
            if (end == '}') return;
            Require(end == ',', "Invalid metadata object delimiter"); Space();
        }
    }
public:
    explicit Metadata(const std::filesystem::path& path) {
        const auto data = ReadBounded(path, 65536);
        data_.assign(reinterpret_cast<const char*>(data.data()), data.size());
        if (data_.starts_with("\xef\xbb\xbf")) at_ = 3;
        Space(); Object("", 0); Space();
        Require(at_ == data_.size(), "Trailing issuer metadata");
    }
    std::string Get(const char* key) const {
        const auto item = strings_.find(key);
        Require(item != strings_.end(), "Missing issuer metadata field");
        return item->second;
    }
};
std::string CheckStore(const ProductDescriptor& product, const std::filesystem::path& directory) {
    ValidatePublicBlob(product.publicKey);
    const auto publicKey = ReadBounded(directory / L"issuer-public.blob", 72);
    Require(publicKey.size() == product.publicKey.size()
        && std::equal(publicKey.begin(), publicKey.end(), product.publicKey.begin()),
        "Signing store does not match the selected product public key");
    const auto identifier = Metadata(directory / L"issuer.json").Get("issuer_id");
    Require(identifier == product.issuerId, "Signing issuer does not match the selected product");
    Require(std::filesystem::is_regular_file(directory / L"issuer-private.pem"),
        "Signing private key is missing");
    return identifier;
}
std::filesystem::path SettingsPath(const ProductDescriptor& product) {
    PWSTR value{};
    Require(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &value)),
        "Cannot locate local settings");
    const std::filesystem::path directory(value);
    CoTaskMemFree(value);
    Require(!product.settingsDirectory.empty(), "Product settings directory is missing");
    return directory / std::filesystem::path(product.settingsDirectory) / L"Issuer" / L"settings.dat";
}
std::filesystem::path ExecutableDirectory() {
    std::wstring name(32768, L'\0');
    const auto count = GetModuleFileNameW(nullptr, name.data(), static_cast<DWORD>(name.size()));
    Require(count > 0 && count < name.size(), "Cannot locate issuer executable");
    name.resize(count);
    return std::filesystem::path(name).parent_path();
}
Bytes ProtectPassword(std::span<const unsigned char> password, std::string_view entropy, bool protect) {
    Require(password.size() <= 65536 && entropy.size() <= 256, "Invalid signing credential");
    DATA_BLOB input{static_cast<DWORD>(password.size()), const_cast<BYTE*>(password.data())};
    DATA_BLOB salt{static_cast<DWORD>(entropy.size()), reinterpret_cast<BYTE*>(const_cast<char*>(entropy.data()))};
    DATA_BLOB output{};
    const auto okay = protect
        ? CryptProtectData(&input, L"iCAX signing credential", &salt, nullptr, nullptr,
            CRYPTPROTECT_UI_FORBIDDEN, &output)
        : CryptUnprotectData(&input, nullptr, &salt, nullptr, nullptr,
            CRYPTPROTECT_UI_FORBIDDEN, &output);
    Require(okay != FALSE, "Signing credential belongs to another Windows account or is damaged");
    Bytes result(output.pbData, output.pbData + output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return result;
}
Bytes ExistingCredential(const std::filesystem::path& directory, const std::string& issuer) {
    const auto metadataPath = directory.parent_path() / L"credentials.json";
    if (!std::filesystem::is_regular_file(metadataPath)) return {};
    const Metadata metadata(metadataPath);
    Require(metadata.Get("primary.issuerId") == issuer, "Saved signing credential has another issuer");
    const auto vault = std::filesystem::weakly_canonical(std::filesystem::path(
        Utf8ToWide(metadata.Get("primary.vault"))));
    Require(vault == std::filesystem::weakly_canonical(directory), "Saved credential belongs to another store");
    auto protectedPassword = ReadBounded(std::filesystem::path(
        Utf8ToWide(metadata.Get("primary.credentialFile"))), 65536);
    auto password = ProtectPassword(protectedPassword, issuer, false);
    Require(!password.empty() && password.size() <= 4096, "Invalid signing password");
    return password;
}
void WriteAtomic(const std::filesystem::path& path, std::span<const unsigned char> data) {
    std::filesystem::create_directories(path.parent_path());
    const auto temporary = path.wstring() + L"." + std::to_wstring(GetCurrentProcessId())
        + L"." + std::to_wstring(GetTickCount64()) + L".tmp";
    const auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    Require(file != INVALID_HANDLE_VALUE, "Cannot save signing settings");
    DWORD written{};
    const auto okay = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    const auto flushed = FlushFileBuffers(file);
    CloseHandle(file);
    if (!okay || !flushed || written != data.size()) {
        DeleteFileW(temporary.c_str());
        throw std::runtime_error("Cannot finish signing settings");
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        throw std::runtime_error("Cannot replace signing settings");
    }
}
}
std::filesystem::path DiscoverStore(const ProductDescriptor& product) {
    std::vector<std::filesystem::path> candidates;
    const auto executable = ExecutableDirectory();
    for (const auto& base : {executable, executable.parent_path()}) {
        candidates.push_back(base / L"primary");
        candidates.push_back(base / L"Signing" / L"primary");
        candidates.push_back(base / L"签发库");
    }
    const std::filesystem::path signingRoot(product.signingRoot);
    std::error_code error;
    if (std::filesystem::is_directory(signingRoot, error)) {
        std::vector<std::filesystem::path> dated;
        for (std::filesystem::directory_iterator it(signingRoot,
            std::filesystem::directory_options::skip_permission_denied, error), end;
            it != end && !error; it.increment(error)) {
            if (it->is_directory(error)) dated.push_back(it->path() / L"primary");
        }
        std::sort(dated.rbegin(), dated.rend());
        candidates.insert(candidates.end(), dated.begin(), dated.end());
    }
    for (const auto& candidate : candidates) {
        try { (void)CheckStore(product, candidate); return std::filesystem::weakly_canonical(candidate); }
        catch (const std::exception&) {}
    }
    return {};
}
Settings LoadSettings(const ProductDescriptor& product) {
    Settings settings;
    try {
        const auto path = SettingsPath(product);
        if (std::filesystem::is_regular_file(path)) {
            const auto data = ReadBounded(path, 131072);
            Reader reader(data);
            const auto magic = reader.Take(8);
            Require(std::memcmp(magic.data(), "TDIS0001", 8) == 0, "Unknown issuer settings format");
            const auto directory = reader.Field(4096);
            settings.storeDirectory = std::filesystem::path(Utf8ToWide(std::string(
                reinterpret_cast<const char*>(directory.data()), directory.size())));
            const auto encrypted = reader.Field(65536);
            Require(reader.End(), "Trailing issuer settings");
            const auto issuer = CheckStore(product, settings.storeDirectory);
            if (!encrypted.empty()) {
                settings.password = ProtectPassword(encrypted, issuer, false);
                settings.remembered = true;
            } else settings.password = ExistingCredential(settings.storeDirectory, issuer);
        } else {
            settings.storeDirectory = DiscoverStore(product);
            if (!settings.storeDirectory.empty()) {
                settings.password = ExistingCredential(settings.storeDirectory, CheckStore(product, settings.storeDirectory));
                settings.remembered = !settings.password.empty();
            }
        }
        settings.message = settings.storeDirectory.empty() ? L"请在设置中选择签发库。"
            : settings.password.empty() ? L"已找到签发库，请在设置中保存签发口令。"
            : L"签发配置已就绪，选择申请文件即可生成授权。";
    } catch (const std::exception& error) {
        SecureZeroMemory(settings.password.data(), settings.password.size());
        settings.password.clear();
        settings.message = L"签发设置未就绪：" + Utf8ToWide(error.what());
    }
    return settings;
}
void SaveSettings(const ProductDescriptor& product, const std::filesystem::path& directory,
    std::span<const unsigned char> password, bool remember) {
    const auto normalized = std::filesystem::weakly_canonical(directory);
    const auto issuer = CheckStore(product, normalized);
    Require(password.size() <= 4096, "Signing password is too long");
    const auto path = WideToUtf8(normalized.wstring());
    Bytes data{'T','D','I','S','0','0','0','1'};
    AppendField(data, std::span(reinterpret_cast<const unsigned char*>(path.data()), path.size()));
    const auto encrypted = remember && !password.empty() ? ProtectPassword(password, issuer, true) : Bytes{};
    AppendField(data, encrypted);
    WriteAtomic(SettingsPath(product), data);
}
}
