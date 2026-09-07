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
template<std::uint32_t Site, Feature Required>
__forceinline void Enforce() {
#if defined(_DEBUG) && !defined(NDEBUG)
    // Compile-time only: no certificate reads, TPM access or trial writes.
    return;
#else
    try {
        Require(trust::PublicKey.size() == 72, "此构建尚未配置正式签发公钥，请联系软件供应商");
        const auto file = ReadFileBounded(LicenseDirectory() / L"license.tdlic", static_cast<DWORD>(MaxCertificateBytes));
        VerifyTpmAtSite<Site, Required>(file, trust::Issuer, trust::PublicKey, ProductMajor);
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string("授权校验未通过，请在“关于 → 授权”中申请或激活：") + error.what());
    }
#endif
}
}
