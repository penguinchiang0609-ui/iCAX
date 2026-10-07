#include "IssuerCrypto.h"
#include "LicenseFiles.h"
#include "TpmEvidence.h"
#include <wincrypt.h>
#include <algorithm>
#include <initializer_list>
#include <limits>

namespace tube::license::issuer_native::crypto {
namespace {
struct Wipe final {
    Bytes& value;
    ~Wipe() { if (!value.empty()) SecureZeroMemory(value.data(), value.size()); }
};
class DerReader final {
    std::span<const unsigned char> data_;
    std::size_t offset_{};
public:
    explicit DerReader(std::span<const unsigned char> data) : data_(data) {}
    bool End() const { return offset_ == data_.size(); }
    unsigned char Peek() const { Require(!End(), "Truncated encrypted key"); return data_[offset_]; }
    std::span<const unsigned char> Value(unsigned char tag) {
        Require(data_.size() - offset_ >= 2 && data_[offset_++] == tag, "Invalid encrypted-key DER tag");
        std::size_t size = data_[offset_++];
        if (size & 0x80) {
            const auto count = size & 0x7f;
            Require(count > 0 && count <= 2 && count <= data_.size() - offset_, "Invalid encrypted-key DER length");
            Require(data_[offset_] != 0, "Noncanonical encrypted-key DER length");
            size = 0;
            for (std::size_t i = 0; i < count; ++i) size = (size << 8) | data_[offset_++];
            Require(size >= 128, "Noncanonical encrypted-key DER length");
        }
        Require(size <= data_.size() - offset_, "Truncated encrypted-key DER value");
        const auto result = data_.subspan(offset_, size); offset_ += size; return result;
    }
    std::uint32_t Integer() {
        const auto raw = Value(2);
        Require(!raw.empty() && raw.size() <= 5 && (raw[0] & 0x80) == 0,
            "Invalid encrypted-key integer");
        Require(raw.size() == 1 || raw[0] != 0 || (raw[1] & 0x80) != 0,
            "Noncanonical encrypted-key integer");
        Require(raw.size() < 5 || raw[0] == 0, "Encrypted-key integer overflow");
        std::uint32_t result{}; for (auto byte : raw) result = (result << 8) | byte; return result;
    }
};
bool Equals(std::span<const unsigned char> actual, std::initializer_list<unsigned char> expected) {
    return actual.size() == expected.size() && std::equal(actual.begin(), actual.end(), expected.begin());
}
Bytes DecodePem(const std::filesystem::path& path) {
    const auto raw = ReadFileBounded(path, 16384);
    const std::string pem(raw.begin(), raw.end());
    constexpr std::string_view begin = "-----BEGIN ENCRYPTED PRIVATE KEY-----";
    constexpr std::string_view end = "-----END ENCRYPTED PRIVATE KEY-----";
    Require(pem.starts_with(begin), "An encrypted PKCS#8 signing key is required");
    const auto last = pem.find(end, begin.size());
    Require(last != std::string::npos, "Incomplete encrypted signing key");
    for (std::size_t i = last + end.size(); i < pem.size(); ++i)
        Require(pem[i] == '\r' || pem[i] == '\n' || pem[i] == ' ' || pem[i] == '\t', "Trailing encrypted-key data");
    const auto base64 = std::string_view(pem).substr(begin.size(), last - begin.size());
    DWORD size{};
    Require(CryptStringToBinaryA(base64.data(), static_cast<DWORD>(base64.size()),
        CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, nullptr, &size, nullptr, nullptr) && size > 0 && size <= 8192,
        "Invalid encrypted-key PEM");
    Bytes result(size);
    Require(CryptStringToBinaryA(base64.data(), static_cast<DWORD>(base64.size()),
        CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, result.data(), &size, nullptr, nullptr), "Invalid encrypted-key PEM");
    result.resize(size); return result;
}
Bytes DecryptPkcs8(std::span<const unsigned char> encoded, std::string_view password) {
    Require(!password.empty() && password.size() <= 4096 && password.find('\0') == std::string_view::npos,
        "A signing-key password is required");
    DerReader outer(encoded); DerReader top(outer.Value(0x30)); Require(outer.End(), "Trailing encrypted-key DER");
    DerReader algorithm(top.Value(0x30));
    Require(Equals(algorithm.Value(6), {0x2a,0x86,0x48,0x86,0xf7,0x0d,1,5,0x0d}), "Signing key must use PBES2 encryption");
    DerReader parameters(algorithm.Value(0x30)); Require(algorithm.End(), "Trailing PBES2 parameters");
    DerReader derivation(parameters.Value(0x30));
    Require(Equals(derivation.Value(6), {0x2a,0x86,0x48,0x86,0xf7,0x0d,1,5,0x0c}), "Signing key must use PBKDF2");
    DerReader pbkdf2(derivation.Value(0x30)); Require(derivation.End(), "Trailing PBKDF2 parameters");
    const auto salt = pbkdf2.Value(4);
    Require(salt.size() >= 8 && salt.size() <= 64, "Invalid signing-key salt");
    const auto iterations = pbkdf2.Integer();
    Require(iterations > 0 && iterations <= 10000000, "Invalid signing-key iteration count");
    if (!pbkdf2.End() && pbkdf2.Peek() == 2) Require(pbkdf2.Integer() == 32, "Signing-key length must be 256 bits");
    DerReader prf(pbkdf2.Value(0x30));
    Require(Equals(prf.Value(6), {0x2a,0x86,0x48,0x86,0xf7,0x0d,2,9}), "Signing key must use PBKDF2-HMAC-SHA256");
    if (!prf.End()) Require(prf.Value(5).empty(), "Invalid PBKDF2 PRF parameters");
    Require(prf.End() && pbkdf2.End(), "Trailing PBKDF2 data");
    DerReader encryption(parameters.Value(0x30));
    Require(Equals(encryption.Value(6), {0x60,0x86,0x48,1,0x65,3,4,1,0x2a}), "Signing key must use AES-256-CBC");
    const auto ivSpan = encryption.Value(4);
    Require(ivSpan.size() == 16 && encryption.End() && parameters.End(), "Invalid signing-key IV");
    const auto encrypted = top.Value(4);
    Require(!encrypted.empty() && encrypted.size() % 16 == 0 && top.End(), "Invalid encrypted private key");
    BCRYPT_ALG_HANDLE hmac{};
    BCryptCheck(BCryptOpenAlgorithmProvider(&hmac, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG), "Open PBKDF2");
    struct Close { BCRYPT_ALG_HANDLE value; ~Close() { BCryptCloseAlgorithmProvider(value, 0); } } close{hmac};
    Bytes aesKey(32); Wipe clearKey{aesKey};
    BCryptCheck(BCryptDeriveKeyPBKDF2(hmac, reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
        static_cast<ULONG>(password.size()), const_cast<PUCHAR>(salt.data()), static_cast<ULONG>(salt.size()),
        iterations, aesKey.data(), static_cast<ULONG>(aesKey.size()), 0), "Derive signing-key password");
    Algorithm aes(BCRYPT_AES_ALGORITHM);
    BCryptCheck(BCryptSetProperty(aes.value, BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)), sizeof(BCRYPT_CHAIN_MODE_CBC), 0), "Set key CBC");
    PublicKey key;
    BCryptCheck(BCryptGenerateSymmetricKey(aes.value, &key.value, nullptr, 0, aesKey.data(), 32, 0), "Import password-derived key");
    Bytes iv(ivSpan.begin(), ivSpan.end()); Bytes plain(encrypted.size()); ULONG written{};
    const auto result = BCryptDecrypt(key.value, const_cast<PUCHAR>(encrypted.data()), static_cast<ULONG>(encrypted.size()),
        nullptr, iv.data(), static_cast<ULONG>(iv.size()), plain.data(), static_cast<ULONG>(plain.size()), &written, BCRYPT_BLOCK_PADDING);
    if (result < 0 || written == 0 || written > plain.size()) {
        SecureZeroMemory(plain.data(), plain.size()); throw std::runtime_error("Signing-key password is incorrect or the encrypted key is invalid");
    }
    // Clear padding bytes before shrinking; no decrypted bytes are ever written to disk.
    SecureZeroMemory(plain.data() + written, plain.size() - written); plain.resize(written); return plain;
}
Digest Hmac(std::span<const unsigned char> key, std::span<const unsigned char> message) {
    BCRYPT_ALG_HANDLE value{};
    BCryptCheck(BCryptOpenAlgorithmProvider(&value, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG), "Open HMAC");
    struct Close { BCRYPT_ALG_HANDLE value; ~Close() { BCryptCloseAlgorithmProvider(value, 0); } } close{value};
    Digest result{};
    BCryptCheck(BCryptHash(value, const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()),
        const_cast<PUCHAR>(message.data()), static_cast<ULONG>(message.size()), result.data(), 32), "HMAC-SHA256");
    return result;
}
Bytes Kdfa(std::span<const unsigned char> key, std::string_view label,
    std::span<const unsigned char> context, std::uint32_t bits) {
    Require(bits == 128 || bits == 256, "Unsupported credential KDF length");
    Bytes input; Append32(input, 1); input.insert(input.end(), label.begin(), label.end()); input.push_back(0);
    input.insert(input.end(), context.begin(), context.end()); Append32(input, bits);
    auto derived = Hmac(key, input); Bytes result(derived.begin(), derived.begin() + bits / 8);
    SecureZeroMemory(derived.data(), derived.size()); return result;
}
Bytes Cfb128(std::span<const unsigned char> key, std::span<const unsigned char> plain) {
    // CNG CFB128 rejects a partial final block on some Windows versions. Standard
    // CFB128 uses AES-ECB feedback and truncates only the final keystream block.
    Algorithm aes(BCRYPT_AES_ALGORITHM);
    BCryptCheck(BCryptSetProperty(aes.value, BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_ECB)), sizeof(BCRYPT_CHAIN_MODE_ECB), 0), "Set credential ECB primitive");
    PublicKey symmetric;
    BCryptCheck(BCryptGenerateSymmetricKey(aes.value, &symmetric.value, nullptr, 0,
        const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0), "Import credential storage key");
    Bytes result(plain.size()); std::array<unsigned char,16> feedback{}, stream{};
    struct Clear { std::array<unsigned char,16>& a; std::array<unsigned char,16>& b;
        ~Clear() { SecureZeroMemory(a.data(), a.size()); SecureZeroMemory(b.data(), b.size()); } } clear{feedback, stream};
    for (std::size_t offset = 0; offset < plain.size(); offset += 16) {
        ULONG written{};
        BCryptCheck(BCryptEncrypt(symmetric.value, feedback.data(), 16, nullptr, nullptr, 0,
            stream.data(), 16, &written, 0), "Encrypt credential feedback");
        Require(written == 16, "Unexpected AES block length");
        const auto count = (std::min)(std::size_t(16), plain.size() - offset);
        for (std::size_t i = 0; i < count; ++i) result[offset+i] = plain[offset+i] ^ stream[i];
        if (count == 16) std::copy_n(result.begin()+offset, 16, feedback.begin());
    }
    return result;
}
struct Credential final { Bytes blob, secret; };
Credential MakeCredential(std::span<const unsigned char> ekArea, std::span<const unsigned char> akName,
    std::span<const unsigned char> credential) {
    Require(akName.size() == 34 && akName[0] == 0 && akName[1] == 11 && credential.size() == 32, "Invalid activation credential");
    const auto rsaBlob = RsaEkPublicBlob(ekArea);
    Algorithm rsa(BCRYPT_RSA_ALGORITHM); PublicKey ek;
    BCryptCheck(BCryptImportKeyPair(rsa.value, nullptr, BCRYPT_RSAPUBLIC_BLOB, &ek.value,
        const_cast<PUCHAR>(rsaBlob.data()), static_cast<ULONG>(rsaBlob.size()), 0), "Import RSA EK");
    auto random = RandomChallenge(); Bytes seed(random.begin(), random.end()); SecureZeroMemory(random.data(), random.size()); Wipe clearSeed{seed};
    unsigned char label[]{'I','D','E','N','T','I','T','Y',0};
    BCRYPT_OAEP_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM, label, static_cast<ULONG>(sizeof(label))};
    Bytes secret(256); ULONG written{};
    BCryptCheck(BCryptEncrypt(ek.value, seed.data(), 32, &padding, nullptr, 0,
        secret.data(), static_cast<ULONG>(secret.size()), &written, BCRYPT_PAD_OAEP), "Wrap credential seed");
    Require(written == secret.size(), "Invalid wrapped credential seed");
    auto storage = Kdfa(seed, "STORAGE", akName, 128); Wipe clearStorage{storage};
    Bytes sized{0,32}; sized.insert(sized.end(), credential.begin(), credential.end()); Wipe clearSized{sized};
    const auto encrypted = Cfb128(storage, sized);
    auto integrityKey = Kdfa(seed, "INTEGRITY", {}, 256); Wipe clearIntegrity{integrityKey};
    Bytes integrityInput(encrypted); integrityInput.insert(integrityInput.end(), akName.begin(), akName.end());
    auto integrity = Hmac(integrityKey, integrityInput);
    Bytes blob{0,32}; blob.insert(blob.end(), integrity.begin(), integrity.end()); blob.insert(blob.end(), encrypted.begin(), encrypted.end());
    SecureZeroMemory(integrity.data(), integrity.size()); return {std::move(blob), std::move(secret)};
}
}

