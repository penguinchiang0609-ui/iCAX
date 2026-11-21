#pragma once
#include "Enrollment.h"
#include <winhttp.h>
#include <functional>

namespace tube::license {
class EnrollmentRepairError : public std::runtime_error {
public:
    std::string code;
    EnrollmentRepairError(std::string value, const std::string& message) : std::runtime_error(message), code(std::move(value)) {}
};
inline std::wstring IntelEnrollmentPath(std::span<const unsigned char> area) {
    const auto digest = EnrollmentEkDigest(area); DWORD count{};
    Require(CryptBinaryToStringA(digest.data(), static_cast<DWORD>(digest.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &count), "Cannot encode EK identifier");
    std::string encoded(count, '\0');
    Require(CryptBinaryToStringA(digest.data(), static_cast<DWORD>(digest.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, encoded.data(), &count), "Cannot encode EK identifier");
    std::wstring path = L"/ekcertservice/";
    for (const auto c : encoded) {
        if (!c) break;
        if (c == '=') path += L"%3D";
        else path += c == '+' ? L'-' : c == '/' ? L'_' : static_cast<wchar_t>(c);
    }
    return path;
}
inline Bytes DecodeIntelEnrollmentCertificate(std::string value) {
    Require(!value.empty() && value.size() <= 16384, "厂商返回的证书长度无效。");
    std::string encoded;
    const auto digit = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    for (std::size_t i = 0; i < value.size(); ++i) {
        char c = value[i];
        if (c == '%') {
            Require(i + 2 < value.size() && digit(value[i + 1]) >= 0 && digit(value[i + 2]) >= 0, "Invalid certificate escape");
            c = static_cast<char>((digit(value[i + 1]) << 4) | digit(value[i + 2])); i += 2;
        }
        if (c == '-') c = '+'; else if (c == '_') c = '/';
        Require((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=', "Invalid certificate base64");
        encoded += c;
    }
    Require(encoded.size() % 4 != 1, "Invalid certificate base64 length");
    while (encoded.size() % 4) encoded += '=';
    DWORD size{}, skipped{}, format{};
    Require(CryptStringToBinaryA(encoded.data(), static_cast<DWORD>(encoded.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, nullptr, &size, &skipped, &format) && size > 0 && size <= 8192 && skipped == 0, "厂商返回的证书编码无效。");
    Bytes der(size);
    Require(CryptStringToBinaryA(encoded.data(), static_cast<DWORD>(encoded.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, der.data(), &size, &skipped, &format), "Cannot decode EK certificate");
    // Canonical re-encoding also rejects embedded or excess padding.
    DWORD canonicalSize{}; CryptBinaryToStringA(der.data(), size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &canonicalSize);
    std::string canonical(canonicalSize, '\0');
    Require(CryptBinaryToStringA(der.data(), size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, canonical.data(), &canonicalSize), "Cannot validate EK base64");
    canonical.resize(std::strlen(canonical.c_str())); Require(canonical == encoded, "Noncanonical certificate base64");
    return der;
}
struct EnrollmentInternetHandle final {
    HINTERNET value{};
    explicit EnrollmentInternetHandle(HINTERNET handle) : value(handle) {}
    ~EnrollmentInternetHandle() { if (value) WinHttpCloseHandle(value); }
    EnrollmentInternetHandle(const EnrollmentInternetHandle&) = delete;
};
inline void EnrollmentHttpCheck(BOOL success) {
    if (success) return;
    const auto error = GetLastError();
    const auto code = error == ERROR_WINHTTP_TIMEOUT ? "network_timeout" : error == ERROR_WINHTTP_SECURE_FAILURE ? "tls_failed" : "network_failed";
    throw EnrollmentRepairError(code, "无法安全连接 Intel 证书服务，请检查网络、代理及系统时间后重试。（错误码 " + std::to_string(error) + "）");
}
inline void CheckEnrollmentHttpStatus(DWORD status) {
    if (status == 200) return;
    if (status == 404 || status == 204) throw EnrollmentRepairError("certificate_unavailable", "Intel 服务暂未提供本机的厂商证书，请联系电脑或主板厂商确认 TPM 证书供应情况。");
    throw EnrollmentRepairError("manufacturer_service_failed", "Intel 证书服务未完成请求，请稍后重试。（HTTP " + std::to_string(status) + "）");
}
inline std::string DownloadIntelEnrollmentResponse(std::span<const unsigned char> area) {
    const auto path = IntelEnrollmentPath(area);
    EnrollmentInternetHandle session(WinHttpOpen(L"TubeDesigner-EnrollmentRepair/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    EnrollmentHttpCheck(session.value != nullptr);
    EnrollmentHttpCheck(WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 10000));
    DWORD tls = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    EnrollmentHttpCheck(WinHttpSetOption(session.value, WINHTTP_OPTION_SECURE_PROTOCOLS, &tls, sizeof(tls)));
    EnrollmentInternetHandle connection(WinHttpConnect(session.value, L"ekop.intel.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    EnrollmentHttpCheck(connection.value != nullptr);
    EnrollmentInternetHandle request(WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    EnrollmentHttpCheck(request.value != nullptr);
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER, logon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH, disable = WINHTTP_DISABLE_COOKIES;
    EnrollmentHttpCheck(WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)));
    EnrollmentHttpCheck(WinHttpSetOption(request.value, WINHTTP_OPTION_AUTOLOGON_POLICY, &logon, sizeof(logon)));
    EnrollmentHttpCheck(WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)));
    // HTTPS validation is never disabled. No configurable URL, redirects,
    // credentials, private keys, application license or model data are sent.
    EnrollmentHttpCheck(WinHttpSendRequest(request.value, L"Accept: application/json\r\n", static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0));
    EnrollmentHttpCheck(WinHttpReceiveResponse(request.value, nullptr));
    DWORD status{}, size = sizeof(status);
    EnrollmentHttpCheck(WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX));
    CheckEnrollmentHttpStatus(status);
    std::string response; const auto started = GetTickCount64();
    for (;;) {
        char buffer[4096]; DWORD read{};
        EnrollmentHttpCheck(WinHttpReadData(request.value, buffer, sizeof(buffer), &read));
        if (!read) break;
        Require(response.size() + read <= 24576, "厂商证书响应过大，已拒绝。");
        response.append(buffer, read);
        if (GetTickCount64() - started > 15000) throw EnrollmentRepairError("network_timeout", "读取 Intel 证书响应超时，请检查网络后重试。");
    }
    Require(!response.empty(), "Intel 服务返回了空响应。"); return response;
}
inline std::uint32_t EnrollmentTpmManufacturer(tpm::Context& ctx) {
    Bytes command; Append32(command, 6); Append32(command, 0x105); Append32(command, 1);
    const auto reply = ctx.Send(0x17a, command); Reader r(std::span(reply).subspan(10));
    r.Take(1); Require(r.U32() == 6 && r.U32() == 1 && r.U32() == 0x105, "Invalid TPM manufacturer response");
    const auto manufacturer = r.U32(); Require(r.End(), "Invalid TPM manufacturer data"); return manufacturer;
}
struct EnrollmentRepairResult { bool requestReady{}; std::string code, message; };
inline EnrollmentRepairResult RepairEnrollmentMaterial(std::span<const unsigned char> area,
    const std::vector<Bytes>& certificates, std::uint32_t manufacturer, const std::filesystem::path& cache,
    const std::function<Bytes(std::span<const unsigned char>)>& fetch) {
    for (const auto& der : certificates) {
        try { ValidateEnrollmentCertificate(der, area); return {true, "ready", "本机授权申请材料已就绪，可以导出授权申请；仍需签发并导入激活文件。"}; }
        catch (const std::exception&) {}
    }
    if (manufacturer != 0x494e5443) return {false, "manufacturer_not_supported", "本机缺少厂商证书，当前自动获取支持 Intel TPM。请联系电脑厂商补齐证书。"};
    try {
        const auto der = fetch(area);
        ValidateEnrollmentCertificate(der, area);
        SaveEnrollmentCertificate(der, area, cache);
        return {true, "repaired", "已获取并保存与本机匹配的厂商证书，可以导出授权申请；仍需签发并导入激活文件。"};
    } catch (const EnrollmentRepairError& error) { return {false, error.code, error.what()}; }
    catch (const std::exception& error) { return {false, "certificate_repair_failed", error.what()}; }
}
inline EnrollmentRepairResult RepairEnrollmentEnvironment(const std::function<Bytes(std::span<const unsigned char>)>& fetch) {
    try {
        tpm::Context ctx; const auto manufacturer = EnrollmentTpmManufacturer(ctx);
        tpm::Object ek(ctx, 0x4000000b, tpm::RsaEkTemplate());
        auto certificates = EndorsementCertificates();
        try { AppendIntelNvChain(ctx, certificates); } catch (const std::exception&) {}
        const auto cache = EnrollmentCacheDirectory(); AppendCachedEnrollmentCertificate(certificates, ek.publicArea, cache);
        return RepairEnrollmentMaterial(ek.publicArea, certificates, manufacturer, cache, fetch);
    } catch (const tpm::TransportError& error) {
        return {false, "tpm_check_failed", error.what()};
    } catch (const std::exception& error) {
        return {false, "tpm_check_failed", std::string("授权申请环境检查未完成。详情：") + error.what()};
    }
}
}
