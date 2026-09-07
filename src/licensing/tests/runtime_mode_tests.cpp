// Missing compiled trust makes the release rejection provably unconditional.
// These optimizer diagnostics are expected in this negative-path test only.
#if defined(_MSC_VER)
#pragma warning(disable: 4702 4714)
#endif
#include "LicenseRuntime.h"
#include <iostream>

int main() {
#if defined(_DEBUG) && !defined(NDEBUG)
    static_assert(tube::license::DevelopmentBypass);
    tube::license::Enforce<9901, tube::license::Feature::Design>();
    tube::license::Enforce<9902, tube::license::Feature::Breakdown>();
    tube::license::Enforce<9903, tube::license::Feature::StepExport>();
    tube::license::Enforce<9904, tube::license::Feature::Production>();
    std::cout << "Debug operations require no license or TPM\n";
#else
    static_assert(!tube::license::DevelopmentBypass);
    if constexpr (tube::license::trust::PublicKey.size() != 72) {
        try { tube::license::Enforce<9901, tube::license::Feature::Design>(); }
        catch (const std::exception&) { return 0; }
        return 1;
    }
    std::cout << "Release authorization remains enabled\n";
#endif
    return 0;
}
