#pragma once
#include "LicenseCore.h"
#include <algorithm>

namespace tube::license {
inline constexpr std::string_view UpgradeFormat = "TDUPG001";
inline constexpr std::size_t MaxActivationBytes = 16384;

struct UpgradePackage final {
    Digest baseDigest{};
    Bytes certificate;
    Certificate parsed;
};

inline bool IsUpgradePackage(std::span<const unsigned char> file) {
    return file.size() >= UpgradeFormat.size()
        && std::memcmp(file.data(), UpgradeFormat.data(), UpgradeFormat.size()) == 0;
}

inline void RequireAuthorizationUpgrade(const Certificate& base, const Certificate& next) {
    Require(base.issuerId == next.issuerId && base.product == next.product,
        "Upgrade belongs to another issuer or product");
    Require(base.licenseId == next.licenseId && base.requestId == next.requestId,
        "Upgrade belongs to another license or enrollment request");
    Require(base.customerId == next.customerId, "Upgrade belongs to another customer");
    Require(base.strategy == Strategy::Tpm2 && next.strategy == Strategy::Tpm2
        && base.devicePublicKey == next.devicePublicKey, "Upgrade belongs to another TPM device");
    Require((next.features & base.features) == base.features, "Upgrade cannot remove permissions");
    Require(next.minMajor <= base.minMajor && next.maxMajor >= base.maxMajor,
        "Upgrade cannot narrow the licensed version range");
    Require(next.issuedAt >= base.issuedAt, "Upgrade issue date predates the current license");
    Require(base.kind != Kind::Permanent || next.kind == Kind::Permanent,
        "A permanent license cannot become a trial");
    if (base.kind == Kind::Trial && next.kind == Kind::Trial) {
        Require(next.issuedAt == base.issuedAt, "Upgrade cannot reset the trial issue date");
        Require(next.notBefore == base.notBefore && next.expiresAt == base.expiresAt,
            "Upgrade cannot change the trial period");
        Require(next.trialNvPublic == base.trialNvPublic
            && next.trialInitialCounter == base.trialInitialCounter
            && next.trialQuantumSeconds == base.trialQuantumSeconds,
            "Upgrade cannot reset the trial counter binding");
    }
}

// The issuer signs this body and appends its 64-byte P-256 signature. The
// certificate retains TDLIC003 and is independently signed by the same issuer.
inline Bytes UpgradeBody(const Certificate& base, std::span<const unsigned char> certificate,
    std::string_view issuer, std::span<const unsigned char> trustedKey,
    const ProductDescriptor& product = TubeDesignerProduct) {
    const auto next = VerifyCertificateForProduct(certificate, product, issuer, trustedKey);
    RequireAuthorizationUpgrade(base, next);
    Bytes body(UpgradeFormat.begin(), UpgradeFormat.end());
    AppendField(body, base.bodyDigest);
    Append32(body, static_cast<std::uint32_t>(certificate.size()));
    body.insert(body.end(), certificate.begin(), certificate.end());
    return body;
}

inline UpgradePackage VerifyUpgradePackage(std::span<const unsigned char> file,
    std::string_view issuer, std::span<const unsigned char> trustedKey,
    const ProductDescriptor& product = TubeDesignerProduct) {
    Require(file.size() > 64 && file.size() <= MaxActivationBytes, "Invalid upgrade file size");
    const auto body = file.first(file.size() - 64);
    VerifySignature(trustedKey, body, file.last(64));
    Reader reader(body);
    const auto magic = reader.Take(UpgradeFormat.size());
    Require(std::memcmp(magic.data(), UpgradeFormat.data(), UpgradeFormat.size()) == 0,
        "Unknown upgrade format");
    UpgradePackage result;
    const auto digest = reader.Field(result.baseDigest.size());
    Require(digest.size() == result.baseDigest.size(), "Invalid base certificate digest");
    std::copy(digest.begin(), digest.end(), result.baseDigest.begin());
    result.certificate = reader.Field(MaxCertificateBytes);
    Require(reader.End(), "Trailing upgrade data");
    result.parsed = VerifyCertificateForProduct(result.certificate, product, issuer, trustedKey);
    return result;
}

inline Bytes ResolveUpgradeCertificate(std::span<const unsigned char> package,
    std::span<const unsigned char> installed, std::string_view issuer,
    std::span<const unsigned char> trustedKey,
    const ProductDescriptor& product = TubeDesignerProduct) {
    const auto upgrade = VerifyUpgradePackage(package, issuer, trustedKey, product);
    const auto base = VerifyCertificateForProduct(installed, product, issuer, trustedKey);
    if (installed.size() == upgrade.certificate.size()
        && std::equal(installed.begin(), installed.end(), upgrade.certificate.begin()))
        return upgrade.certificate; // The same signed upgrade can be imported again.
    Require(base.bodyDigest == upgrade.baseDigest, "Install the license used to issue this upgrade first");
    RequireAuthorizationUpgrade(base, upgrade.parsed);
    return upgrade.certificate;
}
// The signed predecessor digest binds each upgrade to the installed chain tip.
// These checks reject downgrades while that tip is present. Offline authorization
// does not prevent an administrator from restoring the entire license directory
// to an earlier snapshot; a hardware/server revision watermark would be needed.
}
