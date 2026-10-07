#pragma once
#include "LicenseCore.h"
#include <filesystem>

namespace tube::license::issuer_native::crypto {
class SigningKey final {
    NCRYPT_PROV_HANDLE provider_{};
    NCRYPT_KEY_HANDLE key_{};
public:
    SigningKey(const std::filesystem::path& encryptedPem, std::string_view passwordUtf8,
        std::span<const unsigned char> expectedPublicKey);
    ~SigningKey();
    SigningKey(const SigningKey&) = delete;
    SigningKey& operator=(const SigningKey&) = delete;
    Bytes Sign(std::span<const unsigned char> message) const;
};
// Validates the exact TCG low-range RSA EK template before producing a CNG blob.
Bytes RsaEkPublicBlob(std::span<const unsigned char> publicArea);
Bytes ActivationPackage(const SigningKey& key, std::span<const unsigned char> ekPublicArea,
    std::span<const unsigned char> akPublicArea, const Digest& requestDigest,
    std::span<const unsigned char> certificate);
}
