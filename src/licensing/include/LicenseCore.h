#pragma once

// No issuer private keys, test keys, environment-based overrides or global
// "licensed" flags are permitted in this library.
#include <windows.h>
#include <bcrypt.h>
#include <ncrypt.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace tube::license {
using Bytes = std::vector<unsigned char>;
using Digest = std::array<unsigned char, 32>;
inline constexpr std::string_view Product = "icax.tube-designer";
inline constexpr std::size_t MaxCertificateBytes = 8192;
enum class Strategy : std::uint32_t { Tpm2 = 1, Dongle = 2 };
enum class Kind : std::uint32_t { Permanent = 1, Trial = 2 };
enum class Feature : std::uint32_t { Design = 1, Breakdown = 2, StepExport = 4, Production = 8 };
inline constexpr std::uint32_t KnownFeatures = 15;

inline void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
inline void CngCheck(SECURITY_STATUS result, const char* operation) {
    if (result != ERROR_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed (" + std::to_string(static_cast<unsigned long>(result)) + ")");
}
inline void BCryptCheck(NTSTATUS result, const char* operation) {
    if (result < 0) throw std::runtime_error(std::string(operation) + " failed");
}
struct Algorithm final {
    BCRYPT_ALG_HANDLE value{};
    explicit Algorithm(LPCWSTR name) { BCryptCheck(BCryptOpenAlgorithmProvider(&value, name, nullptr, 0), "Open algorithm"); }
    ~Algorithm() { if (value) BCryptCloseAlgorithmProvider(value, 0); }
    Algorithm(const Algorithm&) = delete;
    Algorithm& operator=(const Algorithm&) = delete;
};
struct PublicKey final {
    BCRYPT_KEY_HANDLE value{};
    PublicKey() = default;
    PublicKey(const PublicKey&) = delete;
    PublicKey& operator=(const PublicKey&) = delete;
    ~PublicKey() { if (value) BCryptDestroyKey(value); }
};
struct Provider final {
    NCRYPT_PROV_HANDLE value{};
    Provider() { CngCheck(NCryptOpenStorageProvider(&value, MS_PLATFORM_CRYPTO_PROVIDER, 0), "Open TPM provider"); }
    ~Provider() { if (value) NCryptFreeObject(value); }
    Provider(const Provider&) = delete;
    Provider& operator=(const Provider&) = delete;
};
struct DeviceKey final {
    NCRYPT_KEY_HANDLE value{};
    DeviceKey() = default;
    DeviceKey(const DeviceKey&) = delete;
    DeviceKey& operator=(const DeviceKey&) = delete;
    ~DeviceKey() { if (value) NCryptFreeObject(value); }
};
inline Digest Hash(std::span<const unsigned char> bytes) {
    Require(bytes.size() <= 1024 * 1024, "Hash input too large");
    Algorithm algorithm(BCRYPT_SHA256_ALGORITHM);
    Digest result{};
    BCryptCheck(BCryptHash(algorithm.value, nullptr, 0,
        const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), result.data(), static_cast<ULONG>(result.size())), "SHA256");
    return result;
}
inline Digest RandomChallenge() {
    Digest result{};
    BCryptCheck(BCryptGenRandom(nullptr, result.data(), static_cast<ULONG>(result.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG), "Random challenge");
    return result;
}
inline void ValidatePublicBlob(std::span<const unsigned char> blob) {
    // Canonical Windows BCRYPT_ECCPUBLIC_BLOB: little-endian header, then X/Y.
    Require(blob.size() == 72, "Expected P-256 public key");
    BCRYPT_ECCKEY_BLOB header{};
    std::memcpy(&header, blob.data(), sizeof(header));
    Require(header.dwMagic == BCRYPT_ECDSA_PUBLIC_P256_MAGIC && header.cbKey == 32, "Invalid P-256 public key header");
}
inline void VerifySignature(std::span<const unsigned char> publicBlob,
    std::span<const unsigned char> message, std::span<const unsigned char> signature) {
    ValidatePublicBlob(publicBlob);
    Require(signature.size() == 64, "Invalid signature length");
    Algorithm algorithm(BCRYPT_ECDSA_P256_ALGORITHM);
    PublicKey key;
    BCryptCheck(BCryptImportKeyPair(algorithm.value, nullptr, BCRYPT_ECCPUBLIC_BLOB,
        &key.value, const_cast<PUCHAR>(publicBlob.data()), static_cast<ULONG>(publicBlob.size()), 0), "Import public key");
    auto digest = Hash(message);
    BCryptCheck(BCryptVerifySignature(key.value, nullptr, digest.data(), static_cast<ULONG>(digest.size()),
        const_cast<PUCHAR>(signature.data()), static_cast<ULONG>(signature.size()), 0), "Verify signature");
}
inline void Append32(Bytes& bytes, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(static_cast<unsigned char>(value >> shift));
}
inline void Append64(Bytes& bytes, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) bytes.push_back(static_cast<unsigned char>(value >> shift));
}
inline void AppendField(Bytes& bytes, std::span<const unsigned char> value) {
    Require(value.size() <= 4096, "Field too large");
    Append32(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}
inline void AppendText(Bytes& bytes, std::string_view value) {
    AppendField(bytes, {reinterpret_cast<const unsigned char*>(value.data()), value.size()});
}
class Reader final {
    std::span<const unsigned char> bytes_;
    std::size_t offset_{};
public:
    explicit Reader(std::span<const unsigned char> bytes) : bytes_(bytes) {}
    std::span<const unsigned char> Take(std::size_t size) {
        Require(size <= bytes_.size() - offset_, "Truncated certificate");
        const auto result = bytes_.subspan(offset_, size); offset_ += size; return result;
    }
    std::uint32_t U32() { std::uint32_t value{}; for (auto byte : Take(4)) value = (value << 8) | byte; return value; }
    std::uint64_t U64() { std::uint64_t value{}; for (auto byte : Take(8)) value = (value << 8) | byte; return value; }
    Bytes Field(std::size_t maximum) { auto size = U32(); Require(size <= maximum, "Field exceeds limit"); auto data = Take(size); return {data.begin(), data.end()}; }
    std::string Text(std::size_t maximum) {
        const auto data = Field(maximum);
        // Protocol identifiers are printable ASCII; customer PII belongs in the issuer ledger.
        for (auto byte : data) Require(byte >= 33 && byte <= 126, "Invalid identifier");
        Require(!data.empty(), "Empty identifier");
        return {data.begin(), data.end()};
    }
    bool End() const { return offset_ == bytes_.size(); }
};
struct Certificate final {
    std::string issuerId, licenseId, requestId, customerId, product;
    Strategy strategy{};
    Kind kind{};
    std::uint32_t features{}, minMajor{}, maxMajor{};
    std::uint64_t issuedAt{}, notBefore{}, expiresAt{};
    Bytes devicePublicKey;
    Bytes trialNvPublic;
    std::uint64_t trialInitialCounter{};
    std::uint32_t trialQuantumSeconds{};
    Digest bodyDigest{};
};
// Caller supplies a trusted public key compiled into the release, NOT a key
// from a certificate, request, writable configuration or environment variable.
inline Certificate VerifyCertificate(std::span<const unsigned char> file,
    std::string_view trustedIssuerId, std::span<const unsigned char> trustedPublicKey) {
    Require(file.size() <= MaxCertificateBytes && file.size() > 64, "Invalid certificate size");
    auto body = file.first(file.size() - 64);
    VerifySignature(trustedPublicKey, body, file.last(64));
    Reader reader(body);
    const auto magic = reader.Take(8);
    const bool v2 = std::memcmp(magic.data(), "TDLIC002", 8) == 0;
    Require(v2 || std::memcmp(magic.data(), "TDLIC001", 8) == 0, "Unknown certificate format");
    Certificate c;
    c.issuerId = reader.Text(128);
    Require(c.issuerId == trustedIssuerId, "Untrusted issuer");
    c.licenseId = reader.Text(128); c.requestId = reader.Text(128); c.customerId = reader.Text(128);
    c.product = reader.Text(128);
    Require(c.product == Product, "Wrong product");
    c.strategy = static_cast<Strategy>(reader.U32()); c.kind = static_cast<Kind>(reader.U32());
    c.features = reader.U32(); c.minMajor = reader.U32(); c.maxMajor = reader.U32();
    c.issuedAt = reader.U64(); c.notBefore = reader.U64(); c.expiresAt = reader.U64();
    c.devicePublicKey = reader.Field(72);
    if (v2) {
        c.trialNvPublic = reader.Field(128); c.trialInitialCounter = reader.U64(); c.trialQuantumSeconds = reader.U32();
        Require(c.kind == Kind::Trial && c.trialNvPublic.size() == 14 && c.trialInitialCounter > 0
            && c.trialQuantumSeconds == 3600, "Invalid TPM trial binding");
    }
    Require(reader.End(), "Trailing certificate data");
    Require(c.strategy == Strategy::Tpm2 || c.strategy == Strategy::Dongle, "Unknown strategy");
    Require(c.features != 0 && (c.features & ~KnownFeatures) == 0, "Invalid feature set");
    Require(c.minMajor <= c.maxMajor, "Invalid version range");
    Require(c.issuedAt != 0, "Invalid issue date");
    Require(c.kind == Kind::Permanent || c.kind == Kind::Trial, "Unknown license kind");
    if (c.kind == Kind::Permanent) Require(c.notBefore == 0 && c.expiresAt == 0, "Permanent license must not depend on wall clock");
    else Require(c.notBefore >= c.issuedAt && c.expiresAt > c.notBefore, "Invalid trial dates");
    ValidatePublicBlob(c.devicePublicKey);
    c.bodyDigest = Hash(body);
    return c;
}
inline std::array<unsigned char, 104> DeviceChallenge(const Certificate& certificate,
    Feature feature, const Digest& nonce, std::uint32_t site = 1) {
    // Fixed-size local buffer, no global challenge or heap allocation here.
    // Domain (32), signed-body hash (32), random nonce (32), feature/site (4 each).
    // The body hash binds license ID, device key, permissions and every other field.
    std::array<unsigned char, 104> result{};
    constexpr std::string_view domain = "TubeDesigner.DeviceProof.v1";
    std::memcpy(result.data(), domain.data(), domain.size());
    std::memcpy(result.data() + 32, certificate.bodyDigest.data(), 32);
    std::memcpy(result.data() + 64, nonce.data(), 32);
    for (unsigned i = 0; i < 4; ++i) {
        result[96 + i] = static_cast<unsigned char>(static_cast<std::uint32_t>(feature) >> (24 - i * 8));
        result[100 + i] = static_cast<unsigned char>(site >> (24 - i * 8));
    }
    return result;
}
inline Bytes ExportDevicePublicKey(NCRYPT_KEY_HANDLE key) {
    DWORD size{};
    CngCheck(NCryptExportKey(key, 0, BCRYPT_ECCPUBLIC_BLOB, nullptr, nullptr, 0, &size, 0), "Size device public key");
    Require(size == 72, "Device key must be P-256");
    Bytes result(size);
    CngCheck(NCryptExportKey(key, 0, BCRYPT_ECCPUBLIC_BLOB, nullptr, result.data(), size, &size, 0), "Export device public key");
    Require(size == result.size(), "Invalid public key export");
    ValidatePublicBlob(result); return result;
}
inline Bytes SignDeviceChallenge(NCRYPT_KEY_HANDLE key, std::span<const unsigned char> challenge) {
    auto digest = Hash(challenge);
    Bytes signature(64); DWORD size{};
    CngCheck(NCryptSignHash(key, nullptr, digest.data(), static_cast<DWORD>(digest.size()),
        signature.data(), static_cast<DWORD>(signature.size()), &size, 0), "TPM challenge signature");
    Require(size == 64, "Invalid TPM signature length"); return signature;
}
// Deliberately no automatic key creation here. Lost keys require a new request.
inline void ProveDevice(const Certificate& certificate, Feature feature, const wchar_t* keyName, std::uint32_t site) {
    Require(certificate.strategy == Strategy::Tpm2, "Dongle provider is not installed");
    Provider provider; DeviceKey key;
    CngCheck(NCryptOpenKey(provider.value, &key.value, keyName, 0, NCRYPT_MACHINE_KEY_FLAG), "Open enrolled device key");
    DWORD exportPolicy{}, bytes{};
    CngCheck(NCryptGetProperty(key.value, NCRYPT_EXPORT_POLICY_PROPERTY,
        reinterpret_cast<PBYTE>(&exportPolicy), sizeof(exportPolicy), &bytes, 0), "Read export policy");
    Require(bytes == sizeof(exportPolicy) && exportPolicy == 0, "Exportable device key rejected");
    const auto actualPublic = ExportDevicePublicKey(key.value);
    Require(actualPublic == certificate.devicePublicKey, "Device binding mismatch");
    const auto nonce = RandomChallenge(); // fresh at every call; not a shared challenge
    const auto challenge = DeviceChallenge(certificate, feature, nonce, site);
    const auto response = SignDeviceChallenge(key.value, challenge);
    VerifySignature(certificate.devicePublicKey, challenge, response);
}
struct TrialCheckpoint final {
    std::uint64_t diskSequence{}, tpmSequence{}, lastAcceptedUtc{}, nowUtc{};
    bool diskPresent{}, tpmPresent{}, integrityVerified{}, counterProofVerified{};
};
enum class TrialDecision { Allowed, Expired, ClockRollback, RecoveryRequired };
// Policy only: booleans are NOT a production trust boundary. A production
// adapter must verify NV_Certify, nonce, index name, attributes and binding.
inline TrialDecision EvaluateTrial(const Certificate& c, const TrialCheckpoint& state) {
    Require(c.kind == Kind::Trial, "Not a trial certificate");
    if (!state.diskPresent || !state.tpmPresent || !state.integrityVerified || !state.counterProofVerified
        || state.diskSequence != state.tpmSequence) return TrialDecision::RecoveryRequired;
    if (state.nowUtc < c.notBefore) return TrialDecision::ClockRollback;
    if (state.nowUtc < state.lastAcceptedUtc && state.lastAcceptedUtc - state.nowUtc > 300)
        return TrialDecision::ClockRollback;
    return (state.nowUtc >= c.expiresAt || state.lastAcceptedUtc >= c.expiresAt)
        ? TrialDecision::Expired : TrialDecision::Allowed;
}
// Per-site specialization provides distinct challenge context. It is not a
// promise against optimizer folding or binary patching; inspect Release output.
template<std::uint32_t Site, Feature Required>
__forceinline void VerifyAtSite(std::span<const unsigned char> file,
    std::string_view issuer, std::span<const unsigned char> publicKey,
    std::uint32_t productMajor, const wchar_t* deviceKeyName) {
    static_assert(Site != 0);
    const auto certificate = VerifyCertificate(file, issuer, publicKey);
    Require(productMajor >= certificate.minMajor && productMajor <= certificate.maxMajor, "Version not licensed");
    Require((certificate.features & static_cast<std::uint32_t>(Required)) != 0, "Feature not licensed");
    // Never silently fall back to disk-only trial records or a software device.
    Require(certificate.kind == Kind::Permanent, "Trial NV provider has not passed qualification");
    ProveDevice(certificate, Required, deviceKeyName, Site);
}
}
