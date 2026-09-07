#pragma once
#include "LicenseCore.h"

namespace tube::license {
// Wire values are from the TPM 2.0 specification, not a Windows struct layout.
class TpmReader final {
    Reader reader_;
public:
    explicit TpmReader(std::span<const unsigned char> data) : reader_(data) {}
    std::uint8_t U8() { return reader_.Take(1)[0]; }
    std::uint16_t U16() { const auto b = reader_.Take(2); return static_cast<std::uint16_t>((b[0] << 8) | b[1]); }
    std::uint32_t U32() { return reader_.U32(); }
    std::uint64_t U64() { return reader_.U64(); }
    Bytes Sized(std::size_t maximum) {
        const auto size = U16(); Require(size <= maximum, "TPM field exceeds limit");
        const auto data = reader_.Take(size); return {data.begin(), data.end()};
    }
    bool End() const { return reader_.End(); }
};
inline Bytes TpmSha256Name(std::span<const unsigned char> publicArea) {
    const auto digest = Hash(publicArea);
    Bytes name{0, 0x0b}; name.insert(name.end(), digest.begin(), digest.end()); return name;
}
inline Bytes AttestationPublicKey(std::span<const unsigned char> tpmPublicArea) {
    Require(tpmPublicArea.size() <= 256, "AK public area too large");
    TpmReader r(tpmPublicArea);
    Require(r.U16() == 0x23 && r.U16() == 0x0b, "AK must use ECC and SHA256 names");
    const auto attributes = r.U32();
    // fixedTPM | fixedParent | sensitiveDataOrigin | restricted | sign
    constexpr std::uint32_t required = 0x00050032;
    Require((attributes & required) == required && (attributes & 0x00020800) == 0,
        "AK must be a non-migratable, TPM-generated restricted signing key");
    const auto policy = r.Sized(32);
    Require(policy.empty() || policy.size() == 32, "Invalid AK policy size");
    Require(r.U16() == 0x10, "AK symmetric algorithm must be NULL");
    Require(r.U16() == 0x18 && r.U16() == 0x0b, "AK signing scheme must be ECDSA/SHA256");
    Require(r.U16() == 3 && r.U16() == 0x10, "AK curve must be P256 and KDF NULL");
    const auto x = r.Sized(32), y = r.Sized(32);
    Require(x.size() == 32 && y.size() == 32 && r.End(), "Invalid AK point");
    Bytes blob(72); BCRYPT_ECCKEY_BLOB header{BCRYPT_ECDSA_PUBLIC_P256_MAGIC, 32};
    std::memcpy(blob.data(), &header, sizeof(header));
    std::memcpy(blob.data() + 8, x.data(), 32); std::memcpy(blob.data() + 40, y.data(), 32);
    return blob;
}
inline Bytes TpmEcdsaSignature(std::span<const unsigned char> wireSignature) {
    Require(wireSignature.size() <= 72, "TPM signature too large");
    TpmReader r(wireSignature);
    Require(r.U16() == 0x18 && r.U16() == 0x0b, "Unexpected TPM signature scheme");
    const auto a = r.Sized(32), b = r.Sized(32);
    Require(!a.empty() && !b.empty() && r.End(), "Invalid TPM signature scalars");
    Bytes signature(64, 0);
    std::memcpy(signature.data() + 32 - a.size(), a.data(), a.size());
    std::memcpy(signature.data() + 64 - b.size(), b.data(), b.size());
    return signature;
}
struct NvBinding final {
    // These values MUST come from issuer-authenticated enrollment/certificate,
    // never directly from the untrusted NV_Certify response or disk state.
    Bytes attestationKeyPublicArea;
    Bytes attestationKeyQualifiedName;
    Bytes nvPublicArea; // TPMS_NV_PUBLIC, including WRITTEN after initialization
    std::uint64_t initialCounter{};
};
class VerifiedCounter final {
    std::uint64_t value_{};
    explicit VerifiedCounter(std::uint64_t value) : value_(value) {}
    friend VerifiedCounter VerifyNvEvidence(const NvBinding&, const Digest&,
        std::span<const unsigned char>, std::span<const unsigned char>);
public:
    std::uint64_t Value() const { return value_; }
};
inline VerifiedCounter VerifyNvEvidence(const NvBinding& binding, const Digest& nonce,
    std::span<const unsigned char> attest, std::span<const unsigned char> signature) {
    Require(attest.size() <= 1024, "Attestation too large");
    const auto publicKey = AttestationPublicKey(binding.attestationKeyPublicArea);
    VerifySignature(publicKey, attest, TpmEcdsaSignature(signature));

    TpmReader nv(binding.nvPublicArea);
    const auto index = nv.U32();
    Require(index >= 0x01000000 && index <= 0x01ffffff && nv.U16() == 0x0b, "Invalid NV identity");
    const auto attributes = nv.U32();
    // Counter, already initialized, not orderly/write-locked, and not reset on startup.
    Require((attributes & 0xf0) == 0x10 && (attributes & 0x20000000) != 0,
        "Expected initialized NV counter");
    Require((attributes & (0x04000000 | 0x08000000 | 0x00000800)) == 0,
        "Unsupported counter persistence or lock attributes");
    const auto policy = nv.Sized(32);
    Require((policy.empty() || policy.size() == 32) && nv.U16() == 8 && nv.End(), "Invalid counter public area");

    TpmReader r(attest);
    Require(r.U32() == 0xff544347 && r.U16() == 0x8014, "Not a TPM NV certification");
    Require(binding.attestationKeyQualifiedName.size() == 34
        && binding.attestationKeyQualifiedName[0] == 0 && binding.attestationKeyQualifiedName[1] == 0x0b,
        "Invalid enrolled AK qualified name");
    Require(r.Sized(68) == binding.attestationKeyQualifiedName, "Attestation signer mismatch");
    const auto returnedNonce = r.Sized(64);
    Require(returnedNonce.size() == nonce.size()
        && std::equal(returnedNonce.begin(), returnedNonce.end(), nonce.begin()), "Stale NV evidence");
    r.U64(); r.U32(); r.U32(); // TPM clock, resetCount and restartCount; not wall time
    Require(r.U8() == 1, "TPM clock state unsafe; recovery required");
    r.U64(); // Firmware version is covered by signature; enrollment handles firmware policy.
    Require(r.Sized(68) == TpmSha256Name(binding.nvPublicArea), "Counter name mismatch");
    Require(r.U16() == 0, "Counter certification offset must be zero");
    const auto contents = r.Sized(8);
    Require(contents.size() == 8 && r.End(), "Incomplete or trailing NV evidence");
    TpmReader counter(contents);
    const auto value = counter.U64();
    Require(value >= binding.initialCounter, "Counter predates enrollment");
    return VerifiedCounter(value);
}
}
