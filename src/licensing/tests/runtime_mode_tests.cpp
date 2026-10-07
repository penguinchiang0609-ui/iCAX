// Test runtime policy without creating or changing a certificate or TPM state.
#if defined(_MSC_VER)
#pragma warning(disable: 4702 4714)
#endif
#include "LicenseRuntime.h"
#include <iostream>

int main() {
    using namespace tube::license;
#if defined(_DEBUG) && !defined(NDEBUG)
    static_assert(DevelopmentBypass && !ReleaseBypass);
#else
    static_assert(!DevelopmentBypass && !ReleaseBypass && !AuthorizationBypass);
#endif
    for (const auto& descriptor : FeatureCatalog) {
        if constexpr (AuthorizationBypass) {
            EnforceFeature<9901>(descriptor.feature);
        } else if constexpr (trust::PublicKey.size() != 72) {
            bool rejected = false;
            try { EnforceFeature<9901>(descriptor.feature); }
            catch (const std::exception& error) {
                rejected = std::string_view(error.what()).find("尚未配置正式签发公钥") != std::string_view::npos;
            }
            if (!rejected) return 1;
        }
    }
    std::cout << (AuthorizationBypass ? "Debug development bypass verified" :
        "Authorization enforced; unconfigured builds fail closed") << '\n';
    return 0;
}
