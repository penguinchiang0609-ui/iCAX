#pragma once
#include "TpmTransport.h"
#include "LicenseFiles.h"
#include <wincrypt.h>
#include <algorithm>

namespace tube::license {
// The same canonical RSA EK profile used by credential activation. This checks
// request material only; manufacturer chain trust remains enforced by the issuer.
inline Bytes EnrollmentEkBlob(std::span<const unsigned char> area) {
    const auto profile = tpm::RsaEkTemplate();
    constexpr std::size_t modulusBytes = 256;
    Require(area.size() == profile.size(), "Unsupported RSA EK size");
    const auto prefix = profile.size() - modulusBytes;
    Require(std::equal(profile.begin(), profile.begin() + prefix, area.begin()), "Unsupported RSA EK profile");
    Require((area[prefix] & 0x80) != 0, "Invalid RSA EK modulus");
    BCRYPT_RSAKEY_BLOB header{BCRYPT_RSAPUBLIC_MAGIC, 2048, 3, 256, 0, 0};
    Bytes blob(sizeof(header)); std::memcpy(blob.data(), &header, sizeof(header));
    blob.insert(blob.end(), {1, 0, 1}); blob.insert(blob.end(), area.begin() + prefix, area.end());
    return blob;
}
inline Digest EnrollmentEkDigest(std::span<const unsigned char> area) {
    const auto blob = EnrollmentEkBlob(area);
    Bytes input(blob.end() - 256, blob.end()); input.insert(input.end(), {1, 0, 1});
    return Hash(input);
}
inline std::string EnrollmentEkId(std::span<const unsigned char> area) {
    std::string text; constexpr char digits[] = "0123456789abcdef";
    for (auto byte : EnrollmentEkDigest(area)) { text += digits[byte >> 4]; text += digits[byte & 15]; }
    return text;
}
struct EnrollmentCertificate final {
    PCCERT_CONTEXT value{};
    explicit EnrollmentCertificate(std::span<const unsigned char> der) {
        Require(der.size() >= 2 && der.size() <= 8192 && der[0] == 0x30, "Invalid EK certificate encoding");
        std::size_t header = 2, size = der[1];
        if (size & 0x80) {
            const auto count = size & 0x7f;
            Require(count > 0 && count <= 2 && der.size() > 2 + count && der[2] != 0, "Invalid EK DER length");
            size = 0; for (std::size_t i = 0; i < count; ++i) size = (size << 8) | der[2 + i];
            Require(size >= 128, "Noncanonical EK DER length"); header += count;
        }
        Require(header + size == der.size(), "Truncated or trailing EK certificate data");
        value = CertCreateCertificateContext(X509_ASN_ENCODING, der.data(), static_cast<DWORD>(der.size()));
        Require(value != nullptr, "Invalid EK certificate");
    }
    ~EnrollmentCertificate() { if (value) CertFreeCertificateContext(value); }
    EnrollmentCertificate(const EnrollmentCertificate&) = delete;
};
inline bool EnrollmentCertificateMatches(const Bytes& der, std::span<const unsigned char> area) {
    try {
        EnrollmentCertificate certificate(der); const auto expected = EnrollmentEkBlob(area);
        PublicKey key;
        if (!CryptImportPublicKeyInfoEx2(X509_ASN_ENCODING, &certificate.value->pCertInfo->SubjectPublicKeyInfo, 0, nullptr, &key.value)) return false;
        ULONG size{}; if (BCryptExportKey(key.value, nullptr, BCRYPT_RSAPUBLIC_BLOB, nullptr, 0, &size, 0) < 0 || size != expected.size()) return false;
        Bytes blob(size);
        return BCryptExportKey(key.value, nullptr, BCRYPT_RSAPUBLIC_BLOB, blob.data(), size, &size, 0) >= 0 && blob == expected;
    } catch (const std::exception&) { return false; }
}
inline void ValidateEnrollmentCertificate(const Bytes& der, std::span<const unsigned char> area) {
    EnrollmentCertificate certificate(der); const auto info = certificate.value->pCertInfo;
    Require(EnrollmentCertificateMatches(der, area), "厂商返回的证书与本机 TPM 不匹配，未保存。");
    Require(CertVerifyTimeValidity(nullptr, info) == 0, "厂商证书不在有效期内，请核对系统时间或联系供应商。");
    const auto basicExtension = CertFindExtension(szOID_BASIC_CONSTRAINTS2, info->cExtension, info->rgExtension);
    Require(basicExtension != nullptr, "EK certificate lacks basic constraints");
    CERT_BASIC_CONSTRAINTS2_INFO* basic{}; DWORD size{};
    Require(CryptDecodeObjectEx(X509_ASN_ENCODING, X509_BASIC_CONSTRAINTS2, basicExtension->Value.pbData, basicExtension->Value.cbData, CRYPT_DECODE_ALLOC_FLAG, nullptr, &basic, &size), "Invalid EK basic constraints");
    const bool isCa = basic->fCA != 0; LocalFree(basic); Require(!isCa, "EK certificate must not be a CA");
    BYTE usage{};
    Require(CertGetIntendedKeyUsage(X509_ASN_ENCODING, info, &usage, 1) && (usage & CERT_KEY_ENCIPHERMENT_KEY_USAGE) && !(usage & CERT_KEY_CERT_SIGN_KEY_USAGE), "Invalid EK certificate key usage");
    const auto extension = CertFindExtension(szOID_ENHANCED_KEY_USAGE, info->cExtension, info->rgExtension);
    Require(extension != nullptr, "EK certificate lacks explicit TCG usage");
    CERT_ENHKEY_USAGE* enhanced{};
    Require(CryptDecodeObjectEx(X509_ASN_ENCODING, X509_ENHANCED_KEY_USAGE, extension->Value.pbData, extension->Value.cbData, CRYPT_DECODE_ALLOC_FLAG, nullptr, &enhanced, &size), "Invalid EK certificate usage");
    bool endorsement = false;
    for (DWORD i = 0; i < enhanced->cUsageIdentifier; ++i) if (std::strcmp(enhanced->rgpszUsageIdentifier[i], "2.23.133.8.1") == 0) endorsement = true;
    LocalFree(enhanced); Require(endorsement, "Certificate is not a TPM endorsement certificate");
}
inline std::filesystem::path EnrollmentCacheDirectory() {
    PWSTR raw{}; Require(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)), "Cannot locate local application data");
    const std::filesystem::path root(raw); CoTaskMemFree(raw);
    return root / L"TubeDesigner" / L"Licensing" / L"Endorsement";
}
inline std::filesystem::path EnrollmentCachePath(const std::filesystem::path& directory, std::span<const unsigned char> area) {
    return directory / (EnrollmentEkId(area) + ".cer");
}
inline void AppendCachedEnrollmentCertificate(std::vector<Bytes>& certificates, std::span<const unsigned char> area,
    const std::filesystem::path& directory) {
    try {
        const auto der = ReadFileBounded(EnrollmentCachePath(directory, area), 8192);
        ValidateEnrollmentCertificate(der, area);
        if (std::find(certificates.begin(), certificates.end(), der) == certificates.end()) {
            Require(certificates.size() < 8, "Too many EK certificates"); certificates.push_back(der);
        }
    } catch (const std::exception&) { /* An absent/invalid cache grants nothing. */ }
}
inline void SaveEnrollmentCertificate(const Bytes& der, std::span<const unsigned char> area,
    const std::filesystem::path& directory) {
    ValidateEnrollmentCertificate(der, area);
    std::filesystem::create_directories(directory);
    const auto target = EnrollmentCachePath(directory, area);
    const auto random = RandomChallenge(); std::wstring suffix;
    for (auto byte : random) suffix += L"0123456789abcdef"[byte & 15];
    const auto temporary = directory / (L"download-" + suffix + L".tmp");
    try {
        WriteNewFile(temporary, der, 8192);
        Require(MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0, "无法保存厂商证书，请检查当前用户目录的写入权限。");
    } catch (...) { DeleteFileW(temporary.c_str()); throw; }
}
}
