// Explicit acceptance executable. Runtime trust and install paths are test inputs
// here only; the shipped client retains compiled trust and fixed ProgramData.
#include "Enrollment.h"
#include <cwctype>
#include <functional>
#include <iostream>

using namespace tube::license;
namespace {
constexpr std::uint32_t TestProductMajor = 0;
std::filesystem::path AbsolutePath(const wchar_t* value) {
    const std::filesystem::path path(value);
    Require(path.is_absolute(), "Hardware test paths must be absolute");
    return std::filesystem::weakly_canonical(path);
}
bool Within(const std::filesystem::path& path, const std::filesystem::path& parent) {
    auto item = path.begin();
    for (auto expected = parent.begin(); expected != parent.end(); ++expected, ++item) {
        if (item == path.end()) return false;
        auto left = item->native(), right = expected->native();
        for (auto& value : left) value = static_cast<wchar_t>(std::towlower(value));
        for (auto& value : right) value = static_cast<wchar_t>(std::towlower(value));
        if (left != right) return false;
    }
    return true;
}
std::filesystem::path FreshTestDirectory(const wchar_t* value) {
    const auto folder = AbsolutePath(value);
    const auto production = std::filesystem::weakly_canonical(LicenseDirectory());
    Require(!Within(folder, production) && !Within(production, folder), "Refuse production licensing directory");
    Require(!std::filesystem::exists(folder), "Hardware test directory must be new");
    Require(std::filesystem::create_directories(folder), "Cannot create isolated hardware test directory");
    return folder;
}
std::string TestIssuer(const wchar_t* value) {
    const std::wstring_view text(value);
    Require(!text.empty() && text.size() <= 128, "Invalid test issuer identifier");
    std::string issuer;
    for (const auto character : text) {
        Require(character >= 33 && character <= 126, "Test issuer identifier must be printable ASCII");
        issuer.push_back(static_cast<char>(character));
    }
    return issuer;
}
void Reject(const std::function<void()>& action, const char* name) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    Require(rejected, name);
}
template<std::uint32_t Site, Feature Required>
void VerifyInstalled(const std::filesystem::path& folder, std::string_view issuer, const Bytes& publicKey) {
    const auto file = ReadFileBounded(folder / L"license.tdlic", static_cast<DWORD>(MaxCertificateBytes));
    VerifyTpmAtSite<Site, Required>(file, issuer, publicKey, TestProductMajor);
}
void VerifyAll(const std::filesystem::path& folder, std::string_view issuer, const Bytes& publicKey) {
    const auto file = ReadFileBounded(folder / L"license.tdlic", static_cast<DWORD>(MaxCertificateBytes));
    for (const auto& feature : FeatureCatalog)
        VerifyTpmFeatureAtSite<9001>(file, issuer, publicKey, TestProductMajor, feature.feature);
}
void ActivateAndVerify(const wchar_t* keyPath, const wchar_t* issuerText,
    const wchar_t* packagePath, const wchar_t* folderText) {
    const auto publicKey = ReadFileBounded(AbsolutePath(keyPath), 72);
    ValidatePublicBlob(publicKey);
    const auto issuer = TestIssuer(issuerText);
    const auto package = ReadFileBounded(AbsolutePath(packagePath), 16384);
    const auto certificate = DecryptActivation(package, issuer, publicKey);
    const auto parsed = VerifyCertificate(certificate, issuer, publicKey);
    Require(parsed.kind == Kind::Permanent && parsed.strategy == Strategy::Tpm2,
        "Hardware acceptance accepts permanent TPM certificates only; no trial NV operations");
    Require(parsed.minMajor <= TestProductMajor && TestProductMajor <= parsed.maxMajor,
        "Test product version outside certificate range");
    Require(parsed.features == KnownFeatures, "Hardware acceptance requires the complete feature catalog");
    const auto folder = FreshTestDirectory(folderText);
    const auto installedPath = folder / L"license.tdlic";
    InstallCertificateAtDirectory(certificate, folder);
    Require(ReadFileBounded(installedPath, static_cast<DWORD>(MaxCertificateBytes)) == certificate,
        "Installed certificate differs from decrypted activation");
    std::cout << "activation_decrypt=passed\ncertificate_atomic_install=passed\n";
    VerifyAll(folder, issuer, publicKey);
    std::cout << "all_catalog_features_tpm_proof=passed\n";
    // Repeat exactly the same call sites. Each invocation generates a new nonce
    // inside VerifyTpmAtSite rather than reusing an authorization flag.
    VerifyAll(folder, issuer, publicKey);
    std::cout << "repeated_fresh_tpm_challenges=passed\n";
    auto tamperedPackage = package;
    tamperedPackage.back() ^= 1;
    Reject([&] { DecryptActivation(tamperedPackage, issuer, publicKey); }, "Tampered activation was accepted");
    Require(ReadFileBounded(installedPath, static_cast<DWORD>(MaxCertificateBytes)) == certificate,
        "Failed activation replaced the installed certificate");
    std::cout << "tampered_activation=rejected\nfailed_activation_preserves_certificate=passed\n";
    auto tamperedCertificate = certificate;
    tamperedCertificate.back() ^= 1;
    InstallCertificateAtDirectory(tamperedCertificate, folder);
    Reject([&] { VerifyInstalled<9001, Feature::ProductDesign>(folder, issuer, publicKey); },
        "Tampered installed certificate was accepted");
    InstallCertificateAtDirectory(certificate, folder);
    std::cout << "tampered_installed_certificate=rejected\n";
    Require(std::filesystem::remove(installedPath), "Cannot remove isolated test certificate");
    Reject([&] { VerifyInstalled<9001, Feature::ProductDesign>(folder, issuer, publicKey); },
        "Missing certificate was accepted");
    InstallCertificateAtDirectory(certificate, folder);
    VerifyAll(folder, issuer, publicKey);
    Require(ReadFileBounded(installedPath, static_cast<DWORD>(MaxCertificateBytes)) == certificate,
        "Final certificate restoration failed");
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        Require(entry.path().filename() == L"license.tdlic", "Atomic installation left a temporary file");
    }
    std::cout << "deleted_certificate=rejected\ncertificate_restored_and_reverified=passed\n"
        "atomic_install_temporary_files=none\nproduction_license_store=untouched\ntrial_nv_operations=none\n";
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc >= 2, "Usage: td-license-hardware-tests request output.tdreq | activate-verify public.blob issuer package.tdact NEW-TEST-DIRECTORY");
        const std::wstring_view command(argv[1]);
        if (command == L"request") {
            Require(argc == 3, "Permanent request output path required");
            ExportEnrollmentRequest(AbsolutePath(argv[2]), false);
            std::cout << "permanent_enrollment_request=created\ntrial_nv_operations=none\n";
        } else if (command == L"activate-verify") {
            Require(argc == 6, "Activation test requires public key, issuer, activation package and new isolated directory");
            ActivateAndVerify(argv[2], argv[3], argv[4], argv[5]);
        } else throw std::runtime_error("Unknown hardware test command");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
