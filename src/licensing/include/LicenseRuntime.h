#pragma once
#include "Enrollment.h"
#include "LicenseFiles.h"
#include "IssuerTrust.h"

namespace tube::license {
inline constexpr std::uint32_t ProductMajor = 0; // product.manifest.json: 0.1.x
#if defined(_DEBUG) && !defined(NDEBUG)
inline constexpr bool DevelopmentBypass = true;
#else
inline constexpr bool DevelopmentBypass = false;
#endif
inline constexpr bool ReleaseBypass = false;
inline constexpr bool AuthorizationBypass = DevelopmentBypass;
template<std::uint32_t Site>
__forceinline void EnforceFeature(Feature required) {
    Require(RequiredFeatures(required) != 0, "Unknown licensed feature");
    if constexpr (AuthorizationBypass) {
        // Compile-time only: no certificate reads, TPM access or trial writes.
        return;
    } else {
        try {
            Require(trust::PublicKey.size() == 72, "此构建尚未配置正式签发公钥，请联系软件供应商");
            const auto file = ReadFileBounded(LicenseDirectory() / L"license.tdlic", static_cast<DWORD>(MaxCertificateBytes));
            VerifyTpmFeatureAtSite<Site>(file, trust::Issuer, trust::PublicKey, ProductMajor, required);
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string("授权校验未通过，请在“关于 → 授权”中申请或激活：") + error.what());
        }
    }
}
template<std::uint32_t Site, Feature Required>
__forceinline void Enforce() {
    static_assert(Site != 0 && RequiredFeatures(Required) != 0);
    EnforceFeature<Site>(Required);
}

// Shared resource previews/editors are reused by the product and nesting pages.
// Each alternative still requires its own page + operation and a fresh TPM proof.
template<std::uint32_t Site, Feature... Alternatives>
__forceinline void EnforceAny() {
    static_assert(Site != 0 && sizeof...(Alternatives) != 0 &&
        ((RequiredFeatures(Alternatives) != 0) && ...));
    if constexpr (AuthorizationBypass) return;
    else {
        try {
            Require(trust::PublicKey.size() == 72, "此构建尚未配置正式签发公钥，请联系软件供应商");
            const auto file = ReadFileBounded(LicenseDirectory() / L"license.tdlic", static_cast<DWORD>(MaxCertificateBytes));
            const auto certificate = VerifyCertificate(file, trust::Issuer, trust::PublicKey);
            for (const auto feature : {Alternatives...}) {
                if (HasFeature(certificate.features, feature)) {
                    VerifyTpmFeatureAtSite<Site>(file, trust::Issuer, trust::PublicKey, ProductMajor, feature);
                    return;
                }
            }
            throw std::runtime_error("此操作未获授权");
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string("授权校验未通过，请在“关于 → 授权”中申请或激活：") + error.what());
        }
    }
}
}
