#pragma once
#include "TpmTransport.h"
#include "TrialRuntime.h"
#include "LicenseFiles.h"
#include <wincrypt.h>

namespace tube::license {
inline Bytes ReadProperty(NCRYPT_HANDLE handle, LPCWSTR property, DWORD maximum) {
    DWORD size{}; CngCheck(NCryptGetProperty(handle, property, nullptr, 0, &size, 0), "Query TPM property");
    Require(size <= maximum, "TPM property too large"); Bytes result(size);
    CngCheck(NCryptGetProperty(handle, property, result.data(), size, &size, 0), "Read TPM property");
    Require(size == result.size(), "TPM property size changed"); return result;
}
inline std::vector<Bytes> EndorsementCertificates() {
    Provider provider;
    std::vector<Bytes> certificates;
    for (auto property : {NCRYPT_PCP_RSA_EKCERT_PROPERTY, NCRYPT_PCP_RSA_EKNVCERT_PROPERTY}) {
        DWORD size{}; HCERTSTORE store{};
        const auto status = NCryptGetProperty(provider.value, property, reinterpret_cast<PBYTE>(&store), sizeof(store), &size, 0);
        if (status != ERROR_SUCCESS || size != sizeof(store) || !store) continue;
        PCCERT_CONTEXT cert{};
        while ((cert = CertEnumCertificatesInStore(store, cert)) != nullptr) {
            if (cert->cbCertEncoded <= 8192 && certificates.size() < 8) {
                Bytes der(cert->pbCertEncoded, cert->pbCertEncoded + cert->cbCertEncoded);
                if (std::find(certificates.begin(), certificates.end(), der) == certificates.end()) certificates.push_back(std::move(der));
            }
        }
        CertCloseStore(store, 0);
    }
    return certificates;
}
inline Bytes ReadIntelNvChainIndex(tpm::Context& ctx, std::uint32_t index) {
    // Intel's documented ODCA certificate-chain NV index. Read-only; absent on
    // other vendors. Never redefine, increment, delete, or change its auth.
    Bytes command; Append32(command, index); const auto reply = ctx.Send(0x169, command);
    TpmReader publicReply(std::span(reply).subspan(10)); const auto publicArea = publicReply.Sized(256);
    TpmReader info(publicArea); Require(info.U32() == index, "Unexpected ODCA index");
    info.U16(); info.U32(); info.Sized(64); const auto size = info.U16();
    Require(info.End() && size > 0 && size <= 16384, "Invalid ODCA chain length");
    Bytes chain;
    while (chain.size() < size) {
        const auto amount = static_cast<std::uint16_t>((std::min)(std::size_t(512), size - chain.size()));
        Bytes read; Append32(read, 0x40000001); Append32(read, index); tpm::Passwords(read);
        tpm::U16(read, amount); tpm::U16(read, static_cast<std::uint16_t>(chain.size()));
        const auto out = ctx.Send(0x14e, read, true); Reader header(out); header.Take(10);
        const auto parameters = header.Take(header.U32()); TpmReader fields(parameters); const auto part = fields.Sized(512);
        Require(part.size() == amount && fields.End(), "Truncated ODCA chain"); chain.insert(chain.end(), part.begin(), part.end());
    }
    return chain;
}
inline void AppendIntelNvChain(tpm::Context& ctx, std::vector<Bytes>& certificates) {
    Bytes chain;
    for (std::uint32_t index = 0x01c00100; index < 0x01c00108; ++index) {
        try {
            const auto part = ReadIntelNvChainIndex(ctx, index);
            Require(chain.size() + part.size() <= 32768, "EK chain too large");
            chain.insert(chain.end(), part.begin(), part.end());
        } catch (const std::exception&) { break; }
    }
    std::size_t offset = 0;
    while (offset < chain.size()) {
        Require(chain.size() - offset >= 2 && chain[offset] == 0x30, "Invalid ODCA DER sequence");
        std::size_t header = 2, length = chain[offset + 1];
        if ((length & 0x80) != 0) {
            const auto count = length & 0x7f; Require(count > 0 && count <= 2 && chain.size() - offset >= 2 + count, "Invalid ODCA DER length");
            length = 0; for (std::size_t i = 0; i < count; ++i) length = (length << 8) | chain[offset + 2 + i]; header += count;
        }
        Require(length <= 8192 && header + length <= chain.size() - offset, "Truncated ODCA certificate");
        Bytes der(chain.begin() + offset, chain.begin() + offset + header + length);
        if (std::find(certificates.begin(), certificates.end(), der) == certificates.end()) {
            Require(certificates.size() < 8, "Too many EK chain certificates"); certificates.push_back(std::move(der));
        }
        offset += header + length;
    }
}
inline Bytes CreateEnrollmentRequest(bool trial = false, std::uint32_t* ownedIndex = nullptr) {
    tpm::Context ctx; tpm::Object ak(ctx, 0x40000001, tpm::AkTemplate());
    tpm::Object ek(ctx, 0x4000000b, tpm::RsaEkTemplate());
    const auto nonce = RandomChallenge(); const auto evidence = tpm::Quote(ctx, ak, nonce);
    tpm::VerifyQuote(ak.publicArea, ak.qualifiedName, nonce, evidence);
    auto certs = EndorsementCertificates();
    // Extra chain data is optional and untrusted. Issuer still builds a complete
    // valid chain to its independently configured manufacturer roots.
    try { AppendIntelNvChain(ctx, certs); } catch (const std::exception&) {}
    Require(!certs.empty(), "TPM endorsement certificate unavailable; device cannot enroll automatically");
    Bytes request{'T','D','R','E','Q','0','0', static_cast<unsigned char>(trial ? '3' : '2')};
    AppendText(request, Product); AppendField(request, nonce); AppendField(request, ak.publicArea);
    AppendField(request, ak.qualifiedName); AppendField(request, ek.publicArea);
    Append32(request, static_cast<std::uint32_t>(certs.size()));
    for (const auto& cert : certs) {
        // Certificate fields allow 8 KiB, larger than ordinary protocol identifiers.
        Append32(request, static_cast<std::uint32_t>(cert.size())); request.insert(request.end(), cert.begin(), cert.end());
    }
    AppendField(request, evidence.attest); AppendField(request, evidence.signature);
    if (trial) {
        Require(ownedIndex != nullptr, "Trial counter ownership is required");
        const auto state = NewTrialCounter(ctx, ak, nonce, *ownedIndex);
        AppendField(request, state.area); Append64(request, state.initial);
        AppendField(request, state.evidence.attest); AppendField(request, state.evidence.signature);
    }
    return request;
}
inline void ExportEnrollmentRequest(const std::filesystem::path& path, bool trial) {
    Require(!std::filesystem::exists(path), "Request file already exists");
    std::uint32_t ownedIndex{};
    try { WriteNewFile(path, CreateEnrollmentRequest(trial, &ownedIndex)); }
    catch (...) {
        if (ownedIndex != 0) {
            try { tpm::Context ctx; RemoveNewTrialCounter(ctx, ownedIndex); }
            catch (...) { throw std::runtime_error("Request failed; newly allocated trial index requires manual review: " + std::to_string(ownedIndex)); }
        }
        throw;
    }
}
inline Bytes DecryptActivation(std::span<const unsigned char> file, std::string_view issuer,
    std::span<const unsigned char> trustedKey) {
    Require(file.size() > 64 && file.size() <= 16384, "Invalid activation file size");
    const auto body = file.first(file.size() - 64); VerifySignature(trustedKey, body, file.last(64));
    Reader r(body); const auto magic = r.Take(8); Require(std::memcmp(magic.data(), "TDACT001", 8) == 0, "Unknown activation format");
    const auto requestDigest = r.Field(32); Require(requestDigest.size() == 32, "Invalid request digest");
    const auto name = r.Field(34), credential = r.Field(1024), secret = r.Field(512), iv = r.Field(12), encrypted = r.Field(8192);
    Require(r.End() && name.size() == 34 && iv.size() == 12 && encrypted.size() > 16, "Invalid activation fields");
    tpm::Context ctx; tpm::Object ak(ctx, 0x40000001, tpm::AkTemplate());
    Require(name == ak.name, "Activation belongs to another TPM key");
    tpm::Object ek(ctx, 0x4000000b, tpm::RsaEkTemplate());
    auto key = tpm::Activate(ctx, ak, ek, credential, secret);
    struct Clear { Bytes& key; ~Clear() { SecureZeroMemory(key.data(), key.size()); } } clear{key};
    Algorithm aes(BCRYPT_AES_ALGORITHM);
    BCryptCheck(BCryptSetProperty(aes.value, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
        sizeof(BCRYPT_CHAIN_MODE_GCM), 0), "Set AES-GCM");
    PublicKey symmetric;
    BCryptCheck(BCryptGenerateSymmetricKey(aes.value, &symmetric.value, nullptr, 0, key.data(), static_cast<ULONG>(key.size()), 0), "Import activation key");
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info; BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = const_cast<PUCHAR>(iv.data()); info.cbNonce = static_cast<ULONG>(iv.size());
    info.pbAuthData = const_cast<PUCHAR>(requestDigest.data()); info.cbAuthData = static_cast<ULONG>(requestDigest.size());
    info.pbTag = const_cast<PUCHAR>(encrypted.data() + encrypted.size() - 16); info.cbTag = 16;
    Bytes certificate(encrypted.size() - 16); ULONG written{};
    BCryptCheck(BCryptDecrypt(symmetric.value, const_cast<PUCHAR>(encrypted.data()), static_cast<ULONG>(encrypted.size() - 16),
        &info, nullptr, 0, certificate.data(), static_cast<ULONG>(certificate.size()), &written, 0), "Decrypt activation");
    Require(written == certificate.size(), "Invalid decrypted certificate size");
    const auto verified = VerifyCertificate(certificate, issuer, trustedKey);
    Require(verified.devicePublicKey == AttestationPublicKey(ak.publicArea), "Certificate and activation device differ");
    return certificate;
}
template<std::uint32_t Site, Feature Required>
__forceinline void VerifyTpmAtSite(std::span<const unsigned char> file, std::string_view issuer,
    std::span<const unsigned char> trustedKey, std::uint32_t major) {
    static_assert(Site != 0);
    const auto c = VerifyCertificate(file, issuer, trustedKey);
    Require(c.strategy == Strategy::Tpm2, "Dongle adapter not installed");
    Require(major >= c.minMajor && major <= c.maxMajor && (c.features & static_cast<unsigned>(Required)) != 0, "Operation not licensed");
    tpm::Context ctx; tpm::Object ak(ctx, 0x40000001, tpm::AkTemplate());
    Require(AttestationPublicKey(ak.publicArea) == c.devicePublicKey, "TPM device mismatch");
    const auto random = RandomChallenge(); const auto challenge = DeviceChallenge(c, Required, random, Site);
    const auto nonce = Hash(challenge); const auto evidence = tpm::Quote(ctx, ak, nonce);
    tpm::VerifyQuote(ak.publicArea, ak.qualifiedName, nonce, evidence);
    if (c.kind == Kind::Trial) EnforceTrial(ctx, ak, c);
}
}
