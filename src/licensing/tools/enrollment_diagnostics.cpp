// Read-only EK diagnosis. No activation, trust-store changes, persistent keys,
// TPM Clear/ChangeEPS, NV writes, or license-file access. Public key digests are
// sent only to the fixed Intel service, exactly as enrollment repair does.
#include "EnrollmentRepair.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <optional>
using namespace tube::license;
namespace {
std::string Hex(std::span<const unsigned char> bytes) {
    std::string result; constexpr char digits[] = "0123456789abcdef";
    for (const auto byte : bytes) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}
std::string HexNumber(std::uint32_t value) {
    std::ostringstream result; result << "0x" << std::hex << std::setw(8) << std::setfill('0') << value; return result.str();
}
void Out(const std::string& key, std::string value) {
    for (auto& c : value) if (static_cast<unsigned char>(c) < 32) c = ' ';
    std::cout << key << '=' << value << '\n'; std::cout.flush();
}
std::wstring IntelPath(const Digest& digest) {
    DWORD size{};
    Require(CryptBinaryToStringA(digest.data(), static_cast<DWORD>(digest.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &size), "Cannot encode public digest");
    std::string text(size, '\0');
    Require(CryptBinaryToStringA(digest.data(), static_cast<DWORD>(digest.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, text.data(), &size), "Cannot encode public digest");
    std::wstring path = L"/ekcertservice/";
    for (auto c : text) { if (!c) break; if (c == '=') path += L"%3D"; else path += c == '+' ? L'-' : c == '/' ? L'_' : static_cast<wchar_t>(c); }
    return path;
}
Digest RsaDigest(const Bytes& blob) {
    BCRYPT_RSAKEY_BLOB header{};
    Require(blob.size() >= sizeof(header), "Short Windows RSA public blob"); std::memcpy(&header, blob.data(), sizeof(header));
    Require(header.Magic == BCRYPT_RSAPUBLIC_MAGIC && header.BitLength == 2048 && header.cbModulus == 256 && header.cbPublicExp == 3
        && header.cbPrime1 == 0 && header.cbPrime2 == 0 && blob.size() == sizeof(header) + 259, "Unsupported Windows RSA public blob");
    const auto start = blob.begin() + sizeof(header);
    Require(start[0] == 1 && start[1] == 0 && start[2] == 1, "Unsupported Windows EK exponent");
    Bytes material(start + 3, blob.end()); material.insert(material.end(), start, start + 3); return Hash(material);
}
Digest EccDigest(const Bytes& blob) {
    BCRYPT_ECCKEY_BLOB header{};
    Require(blob.size() >= sizeof(header), "Short Windows ECC public blob"); std::memcpy(&header, blob.data(), sizeof(header));
    if (header.dwMagic == BCRYPT_ECDH_PUBLIC_GENERIC_MAGIC || header.dwMagic == BCRYPT_ECDSA_PUBLIC_GENERIC_MAGIC) {
        BCRYPT_ECCFULLKEY_BLOB full{};
        Require(blob.size() >= sizeof(full), "Short Windows full ECC blob"); std::memcpy(&full, blob.data(), sizeof(full));
        Require(full.dwVersion == BCRYPT_ECC_FULLKEY_BLOB_V1 && (full.cbFieldLength == 32 || full.cbFieldLength == 48)
            && full.cbSubgroupOrder <= 66 && full.cbCofactor <= 8 && full.cbSeed <= 128, "Unsupported Windows full ECC parameters");
        const std::size_t expected = sizeof(full) + 7 * full.cbFieldLength + full.cbSubgroupOrder + full.cbCofactor + full.cbSeed;
        Require(blob.size() == expected, "Invalid Windows full ECC public blob length");
        Out("windowsEcc.fieldBits", std::to_string(full.cbFieldLength * 8));
        return Hash(std::span(blob).last(2 * full.cbFieldLength));
    }
    Require((header.dwMagic == BCRYPT_ECDH_PUBLIC_P256_MAGIC || header.dwMagic == BCRYPT_ECDSA_PUBLIC_P256_MAGIC)
        && header.cbKey == 32 && blob.size() == sizeof(header) + 64, "Unsupported Windows ECC public blob");
    Out("windowsEcc.fieldBits", "256");
    return Hash(std::span(blob).subspan(sizeof(header)));
}
Bytes EccEkTemplate() {
    // TCG low-range ECC NIST P-256 EK profile; used only by this diagnosis.
    const auto rsa = tpm::RsaEkTemplate();
    Bytes p(rsa.begin(), rsa.begin() + 42); p[1] = 0x23;
    tpm::U16(p, 6); tpm::U16(p, 128); tpm::U16(p, 0x43); tpm::U16(p, 0x10);
    tpm::U16(p, 3); tpm::U16(p, 0x10); tpm::Sized(p, Bytes(32, 0)); tpm::Sized(p, Bytes(32, 0));
    return p;
}
Bytes EccEkPoint(const Bytes& area) {
    const auto profile = EccEkTemplate(); constexpr std::size_t prefix = 54;
    Require(area.size() == profile.size() && std::equal(profile.begin(), profile.begin() + prefix, area.begin()), "Unexpected standard ECC EK profile");
    TpmReader reader(std::span(area).subspan(prefix)); auto x = reader.Sized(32); const auto y = reader.Sized(32);
    Require(x.size() == 32 && y.size() == 32 && reader.End(), "Invalid standard ECC EK point");
    x.insert(x.end(), y.begin(), y.end()); return x;
}
std::string Header(HINTERNET request, DWORD name) {
    wchar_t value[512]{}; DWORD bytes = sizeof(value);
    if (!WinHttpQueryHeaders(request, name, WINHTTP_HEADER_NAME_BY_INDEX, value, &bytes, WINHTTP_NO_HEADER_INDEX)) return {};
    const auto count = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {}; std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), count, nullptr, nullptr); result.resize(result.size() - 1); return result;
}
void Fetch(const std::string& key, const std::wstring& path) {
    std::string asciiPath;
    for (const auto c : path) { Require(c >= 0 && c <= 127, "Non-ASCII Intel path"); asciiPath += static_cast<char>(c); }
    Out(key + ".path", asciiPath);
    const auto began = GetTickCount64();
    try {
        EnrollmentInternetHandle session(WinHttpOpen(L"TubeDesigner-EnrollmentRepair/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        EnrollmentHttpCheck(session.value != nullptr); EnrollmentHttpCheck(WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 10000));
        DWORD tls = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2; EnrollmentHttpCheck(WinHttpSetOption(session.value, WINHTTP_OPTION_SECURE_PROTOCOLS, &tls, sizeof(tls)));
        EnrollmentInternetHandle connection(WinHttpConnect(session.value, L"ekop.intel.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
        EnrollmentHttpCheck(connection.value != nullptr);
        EnrollmentInternetHandle request(WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        EnrollmentHttpCheck(request.value != nullptr);
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER, logon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH, disabled = WINHTTP_DISABLE_COOKIES;
        EnrollmentHttpCheck(WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)));
        EnrollmentHttpCheck(WinHttpSetOption(request.value, WINHTTP_OPTION_AUTOLOGON_POLICY, &logon, sizeof(logon)));
        EnrollmentHttpCheck(WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)));
        EnrollmentHttpCheck(WinHttpSendRequest(request.value, L"Accept: application/json\r\n", static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0));
        EnrollmentHttpCheck(WinHttpReceiveResponse(request.value, nullptr));
        DWORD status{}, size = sizeof(status);
        EnrollmentHttpCheck(WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX));
        Out(key + ".httpStatus", std::to_string(status)); Out(key + ".contentType", Header(request.value, WINHTTP_QUERY_CONTENT_TYPE));
        Out(key + ".serverDate", Header(request.value, WINHTTP_QUERY_DATE));
        PCCERT_CONTEXT certificate{}; size = sizeof(certificate);
        if (WinHttpQueryOption(request.value, WINHTTP_OPTION_SERVER_CERT_CONTEXT, &certificate, &size) && certificate) {
            char name[512]{}; CertGetNameStringA(certificate, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, sizeof(name)); Out(key + ".tlsSubject", name);
            CertGetNameStringA(certificate, CERT_NAME_SIMPLE_DISPLAY_TYPE, CERT_NAME_ISSUER_FLAG, nullptr, name, sizeof(name)); Out(key + ".tlsIssuer", name);
            CertFreeCertificateContext(certificate);
        }
        std::string response; const auto receiveBegan = GetTickCount64();
        for (;;) {
            char bytes[4096]; DWORD read{}; EnrollmentHttpCheck(WinHttpReadData(request.value, bytes, sizeof(bytes), &read));
            if (!read) break; Require(response.size() + read <= 24576, "Response exceeds diagnosis limit"); response.append(bytes, read);
            Require(GetTickCount64() - receiveBegan <= 15000, "Response exceeded diagnosis time limit");
        }
        Out(key + ".responseBytes", std::to_string(response.size()));
        Out(key + ".responseSha256", Hex(Hash({reinterpret_cast<const unsigned char*>(response.data()), response.size()})));
        if (status != 200) Out(key + ".errorBody", response.substr(0, 1024));
        else Out(key + ".containsCertificateField", response.find("\"certificate\"") != std::string::npos ? "true" : "false");
    } catch (const std::exception& error) { Out(key + ".error", error.what()); }
    Out(key + ".elapsedMs", std::to_string(GetTickCount64() - began));
}
void Nv(tpm::Context& context, std::uint32_t index, bool readPublicContent) {
    const auto key = "nv." + HexNumber(index);
    try {
        Bytes command; Append32(command, index); const auto response = context.Send(0x169, command);
        Out(key + ".present", "true");
        TpmReader reader(std::span(response).subspan(10)); const auto area = reader.Sized(256);
        TpmReader info(area); Require(info.U32() == index, "Wrong NV public index"); info.U16(); const auto attributes = info.U32(); info.Sized(64); const auto size = info.U16();
        Out(key + ".attributes", HexNumber(attributes)); Out(key + ".size", std::to_string(size));
        if (readPublicContent) {
            // Only the standardized public EK template and nonce slots are read.
            // Public empty index authorization only; never guess owner auth.
            Require((attributes & 0x02040000u) == 0x02040000u, "Public no-DA index read unavailable; contents skipped");
            Require(size > 0 && size <= 1024, "Unexpected EK template/nonce length");
            Bytes data;
            while (data.size() < size) {
                const auto amount = static_cast<std::uint16_t>((std::min)(std::size_t(512), size - data.size()));
                Bytes read; Append32(read, index); Append32(read, index); tpm::Passwords(read);
                tpm::U16(read, amount); tpm::U16(read, static_cast<std::uint16_t>(data.size()));
                const auto out = context.Send(0x14e, read, true); Reader header(out); header.Take(10);
                const auto parameters = header.Take(header.U32()); TpmReader fields(parameters); const auto part = fields.Sized(512);
                Require(part.size() == amount && fields.End(), "Truncated public NV content"); data.insert(data.end(), part.begin(), part.end());
            }
            Out(key + ".publicContentSha256", Hex(Hash(data))); Out(key + ".publicContentHex", Hex(data));
        }
    } catch (const std::exception& error) { Out(key + ".error", error.what()); }
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || std::wstring_view(argv[1]) != L"--collect") return 1;
    Out("version", "2"); Out("authorizationChanged", "false");
    std::optional<Bytes> windowsRsa, windowsEcc, standardRsa, standardEccPoint;
    try {
        Provider provider;
        for (const auto& item : {std::pair{NCRYPT_PCP_RSA_EKPUB_PROPERTY, "windowsRsa"}, std::pair{NCRYPT_PCP_ECC_EKPUB_PROPERTY, "windowsEcc"}}) {
            try {
                auto value = ReadProperty(provider.value, item.first, 4096); Out(std::string(item.second) + ".publicBlobSha256", Hex(Hash(value)));
                Out(std::string(item.second) + ".publicBlobBytes", std::to_string(value.size()));
                if (std::string_view(item.second) == "windowsRsa") windowsRsa = std::move(value); else windowsEcc = std::move(value);
            } catch (const std::exception& error) { Out(std::string(item.second) + ".error", error.what()); }
        }
    } catch (const std::exception& error) { Out("windowsProvider.error", error.what()); }
    bool intel{};
    try {
        tpm::Context context; const auto manufacturer = EnrollmentTpmManufacturer(context); intel = manufacturer == 0x494e5443;
        Out("manufacturer", HexNumber(manufacturer));
        try {
            tpm::Object ek(context, 0x4000000b, tpm::RsaEkTemplate()); standardRsa = EnrollmentEkBlob(ek.publicArea);
            Out("standardRsa.publicBlobSha256", Hex(Hash(*standardRsa)));
            Require(IntelPath(RsaDigest(*standardRsa)) == IntelEnrollmentPath(ek.publicArea), "Diagnostic and production Intel paths differ");
        } catch (const std::exception& error) { Out("standardRsa.error", error.what()); }
        try {
            tpm::Object ek(context, 0x4000000b, EccEkTemplate()); standardEccPoint = EccEkPoint(ek.publicArea);
            Out("standardEccP256.pointSha256", Hex(Hash(*standardEccPoint)));
        } catch (const std::exception& error) { Out("standardEccP256.error", error.what()); }
        for (const auto index : {0x01c00002u, 0x01c00003u, 0x01c00004u, 0x01c0000au, 0x01c0000bu, 0x01c0000cu, 0x01c00100u})
            Nv(context, index, index == 0x01c00003u || index == 0x01c00004u || index == 0x01c0000bu || index == 0x01c0000cu);
    } catch (const std::exception& error) { Out("tpm.error", error.what()); }
    if (windowsRsa && standardRsa) Out("windowsRsa.matchesProduction", *windowsRsa == *standardRsa ? "true" : "false");
    if (!intel) { Out("network.skipped", "TPM manufacturer not confirmed as Intel"); return 0; }
    // Public, published upstream reference succeeds independently of this PC.
    Fetch("intel.reference", L"/ekcertservice/WVEG2rRwkQ7m3RpXlUphgo6Y2HLxl18h6ZZkkOAdnBE%3D");
    if (standardRsa) { try { Fetch("intel.standardRsa", IntelPath(RsaDigest(*standardRsa))); } catch (const std::exception& e) { Out("intel.standardRsa.error", e.what()); } }
    if (windowsRsa) {
        if (standardRsa && *windowsRsa == *standardRsa) Out("intel.windowsRsa.sameRequestAs", "intel.standardRsa");
        else { try { Fetch("intel.windowsRsa", IntelPath(RsaDigest(*windowsRsa))); } catch (const std::exception& e) { Out("intel.windowsRsa.error", e.what()); } }
    }
    if (windowsEcc) { try { Fetch("intel.windowsEcc", IntelPath(EccDigest(*windowsEcc))); } catch (const std::exception& e) { Out("intel.windowsEcc.error", e.what()); } }
    if (standardEccPoint) Fetch("intel.standardEccP256", IntelPath(Hash(*standardEccPoint)));
    Out("completed", "true"); return 0;
}