SigningKey::SigningKey(const std::filesystem::path& encryptedPem, std::string_view passwordUtf8,
    std::span<const unsigned char> expectedPublicKey) {
    ValidatePublicBlob(expectedPublicKey);
    auto der = DecodePem(encryptedPem); auto plain = DecryptPkcs8(der, passwordUtf8); Wipe wipe{plain};
    try {
        CngCheck(NCryptOpenStorageProvider(&provider_, MS_KEY_STORAGE_PROVIDER, 0), "Open signing provider");
        // No key name or persistence flags: the imported signing key is ephemeral.
        CngCheck(NCryptImportKey(provider_, 0, NCRYPT_PKCS8_PRIVATE_KEY_BLOB, nullptr, &key_,
            plain.data(), static_cast<DWORD>(plain.size()), NCRYPT_SILENT_FLAG), "Import decrypted PKCS#8 signing key");
        DWORD size{}; CngCheck(NCryptExportKey(key_, 0, BCRYPT_ECCPUBLIC_BLOB, nullptr, nullptr, 0, &size, 0), "Query signing public key");
        Require(size == 72, "Signing key must be ECDSA P-256");
        Bytes publicKey(size);
        CngCheck(NCryptExportKey(key_, 0, BCRYPT_ECCPUBLIC_BLOB, nullptr, publicKey.data(), size, &size, 0), "Read signing public key");
        // Standard PKCS#8 id-ecPublicKey has no ECDSA/ECDH usage distinction.
        // Windows imports it as ECDH P-256; the same ephemeral handle supports
        // ECDSA signing. Normalize only this exported header for exact key match.
        BCRYPT_ECCKEY_BLOB header{};
        std::memcpy(&header, publicKey.data(), sizeof(header));
        if (header.dwMagic == BCRYPT_ECDH_PUBLIC_P256_MAGIC && header.cbKey == 32) {
            header.dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
            std::memcpy(publicKey.data(), &header, sizeof(header));
        }
        ValidatePublicBlob(publicKey);
        Require(std::equal(publicKey.begin(), publicKey.end(), expectedPublicKey.begin(), expectedPublicKey.end()), "Signing private key does not match issuer public key");
    } catch (...) {
        if (key_) { NCryptFreeObject(key_); key_ = 0; }
        if (provider_) { NCryptFreeObject(provider_); provider_ = 0; }
        throw;
    }
}
SigningKey::~SigningKey() { if (key_) NCryptFreeObject(key_); if (provider_) NCryptFreeObject(provider_); }
Bytes SigningKey::Sign(std::span<const unsigned char> message) const {
    auto digest = Hash(message); Bytes signature(64); DWORD written{};
    CngCheck(NCryptSignHash(key_, nullptr, digest.data(), 32, signature.data(), 64, &written, NCRYPT_SILENT_FLAG), "Sign authorization");
    Require(written == signature.size(), "Unexpected P-256 signature length"); return signature;
}
Bytes RsaEkPublicBlob(std::span<const unsigned char> publicArea) {
    TpmReader r(publicArea);
    Require(r.U16() == 1 && r.U16() == 11 && r.U32() == 0x300b2, "Expected TCG low-range RSA EK");
    const auto policy = r.Sized(32);
    Require(Equals(policy, {0x83,0x71,0x97,0x67,0x44,0x84,0xb3,0xf8,0x1a,0x90,0xcc,0x8d,0x46,0xa5,0xd7,0x24,
        0xfd,0x52,0xd7,0x6e,0x06,0x52,0x0b,0x64,0xf2,0xa1,0xda,0x1b,0x33,0x14,0x69,0xaa}), "Unexpected EK policy");
    Require(r.U16() == 6 && r.U16() == 128 && r.U16() == 0x43 && r.U16() == 0x10 && r.U16() == 2048, "Unsupported EK algorithm");
    auto exponent = r.U32(); if (exponent == 0) exponent = 65537;
    const auto modulus = r.Sized(256);
    Require(r.End() && exponent == 65537 && modulus.size() == 256 && (modulus[0] & 0x80), "Invalid RSA EK");
    BCRYPT_RSAKEY_BLOB header{BCRYPT_RSAPUBLIC_MAGIC,2048,3,256,0,0}; Bytes result(sizeof(header));
    std::memcpy(result.data(), &header, sizeof(header)); result.insert(result.end(), {1,0,1});
    result.insert(result.end(), modulus.begin(), modulus.end()); return result;
}
Bytes ActivationPackage(const SigningKey& key, std::span<const unsigned char> ekPublicArea,
    std::span<const unsigned char> akPublicArea, const Digest& requestDigest, std::span<const unsigned char> certificate) {
    Require(certificate.size() <= 4096, "Certificate is too large for activation");
    auto secret = RandomChallenge(); Bytes credential(secret.begin(), secret.end()); SecureZeroMemory(secret.data(), secret.size()); Wipe clear{credential};
    const auto akName = TpmSha256Name(akPublicArea);
    const auto wrapped = MakeCredential(ekPublicArea, akName, credential);
    auto randomNonce = RandomChallenge(); Bytes nonce(randomNonce.begin(), randomNonce.begin()+12);
    Algorithm aes(BCRYPT_AES_ALGORITHM);
    BCryptCheck(BCryptSetProperty(aes.value, BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)), sizeof(BCRYPT_CHAIN_MODE_GCM), 0), "Set activation AES-GCM");
    PublicKey symmetric;
    BCryptCheck(BCryptGenerateSymmetricKey(aes.value, &symmetric.value, nullptr, 0, credential.data(), 32, 0), "Import activation key");
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth; BCRYPT_INIT_AUTH_MODE_INFO(auth);
    std::array<unsigned char,16> tag{}; auth.pbNonce = nonce.data(); auth.cbNonce = 12;
    auth.pbAuthData = const_cast<PUCHAR>(requestDigest.data()); auth.cbAuthData = 32; auth.pbTag = tag.data(); auth.cbTag = 16;
    Bytes encrypted(certificate.size()); ULONG written{};
    BCryptCheck(BCryptEncrypt(symmetric.value, const_cast<PUCHAR>(certificate.data()), static_cast<ULONG>(certificate.size()),
        &auth, nullptr, 0, encrypted.data(), static_cast<ULONG>(encrypted.size()), &written, 0), "Encrypt activation certificate");
    Require(written == certificate.size(), "Unexpected encrypted certificate length"); encrypted.insert(encrypted.end(), tag.begin(), tag.end());
    Bytes body{'T','D','A','C','T','0','0','1'};
    AppendField(body, requestDigest); AppendField(body, akName); AppendField(body, wrapped.blob);
    AppendField(body, wrapped.secret); AppendField(body, nonce); AppendField(body, encrypted);
    const auto signature = key.Sign(body); body.insert(body.end(), signature.begin(), signature.end()); return body;
}
}
